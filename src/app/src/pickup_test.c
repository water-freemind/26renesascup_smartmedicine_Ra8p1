/*
 * 取药流程模块实现
 * 非阻塞状态机：每个阶段向 gantry_robot 命令队列投递一条命令，等待
 * Gantry_IsIdle()（命令执行完毕）后推进下一阶段。
 *
 * 关键约束（用户验收要求）：
 *   1. 必须先把 Z 收回（0mm）才能移动 XY —— 本流程 Z_UP 完成前不发出任何 XY 命令；
 *   2. XY 可以（且应当）一起动 —— 使用 Gantry_MoveXYTo（X|Y 同步并行）；
 *   3. 多药取药：一种药放到取药区并放下收回 Z 之后，才能移动去下一个药柜。
 *
 * 两种模式：
 *   - 单目标测试（PickupTest_Start）：目标 = 参数 test_shelf/test_slot（取药调试页点格子）；
 *   - 取药单多药（PickupTest_StartOrder）：药品 code 列表 → 逐个按 drug_db 仓位
 *     （"X<脉冲>Y<脉冲>"）取药放到取药区，全部完成结束。
 */

#include "pickup_test.h"

#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "gantry_robot.h"
#include "pickup_params.h"
#include "drug_db.h"
#include "esp01s_proto.h"   /* RELAY_CTRL：云端继电器开关（网页 WebHID 执行） */

typedef enum
{
    PH_IDLE = 0,
    PH_ENABLE,      /* 使能四轴 */
    PH_HOME,        /* 回零：Z 先单独回零，再 X|Y|抓手 一起回零（回到第一个药柜） */
    PH_TO_BOX,      /* XY → 当前药/格位置，Z 保持收回 */
    PH_Z_DOWN,      /* Z → z_reach_mm（伸入药柜） */
    PH_GRIP,        /* 夹爪 → +grip_pulses（全闭=+1550，正值合上；量程 0~1550） */
    PH_Z_UP,        /* Z → 0（先收 Z！） */
    PH_TO_STORE,    /* XY → 取药区(store_x, store_y)（Z 已收回，XY 同步并行） */
    PH_Z_DOWN2,     /* Z → z_reach_mm（放下） */
    PH_RELEASE,     /* 夹爪 → 0（张开） */
    PH_Z_UP2,       /* Z → 0（收好） */
    PH_NEXT,        /* 本药完成：取下一药（若有），否则回零点备机 */
    PH_PARK,        /* 备机：最后一盒/单盒放完后 XY → (0,0) 回零点（Z/抓手已在 0） */
    PH_DONE
} pickup_phase_t;

static pickup_phase_t s_phase = PH_IDLE;
static bool s_waiting;              /* 已投递命令，等待 gantry_robot 空闲 */
static char s_text[56];

/* 多药取药单 */
static char s_order_items[PICKUP_ORDER_MAX_ITEMS][32];
static uint32_t s_order_count;
static uint32_t s_order_index;      /* 当前正在取第几味药（0 起） */
/* 显式坐标模式（云端二维码 layer/x 定位，不查 drug_db；手册 §6） */
static float s_order_x_mm[PICKUP_ORDER_MAX_ITEMS];
static float s_order_y_mm[PICKUP_ORDER_MAX_ITEMS];
static float s_order_w_mm[PICKUP_ORDER_MAX_ITEMS]; /* 每味药宽度 mm（items[].w） */
static bool  s_order_has_xy;        /* true = 用 s_order_x/y_mm 定位 */
static bool  s_order_has_w;         /* true = 用 s_order_w_mm 算夹爪闭合 */

/* 云端出药模式（DISPENSE_ACTION）：显式目标坐标 + 盒数 */
static float    s_cloud_x_mm;
static float    s_cloud_y_mm;
static uint32_t s_cloud_qty;        /* 剩余盒数 */
static bool     s_cloud_active;

/* 云端存药模式（STORAGE_PLACE）：显式目标坐标（layer/x → 层Y/x mm） */
static float s_place_x_mm;
static float s_place_y_mm;
static float s_place_w_mm;          /* 药品宽度 mm（item.w；≤0 = 回退 grip_pulses） */
static bool  s_place_active;

