#include "gantry_robot.h"

#include <math.h>
#include <string.h>

#include "queue.h"
#include "task.h"

#include "ZDT_drv.h"
#include "bsp_api.h" /* R_BSP_SoftwareDelay */

#define GANTRY_COMMAND_QUEUE_LENGTH (8U)
typedef enum
{
    GANTRY_COMMAND_ENABLE = 0,
    GANTRY_COMMAND_UNPROTECT,
    GANTRY_COMMAND_HOME,
    GANTRY_COMMAND_MOVE_ABSOLUTE,
    GANTRY_COMMAND_MOVE_RELATIVE
} gantry_command_type_t;

typedef struct
{
    gantry_command_type_t type;
    uint8_t axis_mask;
    bool enable;
    gantry_position_t position;
} gantry_command_t;

static StaticQueue_t s_queue_control;
static uint8_t s_queue_storage[GANTRY_COMMAND_QUEUE_LENGTH * sizeof(gantry_command_t)];
static QueueHandle_t s_command_queue;
static gantry_config_t s_config;
static gantry_status_t s_status;
static TickType_t s_deadline;
static uint8_t s_expected_axes;
static uint8_t s_completed_axes;
/* 回零固定时序（与用户原 ZDT_Gozero_ALL() 一致）：
 * 实测各轴"回零完成"应答只有 Z 可靠（Y/X/抓手无应答），
 * 若等应答全部会 20s 超时 → 回零用固定等待，不依赖应答。 */
#define GANTRY_HOME_Z_GAP_MS   (2000U)  /* Z 回零后到 X/Y/抓手的间隔 */
#define GANTRY_HOME_SETTLE_MS  (1500U)  /* 其余轴回零后的稳定等待 */
/* Y 轴下降（往 0 方向走，目标 Y < 当前 Y）速度：用户实测要求固定 60，防抖动。
 * 注意：Y 减小 = 下降（机械结构重力方向），速度大容易抖动。 */
#define GANTRY_Y_DESCEND_SPEED (60U)

typedef enum
{
    GANTRY_HOME_PHASE_Z = 0, /* 已发 Z 回零，等待间隔 */
    GANTRY_HOME_PHASE_REST,  /* 已发 X/Y/抓手 回零，等待稳定 */
    GANTRY_HOME_PHASE_DONE   /* 全部标记完成 */
} gantry_home_phase_t;

static gantry_home_phase_t s_home_phase;
static uint8_t s_home_mask;        /* 本次回零请求的轴掩码 */
static TickType_t s_home_phase_at; /* 当前阶段开始时刻 */
static volatile bool s_emergency_stop_requested;
static volatile uint8_t s_axis_completion[GANTRY_AXIS_COUNT];
static bool s_initialized;
/* 移动固定时序（与回零同理，同用户原 Move_XY_To_mm 思路：发命令+固定等待）：
 * 实测各轴"移动到位"应答不可靠（同步移动时 Y/抓手等收不到 FD 9F 6B），
 * 若等应答会 15s 超时 FAULT 卡流程 → 移动命令发出后按"距离/速度"估算
 * 时间固定等待完成。收到的到位应答仅用于提前结束等待，不依赖它。 */
static TickType_t s_move_est_at;   /* 移动估算到期时刻 */

static const uint32_t s_axis_ids[GANTRY_AXIS_COUNT] = {ZDT_ID_X, ZDT_ID_Y, ZDT_ID_Z, ZDT_ID_CATCH};

static uint8_t axis_bit(gantry_axis_t axis)
{
    return (uint8_t) (1U << (uint8_t) axis);
}

static float * position_component(gantry_position_t * position, gantry_axis_t axis)
{
    if (GANTRY_AXIS_X == axis) return &position->x_mm;
    if (GANTRY_AXIS_Y == axis) return &position->y_mm;
    if (GANTRY_AXIS_Z == axis) return &position->z_mm;
    return &position->catch_mm;
}

static float position_value(const gantry_position_t * position, gantry_axis_t axis)
{
    if (GANTRY_AXIS_X == axis) return position->x_mm;
    if (GANTRY_AXIS_Y == axis) return position->y_mm;
    if (GANTRY_AXIS_Z == axis) return position->z_mm;
    return position->catch_mm;
}

