/*
 * 继电器输出驱动（真空泵 / 电磁阀）
 *
 * 硬件：MCU GPIO 输出 → 继电器模块（高电平开启，低电平关闭）。
 *   PIN904 (P904)  = 真空泵
 *   PIN807 (P807)  = 电磁阀 1
 *   PA07  (P10.07) = 电磁阀 2
 *
 * 引脚已在 ra_gen/pin_data.c 配置为 GPIO 输出、初始低电平（上电即关闭，
 * 继电器不会误动作）。输出类型为**推挽输出**：PmnPFS ODR=00（未启用
 * NMOS/PMOS 开漏），高电平由内部 PMOS 拉高、低电平由 NMOS 拉低；配合
 * IOPORT_CFG_DRIVE_HIGH 高驱动能力，可直接驱动继电器模块的光耦/MOS
 * 栅极等高阻输入。本驱动提供运行时开关接口（R_IOPORT_PinWrite）。
 */

#ifndef RELAY_DRV_H
#define RELAY_DRV_H

#include <stdbool.h>
#include <stdint.h>
#include "bsp_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 继电器通道枚举 */
typedef enum
{
    RELAY_PUMP = 0,   /* PIN904：真空泵 */
    RELAY_VALVE1,     /* PIN807：电磁阀 1 */
    RELAY_VALVE2,     /* PA07 ：电磁阀 2 */
    RELAY_CHANNEL_COUNT
} relay_channel_t;

/* 重新配置引脚为 GPIO 输出并置低（上电 pin_data.c 已配好，此处幂等兜底） */
fsp_err_t RelayDrv_Init(void);

/* 开关继电器：on=true 输出高电平（开启），false 输出低电平（关闭） */
fsp_err_t RelayDrv_Set(relay_channel_t channel, bool on);

/* 便捷接口 */
static inline fsp_err_t RelayDrv_PumpOn(void)   { return RelayDrv_Set(RELAY_PUMP, true); }
static inline fsp_err_t RelayDrv_PumpOff(void)  { return RelayDrv_Set(RELAY_PUMP, false); }
static inline fsp_err_t RelayDrv_Valve1On(void) { return RelayDrv_Set(RELAY_VALVE1, true); }
static inline fsp_err_t RelayDrv_Valve1Off(void){ return RelayDrv_Set(RELAY_VALVE1, false); }
static inline fsp_err_t RelayDrv_Valve2On(void) { return RelayDrv_Set(RELAY_VALVE2, true); }
static inline fsp_err_t RelayDrv_Valve2Off(void){ return RelayDrv_Set(RELAY_VALVE2, false); }

#ifdef __cplusplus
}
#endif

#endif /* RELAY_DRV_H */