/* 继电器控制（《硬件指导_继电器控制RELAY_CTRL.md》——上报云端 → 网页 WebHID
 * 执行 USB 继电器）：
 *   - 取药/存药开始（PH_ENABLE）→ 关闭 2 号继电器；
 *   - Z 轴开始伸出（PH_Z_DOWN / PH_Z_DOWN2）→ 打开 1 号继电器（吸取/保持）；
 *   - 夹爪开始松开（PH_RELEASE）→ 关闭 1 号继电器并同时打开 2 号继电器
 *     （放药瞬间释放吸取、切换保持态）；
 *   - 流程完成/失败 → 打开 2 号继电器并关闭 1 号继电器（收尾兜底）。
 * 标志记录"期望状态"，仅状态变化时上报（避免重复广播）。 */
static bool s_relay1_on;
static bool s_relay2_on;

#define PICKUP_RELAY_VACUUM (1U)    /* 1 号：吸取（Z 伸出时开） */
#define PICKUP_RELAY_HOLD   (2U)    /* 2 号：保持/收尾（流程外常开，取放时关） */

/* 期望状态变化才上报（幂等、省流量） */
static void relay_set_if_changed(uint8_t relay, bool on)
{
    bool * p_state = (relay == PICKUP_RELAY_VACUUM) ? &s_relay1_on : &s_relay2_on;
    if ((NULL == p_state) || (*p_state == on))
    {
        return;
    }
    *p_state = on;
    esp01s_proto_send_relay_ctrl(relay, on);
}

static void fail(const char * reason)
{
    (void) snprintf(s_text, sizeof(s_text), "取药失败：%s", reason);
    s_phase = PH_IDLE;
    s_waiting = false;
    /* 异常终止也要恢复继电器：开 2 号、关 1 号（防吸盘一直吸着） */
    relay_set_if_changed(PICKUP_RELAY_HOLD, true);
    relay_set_if_changed(PICKUP_RELAY_VACUUM, false);
}

void PickupTest_Start(void)
{
    /* 上次失败可能遗留 FAULT/STOPPED 状态，先尝试清除（非 FAULT 时返回 BUSY，无害） */
    (void) Gantry_ClearFault();
    s_cloud_active = false;
    s_place_active = false;
    s_order_count = 0U;
    s_phase = PH_ENABLE;
    s_waiting = false;
    (void) snprintf(s_text, sizeof(s_text), "取药测试启动：使能电机...");
}

void PickupTest_StartOrder(const char (*p_items)[32], uint32_t count)
{
    PickupTest_StartOrderXY(p_items, NULL, NULL, NULL, count);
}

void PickupTest_StartOrderXY(const char (*p_items)[32],
                             const float * p_x_mm, const float * p_y_mm,
                             const float * p_w_mm,
                             uint32_t count)
{
    if ((NULL == p_items) || (count == 0U))
    {
        return;
    }
    if (count > PICKUP_ORDER_MAX_ITEMS)
    {
        count = PICKUP_ORDER_MAX_ITEMS;
    }
    (void) Gantry_ClearFault();
    s_cloud_active = false;
    s_place_active = false;
    s_order_count = count;
    s_order_index = 0U;
    s_order_has_xy = ((NULL != p_x_mm) && (NULL != p_y_mm));
    s_order_has_w = (NULL != p_w_mm);
    for (uint32_t i = 0U; i < count; i++)
    {
        (void) snprintf(s_order_items[i], sizeof(s_order_items[i]), "%s", p_items[i]);
        if (s_order_has_xy)
        {
            s_order_x_mm[i] = p_x_mm[i];
            s_order_y_mm[i] = p_y_mm[i];
        }
        if (s_order_has_w)
        {
            s_order_w_mm[i] = p_w_mm[i];
        }
    }
    s_phase = PH_ENABLE;
    s_waiting = false;
    (void) snprintf(s_text, sizeof(s_text), "取药单启动：共%lu种药，第1种...", (unsigned long) count);
}

void PickupTest_StartCloud(float x_mm, float y_mm, uint32_t qty)
{
    if (qty == 0U)
    {
        qty = 1U;
    }
    /* 上次失败可能遗留 FAULT/STOPPED 状态，先尝试清除 */
    (void) Gantry_ClearFault();
    s_cloud_active = true;
    s_cloud_x_mm = x_mm;
    s_cloud_y_mm = y_mm;
    s_cloud_qty = qty;
    s_place_active = false;
    s_order_count = 0U;
    s_phase = PH_ENABLE;
    s_waiting = false;
    (void) snprintf(s_text, sizeof(s_text), "云端出药：去(%d,%d)取%lu盒...",
                    (int) x_mm, (int) y_mm, (unsigned long) qty);
}

