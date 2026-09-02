/*
 * 取药系统参数模块实现
 * - 默认值 = 之前的药柜设置（cabinet 宏）；
 * - 持久化到 OSPI 参数区（主 0x01000000 + 备份 0x01001000，双份 CRC16）；
 *   参数区远离 TTF（0~0x94B000）与图标区（0x950000 起），16MB 偏移安全。
 * - 写 OSPI 要求 8 字节对齐、长度 8 的倍数：按 PICKUP_PARAMS_STORAGE_SIZE
 *   （结构体向上取整到 8）整块写。
 */

#include "pickup_params.h"

#include <stddef.h>
#include <string.h>

#include "ospi_storage.h"
#include "gantry_robot.h"

#define PICKUP_PARAMS_MAGIC        (0x5A504153U) /* "ZPAS" */
#define PICKUP_PARAMS_VERSION      (4U)          /* v4：新增夹紧余量 grip_margin_mm（v3 新增药盒宽度 box_width） */
#define PICKUP_PARAMS_FLASH_ADDR   (0x01000000U)
#define PICKUP_PARAMS_FLASH_BACKUP (0x01001000U)
#define PICKUP_PARAMS_STORAGE_SIZE (((sizeof(pickup_params_t) + 7U) & ~7U))

/* X 轴实测最大量程（mm）：用户实测最远 17000 脉冲 → 17000/(3600/84) ≈ 396.7mm。
 * 格宽不得超过 X 量程/格数；调试页格子均分上限也用它。 */
#define PICKUP_PARAMS_X_TRAVEL_MM  (396.7f)
/* 夹爪最小间距 3.5cm：格子最小宽度不得小于它（否则夹爪伸不进格子/刮相邻药盒）；
 * 用户确认：不会放夹爪最小都夹不住的东西（药盒宽度 ≥ 夹爪可夹范围） */
#define PICKUP_PARAMS_MIN_SLOT_MM  (35.0f)
/* 自动格宽的余量：格子宽度 = 药盒宽度 + 10mm（抓取余量） */
#define PICKUP_PARAMS_SLOT_MARGIN_MM (10.0f)

/* 编译期默认值：保证 PickupParams_Get() 在任何时刻（含 Camera 线程尚未
 * 完成 OSPI 加载、Motor 线程等待超时）都返回有效的"之前的药柜设置"，
 * 上电自动归零默认开启。crc16 在 LoadDefault/Init/Save 时重算。 */
static pickup_params_t s_params = {
    .magic           = PICKUP_PARAMS_MAGIC,
    .version         = PICKUP_PARAMS_VERSION,
    .crc16           = 0U,
    .shelf_count     = 3U,
    .slots_per_row   = 3U,
    .test_shelf      = 0U,
    .test_slot       = 1U,   /* 测试目标默认第 2 格（非零点，XY 有实际移动） */
    .shelf_y_mm      = {9.33f, 149.33f, 291.7f, 0.0f}, /* 层Y: 400/6400/12500脉冲 = 9.33/149.33/291.7mm（第一层200+100+150-50脉冲，第二层6300+100脉冲，第三层=Y最大12500） */
    .box_width_mm    = 88.0f,   /* 药盒宽度=8.8cm(88mm，用户确认)，格宽自动 = 盒宽+10mm=98mm */
    .slot_width_mm   = 0.0f,   /* 0 = 自动规划：药盒宽+10mm 余量（下限夹爪宽 30mm），格子贴合药盒 */
    .cabinet_x0_mm   = 0.0f,
    .z_reach_mm      = 151.8f,   /* Z 伸出到底：实测最大量程 151.8mm(6600脉冲) */
    .grip_pulses     = 651,     /* 夹爪闭合：目标中间宽度87mm(88mm盒留1mm夹紧)→查表651脉冲（标定表见 PickupParams_GripWidthToPulses） */
    .grip_margin_mm  = 2.0f,    /* 夹紧余量：闭合中间宽度 = 物品宽度 - 2mm（默认加大力度；参数页可改 0~5mm） */
    .store_x_mm      = 0.0f,
    .store_y_mm      = 291.7f,  /* 取药区高度 = Y 轴最大量程 12500 脉冲 = 291.7mm（用户确认） */
    .xy_speed        = 300U,
    .xy_acc          = 60U,
    .z_speed         = 1500U,
    .z_acc           = 60U,
    .grip_speed      = 300U,
    .grip_acc        = 60U,
    .auto_home_on_boot = 1U,
    .reserved        = {0U},
};
static bool s_ready;