/* 有效轴掩码：XYZ 流程掩码 + 抓手 */
#define GANTRY_AXIS_MASK_VALID (GANTRY_AXIS_MASK_ALL | GANTRY_AXIS_MASK_CATCH)

static bool config_is_valid(const gantry_config_t * config)
{
    if ((NULL == config) || (0U == config->move_timeout_ms) || (0U == config->home_timeout_ms)) return false;
    for (gantry_axis_t axis = GANTRY_AXIS_X; axis < GANTRY_AXIS_COUNT; axis++)
    {
        const gantry_axis_config_t * item = &config->axis[axis];
        if (!isfinite(item->pulses_per_mm) || (item->pulses_per_mm <= 0.0f) ||
            !isfinite(item->min_mm) || !isfinite(item->max_mm) || (item->min_mm >= item->max_mm) ||
            ((item->direction != 1) && (item->direction != -1)) || (0U == item->speed)) return false;
    }
    return true;
}

static gantry_result_t validate_position(const gantry_position_t * position, uint8_t axis_mask)
{
    if ((NULL == position) || (0U == (axis_mask & GANTRY_AXIS_MASK_VALID))) return GANTRY_ERR_INVALID_ARGUMENT;
    for (gantry_axis_t axis = GANTRY_AXIS_X; axis < GANTRY_AXIS_COUNT; axis++)
    {
        if (0U == (axis_mask & axis_bit(axis))) continue;
        float value = position_value(position, axis);
        if (!isfinite(value)) return GANTRY_ERR_INVALID_ARGUMENT;
        if ((value < s_config.axis[axis].min_mm) || (value > s_config.axis[axis].max_mm)) return GANTRY_ERR_SOFT_LIMIT;
    }
    return GANTRY_OK;
}

static int32_t millimetres_to_pulses(gantry_axis_t axis, float millimetres)
{
    float pulses = millimetres * s_config.axis[axis].pulses_per_mm * (float) s_config.axis[axis].direction;
    pulses += (pulses >= 0.0f) ? 0.5f : -0.5f;
    return (int32_t) pulses;
}

static gantry_result_t enqueue_command(const gantry_command_t * command)
{
    if (!s_initialized) return GANTRY_ERR_NOT_INITIALIZED;
    if ((NULL == command) || (0U == (command->axis_mask & GANTRY_AXIS_MASK_VALID))) return GANTRY_ERR_INVALID_ARGUMENT;
    if ((GANTRY_STATE_FAULT == s_status.state) || (GANTRY_STATE_STOPPED == s_status.state)) return s_status.last_error;
    return (pdPASS == xQueueSend(s_command_queue, command, 0U)) ? GANTRY_OK : GANTRY_ERR_QUEUE_FULL;
}

static void finish_active_command(void)
{
    s_status.position = s_status.target;
    s_status.active_axes = 0U;
    s_status.state = GANTRY_STATE_IDLE;
    s_status.last_error = GANTRY_OK;
    s_status.completed_commands++;
    s_expected_axes = 0U;
    s_completed_axes = 0U;
}

static void stop_with_error(gantry_result_t error)
{
    ZDT_Stop(ZDT_ID_ALL);
    s_status.active_axes = 0U;
    s_status.state = (GANTRY_ERR_EMERGENCY_STOP == error) ? GANTRY_STATE_STOPPED : GANTRY_STATE_FAULT;
    s_status.last_error = error;
    s_expected_axes = 0U;
    s_completed_axes = 0U;
    (void) xQueueReset(s_command_queue);
}

/* 回零完成：位置归零 + 标记 homed（后续 MoveTo/MoveXYTo 需要 homed） */
static void mark_home_done(void)
{
    for (gantry_axis_t axis = GANTRY_AXIS_X; axis < GANTRY_AXIS_COUNT; axis++)
    {
        if (0U != (s_home_mask & axis_bit(axis)))
        {
            *position_component(&s_status.position, axis) = 0.0f;
            *position_component(&s_status.target, axis) = 0.0f;
            s_status.homed_axes |= axis_bit(axis);
        }
    }
}