void PickupTest_StartPlace(float x_mm, float y_mm, float w_mm)
{
    /* 上次失败可能遗留 FAULT/STOPPED 状态，先尝试清除 */
    (void) Gantry_ClearFault();
    s_place_active = true;
    s_place_x_mm = x_mm;
    s_place_y_mm = y_mm;
    s_place_w_mm = w_mm;
    s_cloud_active = false;
    s_order_count = 0U;
    s_phase = PH_ENABLE;
    s_waiting = false;
    (void) snprintf(s_text, sizeof(s_text), "存药：去取药口抓药放到(%d,%d)...",
                    (int) x_mm, (int) y_mm);
}

bool PickupTest_IsPlaceMode(void)
{
    return s_place_active;
}

bool PickupTest_IsCloudMode(void)
{
    return s_cloud_active;
}

bool PickupTest_IsRunning(void)
{
    return (s_phase != PH_IDLE) && (s_phase != PH_DONE);
}

void PickupTest_Reset(void)
{
    s_phase = PH_IDLE;
    s_waiting = false;
    s_order_count = 0U;
    s_order_index = 0U;
    s_cloud_active = false;
    s_place_active = false;
    s_text[0] = '\0';
    /* 兜底：流程结束/复位后继电器恢复"开 2 号、关 1 号"
     * （DONE 转换已发过则状态未变、不再上报） */
    relay_set_if_changed(PICKUP_RELAY_HOLD, true);
    relay_set_if_changed(PICKUP_RELAY_VACUUM, false);
}

void PickupTest_GetStatus(pickup_test_state_t * state, const char ** text)
{
    if (state != NULL)
    {
        if ((s_phase == PH_IDLE))
        {
            *state = PICKUP_TEST_IDLE;
        }
        else if (s_phase == PH_DONE)
        {
            *state = PICKUP_TEST_DONE_OK;
        }
        else
        {
            *state = PICKUP_TEST_RUNNING;
        }
    }
    if (text != NULL)
    {
        *text = s_text;
    }
}

void PickupTest_GetProgress(uint32_t * p_index, uint32_t * p_count)
{
    if (p_index != NULL)
    {
        *p_index = s_order_index;
    }
    if (p_count != NULL)
    {
        *p_count = s_order_count;
    }
}

/* 解析仓位字符串 "X<脉冲>Y<脉冲>" → X/Y 脉冲；失败返回 false */
static bool parse_position_pulses(const char * p_pos, int32_t * p_x, int32_t * p_y)
{
    if ((NULL == p_pos) || (NULL == p_x) || (NULL == p_y))
    {
        return false;
    }
    const char * px = strchr(p_pos, 'X');
    if (NULL == px)
    {
        px = strchr(p_pos, 'x');
    }
    const char * py = strchr(p_pos, 'Y');
    if (NULL == py)
    {
        py = strchr(p_pos, 'y');
    }
    if ((NULL == px) || (NULL == py))
    {
        return false;
    }
    char * end = NULL;
    long vx = strtol(px + 1, &end, 10);
    if ((end == px + 1) || (NULL == end))
    {
        return false;
    }
    long vy = strtol(py + 1, &end, 10);
    if ((end == py + 1) || (NULL == end))
    {
        return false;
    }
    *p_x = (int32_t) vx;
    *p_y = (int32_t) vy;
    return true;
}

/* 当前药目标位置（mm）：云端出药模式用显式坐标；取药单模式优先用二维码
 * 显式坐标（手册 §6 layer/x 机械定位），否则读 drug_db position
 * （"X脉冲Y脉冲"→mm）；单目标模式用参数 test_shelf/test_slot。
 * 成功返回 true。 */
static bool current_target_mm(float * p_x_mm, float * p_y_mm)
{
    const pickup_params_t * pp = PickupParams_Get();
    if (s_cloud_active)
    {
        *p_x_mm = s_cloud_x_mm;
        *p_y_mm = s_cloud_y_mm;
        return true;
    }
    if (s_order_count > 0U)
    {
        if (s_order_has_xy)
        {
            /* 云端二维码自带坐标（items[].x mm / layer → 层Y mm） */
            *p_x_mm = s_order_x_mm[s_order_index];
            *p_y_mm = s_order_y_mm[s_order_index];
            return true;
        }
        const char * code = s_order_items[s_order_index];
        const drug_db_entry_t * p_drug = drug_db_lookup(code);
        if ((NULL == p_drug) || (NULL == p_drug->position))
        {
            return false;
        }
        int32_t px = 0, py = 0;
        if (!parse_position_pulses(p_drug->position, &px, &py))
        {
            return false;
        }
        /* 脉冲 → mm：X/Y 轴标定 3600/84 脉冲/mm */
        const float ppm = 3600.0f / 84.0f;
        *p_x_mm = (float) px / ppm;
        *p_y_mm = (float) py / ppm;
        return true;
    }
    *p_x_mm = PickupParams_TestSlotX(pp->test_slot);   /* 调试页：第1列70mm/第2列折中233.3mm/第3列X最大396.7mm */
    *p_y_mm = PickupParams_ShelfY(pp->test_shelf);
    return true;
}

