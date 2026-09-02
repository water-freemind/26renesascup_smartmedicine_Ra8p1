#ifndef GANTRY_ROBOT_H
#define GANTRY_ROBOT_H

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GANTRY_AXIS_MASK_X     (1U << 0)
#define GANTRY_AXIS_MASK_Y     (1U << 1)
#define GANTRY_AXIS_MASK_Z     (1U << 2)
#define GANTRY_AXIS_MASK_CATCH (1U << 3)
/* 正式 XYZ 流程掩码保持不变；抓手(CATCH)为独立轴，用 Gantry_MoveAxisTo 单独操作 */
#define GANTRY_AXIS_MASK_ALL   (GANTRY_AXIS_MASK_X | GANTRY_AXIS_MASK_Y | GANTRY_AXIS_MASK_Z)

typedef enum
{
    GANTRY_AXIS_X = 0,
    GANTRY_AXIS_Y,
    GANTRY_AXIS_Z,
    GANTRY_AXIS_CATCH,
    GANTRY_AXIS_COUNT
} gantry_axis_t;

typedef enum
{
    GANTRY_OK = 0,
    GANTRY_ERR_NOT_INITIALIZED,
    GANTRY_ERR_INVALID_ARGUMENT,
    GANTRY_ERR_QUEUE_FULL,
    GANTRY_ERR_BUSY,
    GANTRY_ERR_NOT_HOMED,
    GANTRY_ERR_SOFT_LIMIT,
    GANTRY_ERR_TIMEOUT,
    GANTRY_ERR_AXIS_FAULT,
    GANTRY_ERR_EMERGENCY_STOP
} gantry_result_t;

typedef enum
{
    GANTRY_STATE_UNINITIALIZED = 0,
    GANTRY_STATE_IDLE,
    GANTRY_STATE_MOVING,
    GANTRY_STATE_HOMING,
    GANTRY_STATE_STOPPED,
    GANTRY_STATE_FAULT
} gantry_state_t;

typedef struct
{
    float x_mm;
    float y_mm;
    float z_mm;
    float catch_mm; /* 抓手（夹爪）位置：单位按配置 pulses_per_mm=1 时为脉冲 */
} gantry_position_t;

typedef struct
{
    float pulses_per_mm;
    float min_mm;
    float max_mm;
    int8_t direction;
    uint16_t speed;
    uint8_t acceleration;
} gantry_axis_config_t;

typedef struct
{
    gantry_axis_config_t axis[GANTRY_AXIS_COUNT];
    uint32_t move_timeout_ms;
    uint32_t home_timeout_ms;
    bool require_homing;
} gantry_config_t;

typedef struct
{
    gantry_state_t state;
    gantry_result_t last_error;
    gantry_position_t position;
    gantry_position_t target;
    uint8_t homed_axes;
    uint8_t active_axes;
    uint32_t completed_commands;
} gantry_status_t;

void Gantry_GetDefaultConfig(gantry_config_t * config);
gantry_result_t Gantry_Init(const gantry_config_t * config);
gantry_result_t Gantry_Configure(const gantry_config_t * config);

gantry_result_t Gantry_EnableAxes(uint8_t axis_mask, bool enable);
gantry_result_t Gantry_Home(uint8_t axis_mask);
/** 解除堵转保护（0x0E 0x52）：撞限位/堵转后必须先解除才能再动。 */
gantry_result_t Gantry_UnprotectAxes(uint8_t axis_mask);
gantry_result_t Gantry_MoveTo(const gantry_position_t * target);
gantry_result_t Gantry_MoveBy(const gantry_position_t * delta);
gantry_result_t Gantry_MoveAxisTo(gantry_axis_t axis, float position_mm);
/** 仅移动 X/Y 两轴到绝对坐标（Z 保持不动），X/Y 同步并行移动。 */
gantry_result_t Gantry_MoveXYTo(float x_mm, float y_mm);
/** Z退至safe_z_mm，再移动XY，最后下降到目标Z；三段命令会原子入队。 */
gantry_result_t Gantry_MoveSafeTo(const gantry_position_t * target, float safe_z_mm);
gantry_result_t Gantry_EmergencyStop(void);
gantry_result_t Gantry_ClearFault(void);
gantry_result_t Gantry_SetLogicalPosition(const gantry_position_t * position, uint8_t homed_axes);

void Gantry_GetStatus(gantry_status_t * status);
bool Gantry_IsIdle(void);
gantry_result_t Gantry_WaitIdle(uint32_t timeout_ms);

/* Motor线程持续调用；其他线程不得绕过它直接操作ZDT/CAN。 */
void Gantry_Service(void);

/* 由现有、已验证的ZDT到位应答解析位置调用。 */
void Gantry_NotifyAxisDone(gantry_axis_t axis, bool success);
void Gantry_NotifyAxisDoneFromISR(gantry_axis_t axis, bool success, BaseType_t * higher_priority_task_woken);

#ifdef __cplusplus
}
#endif
#endif