static void start_home_command(const gantry_command_t * command)
{
    s_home_mask = command->axis_mask;
    s_expected_axes = command->axis_mask;
    s_completed_axes = 0U;
    s_status.active_axes = command->axis_mask;
    s_status.state = GANTRY_STATE_HOMING;
    s_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(s_config.home_timeout_ms);
    memset((void *) s_axis_completion, 0, sizeof(s_axis_completion));
    if (0U != (command->axis_mask & axis_bit(GANTRY_AXIS_Z)))
    {
        /* Z 先单独回零（用户约束：先等 Z 回零，其他再跟着回零） */
        ZDT_Gozero(s_axis_ids[GANTRY_AXIS_Z], false);
        s_home_phase = GANTRY_HOME_PHASE_Z;
    }
    else
    {
        s_home_phase = GANTRY_HOME_PHASE_REST;
    }
    s_home_phase_at = xTaskGetTickCount();
}

/* 固定时序推进回零（Gantry_Service 每周期调用）：
 *   Z 单独 → 间隔 GANTRY_HOME_Z_GAP_MS → X+Y+抓手 一起 → 稳定
 *   GANTRY_HOME_SETTLE_MS → 标记完成。与用户原 ZDT_Gozero_ALL() 一致，
 *   不等待各轴应答（实测只有 Z 会应答）。 */
static void advance_home_phase(void)
{
    TickType_t now = xTaskGetTickCount();
    if (GANTRY_HOME_PHASE_Z == s_home_phase)
    {
        if ((int32_t) (now - s_home_phase_at) < (int32_t) pdMS_TO_TICKS(GANTRY_HOME_Z_GAP_MS))
        {
            return;
        }
        s_home_phase = GANTRY_HOME_PHASE_REST;
        s_home_phase_at = now;
        /* X+Y+抓手 一起回零 */
        for (gantry_axis_t axis = GANTRY_AXIS_X; axis < GANTRY_AXIS_COUNT; axis++)
        {
            if ((GANTRY_AXIS_Z != axis) && (0U != (s_home_mask & axis_bit(axis))))
            {
                ZDT_Gozero(s_axis_ids[axis], false);
            }
        }
        return;
    }
    if (GANTRY_HOME_PHASE_REST == s_home_phase)
    {
        if ((int32_t) (now - s_home_phase_at) < (int32_t) pdMS_TO_TICKS(GANTRY_HOME_SETTLE_MS))
        {
            return;
        }
        s_home_phase = GANTRY_HOME_PHASE_DONE;
        mark_home_done();
        finish_active_command();
        return;
    }
}

