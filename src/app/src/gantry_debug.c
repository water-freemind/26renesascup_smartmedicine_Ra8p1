/*
 * 电机调试模块实现
 * 四轴(X/Y/Z/抓手) 使能 / 设零 / 脉冲点动 / 停止，Motor 线程独占执行。
 * 位置本地累计（ZDT 无回读）。gantry_robot 忙时命令暂缓执行。
 */

#include "gantry_debug.h"

#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "ZDT_drv.h"
#include "gantry_robot.h"
#include "r_can_api.h"
#include "hal_data.h"
#include "bsp_api.h"
#include "Motor_thread.h" /* extern const can_instance_t g_canfd0; */

#define GDBG_QUEUE_LENGTH (16U)

typedef enum
{
    GDBG_CMD_ENABLE = 0,
    GDBG_CMD_SET_ZERO,
    GDBG_CMD_GOZERO,
    GDBG_CMD_JOG,
    GDBG_CMD_UNPROTECT,
    GDBG_CMD_STOP
} gdbg_command_type_t;

typedef struct
{
    gdbg_command_type_t type;
    uint8_t axis_mask;
    bool enable;
    int32_t pulses;
    uint16_t speed;
    uint8_t acc;
} gdbg_command_t;

/* 轴索引 → ZDT ID */
static const uint32_t s_axis_ids[GDBG_AXIS_COUNT] = {ZDT_ID_X, ZDT_ID_Y, ZDT_ID_Z, ZDT_ID_CATCH};

static StaticQueue_t s_queue_control;
static uint8_t s_queue_storage[GDBG_QUEUE_LENGTH * sizeof(gdbg_command_t)];
static QueueHandle_t s_command_queue;
static volatile int32_t s_pulse[GDBG_AXIS_COUNT];
static volatile uint32_t s_completed;
static bool s_initialized;

/* 最近收到的各轴应答原始帧（ISR 写，GUI 读） */
static volatile gdbg_rx_raw_t s_rx_raw[GDBG_AXIS_COUNT];
/* 各轴到位应答计数（FD 9F 6B=移动到位；9A 02 6B=回零完成） */
static volatile uint32_t s_arrived[GDBG_AXIS_COUNT];
/* 协议探测环形缓冲：记录每轴最近 8 帧应答（ISR 写，J-Link 读） */
#define GDBG_RING_DEPTH (8U)
typedef struct
{
    uint8_t data[8];
    uint8_t len;
    uint32_t frame_id;
} gdbg_ring_frame_t;
static volatile gdbg_ring_frame_t s_rx_ring[GDBG_AXIS_COUNT][GDBG_RING_DEPTH];
static volatile uint8_t s_rx_ring_head[GDBG_AXIS_COUNT];

static uint8_t axis_bit(uint8_t axis)
{
    return (uint8_t) (1U << axis);
}

/* 直接发送一帧（扩展帧数据帧），不经过 ZDT_drv（保持其零差异） */
static bool gdbg_send_raw(uint32_t id, const uint8_t * data, uint8_t len)
{
    can_frame_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.id = id;
    frame.id_mode = CAN_ID_MODE_EXTENDED;
    frame.type = CAN_FRAME_TYPE_DATA;
    frame.data_length_code = len;
    frame.options = 0;
    if (len > 8U) len = 8U;
    memcpy(frame.data, data, len);

    fsp_err_t err;
    uint32_t timeout = 50000U;
    do
    {
        err = g_canfd0.p_api->write(g_canfd0.p_ctrl, 0U, &frame);
        if (FSP_ERR_IN_USE == err)
        {
            R_BSP_SoftwareDelay(1U, BSP_DELAY_UNITS_MICROSECONDS);
            timeout--;
        }
    } while ((FSP_ERR_IN_USE == err) && (timeout > 0U));
    return (FSP_SUCCESS == err);
}

gdbg_result_t GantryDebug_Init(void)
{
    if (NULL == s_command_queue)
    {
        s_command_queue = xQueueCreateStatic(GDBG_QUEUE_LENGTH, sizeof(gdbg_command_t),
                                             s_queue_storage, &s_queue_control);
    }
    if (NULL == s_command_queue)
    {
        return GDBG_ERR_NOT_INITIALIZED;
    }
    (void) xQueueReset(s_command_queue);
    memset((void *) s_pulse, 0, sizeof(s_pulse));
    s_completed = 0U;
    s_initialized = true;
    return GDBG_OK;
}

static gdbg_result_t enqueue(const gdbg_command_t * command)
{
    if (!s_initialized)
    {
        return GDBG_ERR_NOT_INITIALIZED;
    }
    if ((NULL == command) || (0U == (command->axis_mask & GDBG_AXIS_ALL)))
    {
        return GDBG_ERR_INVALID_ARGUMENT;
    }
    return (pdPASS == xQueueSend(s_command_queue, command, 0U)) ? GDBG_OK : GDBG_ERR_QUEUE_FULL;
}