/* 当前药的目标夹爪闭合脉冲：
 * - 订单模式（二维码 v2/v1 自带 items[].w）→ 查表 (w - 1mm 夹紧)；
 * - 存药模式（STORAGE_PLACE item.w）→ 查表 (w - 1mm 夹紧)；
 * - 其余（出药/本地定位/测试）→ 参数盒宽 box_width_mm → 查表；
 * - 无有效宽度/查表越界 → 回退参数 grip_pulses。
 * 返回 1~1550 脉冲。 */
static int32_t current_grip_pulses(void)
{
    const pickup_params_t * pp = PickupParams_Get();
    float width = 0.0f;

    if (s_place_active)
    {
        width = s_place_w_mm;
    }
    else if ((s_order_count > 0U) && s_order_has_w)
    {
        width = s_order_w_mm[s_order_index];
    }
    else
    {
        width = pp->box_width_mm;   /* 出药/本地定位/测试均按参数盒宽 */
    }

    if (width > 1.0f)
    {
        /* 闭合中间宽度 = 物品宽度 - 夹紧余量（参数 grip_margin_mm，默认 1mm；
         * 调小余量 = 夹得更紧/力度更大）。查表失败/越界时回退 grip_pulses。 */
        float const margin = (pp->grip_margin_mm >= 0.0f) ? pp->grip_margin_mm : 1.0f;
        int32_t const pulses = PickupParams_GripWidthToPulses(width - margin);
        if (pulses >= 1)
        {
            return pulses;
        }
    }
    return (int32_t) pp->grip_pulses;
}