static void start_move_command(const gantry_command_t * command)
{
    gantry_position_t target = command->position;
    if (GANTRY_COMMAND_MOVE_RELATIVE == command->type)
    {
        target.x_mm += s_status.position.x_mm;
        target.y_mm += s_status.position.y_mm;
        target.z_mm += s_status.position.z_mm;
    }
    gantry_result_t result = validate_position(&target, command->axis_mask);
    if (GANTRY_OK != result)
    {
        s_status.last_error = result;
        return;
    }

    s_status.target = s_status.position;
    s_expected_axes = command->axis_mask;
    s_completed_axes = 0U;
    s_status.active_axes = command->axis_mask;
    s_status.state = GANTRY_STATE_MOVING;
    s_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(s_config.move_timeout_ms);

    uint8_t axis_count = 0U;
    for (gantry_axis_t axis = GANTRY_AXIS_X; axis < GANTRY_AXIS_COUNT; axis++)
    {
        if (0U != (command->axis_mask & axis_bit(axis)))
        {
            *position_component(&s_status.target, axis) = position_value(&target, axis);
            axis_count++;
        }
    }
    /* 与用户原 Move_XY_To_mm() 一致：sync=false 各轴独立执行，不调
     * ZDT_SyncTrigger。实测同步模式(ZDT_SyncTrigger)下 XY 电机不动作。
     * 每条命令之间必须加 10ms 间隔（用户原工程 Move_XY_To_mm 中
     * X/Y 之间即 vTaskDelay(10)）。
     * 关键：CAN 帧偶发丢失（实测 X/Y 随机不动，两次测试结果相反），
     * 调试页 JOG 靠"连发 3 次(间隔 5ms)"补偿故可靠——此处同样连发 3 次。 */
    (void) axis_count;
    for (gantry_axis_t axis = GANTRY_AXIS_X; axis < GANTRY_AXIS_COUNT; axis++)
    {
        if (0U != (command->axis_mask & axis_bit(axis)))
        {
            int32_t pulses = millimetres_to_pulses(axis, position_value(&target, axis));
            uint16_t speed = s_config.axis[axis].speed;
            uint8_t acc = s_config.axis[axis].acceleration;
            /* Y 轴下降（目标 < 当前，往 0 方向）强制低速 60 防抖动 */
            if ((GANTRY_AXIS_Y == axis) &&
                (position_value(&target, axis) < position_value(&s_status.position, axis)))
            {
                speed = GANTRY_Y_DESCEND_SPEED;
            }
            for (uint8_t retry = 0U; retry < 3U; retry++)
            {
                ZDT_MovePosition(s_axis_ids[axis], pulses, speed, acc, false);
                R_BSP_SoftwareDelay(5U, BSP_DELAY_UNITS_MILLISECONDS);
            }
            R_BSP_SoftwareDelay(10U, BSP_DELAY_UNITS_MILLISECONDS);
        }
    }

    /* 估算到位时间（固定时序）：按各轴移动距离/速度取最大值，再加余量。
     * 单位脉冲→mm→耗时：dist_pulse / (speed) 为粗估，乘 4 取安全余量，
     * 下限 1500ms 上限 8s（与回零固定时序同一思路，不依赖应答）。 */
    {
        float max_ms = 0.0f;
        for (gantry_axis_t axis = GANTRY_AXIS_X; axis < GANTRY_AXIS_COUNT; axis++)
        {
            if (0U == (command->axis_mask & axis_bit(axis)))
            {
                continue;
            }
            float dist_mm = position_value(&target, axis) - position_value(&s_status.position, axis);
            if (dist_mm < 0.0f) dist_mm = -dist_mm;
            float dist_pulse = dist_mm * s_config.axis[axis].pulses_per_mm;
            float est = (s_config.axis[axis].speed > 0U) ? (dist_pulse * 4000.0f / (float) s_config.axis[axis].speed) : 0.0f;
            if (est > max_ms) max_ms = est;
        }
        if (max_ms < 1500.0f) max_ms = 1500.0f;
        if (max_ms > 8000.0f) max_ms = 8000.0f;
        s_move_est_at = xTaskGetTickCount() + (TickType_t) pdMS_TO_TICKS((uint32_t) max_ms);
    }
}

static void dispatch_command(const gantry_command_t * command)
{
    if (GANTRY_COMMAND_ENABLE == command->type)
    {
        for (gantry_axis_t axis = GANTRY_AXIS_X; axis < GANTRY_AXIS_COUNT; axis++)
            if (0U != (command->axis_mask & axis_bit(axis))) ZDT_Enable(s_axis_ids[axis], command->enable);
        s_status.last_error = GANTRY_OK;
        s_status.completed_commands++;
    }
    else if (GANTRY_COMMAND_UNPROTECT == command->type)
    {
        /* 解除堵转保护（撞限位/堵转后必须先解除才能再动）。
         * 连发 3 次（间隔 5ms）抵抗驱动偶发丢命令。 */
        for (gantry_axis_t axis = GANTRY_AXIS_X; axis < GANTRY_AXIS_COUNT; axis++)
        {
            if (0U == (command->axis_mask & axis_bit(axis)))
            {
                continue;
            }
            ZDT_Unprotect(s_axis_ids[axis]);
            R_BSP_SoftwareDelay(5U, BSP_DELAY_UNITS_MILLISECONDS);
            ZDT_Unprotect(s_axis_ids[axis]);
            R_BSP_SoftwareDelay(5U, BSP_DELAY_UNITS_MILLISECONDS);
            ZDT_Unprotect(s_axis_ids[axis]);
        }
        s_status.last_error = GANTRY_OK;
        s_status.completed_commands++;
    }
    else if (GANTRY_COMMAND_HOME == command->type) start_home_command(command);
    else start_move_command(command);
}