gdbg_result_t GantryDebug_Enable(uint8_t axis_mask, bool enable)
{
    gdbg_command_t command = {
        .type = GDBG_CMD_ENABLE,
        .axis_mask = (uint8_t) (axis_mask & GDBG_AXIS_ALL),
        .enable = enable,
        .pulses = 0,
        .speed = 0U,
        .acc = 0U
    };
    return enqueue(&command);
}

gdbg_result_t GantryDebug_SetZero(uint8_t axis_mask)
{
    gdbg_command_t command = {
        .type = GDBG_CMD_SET_ZERO,
        .axis_mask = (uint8_t) (axis_mask & GDBG_AXIS_ALL),
        .enable = false,
        .pulses = 0,
        .speed = 0U,
        .acc = 0U
    };
    return enqueue(&command);
}

gdbg_result_t GantryDebug_Gozero(uint8_t axis_mask)
{
    gdbg_command_t command = {
        .type = GDBG_CMD_GOZERO,
        .axis_mask = (uint8_t) (axis_mask & GDBG_AXIS_ALL),
        .enable = false,
        .pulses = 0,
        .speed = 0U,
        .acc = 0U
    };
    return enqueue(&command);
}

gdbg_result_t GantryDebug_Jog(uint8_t axis_mask, int32_t pulses, uint16_t speed, uint8_t acc)
{
    gdbg_command_t command = {
        .type = GDBG_CMD_JOG,
        .axis_mask = (uint8_t) (axis_mask & GDBG_AXIS_ALL),
        .enable = false,
        .pulses = pulses,
        .speed = speed,
        .acc = acc
    };
    return enqueue(&command);
}

gdbg_result_t GantryDebug_Stop(uint8_t axis_mask)
{
    gdbg_command_t command = {
        .type = GDBG_CMD_STOP,
        .axis_mask = (uint8_t) (axis_mask & GDBG_AXIS_ALL),
        .enable = false,
        .pulses = 0,
        .speed = 0U,
        .acc = 0U
    };
    return enqueue(&command);
}

gdbg_result_t GantryDebug_Unprotect(uint8_t axis_mask)
{
    gdbg_command_t command = {
        .type = GDBG_CMD_UNPROTECT,
        .axis_mask = (uint8_t) (axis_mask & GDBG_AXIS_ALL),
        .enable = false,
        .pulses = 0,
        .speed = 0U,
        .acc = 0U
    };
    return enqueue(&command);
}

void GantryDebug_GetStatus(gdbg_status_t * status)
{
    if (NULL == status)
    {
        return;
    }
    taskENTER_CRITICAL();
    for (uint8_t i = 0U; i < GDBG_AXIS_COUNT; i++)
    {
        status->pulse[i] = s_pulse[i];
        status->arrived[i] = s_arrived[i];
    }
    status->completed = s_completed;
    taskEXIT_CRITICAL();
}

bool GantryDebug_IsIdle(void)
{
    return s_initialized && (0U == uxQueueMessagesWaiting(s_command_queue));
}

void GantryDebug_GetRxRaw(uint8_t axis, gdbg_rx_raw_t * raw)
{
    if ((NULL == raw) || (axis >= GDBG_AXIS_COUNT))
    {
        return;
    }
    taskENTER_CRITICAL();
    *raw = s_rx_raw[axis];
    taskEXIT_CRITICAL();
}

void GantryDebug_OnCanRxFromISR(uint32_t frame_id, const uint8_t * data, uint8_t len)
{
    if ((NULL == data) || (len > GDBG_RX_RAW_MAX))
    {
        return;
    }
    /* 应答帧 ID 可能为 0x0100..0x0400（发送 ID）或带标志位；按低 16 位归类 */
    uint32_t id_low = frame_id & 0x0000FFFFU;
    uint8_t axis = GDBG_AXIS_COUNT;
    for (uint8_t i = 0U; i < GDBG_AXIS_COUNT; i++)
    {
        if (id_low == (s_axis_ids[i] & 0x0000FFFFU))
        {
            axis = i;
            break;
        }
    }
    if (axis >= GDBG_AXIS_COUNT)
    {
        return; /* 非 ZDT 应答，忽略 */
    }
    /* 到位应答识别：{FD, 9F, 6B}=位置到位；{9A, 02, 6B}=回零完成 */
    if ((len == 3U) && (data[0] == 0xFDU) && (data[1] == 0x9FU) && (data[2] == 0x6BU))
    {
        s_arrived[axis]++;
        /* 同时通知 gantry_robot：正式取药流程靠该事件推进（否则移动 30s 超时） */
        Gantry_NotifyAxisDoneFromISR((gantry_axis_t) axis, true, NULL);
    }
    else if ((len == 3U) && (data[0] == 0x9AU) && (data[1] == 0x02U) && (data[2] == 0x6BU))
    {
        s_arrived[axis]++;
        Gantry_NotifyAxisDoneFromISR((gantry_axis_t) axis, true, NULL);
    }
    volatile gdbg_rx_raw_t * dst = &s_rx_raw[axis];
    for (uint8_t i = 0U; i < len; i++)
    {
        dst->data[i] = data[i];
    }
    dst->len = len;
    dst->frame_id = frame_id;
    dst->seq++; /* ISR 中 volatile 计数，GUI 侧只读 */

    /* 追加到协议探测环形缓冲 */
    uint8_t head = s_rx_ring_head[axis];
    volatile gdbg_ring_frame_t * slot = &s_rx_ring[axis][head % GDBG_RING_DEPTH];
    for (uint8_t i = 0U; i < len; i++)
    {
        slot->data[i] = data[i];
    }
    slot->len = len;
    slot->frame_id = frame_id;
    s_rx_ring_head[axis] = (uint8_t) ((head + 1U) % GDBG_RING_DEPTH);
}