static void pickup_send_current_phase(void)
{
    const pickup_params_t * pp = PickupParams_Get();
    gantry_result_t r = GANTRY_OK;
    switch (s_phase)
    {
        case PH_ENABLE:
            r = Gantry_EnableAxes(GANTRY_AXIS_MASK_ALL | GANTRY_AXIS_MASK_CATCH, true);
            /* 解除堵转保护（撞限位/顶量程后驱动进入保护，不解除则移动命令
             * 全部不执行——实测 Z 顶量程后 arrived=0、命令发了电机不动）。
             * 每次取药前都解一次，保证各轴可移动。 */
            (void) Gantry_UnprotectAxes(GANTRY_AXIS_MASK_ALL | GANTRY_AXIS_MASK_CATCH);
            /* 取药/存药开始：关闭 2 号继电器（用户要求：取放前关 2 号） */
            relay_set_if_changed(PICKUP_RELAY_HOLD, false);
            (void) snprintf(s_text, sizeof(s_text), "取药：使能电机...");
            break;
        case PH_HOME:
            /* 只回 X/Y/Z（先收 Z 再回零）；抓手(CATCH)不回零——
             * 用户约束：抓手只在 上电 或 电机调试界面 归零，取药流程一律用 move。
             * 抓手零点由上电自动归零或调试页抓手"归零"确定（此时已 homed）。 */
            r = Gantry_Home(GANTRY_AXIS_MASK_ALL);
            (void) snprintf(s_text, sizeof(s_text), "取药：回零（回第一个药柜）...");
            break;
        case PH_TO_BOX:
        {
            /* 存药模式：第一步去取药口（暂存区）抓药，不进 current_target_mm */
            if (s_place_active)
            {
                r = Gantry_MoveXYTo(pp->store_x_mm, pp->store_y_mm);
                (void) snprintf(s_text, sizeof(s_text), "存药：XY→取药口(%d,%d)抓药...",
                                (int) pp->store_x_mm, (int) pp->store_y_mm);
                break;
            }
            float x_mm = 0.0f, y_mm = 0.0f;
            if (!current_target_mm(&x_mm, &y_mm))
            {
                fail("仓位坐标无效");
                return;
            }
            r = Gantry_MoveXYTo(x_mm, y_mm);
            if (s_cloud_active)
            {
                (void) snprintf(s_text, sizeof(s_text), "云端出药：去(%d,%d)格...",
                                (int) x_mm, (int) y_mm);
            }
            else if (s_order_count > 0U)
            {
                const char * code = s_order_items[s_order_index];
                const drug_db_entry_t * p_drug = drug_db_lookup(code);
                (void) snprintf(s_text, sizeof(s_text), "取药(%lu/%lu)：去%s仓位...",
                                (unsigned long) s_order_index + 1U,
                                (unsigned long) s_order_count,
                                (p_drug != NULL) ? p_drug->name : code);
            }
            else
            {
                (void) snprintf(s_text, sizeof(s_text), "取药测试：XY→药柜(第%u层 第%u格)...",
                                (unsigned) pp->test_shelf + 1U, (unsigned) pp->test_slot + 1U);
            }
            break;
        }
        case PH_Z_DOWN:
            /* Z 轴开始往前移动：打开 1 号继电器（吸取/保持） */
            relay_set_if_changed(PICKUP_RELAY_VACUUM, true);
            r = Gantry_MoveAxisTo(GANTRY_AXIS_Z, pp->z_reach_mm);
            (void) snprintf(s_text, sizeof(s_text), "取药：Z伸出%dmm...", (int) pp->z_reach_mm);
            break;
        case PH_GRIP:
        {
            /* 夹爪闭合 = 正脉冲往里合（量程 0~1550：0=全开，正值=合上，全闭 +1550）。
             * 目标脉冲按要夹的物品宽度自动计算（查表，留 1mm 夹紧）；
             * 无宽度信息时用参数 grip_pulses。 */
            int32_t const grip = current_grip_pulses();
            r = Gantry_MoveAxisTo(GANTRY_AXIS_CATCH, (float) grip);
            if (s_place_active)
            {
                (void) snprintf(s_text, sizeof(s_text), "存药：夹爪闭合抓药(%d脉冲)...", (int) grip);
            }
            else
            {
                (void) snprintf(s_text, sizeof(s_text), "取药：夹爪闭合(%d脉冲)...", (int) grip);
            }
            break;
        }
        case PH_Z_UP:
            /* Z 收回：取药流程要求 Z 收回到 0（用户确认）。
             * 顺序约束：先收回 Z 再移动 XY。 */
            r = Gantry_MoveAxisTo(GANTRY_AXIS_Z, 0.0f);
            (void) snprintf(s_text, sizeof(s_text), "取药：Z收回（先收Z再动XY）...");
            break;
        case PH_TO_STORE:
            /* 存药模式：目标位 = 云端下发坐标（layer/x → 层Y/x mm） */
            if (s_place_active)
            {
                r = Gantry_MoveXYTo(s_place_x_mm, s_place_y_mm);
                (void) snprintf(s_text, sizeof(s_text), "存药：XY→目标仓(%d,%d)...",
                                (int) s_place_x_mm, (int) s_place_y_mm);
                break;
            }
            r = Gantry_MoveXYTo(pp->store_x_mm, pp->store_y_mm);
            (void) snprintf(s_text, sizeof(s_text), "取药：XY→取药区(%d,%d)...",
                            (int) pp->store_x_mm, (int) pp->store_y_mm);
            break;
        case PH_Z_DOWN2:
            /* Z 轴再次往前移动（放下）：1 号继电器保持打开（状态未变不发） */
            relay_set_if_changed(PICKUP_RELAY_VACUUM, true);
            r = Gantry_MoveAxisTo(GANTRY_AXIS_Z, pp->z_reach_mm);
            (void) snprintf(s_text, sizeof(s_text), "取药：Z伸出放下...");
            break;
        case PH_RELEASE:
            /* 张开 = 量程下限 0 脉冲（全开） */
            r = Gantry_MoveAxisTo(GANTRY_AXIS_CATCH, 0.0f);
            /* 夹爪开始松开：关闭 1 号继电器、同时打开 2 号继电器
             * （放药瞬间释放吸取并切换保持态） */
            relay_set_if_changed(PICKUP_RELAY_VACUUM, false);
            relay_set_if_changed(PICKUP_RELAY_HOLD, true);
            (void) snprintf(s_text, sizeof(s_text), "取药：夹爪张开(-200脉冲)...");
            break;
        case PH_Z_UP2:
            /* Z 收回：收回到 0（用户确认） */
            r = Gantry_MoveAxisTo(GANTRY_AXIS_Z, 0.0f);
            (void) snprintf(s_text, sizeof(s_text), "取药：Z收回...");
            break;
        case PH_NEXT:
            /* 存药模式：1 盒已放到目标仓 → 回零点备机。
             * 注意 s_place_active 保持到 Reset 才清（PH_PARK/DONE 期间
             * IsPlaceMode() 仍为 true），防止 GUI 把存药 DONE 误当取药消费。 */
            if (s_place_active)
            {
                s_phase = PH_PARK;
                (void) snprintf(s_text, sizeof(s_text), "存药完成，回零点备机...");
                s_waiting = false;
                return;
            }
            /* 云端出药：同一格剩余盒数 > 0 → 继续同格取放（逐盒放到取药区） */
            if (s_cloud_active)
            {
                if (s_cloud_qty > 1U)
                {
                    s_cloud_qty--;
                    s_phase = PH_TO_BOX;
                    (void) snprintf(s_text, sizeof(s_text),
                                    "云端出药：同格再取一盒（剩%lu盒）...",
                                    (unsigned long) s_cloud_qty);
                    s_waiting = false;
                    return;
                }
                /* 注意 s_cloud_active 保持到 Reset 才清（PH_PARK/DONE 期间
                 * IsCloudMode() 仍为 true）：云端出药 DONE 由协议层
                 * （proto_dispense_service）消费并 Reset，GUI 取药轮询
                 * 在 IsCloudMode 时让位，防误扣库存/误发核销/漏发回报。 */
                s_phase = PH_PARK;   /* 最后一盒放完 → 回零点备机 */
                (void) snprintf(s_text, sizeof(s_text), "云端出药完成，回零点备机...");
                s_waiting = false;
                return;
            }
            /* 本药已放到取药区且 Z 收回：取下一味药（若有），否则回零点备机 */
            s_order_index++;
            if (s_order_index < s_order_count)
            {
                s_phase = PH_TO_BOX;
                (void) snprintf(s_text, sizeof(s_text), "取药：第%lu种药完成，去下一药柜...",
                                (unsigned long) s_order_index);
                s_waiting = false;
                return;
            }
            s_phase = PH_PARK;   /* 最后一味/单盒放完 → 回零点备机 */
            (void) snprintf(s_text, sizeof(s_text), "取药单完成（%lu种药已放到取药区），回零点备机...",
                            (unsigned long) s_order_count);
            s_waiting = false;
            return;
        case PH_PARK:
            /* 备机：全部轴 move 到 0（Z/抓手已在 0，只需 XY 回零点）。
             * 用户要求：最后一盒放完后直接全部 move 到 0 备机。 */
            r = Gantry_MoveXYTo(0.0f, 0.0f);
            (void) snprintf(s_text, sizeof(s_text), "备机：回零点(0,0)...");
            break;
        default:
            return;
    }
    if (r != GANTRY_OK)
    {
        char buf[40];
        (void) snprintf(buf, sizeof(buf), "err=%d", (int) r);
        fail(buf);
        return;
    }
    s_waiting = true;
}