static void process_axis_events(void)
{
    uint8_t completed = 0U;
    uint8_t failed = 0U;
    taskENTER_CRITICAL();
    for (gantry_axis_t axis = GANTRY_AXIS_X; axis < GANTRY_AXIS_COUNT; axis++)
    {
        uint8_t completion = s_axis_completion[axis];
        s_axis_completion[axis] = 0U;
        if (1U == completion) completed |= axis_bit(axis);
        if (2U == completion) failed |= axis_bit(axis);
    }
    taskEXIT_CRITICAL();

    if (0U != (failed & s_expected_axes))
    {
        stop_with_error(GANTRY_ERR_AXIS_FAULT);
        return;
    }
    s_completed_axes |= completed;
    if ((0U == s_expected_axes) || ((s_completed_axes & s_expected_axes) != s_expected_axes)) return;

    finish_active_command();
}

void Gantry_GetDefaultConfig(gantry_config_t * config)
{
    if (NULL == config) return;
    memset(config, 0, sizeof(*config));
    /* 软限位按实机标定的脉冲行程换算（mm = 脉冲 / pulses_per_mm，取整误差<1脉冲）：
     *   X 轴行程 0 ~ 17000 脉冲（用户实测最远可到 17000）→ 17000 / (3600/84) ≈ 396.7 mm
     *   Y 轴行程 0 ~ 12500 脉冲 → 12500 / (3600/84) ≈ 291.7 mm
     *   Z 轴行程 0 ~ 6600  脉冲 → 6600  / (1000/23) ≈ 151.8 mm
     * 电机调试界面（gantry_debug）不受此限位约束，仅正式取药流程受保护。 */
    config->axis[GANTRY_AXIS_X] = (gantry_axis_config_t) {3600.0f / 84.0f, 0.0f, 396.7f, 1, 300U, 60U};
    /* Y 轴：实测最大量程 0~12500 脉冲 → 12500 / (3600/84) ≈ 291.7mm */
    config->axis[GANTRY_AXIS_Y] = (gantry_axis_config_t) {3600.0f / 84.0f, 0.0f, 291.7f, 1, 300U, 60U};
    /* Z 轴：实测最大量程 6600 脉冲 → 151.8mm（pulses_per_mm = 1000/23）。
     * 伸出到底 = 151.8mm（6600 脉冲），绝不可发超量程值——会顶死堵转！ */
    config->axis[GANTRY_AXIS_Z] = (gantry_axis_config_t) {1000.0f / 23.0f, 0.0f, 151.8f, 1, 1500U, 60U};
    /* 抓手(CATCH)：行程 0 ~ 1550 脉冲（用户实机标定，mstep=16，2026-08-21）。
     * 物理开口：0=115mm 全开(11.5cm)、+1550=51mm 全闭(5.1cm)（用户实测）；
     * 换算 64mm/1550脉冲 ≈ 0.0413 mm/脉冲。
     * 正值=合上。pulses_per_mm=1.0 → 应用层 mm 参数即脉冲数；min=0 max=1550。 */
    config->axis[GANTRY_AXIS_CATCH] = (gantry_axis_config_t) {1.0f, 0.0f, 1550.0f, 1, 300U, 60U};
    config->move_timeout_ms = 15000U;
    config->home_timeout_ms = 20000U;
    config->require_homing = true;
}

gantry_result_t Gantry_Init(const gantry_config_t * config)
{
    if (!config_is_valid(config)) return GANTRY_ERR_INVALID_ARGUMENT;
    if (NULL == s_command_queue)
        s_command_queue = xQueueCreateStatic(GANTRY_COMMAND_QUEUE_LENGTH, sizeof(gantry_command_t), s_queue_storage, &s_queue_control);
    if (NULL == s_command_queue) return GANTRY_ERR_NOT_INITIALIZED;

    (void) xQueueReset(s_command_queue);
    memset((void *) s_axis_completion, 0, sizeof(s_axis_completion));
    s_config = *config;
    memset(&s_status, 0, sizeof(s_status));
    s_status.state = GANTRY_STATE_IDLE;
    s_status.last_error = GANTRY_OK;
    s_emergency_stop_requested = false;
    s_initialized = true;
    return GANTRY_OK;
}

gantry_result_t Gantry_Configure(const gantry_config_t * config)
{
    if (!s_initialized) return GANTRY_ERR_NOT_INITIALIZED;
    if (!config_is_valid(config)) return GANTRY_ERR_INVALID_ARGUMENT;
    if (!Gantry_IsIdle() || (0U != uxQueueMessagesWaiting(s_command_queue))) return GANTRY_ERR_BUSY;
    s_config = *config;
    return GANTRY_OK;
}

