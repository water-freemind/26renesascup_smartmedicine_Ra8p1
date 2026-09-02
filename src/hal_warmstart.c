/*
* Copyright (c) 2020 - 2025 Renesas Electronics Corporation and/or its affiliates
*
* SPDX-License-Identifier: BSD-3-Clause
*/

#include "hal_data.h"

FSP_CPP_HEADER
void R_BSP_WarmStart(bsp_warm_start_event_t event);

FSP_CPP_FOOTER

/*******************************************************************************************************************//**
 * 该函数在启动过程中的多个时间点被调用。本实现使用 main() 之前的事件来配置引脚。
 *
 * @param[in]  event    当前所处的启动阶段
 **********************************************************************************************************************/
void R_BSP_WarmStart (bsp_warm_start_event_t event)
{
    if (BSP_WARM_START_RESET == event)
    {
#if BSP_FEATURE_FLASH_LP_VERSION != 0

        /* 使能读取数据闪存 */
        R_FACI_LP->DFLCTL = 1U;

        /* 通常需要等待 tDSTOP(6us) 让数据闪存恢复。在时钟和 C 运行时初始化之前使能，利用了初始化通常超过 6us 的特点，无需额外延时 */
#endif
    }

#if BSP_CFG_OSPI_B_STARTUP_ENABLED && defined(BSP_CFG_OSPI_B_STARTUP_FN)
    if (BSP_WARM_START_POST_CLOCK == event)
    {
        /* 配置 OSPI_B SiP 闪存并初始化 */
        R_BSP_OspiBInit(BSP_CFG_OSPI_B_STARTUP_FN, true);
    }
#endif

    if (BSP_WARM_START_POST_C == event)
    {
        /* C 运行时环境和系统时钟已就绪 */

        /* 配置引脚 */
        R_IOPORT_Open(&IOPORT_CFG_CTRL, &IOPORT_CFG_NAME);

#if BSP_CFG_SDRAM_ENABLED

        /* 配置 SDRAM 并初始化，必须先配置引脚 */
        R_BSP_SdramInit(true);
#endif
    }
}