/* 量程保护（防止 OSPI 旧参数超量程 → 取药点击软限位失败/可能损坏电机）：
 *   X ≤ 406mm、Y ≤ 291.7mm、Z 伸出 ≤ 151.8mm、夹爪 ≤ 1550 脉冲、
 *   药盒宽 10~200mm、格宽 0=自动(盒宽+10，下限夹爪宽30) 或固定值 ≤ 406/格数 */
static void clamp_ranges(pickup_params_t * p)
{
    if (NULL == p)
    {
        return;
    }
    for (uint8_t i = 0U; i < PICKUP_PARAMS_MAX_SHELVES; i++)
    {
        if (p->shelf_y_mm[i] > 291.7f) p->shelf_y_mm[i] = 291.7f;
        if (p->shelf_y_mm[i] < 0.0f) p->shelf_y_mm[i] = 0.0f;
    }
    if (p->box_width_mm > 200.0f) p->box_width_mm = 200.0f;
    if (p->box_width_mm < 10.0f) p->box_width_mm = 10.0f;
    float maxw = (p->slots_per_row > 0U) ? (PICKUP_PARAMS_X_TRAVEL_MM / (float) p->slots_per_row) : PICKUP_PARAMS_X_TRAVEL_MM;
    if (p->slot_width_mm > maxw) p->slot_width_mm = maxw;
    /* 0 = 自动规划(盒宽+10，下限夹爪宽30mm)；非 0 固定值不得小于夹爪宽度 30mm */
    if ((p->slot_width_mm > 0.0f) && (p->slot_width_mm < PICKUP_PARAMS_MIN_SLOT_MM)) p->slot_width_mm = PICKUP_PARAMS_MIN_SLOT_MM;
    if (p->z_reach_mm > 151.8f) p->z_reach_mm = 151.8f;
    if (p->z_reach_mm < 0.0f) p->z_reach_mm = 0.0f;
    if (p->grip_pulses > 1550L) p->grip_pulses = 1550L;   /* 全闭 +1550（量程 0~1550） */
    if (p->grip_pulses < 1L) p->grip_pulses = 1L;
    if (p->grip_margin_mm > 5.0f) p->grip_margin_mm = 5.0f;
    if (p->grip_margin_mm < 0.0f) p->grip_margin_mm = 0.0f;
    if (p->store_x_mm > PICKUP_PARAMS_X_TRAVEL_MM) p->store_x_mm = PICKUP_PARAMS_X_TRAVEL_MM;
    if (p->store_y_mm > 291.7f) p->store_y_mm = 291.7f;
}

static uint16_t crc16_update(uint16_t crc, uint8_t byte)
{
    crc ^= byte;
    for (uint8_t i = 0U; i < 8U; i++)
    {
        crc = (crc & 1U) ? (uint16_t) ((crc >> 1U) ^ 0xA001U) : (uint16_t) (crc >> 1U);
    }
    return crc;
}

/* CRC16 覆盖除 crc16 字段外的全部字节（含结构体尾部对齐填充，保证写入/读回一致） */
static uint16_t params_crc(const pickup_params_t * p)
{
    const uint8_t * b = (const uint8_t *) p;
    uint16_t crc = 0xFFFFU;
    uint32_t off = (uint32_t) offsetof(pickup_params_t, crc16);
    for (uint32_t i = 0U; i < off; i++)
    {
        crc = crc16_update(crc, b[i]);
    }
    for (uint32_t i = off + 2U; i < sizeof(pickup_params_t); i++)
    {
        crc = crc16_update(crc, b[i]);
    }
    return crc;
}

static bool params_valid(const pickup_params_t * p)
{
    if ((NULL == p) || (p->magic != PICKUP_PARAMS_MAGIC) || (p->version != PICKUP_PARAMS_VERSION))
    {
        return false;
    }
    if (params_crc(p) != p->crc16)
    {
        return false;
    }
    if ((p->shelf_count == 0U) || (p->shelf_count > PICKUP_PARAMS_MAX_SHELVES))
    {
        return false;
    }
    if ((p->slots_per_row == 0U) || (p->slots_per_row > 8U))
    {
        return false;
    }
    if (!(p->z_reach_mm >= 0.0f) || (p->grip_pulses <= 0))
    {
        return false;
    }
    return true;
}