gantry_result_t Gantry_EnableAxes(uint8_t axis_mask, bool enable)
{
    gantry_command_t command = {
        .type = GANTRY_COMMAND_ENABLE,
        .axis_mask = (uint8_t) (axis_mask & GANTRY_AXIS_MASK_VALID),
        .enable = enable,
        .position = {0.0f, 0.0f, 0.0f}
    };
    return enqueue_command(&command);
}

gantry_result_t Gantry_Home(uint8_t axis_mask)
{
    gantry_command_t command = {
        .type = GANTRY_COMMAND_HOME,
        .axis_mask = (uint8_t) (axis_mask & GANTRY_AXIS_MASK_VALID),
        .enable = false,
        .position = {0.0f, 0.0f, 0.0f}
    };
    return enqueue_command(&command);
}

gantry_result_t Gantry_UnprotectAxes(uint8_t axis_mask)
{
    gantry_command_t command = {
        .type = GANTRY_COMMAND_UNPROTECT,
        .axis_mask = (uint8_t) (axis_mask & GANTRY_AXIS_MASK_VALID),
        .enable = false,
        .position = {0.0f, 0.0f, 0.0f}
    };
    return enqueue_command(&command);
}

gantry_result_t Gantry_MoveTo(const gantry_position_t * target)
{
    if (NULL == target) return GANTRY_ERR_INVALID_ARGUMENT;
    if (!s_initialized) return GANTRY_ERR_NOT_INITIALIZED;
    if (s_config.require_homing && ((s_status.homed_axes & GANTRY_AXIS_MASK_ALL) != GANTRY_AXIS_MASK_ALL)) return GANTRY_ERR_NOT_HOMED;
    gantry_result_t result = validate_position(target, GANTRY_AXIS_MASK_ALL);
    if (GANTRY_OK != result) return result;
    gantry_command_t command = {GANTRY_COMMAND_MOVE_ABSOLUTE, GANTRY_AXIS_MASK_ALL, false, *target};
    return enqueue_command(&command);
}

gantry_result_t Gantry_MoveBy(const gantry_position_t * delta)
{
    if ((NULL == delta) || !isfinite(delta->x_mm) || !isfinite(delta->y_mm) || !isfinite(delta->z_mm)) return GANTRY_ERR_INVALID_ARGUMENT;
    if (!s_initialized) return GANTRY_ERR_NOT_INITIALIZED;
    if (s_config.require_homing && ((s_status.homed_axes & GANTRY_AXIS_MASK_ALL) != GANTRY_AXIS_MASK_ALL)) return GANTRY_ERR_NOT_HOMED;
    gantry_position_t target = {s_status.position.x_mm + delta->x_mm, s_status.position.y_mm + delta->y_mm,
                                s_status.position.z_mm + delta->z_mm};
    gantry_result_t result = validate_position(&target, GANTRY_AXIS_MASK_ALL);
    if (GANTRY_OK != result) return result;
    gantry_command_t command = {GANTRY_COMMAND_MOVE_RELATIVE, GANTRY_AXIS_MASK_ALL, false, *delta};
    return enqueue_command(&command);
}

gantry_result_t Gantry_MoveAxisTo(gantry_axis_t axis, float position_mm)
{
    if (((uint32_t) axis >= (uint32_t) GANTRY_AXIS_COUNT) || !isfinite(position_mm)) return GANTRY_ERR_INVALID_ARGUMENT;
    if (!s_initialized) return GANTRY_ERR_NOT_INITIALIZED;
    if (s_config.require_homing && (0U == (s_status.homed_axes & axis_bit(axis)))) return GANTRY_ERR_NOT_HOMED;
    gantry_position_t target = s_status.position;
    *position_component(&target, axis) = position_mm;
    gantry_result_t result = validate_position(&target, axis_bit(axis));
    if (GANTRY_OK != result) return result;
    gantry_command_t command = {GANTRY_COMMAND_MOVE_ABSOLUTE, axis_bit(axis), false, target};
    return enqueue_command(&command);
}

