#ifndef PICKUP_PARAMS_H
#define PICKUP_PARAMS_H

/*
 * 取药系统参数（药柜布局 / 取药动作 / 测试目标 / 上电归零）
 * -----------------------------------------------------------
 * - 默认值 = 之前的药柜设置（cabinet 宏）；
 * - 持久化：板载 OSPI（32MB）固定参数区（主 + 备份双份，CRC16 校验）；
 * - 加载：Camera 线程 OSPI 初始化成功后调用 PickupParams_Init()；
 *   失败时回退默认值（s_ready 仍置位，避免 Motor 线程无限等待）；
 * - 保存：GUI 参数设置页调用 PickupParams_Save()。
 */

#include <stdbool.h>
#include <stdint.h>

#include "bsp_api.h" /* fsp_err_t */
#include "gantry_robot.h" /* gantry_result_t */

#define PICKUP_PARAMS_MAX_SHELVES (4U)

typedef struct
{
    uint32_t magic;               /* PICKUP_PARAMS_MAGIC */
    uint16_t version;
    uint16_t crc16;               /* 覆盖除本字段外全部字节 */
    /* --- 药柜布局 --- */
    uint8_t  shelf_count;         /* 层数（1..MAX_SHELVES） */
    uint8_t  slots_per_row;       /* 每层格数（1..8） */
    uint8_t  test_shelf;          /* 取药测试目标层号（0..shelf_count-1） */
    uint8_t  test_slot;           /* 取药测试目标格号（0..slots_per_row-1） */
    float    shelf_y_mm[PICKUP_PARAMS_MAX_SHELVES]; /* 各层 Y 坐标（mm） */
    float    box_width_mm;      /* 药盒宽度（mm，每种药不一样，参数页可调） */
    float    slot_width_mm;     /* 每格宽度（X 方向，mm）；0=自动规划=药盒宽+10mm余量 */
    float    cabinet_x0_mm;     /* 第一格 X 起点（第一个药柜，mm） */
    /* --- 取药动作 --- */
    float    z_reach_mm;          /* Z 伸出量（mm，行程顶 151.8） */
    int32_t  grip_pulses;         /* 夹爪闭合脉冲（1~1550；0=115mm全开、1550=51mm全闭，≈0.0413mm/脉冲，用户实测） */
    float    grip_margin_mm;      /* 夹紧余量（mm）：闭合中间宽度 = 物品宽度 - 余量；0=刚好贴住、1=轻夹、越大越紧（0~5） */
    float    store_x_mm;          /* 暂存区 X（mm） */
    float    store_y_mm;          /* 暂存区 Y（mm，行程内） */
    uint16_t xy_speed;            /* XY 速度 */
    uint8_t  xy_acc;              /* XY 加速度 */
    uint16_t z_speed;             /* Z 速度 */
    uint8_t  z_acc;               /* Z 加速度 */
    uint16_t grip_speed;          /* 夹爪速度 */
    uint8_t  grip_acc;            /* 夹爪加速度 */
    uint8_t  auto_home_on_boot;   /* 1=上电自动归零 */
    uint8_t  reserved[13];
} pickup_params_t;

/** 填充默认参数（之前的药柜设置：三层 5/150/299、格宽 140、Z 151.8 等）并算好 CRC。 */
void PickupParams_LoadDefault(pickup_params_t * p);

/** 从 OSPI 加载（主→备份→默认），置 ready。OSPI 未就绪也置 ready（用默认）。 */
fsp_err_t PickupParams_Init(void);

/** 参数是否已加载（Init 后恒为 true）。 */
bool PickupParams_Ready(void);

/** 当前生效参数（只读；未 Init 时为默认值）。 */
const pickup_params_t * PickupParams_Get(void);

/** 保存到 OSPI（先写备份扇区再写主扇区，成功后才更新 RAM 副本）。 */
fsp_err_t PickupParams_Save(const pickup_params_t * p);

/** 第 slot 格（0 起）的 X 坐标（mm）。 */
float PickupParams_SlotX(uint8_t slot);

/** 取药调试页格子 X 坐标（mm）（设备检测用）：
 *  第1列 70mm(3000脉冲)、第2列折中 233.3mm(10000脉冲)、第3列 X最大 396.7mm(17000脉冲)。 */
float PickupParams_TestSlotX(uint8_t slot);

/** 生效格宽（mm）：slot_width_mm>0 用固定值，否则自动 = 药盒宽+10mm 余量（下限夹爪宽 35mm），
 *  且不超 X 轴最大量程(396.7mm)/每层格数。 */
float PickupParams_SlotWidthEffective(void);

/** 第 shelf 层（0 起）的 Y 坐标（mm）。 */
float PickupParams_ShelfY(uint8_t shelf);

/** 夹爪「目标中间宽度 → 脉冲」：按用户实测标定表分段线性插值
 *  （0→115mm、1000→72mm、1550→51mm；夹爪机构非线性，两点线性不成立）。
 *  越界钳制到两端（≥115mm→0、≤51mm→1550）。返回脉冲数（1~1550）。 */
int32_t PickupParams_GripWidthToPulses(float width_mm);

/** 夹爪「脉冲 → 中间宽度」（mm）：同一标定表反查，供显示/校准。 */
float PickupParams_GripPulsesToWidth(int32_t pulses);

/** 设置取药测试目标格（层 shelf 0 起 / 格 slot 0 起），供"取药调试"页点选格子。
 *  不写 OSPI（仅本次测试目标；越界则忽略并返回 false）。 */
bool PickupParams_SetTestTarget(uint8_t shelf, uint8_t slot);

/** 参数中的速度/加速度应用到 gantry_robot 配置（软限位保持实机标定值）。
 *  机械臂忙时返回 GANTRY_ERR_BUSY（调用方可忽略，下次上电生效）。 */
gantry_result_t PickupParams_ApplyToGantry(void);

#endif /* PICKUP_PARAMS_H */