void PickupParams_LoadDefault(pickup_params_t * p)
{
    if (NULL == p)
    {
        return;
    }
    memset(p, 0, sizeof(*p));
    p->magic        = PICKUP_PARAMS_MAGIC;
    p->version      = PICKUP_PARAMS_VERSION;
    p->shelf_count  = 3U;
    p->slots_per_row = 3U;
    p->test_shelf   = 0U;
    p->test_slot    = 1U;   /* 测试目标默认第 2 格（非零点，XY 有实际移动） */
    p->shelf_y_mm[0] = 9.33f;    /* 第一层 = 9.33mm(400脉冲 = 原200 + 100 + 150 - 50，用户确认) */
    p->shelf_y_mm[1] = 149.33f;  /* 第二层 = 149.33mm(6400脉冲 = 原6300 + 100，用户确认) */
    p->shelf_y_mm[2] = 291.7f;   /* 第三层 = Y 轴最大量程 291.7mm(12500脉冲) */
    p->box_width_mm = 88.0f;     /* 药盒宽度=8.8cm(88mm，用户确认)；格宽自动 = 盒宽+10mm=98mm */
    p->slot_width_mm = 0.0f;     /* 0 = 自动规划：药盒宽+10mm 余量（下限夹爪宽 30mm） */
    p->cabinet_x0_mm = 0.0f;     /* 第一个药柜 = 原点 */
    p->z_reach_mm    = 151.8f;   /* Z 伸出到底：实测最大量程 151.8mm(6600脉冲) */
    p->grip_pulses   = PickupParams_GripWidthToPulses(87.0f); /* 88mm盒中间留87mm(1mm夹紧)→查表651脉冲 */
    p->grip_margin_mm = 2.0f;  /* 夹紧余量：闭合中间宽度 = 物品宽度 - 2mm（默认加大力度；参数页可改 0~5mm） */
    p->store_x_mm    = 0.0f;
    p->store_y_mm    = 291.7f;   /* 取药区高度 = Y 轴最大量程 12500 脉冲 = 291.7mm（用户确认） */
    p->xy_speed      = 300U;
    p->xy_acc        = 60U;
    p->z_speed       = 1500U;
    p->z_acc         = 60U;
    p->grip_speed    = 300U;
    p->grip_acc      = 60U;
    p->auto_home_on_boot = 1U;
    p->crc16 = params_crc(p);
}

fsp_err_t PickupParams_Init(void)
{
    /* 无论如何先置 ready（用默认值），避免依赖方无限等待 */
    PickupParams_LoadDefault(&s_params);
    s_ready = true;

    if (!ospi_storage_get_ready())
    {
        return FSP_ERR_NOT_OPEN; /* OSPI 不可用，回退默认 */
    }

    uint8_t buf[PICKUP_PARAMS_STORAGE_SIZE];
    pickup_params_t tmp;
    bool adjusted = false;

    if (FSP_SUCCESS == ospi_storage_read(PICKUP_PARAMS_FLASH_ADDR, buf, sizeof(buf)))
    {
        memcpy(&tmp, buf, sizeof(pickup_params_t));
        if (params_valid(&tmp))
        {
            clamp_ranges(&tmp);
            if (tmp.crc16 != params_crc(&tmp))
            {
                adjusted = true; /* 超量程被修正，需要回写 */
            }
            s_params = tmp;
            if (adjusted)
            {
                (void) PickupParams_Save(&s_params); /* 修正后回写主/备份 */
            }
            return FSP_SUCCESS;
        }
    }
    if (FSP_SUCCESS == ospi_storage_read(PICKUP_PARAMS_FLASH_BACKUP, buf, sizeof(buf)))
    {
        memcpy(&tmp, buf, sizeof(pickup_params_t));
        if (params_valid(&tmp))
        {
            clamp_ranges(&tmp);
            if (tmp.crc16 != params_crc(&tmp))
            {
                adjusted = true;
            }
            s_params = tmp;
            if (adjusted)
            {
                (void) PickupParams_Save(&s_params);
            }
            return FSP_SUCCESS;
        }
    }
    return FSP_ERR_WRITE_FAILED; /* 主/备均无效，保持默认（FSP 无 READ_FAILED，复用该码） */
}