static void dispatch(const gdbg_command_t * command)
{
    for (uint8_t axis = 0U; axis < GDBG_AXIS_COUNT; axis++)
    {
        if (0U == (command->axis_mask & axis_bit(axis)))
        {
            continue;
        }
        switch (command->type)
        {
            case GDBG_CMD_ENABLE:
                ZDT_Enable(s_axis_ids[axis], command->enable);
                break;
            case GDBG_CMD_SET_ZERO:
                ZDT_SetZero(s_axis_ids[axis]);
                s_pulse[axis] = 0;
                break;
            case GDBG_CMD_GOZERO:
                /* 回零运动：电机自动回到零点；本地累计清零（与工程 ZDT_Gozero 一致） */
                ZDT_Gozero(s_axis_ids[axis], false);
                s_pulse[axis] = 0;
                break;
            case GDBG_CMD_JOG:
                /* ZDT_MovePosition 的 payload[9]=0x01 是绝对位置模式：
                 * 必须发送"新的绝对目标 = 当前累计位置 + 增量"，
                 * 否则连续点动时第二次目标不变、电机不动。
                 * 绝对目标重复发送无害——连发 3 次抵抗驱动偶发丢命令
                 * （界面偶发"点了没反应"）。
                 * 点动前先使能 + 解除堵转保护（0x0E 0x52）：
                 * 驱动堵转/顶限位后进入保护，不解除则移动命令全不执行——
                 * 实测抓手 JOG 命令发出但电机无响应即此情况。 */
                ZDT_Enable(s_axis_ids[axis], true);
                R_BSP_SoftwareDelay(5U, BSP_DELAY_UNITS_MILLISECONDS);
                {
                    uint8_t up[3] = {0x0EU, 0x52U, 0x6BU};
                    (void) gdbg_send_raw(s_axis_ids[axis], up, sizeof(up));
                    R_BSP_SoftwareDelay(5U, BSP_DELAY_UNITS_MILLISECONDS);
                    (void) gdbg_send_raw(s_axis_ids[axis], up, sizeof(up));
                    R_BSP_SoftwareDelay(5U, BSP_DELAY_UNITS_MILLISECONDS);
                }
                s_pulse[axis] += command->pulses;
                ZDT_MovePosition(s_axis_ids[axis], s_pulse[axis],
                                 command->speed, command->acc, false);
                R_BSP_SoftwareDelay(5U, BSP_DELAY_UNITS_MILLISECONDS);
                ZDT_MovePosition(s_axis_ids[axis], s_pulse[axis],
                                 command->speed, command->acc, false);
                R_BSP_SoftwareDelay(5U, BSP_DELAY_UNITS_MILLISECONDS);
                ZDT_MovePosition(s_axis_ids[axis], s_pulse[axis],
                                 command->speed, command->acc, false);
                break;
            case GDBG_CMD_UNPROTECT:
            {
                /* 解除堵转保护：CAN 版 {0x0E, 0x52, 0x6B} */
                uint8_t up[3] = {0x0EU, 0x52U, 0x6BU};
                (void) gdbg_send_raw(s_axis_ids[axis], up, sizeof(up));
                break;
            }
            case GDBG_CMD_STOP:
                ZDT_Stop(s_axis_ids[axis]);
                break;
            default:
                break;
        }
    }
    s_completed++;
}

void GantryDebug_Service(void)
{
    if (!s_initialized)
    {
        return;
    }
    gdbg_command_t command;
    if (pdPASS != xQueueReceive(s_command_queue, &command, 0U))
    {
        return;
    }
    /* 与 gantry_robot 的互斥：
     * - 点动(JOG) 在机械臂移动/回零中暂缓（放回队尾），避免双写 CAN 干扰正式流程；
     * - 急停/解除/回零/设零/使能 始终放行——自动归零挂起或流程出错时，
     *   调试页仍可急停/解除/手动回零自救（旧实现用 Gantry_IsIdle 挡住全部命令，
     *   导致归零挂起时整个调试页失灵）。 */
    gantry_status_t gst;
    Gantry_GetStatus(&gst);
    bool robot_moving = (gst.state == GANTRY_STATE_MOVING) || (gst.state == GANTRY_STATE_HOMING);
    if (robot_moving && (command.type == GDBG_CMD_JOG))
    {
        (void) xQueueSend(s_command_queue, &command, 0U); /* 放回队尾 */
        return;
    }
    dispatch(&command);
}
