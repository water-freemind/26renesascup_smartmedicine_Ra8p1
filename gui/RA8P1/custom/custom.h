/*
* Copyright 2026 NXP
* NXP Proprietary. This software is owned or controlled by NXP and may only be used strictly in
* accordance with the applicable license terms. By expressly accepting such terms or by downloading, installing,
* activating and/or otherwise using the software, you are agreeing that you have read, and that you agree to
* comply with and are bound by, such license terms.  If you do not agree to be bound by the applicable license
* terms, then you may not retain, install, activate or otherwise use the software.
*/

#ifndef CUSTOM_H
#define CUSTOM_H
#ifdef __cplusplus
extern "C" {
#endif

#include "../generated/gui_guider.h"
extern bool isPrint;
extern int copiesCnt;

/* ESP-01S 云端连接三态（与协议层 esp01s_conn_state_t 值一一对应，
 * 供 Home/Admin 徽章、Device 网络行显示；模拟器不链接协议层，
 * 故在本头独立定义枚举，gui_app.c 负责把协议层状态映射进来）。 */
typedef enum e_esp01s_ui_state
{
    ESP01S_UI_CONNECTING = 0,    /* 连接中 */
    ESP01S_UI_ONLINE,            /* 已连接 */
    ESP01S_UI_OFFLINE,           /* 离线 */
} esp01s_ui_state_t;

void custom_init(gg_ui_t *ui);

/** Update the header network badge from the actual ESP-01S connection state
 *  (三态：CONNECTING/ONLINE/OFFLINE)。由 gui_app.c 在 LVGL 线程周期桥接。 */
void gui_set_esp01s_state(gg_ui_t *ui, esp01s_ui_state_t state);

/** 兼容入口：bool 在线/离线 → 内部映射为 ONLINE/OFFLINE。 */
void gui_set_esp01s_online(gg_ui_t *ui, bool online);

/** Query the current ESP-01S connection state (for the Device self-test). */
bool gui_get_esp01s_online(void);

/** Query the current ESP-01S three-state connection state. */
esp01s_ui_state_t gui_get_esp01s_state(void);

/** Query the current login session state. */
bool gui_get_authenticated(void);

/** Update the mechanical arm logical coordinates shown on the Device page. Call from the LVGL thread. */
void gui_set_arm_coordinates(gg_ui_t *ui, int32_t x, int32_t y, int32_t z);

void slider_adjust_img_cb(lv_obj_t * img, int32_t brightValue, int16_t hueValue);

void label_progress_cb(void * var, int32_t v);

#ifdef __cplusplus
}
#endif
#endif /* CUSTOM_H */