bool PickupParams_Ready(void)
{
    return s_ready;
}

const pickup_params_t * PickupParams_Get(void)
{
    return &s_params;
}

fsp_err_t PickupParams_Save(const pickup_params_t * p)
{
    if (NULL == p)
    {
        return FSP_ERR_ASSERTION;
    }
    if (!ospi_storage_get_ready())
    {
        return FSP_ERR_NOT_OPEN;
    }

    pickup_params_t copy = *p;
    copy.magic   = PICKUP_PARAMS_MAGIC;
    copy.version = PICKUP_PARAMS_VERSION;
    copy.crc16   = params_crc(&copy);

    uint8_t buf[PICKUP_PARAMS_STORAGE_SIZE];
    memset(buf, 0, sizeof(buf));
    memcpy(buf, &copy, sizeof(copy));

    /* 先备份后主：主区损坏时备份仍可恢复 */
    fsp_err_t err = ospi_storage_erase_sector(PICKUP_PARAMS_FLASH_BACKUP);
    if (FSP_SUCCESS == err)
    {
        err = ospi_storage_write(PICKUP_PARAMS_FLASH_BACKUP, buf, sizeof(buf));
    }
    if (FSP_SUCCESS == err)
    {
        err = ospi_storage_erase_sector(PICKUP_PARAMS_FLASH_ADDR);
    }
    if (FSP_SUCCESS == err)
    {
        err = ospi_storage_write(PICKUP_PARAMS_FLASH_ADDR, buf, sizeof(buf));
    }
    if (FSP_SUCCESS == err)
    {
        s_params = copy;
    }
    return err;
}

float PickupParams_SlotWidthEffective(void)
{
    const pickup_params_t * p = &s_params;
    if (p->slot_width_mm > 0.0f)
    {
        return p->slot_width_mm; /* 固定格宽（参数页设置，≥夹爪宽30mm） */
    }
    /* 自动规划：药盒宽度 + 10mm 抓取余量；下限 = 夹爪宽度 30mm；
     * 上限 = X 轴最大量程 / 每层格数（不超行程）。 */
    float w = p->box_width_mm + PICKUP_PARAMS_SLOT_MARGIN_MM;
    float maxw = (p->slots_per_row > 0U) ? (PICKUP_PARAMS_X_TRAVEL_MM / (float) p->slots_per_row) : PICKUP_PARAMS_X_TRAVEL_MM;
    if (w < PICKUP_PARAMS_MIN_SLOT_MM) w = PICKUP_PARAMS_MIN_SLOT_MM;
    if (w > maxw) w = maxw;
    return w;
}

float PickupParams_SlotX(uint8_t slot)
{
    const pickup_params_t * p = &s_params;
    if (slot >= p->slots_per_row)
    {
        slot = 0U;
    }
    return p->cabinet_x0_mm + (float) slot * PickupParams_SlotWidthEffective();
}

/* 取药调试页格子：固定 3 列（设备检测用）。
 * 用户布局：第3列 = X 轴最大脉冲 17000（≈396.7mm）；第1列保持 70mm（3000 脉冲）；
 * 第2列 = 折中（第1/第3列中点）= (3000+17000)/2 = 10000 脉冲 ≈ 233.3mm。 */
float PickupParams_TestSlotX(uint8_t slot)
{
    switch (slot)
    {
        case 1U:  return 233.3f;   /* 折中：(70+396.7)/2 ≈ 233.3mm = 10000 脉冲 */
        case 2U:  return 396.7f;   /* X 最大：≈396.7mm = 17000 脉冲 */
        default:  return 70.0f;    /* 第1列：70mm = 3000 脉冲 */
    }
}

float PickupParams_ShelfY(uint8_t shelf)
{
    const pickup_params_t * p = &s_params;
    if (shelf >= p->shelf_count)
    {
        shelf = 0U;
    }
    return p->shelf_y_mm[shelf];
}

/* ============================================================================
 * 夹爪「脉冲 ↔ 中间宽度」标定表
 * 用户实测（2026-08-21）：0→115mm、1000→72mm、1550→51mm。
 * 夹爪机构非线性（连杆/铰链），两点线性推算不成立——用实测点做分段
 * 线性插值（点越多越准，后续实测可追加）。"中间宽度"= 两爪内沿间距
 * = 能夹住的物品宽度。
 * ==========================================================================*/