gantry_result_t Gantry_MoveXYTo(float x_mm, float y_mm)
{
    if (!s_initialized) return GANTRY_ERR_NOT_INITIALIZED;
    if (!isfinite(x_mm) || !isfinite(y_mm)) return GANTRY_ERR_INVALID_ARGUMENT;
    if (s_config.require_homing && ((s_status.homed_axes & GANTRY_AXIS_MASK_ALL) != GANTRY_AXIS_MASK_ALL))
        return GANTRY_ERR_NOT_HOMED;
    gantry_position_t target = s_status.position;
    target.x_mm = x_mm;
    target.y_mm = y_mm;
    gantry_result_t result = validate_position(&target, (GANTRY_AXIS_MASK_X | GANTRY_AXIS_MASK_Y));
    if (GANTRY_OK != result) return result;
    /* 掩码仅 X|Y：start_move_command 中 axis_count=2 → ZDT sync=1 + ZDT_SyncTrigger，
     * 两轴同步并行移动，Z 保持当前（调用方必须已把 Z 收到安全位）。 */
    gantry_command_t command = {
        .type = GANTRY_COMMAND_MOVE_ABSOLUTE,
        .axis_mask = (GANTRY_AXIS_MASK_X | GANTRY_AXIS_MASK_Y),
        .enable = false,
        .position = target
    };
    return enqueue_command(&command);
}

gantry_result_t Gantry_MoveSafeTo(const gantry_position_t * target, float safe_z_mm)
{
    if ((NULL == target) || !isfinite(safe_z_mm)) return GANTRY_ERR_INVALID_ARGUMENT;
    if (!s_initialized) return GANTRY_ERR_NOT_INITIALIZED;
    if (s_config.require_homing && ((s_status.homed_axes & GANTRY_AXIS_MASK_ALL) != GANTRY_AXIS_MASK_ALL))
        return GANTRY_ERR_NOT_HOMED;

    gantry_position_t safe_point = *target;
    safe_point.z_mm = safe_z_mm;
    gantry_result_t result = validate_position(target, GANTRY_AXIS_MASK_ALL);
    if (GANTRY_OK == result) result = validate_position(&safe_point, GANTRY_AXIS_MASK_ALL);
    if (GANTRY_OK != result) return result;
    if ((GANTRY_STATE_FAULT == s_status.state) || (GANTRY_STATE_STOPPED == s_status.state)) return s_status.last_error;
    if (uxQueueSpacesAvailable(s_command_queue) < 3U) return GANTRY_ERR_QUEUE_FULL;

    gantry_command_t retract = {
        .type = GANTRY_COMMAND_MOVE_ABSOLUTE,
        .axis_mask = GANTRY_AXIS_MASK_Z,
        .enable = false,
        .position = safe_point
    };
    gantry_command_t traverse = {
        .type = GANTRY_COMMAND_MOVE_ABSOLUTE,
        .axis_mask = (GANTRY_AXIS_MASK_X | GANTRY_AXIS_MASK_Y),
        .enable = false,
        .position = safe_point
    };
    gantry_command_t descend = {
        .type = GANTRY_COMMAND_MOVE_ABSOLUTE,
        .axis_mask = GANTRY_AXIS_MASK_Z,
        .enable = false,
        .position = *target
    };

    taskENTER_CRITICAL();
    BaseType_t queued = xQueueSend(s_command_queue, &retract, 0U);
    if (pdPASS == queued) queued = xQueueSend(s_command_queue, &traverse, 0U);
    if (pdPASS == queued) queued = xQueueSend(s_command_queue, &descend, 0U);
    taskEXIT_CRITICAL();
    return (pdPASS == queued) ? GANTRY_OK : GANTRY_ERR_QUEUE_FULL;
}

gantry_result_t Gantry_EmergencyStop(void)
{
    if (!s_initialized) return GANTRY_ERR_NOT_INITIALIZED;
    s_emergency_stop_requested = true;
    return GANTRY_OK;
}

gantry_result_t Gantry_ClearFault(void)
{
    if (!s_initialized) return GANTRY_ERR_NOT_INITIALIZED;
    if ((GANTRY_STATE_STOPPED != s_status.state) && (GANTRY_STATE_FAULT != s_status.state)) return GANTRY_ERR_BUSY;
    s_status.state = GANTRY_STATE_IDLE;
    s_status.last_error = GANTRY_OK;
    return GANTRY_OK;
}

