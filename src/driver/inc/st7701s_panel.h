#ifndef ST7701S_PANEL_H
#define ST7701S_PANEL_H

#include "bsp_api.h"

/* WLK2802MIPI-15P V2 / W280BF036I: ST7701S, 480 x 640, one DSI data lane,
 * RGB888.  The supplier's screen-side silk and Raspberry Pi reference both
 * select 1 Lane, therefore video uses Clock + DL0 only. */
#define ST7701S_PANEL_WIDTH   (480U)
#define ST7701S_PANEL_HEIGHT  (640U)
#define ST7701S_PANEL_ERROR_RASC_CONFIGURATION (0x77010001UL)
#define ST7701S_PANEL_ERROR_MIPI_EVENT         (0x77010002UL)

extern volatile uint32_t g_st7701s_init_error;
extern volatile uint32_t g_st7701s_init_step;
extern volatile uint32_t g_st7701s_last_command_error;
extern volatile uint32_t g_st7701s_sequence0_count;
extern volatile uint32_t g_st7701s_sequence1_count;
extern volatile uint32_t g_st7701s_last_mipi_event;
extern volatile uint32_t g_st7701s_last_mipi_status;

/* Confirms that RASC generated the panel-specific DSI geometry and lane
 * count. This is intentionally checked before any panel command is sent. */
bool st7701s_panel_mipi_config_is_valid(void);

/* Call only after g_mipi_dsi0 is open and before video mode starts. */
fsp_err_t st7701s_panel_init(void);

/* Optional board hooks.  The current three-cable setup uses DCS software
 * reset and expects the adapter to enable the backlight automatically. */
void st7701s_panel_board_reset(void);
void st7701s_panel_board_backlight(bool enable);

#endif /* ST7701S_PANEL_H */