#define GRIP_CAL_POINTS (3U)
static const uint16_t s_grip_cal_pulses[GRIP_CAL_POINTS] = {0U, 1000U, 1550U};
static const float    s_grip_cal_width_mm[GRIP_CAL_POINTS] = {115.0f, 72.0f, 51.0f};

int32_t PickupParams_GripWidthToPulses(float width_mm)
{
    /* 越界钳制：≥全开宽度 → 0 脉冲；≤全闭宽度 → 1550 脉冲（全闭） */
    if (width_mm >= s_grip_cal_width_mm[0])
    {
        return 0;
    }
    if (width_mm <= s_grip_cal_width_mm[GRIP_CAL_POINTS - 1U])
    {
        return (int32_t) s_grip_cal_pulses[GRIP_CAL_POINTS - 1U];
    }
    /* 表内：脉冲递增、宽度递减，逐段插值 */
    for (uint32_t i = 1U; i < GRIP_CAL_POINTS; i++)
    {
        float const w_hi = s_grip_cal_width_mm[i - 1U];
        float const w_lo = s_grip_cal_width_mm[i];
        if ((width_mm <= w_hi) && (width_mm >= w_lo))
        {
            float const t = (w_hi - width_mm) / (w_hi - w_lo);
            float const p = (float) s_grip_cal_pulses[i - 1U] +
                            t * (float) (s_grip_cal_pulses[i] - s_grip_cal_pulses[i - 1U]);
            return (int32_t) (p + 0.5f);
        }
    }
    return 0;
}

float PickupParams_GripPulsesToWidth(int32_t pulses)
{
    if (pulses <= (int32_t) s_grip_cal_pulses[0])
    {
        return s_grip_cal_width_mm[0];
    }
    if (pulses >= (int32_t) s_grip_cal_pulses[GRIP_CAL_POINTS - 1U])
    {
        return s_grip_cal_width_mm[GRIP_CAL_POINTS - 1U];
    }
    for (uint32_t i = 1U; i < GRIP_CAL_POINTS; i++)
    {
        if (pulses <= (int32_t) s_grip_cal_pulses[i])
        {
            float const t = (float) (pulses - s_grip_cal_pulses[i - 1U]) /
                            (float) (s_grip_cal_pulses[i] - s_grip_cal_pulses[i - 1U]);
            return s_grip_cal_width_mm[i - 1U] +
                   t * (s_grip_cal_width_mm[i] - s_grip_cal_width_mm[i - 1U]);
        }
    }
    return s_grip_cal_width_mm[GRIP_CAL_POINTS - 1U];
}

bool PickupParams_SetTestTarget(uint8_t shelf, uint8_t slot)
{
    const pickup_params_t * p = &s_params;
    if ((shelf >= p->shelf_count) || (slot >= p->slots_per_row))
    {
        return false;
    }
    s_params.test_shelf = shelf;
    s_params.test_slot  = slot;
    return true;
}

/* 参数中的速度/加速度应用到 gantry_robot 配置（软限位保持默认实机标定值）。
 * Gantry_Configure 要求机械臂空闲且队列为空；上电启动时满足，保存参数时
 * 若机械臂忙会返回 GANTRY_ERR_BUSY（调用方可忽略，下次上电生效）。 */
gantry_result_t PickupParams_ApplyToGantry(void)
{
    const pickup_params_t * p = &s_params;
    gantry_config_t cfg;
    Gantry_GetDefaultConfig(&cfg);
    cfg.axis[GANTRY_AXIS_X].speed        = p->xy_speed;
    cfg.axis[GANTRY_AXIS_X].acceleration = p->xy_acc;
    cfg.axis[GANTRY_AXIS_Y].speed        = p->xy_speed;
    cfg.axis[GANTRY_AXIS_Y].acceleration = p->xy_acc;
    cfg.axis[GANTRY_AXIS_Z].speed        = p->z_speed;
    cfg.axis[GANTRY_AXIS_Z].acceleration = p->z_acc;
    cfg.axis[GANTRY_AXIS_CATCH].speed    = p->grip_speed;
    cfg.axis[GANTRY_AXIS_CATCH].acceleration = p->grip_acc;
    return Gantry_Configure(&cfg);
}