gantry_result_t Gantry_SetLogicalPosition(const gantry_position_t * position, uint8_t homed_axes)
{
    if (!s_initialized) return GANTRY_ERR_NOT_INITIALIZED;
    if (!Gantry_IsIdle()) return GANTRY_ERR_BUSY;
    gantry_result_t result = validate_position(position, GANTRY_AXIS_MASK_ALL);
    if (GANTRY_OK != result) return result;
    taskENTER_CRITICAL();
    s_status.position = *position;
    s_status.target = *position;
    s_status.homed_axes = (uint8_t) (homed_axes & GANTRY_AXIS_MASK_VALID);
    taskEXIT_CRITICAL();
    return GANTRY_OK;
}

void Gantry_GetStatus(gantry_status_t * status)
{
    if (NULL == status) return;
    taskENTER_CRITICAL();
    *status = s_status;
    taskEXIT_CRITICAL();
}

bool Gantry_IsIdle(void)
{
    return s_initialized && (GANTRY_STATE_IDLE == s_status.state) &&
           (0U == uxQueueMessagesWaiting(s_command_queue));
}

gantry_result_t Gantry_WaitIdle(uint32_t timeout_ms)
{
    if (!s_initialized) return GANTRY_ERR_NOT_INITIALIZED;
    TickType_t start = xTaskGetTickCount();
    TickType_t timeout = pdMS_TO_TICKS(timeout_ms);
    while (!Gantry_IsIdle())
    {
        if ((GANTRY_STATE_FAULT == s_status.state) || (GANTRY_STATE_STOPPED == s_status.state)) return s_status.last_error;
        if ((xTaskGetTickCount() - start) >= timeout) return GANTRY_ERR_TIMEOUT;
        vTaskDelay(pdMS_TO_TICKS(5U));
    }
    return GANTRY_OK;
}

void Gantry_Service(void)
{
    if (!s_initialized) return;
    if (s_emergency_stop_requested)
    {
        s_emergency_stop_requested = false;
        stop_with_error(GANTRY_ERR_EMERGENCY_STOP);
        return;
    }
    if (GANTRY_STATE_HOMING == s_status.state)
    {
        /* 回零用固定时序推进（advance_home_phase），不依赖各轴应答：
         * 实测只有 Z 有"回零完成"应答，等应答的旧逻辑会让 Y/X/抓手
         * 每轴卡 home_timeout(20s) 超时。 */
        if ((int32_t) (xTaskGetTickCount() - s_deadline) >= 0)
        {
            /* 兜底超时（固定时序理论 ~3.5s）：命令已全部发出，按完成处理，
             * 避免流程锁死（不进入 FAULT）。 */
            mark_home_done();
            finish_active_command();
            return;
        }
        advance_home_phase();
        return;
    }
    if (GANTRY_STATE_MOVING == s_status.state)
    {
        /* 到位事件（收到的 FD 9F 6B 等）：所有期待轴完成 → 提前结束等待 */
        process_axis_events();
        /* 固定时序兜底：估算时间到期即视为完成（同回零思路，不依赖应答，
         * 避免移动应答不可靠时 15s 超时 FAULT 卡流程）。 */
        if ((int32_t) (xTaskGetTickCount() - s_move_est_at) >= 0)
        {
            finish_active_command();
            return;
        }
        return;
    }
    if (GANTRY_STATE_IDLE == s_status.state)
    {
        gantry_command_t command;
        if (pdPASS == xQueueReceive(s_command_queue, &command, 0U)) dispatch_command(&command);
    }
}

void Gantry_NotifyAxisDone(gantry_axis_t axis, bool success)
{
    if (s_initialized && ((uint32_t) axis < (uint32_t) GANTRY_AXIS_COUNT))
    {
        taskENTER_CRITICAL();
        s_axis_completion[axis] = success ? 1U : 2U;
        taskEXIT_CRITICAL();
    }
}

void Gantry_NotifyAxisDoneFromISR(gantry_axis_t axis, bool success, BaseType_t * higher_priority_task_woken)
{
    if (s_initialized && ((uint32_t) axis < (uint32_t) GANTRY_AXIS_COUNT))
    {
        s_axis_completion[axis] = success ? 1U : 2U;
        if (NULL != higher_priority_task_woken) *higher_priority_task_woken = pdFALSE;
    }
}
