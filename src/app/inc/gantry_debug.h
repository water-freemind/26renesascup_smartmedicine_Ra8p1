#ifndef GANTRY_DEBUG_H
#define GANTRY_DEBUG_H

/*
 * 电机调试模块（GUI 电机调试页专用）
 * ------------------------------------
 * - 四轴独立调试：X / Y / Z / 抓手(CATCH)，脉冲级点动，不经过 gantry_robot
 *   的 mm 换算与软限位（用户需要测物理可移动范围）。
 * - 不修改已验证的 ZDT_drv / gantry_robot 协议；本模块只通过 Motor 线程
 *   独占执行 ZDT 命令（与 gantry_robot 同一约束）。
 * - ZDT 无位置回读，脉冲位置由本模块本地累计（int32，设零清零）。
 * - GantryDebug_Service() 仅在 gantry_robot 空闲时消费命令，避免与正式
 *   取药流程同时占用 CAN 总线。
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GDBG_AXIS_X     (1U << 0) /* ZDT_ID_X    0x0100 */
#define GDBG_AXIS_Y     (1U << 1) /* ZDT_ID_Y    0x0200 */
#define GDBG_AXIS_Z     (1U << 2) /* ZDT_ID_Z    0x0300 */
#define GDBG_AXIS_CATCH (1U << 3) /* ZDT_ID_CATCH 0x0400 */
#define GDBG_AXIS_ALL   (GDBG_AXIS_X | GDBG_AXIS_Y | GDBG_AXIS_Z | GDBG_AXIS_CATCH)

#define GDBG_AXIS_COUNT (4U)

typedef enum
{
    GDBG_OK = 0,
    GDBG_ERR_NOT_INITIALIZED,
    GDBG_ERR_INVALID_ARGUMENT,
    GDBG_ERR_QUEUE_FULL,
    GDBG_ERR_BUSY /* gantry_robot 正式流程忙，调试命令暂缓（仍会入队） */
} gdbg_result_t;

typedef struct
{
    int32_t pulse[GDBG_AXIS_COUNT]; /* X/Y/Z/CATCH 本地累计脉冲（无驱动回读） */
    uint32_t completed;             /* 已执行完成的调试命令数 */
    uint32_t arrived[GDBG_AXIS_COUNT]; /* 各轴收到"到位应答"(FD 9F 6B / 9A 02 6B)的次数 */
} gdbg_status_t;

gdbg_result_t GantryDebug_Init(void);

/* 使能/脱机（投递到 Motor 线程队列，立即返回） */
gdbg_result_t GantryDebug_Enable(uint8_t axis_mask, bool enable);

/* 设零：把驱动当前位置记为零点（ZDT_SetZero）+ 本地脉冲清零 */
gdbg_result_t GantryDebug_SetZero(uint8_t axis_mask);

/* 回零：执行回零运动（ZDT_Gozero 0x9A），电机自动回到零点；完成后本地脉冲清零 */
gdbg_result_t GantryDebug_Gozero(uint8_t axis_mask);

/* 点动：按 pulses 脉冲（正负=方向）移动指定轴，速度/加速度可调 */
gdbg_result_t GantryDebug_Jog(uint8_t axis_mask, int32_t pulses, uint16_t speed, uint8_t acc);

/* 停止指定轴（广播=全部急停） */
gdbg_result_t GantryDebug_Stop(uint8_t axis_mask);

/* 解除堵转保护（张大头协议 0x0E 0x52；撞限位/堵转后必须先解除才能再动） */
gdbg_result_t GantryDebug_Unprotect(uint8_t axis_mask);

#define GDBG_RX_RAW_MAX (8U)

/* 最近一次收到的该轴应答原始字节（含长度与帧序号，ISR 安全写入） */
typedef struct
{
    uint8_t data[GDBG_RX_RAW_MAX];
    uint8_t len;
    uint32_t seq;   /* 收到的帧计数（0=从未收到） */
    uint32_t frame_id;
} gdbg_rx_raw_t;

void GantryDebug_GetRxRaw(uint8_t axis, gdbg_rx_raw_t * raw);
/* CAN 中断回调调用（ISR 上下文）：按 ID 归类存储应答帧 */
void GantryDebug_OnCanRxFromISR(uint32_t frame_id, const uint8_t * data, uint8_t len);

void GantryDebug_GetStatus(gdbg_status_t * status);
bool GantryDebug_IsIdle(void);

/* Motor 线程每 5ms 调用；仅当 gantry_robot 空闲时执行命令 */
void GantryDebug_Service(void);

#ifdef __cplusplus
}
#endif
#endif /* GANTRY_DEBUG_H */