void PickupTest_Service(void)
{
    if ((s_phase == PH_IDLE) || (s_phase == PH_DONE))
    {
        return;
    }
    if (s_waiting)
    {
        /* 先检查 FAULT/STOPPED 再检查空闲：命令执行出错时 Gantry_IsIdle()
         * 恒为 false（state != IDLE），若先查空闲会永远 return，界面卡死
         * 在"等待上一条命令完成"，失败永远不显示。 */
        gantry_status_t st;
        Gantry_GetStatus(&st);
        if ((st.state == GANTRY_STATE_FAULT) || (st.state == GANTRY_STATE_STOPPED))
        {
            char buf[40];
            (void) snprintf(buf, sizeof(buf), "err=%d", (int) st.last_error);
            fail(buf);
            return;
        }
        /* 等待当前命令执行完毕 */
        if (!Gantry_IsIdle())
        {
            return;
        }
        s_waiting = false;
        s_phase = (pickup_phase_t) (s_phase + 1);
        if (s_phase > PH_PARK)
        {
            s_phase = PH_DONE;
            /* 取药/存药完成：打开 2 号继电器、关闭 1 号继电器（用户要求） */
            relay_set_if_changed(PICKUP_RELAY_HOLD, true);
            relay_set_if_changed(PICKUP_RELAY_VACUUM, false);
        }
    }
    pickup_send_current_phase();
}
