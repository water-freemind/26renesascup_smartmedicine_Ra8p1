/*
 * 继电器输出驱动实现
 * 见 relay_drv.h 说明。引脚映射：
 *   RELAY_PUMP   -> BSP_IO_PORT_09_PIN_04 (PIN904)
 *   RELAY_VALVE1 -> BSP_IO_PORT_08_PIN_07 (PIN807)
 *   RELAY_VALVE2 -> BSP_IO_PORT_10_PIN_07 (PA07)
 */

#include "relay_drv.h"

#include "hal_data.h" /* g_ioport_ctrl */

static const bsp_io_port_pin_t s_relay_pins[RELAY_CHANNEL_COUNT] = {
    BSP_IO_PORT_09_PIN_04,  /* RELAY_PUMP   : PIN904 真空泵 */
    BSP_IO_PORT_08_PIN_07,  /* RELAY_VALVE1 : PIN807 电磁阀 1 */
    BSP_IO_PORT_10_PIN_07,  /* RELAY_VALVE2 : PA07   电磁阀 2 */
};

fsp_err_t RelayDrv_Init(void)
{
    fsp_err_t err = FSP_SUCCESS;
    /* 幂等兜底：与 ra_gen/pin_data.c 一致，确保为 GPIO 输出且初始低电平。
     * 输出类型 = 推挽（cfg 不含 IOPORT_CFG_NMOS_ENABLE/PMOS_ENABLE，
     * PFS ODR=00，默认推挽），驱动能力 = DRIVE_HIGH（高）。 */
    for (uint8_t i = 0U; i < RELAY_CHANNEL_COUNT; i++)
    {
        err = R_IOPORT_PinCfg(&g_ioport_ctrl, s_relay_pins[i],
                              (uint32_t) IOPORT_CFG_DRIVE_HIGH |
                              (uint32_t) IOPORT_CFG_PORT_DIRECTION_OUTPUT |
                              (uint32_t) IOPORT_CFG_PORT_OUTPUT_LOW);
        if (FSP_SUCCESS != err)
        {
            return err;
        }
    }
    return FSP_SUCCESS;
}

fsp_err_t RelayDrv_Set(relay_channel_t channel, bool on)
{
    if ((uint32_t) channel >= (uint32_t) RELAY_CHANNEL_COUNT)
    {
        return FSP_ERR_ASSERTION;
    }
    return R_IOPORT_PinWrite(&g_ioport_ctrl, s_relay_pins[channel],
                             on ? BSP_IO_LEVEL_HIGH : BSP_IO_LEVEL_LOW);
}
