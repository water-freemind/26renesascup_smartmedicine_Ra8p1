/*
* Copyright 2026 NXP
* NXP Proprietary. This software is owned or controlled by NXP and may only be used strictly in
* accordance with the applicable license terms. By expressly accepting such terms or by downloading, installing,
* activating and/or otherwise using the software, you are agreeing that you have read, and that you agree to
* comply with and are bound by, such license terms.  If you do not agree to be bound by the applicable license
* terms, then you may not retain, install, activate or otherwise use the software.
*/


/*********************
 *      INCLUDES
 *********************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lvgl.h"
#include "custom.h"
#include "ospi_ttf_loader.h"
#include "gantry_debug.h"
#include "pickup_test.h"
#include "pickup_params.h"
#include "esp01s_cfg.h"
#include "relay_drv.h"

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

/**********************
 *  STATIC VARIABLES
 **********************/

/**
 * Create a demo application
 */

bool isPrint = false;
int copiesCnt = 1;

static gg_ui_t * s_ui = NULL;

/* 前置声明：Home 页懒创建后由周期刷新（network_badge_timer_cb）调用，定义在文件后部 */
static void home_build_images(gg_ui_t * ui);
/* 前置声明：Boot 文字样式（白色+深描边+居中）。configure_ui_label_layout 会把
 * Boot.title 的 text_align 重置为 LEFT，每次有新的懒创建页面出现后需重新应用。 */
static void boot_style_texts(gg_ui_t * ui);
/* 前置声明：电机调试页（定义在文件后部，network_badge_timer_cb 调用） */
static void refresh_motor_debug_page(void);
static void install_device_motor_debug_entry(gg_ui_t * ui);
static void install_device_pickup_debug_entry(gg_ui_t * ui);
static void refresh_pickup_debug_page(void);
static void install_device_wireless_entry(gg_ui_t * ui);
static void refresh_wireless_debug_page(void);
/* 前置声明：参数设置页导航（电机调试页顶栏入口，定义在文件后部） */
static void device_params_nav_hook(lv_event_t * e);
static lv_timer_t * s_network_badge_timer = NULL;
static esp01s_ui_state_t s_esp01s_state = ESP01S_UI_CONNECTING;  /* 初始连接中 */
static bool s_authenticated = false;      /* 登录会话状态（Login 提交→true，Admin 登出→false） */
static bool s_auth_hooks_installed = false;
static int32_t s_arm_x = 0;
static int32_t s_arm_y = 0;
static int32_t s_arm_z = 0;

/* gui_app.c 导出的 OSPI 动态字体（tiny_ttf，支持任意中文/ASCII） */
extern lv_font_t * g_tiny_font;

/* 登录输入：运行时把 GUI Guider 的只读 value label 换成 lv_textarea + 软键盘 */
static lv_obj_t * s_login_ta_staff = NULL;   /* 药师工号输入框 */
static lv_obj_t * s_login_ta_pass  = NULL;   /* 登录密码输入框 */
static lv_obj_t * s_login_keyboard = NULL;   /* 软键盘（lv_layer_top 上，全局唯一） */
static bool s_login_inputs_built  = false;
static uint8_t s_login_focus      = 0U;      /* 0=无 1=工号 2=密码 */

/* "记住用户名/记住密码"复选框（默认都勾选：打开页面即预填，免每次输入） */
static lv_obj_t * s_login_cb_user = NULL;
static lv_obj_t * s_login_cb_pass = NULL;
static bool s_remember_user = true;
static bool s_remember_pass = true;

/* LVGL 9.3 的 lv_label_set_text() 对相同文本不做短路（每次都 malloc+strcpy
 * +invalidate）。custom.c 的 500ms 定时刷新（网络徽章/机械臂坐标/设备行/
 * 认证徽章/Admin 欢迎语）同样需要"变化才设置"，避免长期运行碎片化。 */
static void gui_custom_label_set_if_changed(lv_obj_t * label, const char * text)
{
    if ((label == NULL) || !lv_obj_is_valid(label))
    {
        return;
    }
    const char * cur = lv_label_get_text(label);
    if ((cur != NULL) && (text != NULL) && (0 == strcmp(cur, text)))
    {
        return;
    }
    lv_label_set_text(label, text);
}

static void configure_single_line_label(lv_obj_t * label)
{
    if ((label == NULL) || !lv_obj_is_valid(label))
    {
        return;
    }

    /* Do not let a label with a nearly-full width wrap a visually single-line
     * caption.  This is intentionally applied only to known single-line UI
     * elements; changing every label at runtime was the source of the layout
     * reflow/overlap bug. */
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_line_space(label, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_width(label, LV_SIZE_CONTENT);
    lv_obj_set_height(label, LV_SIZE_CONTENT);
}

static void configure_two_line_label(lv_obj_t * label)
{
    if ((label == NULL) || !lv_obj_is_valid(label))
    {
        return;
    }

    /* These are the few descriptions designed to occupy two rows.  Keep the
     * generated fixed height and make the wrap mode explicit. */
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(label, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_height(label, 44);
}

static void emphasize_label(lv_obj_t * label, const lv_font_t * font)
{
    /* Disabled together with configure_ui_typography(); kept as a stub so
     * the call sites document the intended policy. */
    (void) label;
    (void) font;
}

static void emphasize_single_line_label(lv_obj_t * label, const lv_font_t * font)
{
    (void) label;
    (void) font;
}

static void configure_single_line_label_box(lv_obj_t * label, lv_coord_t width, lv_coord_t height)
{
    configure_single_line_label(label);
    if ((label == NULL) || !lv_obj_is_valid(label))
    {
        return;
    }

    /* These boxes are deliberately wider than their longest generated
     * Chinese caption and clip instead of wrapping.  This removes the
     * first-frame two-row title artefact seen on the 480x640 panel. */
    lv_obj_set_size(label, width, height);
}

static void configure_ui_typography(gg_ui_t * ui)
{
    /* Runtime font overrides are DISABLED (2026-08-17).  GUI Guider
     * generated fonts, sizes and line heights are the only display
     * baseline: swapping in larger runtime fonts made short Chinese
     * captions wrap into a second visual row and the 1px outline looked
     * wrong on the cropped character fonts (docs: GUI Guider 字体回归).
     * Layout-only policy (single/two-line labels) remains in
     * configure_ui_label_layout(). */
    (void) ui;
}

static void configure_ui_label_layout(gg_ui_t * ui)
{
    if (ui == NULL)
    {
        return;
    }

    /* The camera viewfinder containers are decorative frames: make their
     * backgrounds transparent so the camera preview shows through the
     * border (generated bg_opa=255 painted an opaque blue box over the
     * preview center). */
    if (ui->Scan.camera_preview_qr_frame != NULL)
    {
        lv_obj_set_style_bg_opa(ui->Scan.camera_preview_qr_frame, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    if (ui->Pickup.camera_card_camera_frame != NULL)
    {
        lv_obj_set_style_bg_opa(ui->Pickup.camera_card_camera_frame, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    configure_two_line_label(ui->Home.card_management_subtitle);
    configure_single_line_label_box(ui->Boot.title, 280, 42);
    configure_single_line_label_box(ui->Home.header_header_title, 240, 34);
    configure_single_line_label_box(ui->Pickup.header_header_title, 240, 34);
    configure_single_line_label_box(ui->Scan.header_header_title, 240, 34);
    configure_single_line_label_box(ui->Medicine.header_header_title, 240, 34);
    configure_single_line_label_box(ui->Login.header_header_title, 240, 34);
    configure_single_line_label_box(ui->Admin.header_header_title, 240, 34);
    configure_single_line_label_box(ui->Store.header_header_title, 240, 34);
    configure_single_line_label_box(ui->Logs.header_header_title, 240, 34);
    configure_single_line_label_box(ui->Device.header_header_title, 240, 34);
    configure_single_line_label(ui->Pickup.button_back_button_back_text);
    configure_single_line_label(ui->Scan.button_back_button_back_text);
    configure_single_line_label(ui->Medicine.button_back_button_back_text);
    configure_single_line_label(ui->Login.button_back_button_back_text);
    configure_single_line_label(ui->Store.button_back_button_back_text);
    configure_single_line_label(ui->Logs.button_back_button_back_text);
    configure_single_line_label(ui->Device.button_back_button_back_text);
    configure_single_line_label_box(ui->Home.card_management_title, 150, 32);
    configure_single_line_label_box(ui->Home.card_identify_title, 150, 32);
    configure_single_line_label_box(ui->Home.card_pickup_title, 150, 32);
    configure_single_line_label_box(ui->Admin.admin_card_store_title, 150, 32);
    configure_single_line_label_box(ui->Admin.admin_card_logs_title, 150, 32);
    configure_single_line_label_box(ui->Admin.admin_card_device_title, 150, 32);
    configure_single_line_label(ui->Home.card_identify_subtitle);
    configure_single_line_label(ui->Home.card_pickup_subtitle);
    configure_two_line_label(ui->Admin.admin_card_device_subtitle);
    configure_two_line_label(ui->Admin.admin_card_store_subtitle);
    configure_two_line_label(ui->Admin.admin_card_logs_subtitle);
    configure_single_line_label(ui->Login.login_card_login_hint);

    /* Apply the explicit label geometry before the first physical flush.
     * Without this, the first frame may still use the GUI Guider parent
     * layout and a title can be painted as a second row. */
    lv_obj_update_layout(lv_screen_active());
}

static void refresh_arm_coordinates(gg_ui_t * ui)
{
    if ((ui == NULL) || (ui->Device.device_arm_coord_row_value == NULL) ||
        !lv_obj_is_valid(ui->Device.device_arm_coord_row_value))
    {
        return;
    }

    char buf[48];
    (void) snprintf(buf, sizeof(buf), "X:%ld  Y:%ld  Z:%ld",
                    (long) s_arm_x, (long) s_arm_y, (long) s_arm_z);
    gui_custom_label_set_if_changed(ui->Device.device_arm_coord_row_value, buf);
}

#if 0 /* superseded by the UTF-8 implementation below */
static void set_network_badge_legacy(lv_obj_t * badge, lv_obj_t * icon, lv_obj_t * text, bool online)
{
    if ((badge == NULL) || (icon == NULL) || (text == NULL))
    {
        return;
    }

    lv_image_set_src(icon, online ? &icon_online_24x24_ARGB8888 : &icon_offline_24x24_ARGB8888);
    lv_label_set_text(text, online ? "系统在线" : "系统离线");
    lv_obj_set_style_bg_color(badge, lv_color_hex(online ? 0xf3fbf7 : 0xfff0f0), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(badge, lv_color_hex(online ? 0xccebdd : 0xf5c8c8), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(text, lv_color_hex(online ? 0x14a66a : 0xef5350), LV_PART_MAIN | LV_STATE_DEFAULT);
}
#endif

static void set_network_badge(lv_obj_t * badge, lv_obj_t * icon, lv_obj_t * text, esp01s_ui_state_t st)
{
    if ((badge == NULL) || (icon == NULL) || (text == NULL))
    {
        return;
    }

    /* 三态（手册 §5）：ONLINE 绿 / CONNECTING 橙 / OFFLINE 红 */
    bool const online = (st == ESP01S_UI_ONLINE);
    lv_image_set_src(icon, online ? &icon_online_24x24_ARGB8888 : &icon_offline_24x24_ARGB8888);
    gui_custom_label_set_if_changed(text,
                                    (st == ESP01S_UI_ONLINE) ? "系统在线" :
                                    (st == ESP01S_UI_CONNECTING) ? "系统连接中" : "系统离线");
    configure_single_line_label(text);
    lv_obj_set_style_bg_color(badge,
                              (st == ESP01S_UI_ONLINE) ? lv_color_hex(0xf3fbf7) :
                              (st == ESP01S_UI_CONNECTING) ? lv_color_hex(0xfff8ed) : lv_color_hex(0xfff0f0),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(badge,
                                  (st == ESP01S_UI_ONLINE) ? lv_color_hex(0xccebdd) :
                                  (st == ESP01S_UI_CONNECTING) ? lv_color_hex(0xf5d9a8) : lv_color_hex(0xf5c8c8),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(text,
                                (st == ESP01S_UI_ONLINE) ? lv_color_hex(0x14a66a) :
                                (st == ESP01S_UI_CONNECTING) ? lv_color_hex(0xf59e0b) : lv_color_hex(0xef5350),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void refresh_network_badges(gg_ui_t * ui)
{
    if (ui == NULL)
    {
        return;
    }

    set_network_badge(ui->Home.header_status_online,
                      ui->Home.status_online_status_icon,
                      ui->Home.status_online_status_text,
                      s_esp01s_state);
    set_network_badge(ui->Admin.header_status_online,
                      ui->Admin.status_online_status_icon,
                      ui->Admin.status_online_status_text,
                      s_esp01s_state);
}

static void refresh_device_page_rows(gg_ui_t * ui)
{
    if (ui == NULL)
    {
        return;
    }

    /* ESP-01S 网络行：与 Home/Admin 徽章共用同一状态源（三态） */
    if ((ui->Device.device_network_row_value != NULL) &&
        lv_obj_is_valid(ui->Device.device_network_row_value))
    {
        gui_custom_label_set_if_changed(ui->Device.device_network_row_value,
                                        (s_esp01s_state == ESP01S_UI_ONLINE) ? "在线" :
                                        (s_esp01s_state == ESP01S_UI_CONNECTING) ? "连接中" : "离线");
        lv_obj_set_style_text_color(ui->Device.device_network_row_value,
                                    (s_esp01s_state == ESP01S_UI_ONLINE) ? lv_color_hex(0x14a66a) :
                                    (s_esp01s_state == ESP01S_UI_CONNECTING) ? lv_color_hex(0xf59e0b) :
                                    lv_color_hex(0xef5350),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    /* 电机行：目前无实时状态源（ZDT 驱动只有命令无回读），保持待机文案 */
    if ((ui->Device.device_motor_row_value != NULL) &&
        lv_obj_is_valid(ui->Device.device_motor_row_value))
    {
        gui_custom_label_set_if_changed(ui->Device.device_motor_row_value, "待机");
    }
}

/* ============================================================================
 * Store 存药页"重新识别"按钮 → 跳转 Scan 识别页。
 * 放在 custom.c（固件+模拟器都编译）而非 gui_app.c：模拟器不跑 gui_app_poll，
 * 只在 gui_app.c 挂事件会导致模拟器里该按钮无响应（历史反馈"扫码按钮没有用"）。
 * 需要 guider_ui 提供 setup_Scan（gui_guider.h 已声明）。
 * ==========================================================================*/
static bool s_store_scan_hooked = false;

static void store_rescan_nav_hook(lv_event_t * e)
{
    (void) e;
    if (s_ui == NULL)
    {
        return;
    }
    if (s_ui->Scan.screen == NULL)
    {
        setup_Scan(s_ui);
    }
    if (s_ui->Scan.screen != NULL)
    {
        lv_screen_load_anim(s_ui->Scan.screen, LV_SCREEN_LOAD_ANIM_FADE_IN, 180, 0, false);
    }
}

static void install_store_scan_hook(gg_ui_t * ui)
{
    if (s_store_scan_hooked || (ui == NULL))
    {
        return;
    }
    lv_obj_t * p_btn = ui->Store.parameter_card_button_read_drug;
    if ((NULL == p_btn) || !lv_obj_is_valid(p_btn))
    {
        return;
    }
    lv_obj_add_event_cb(p_btn, store_rescan_nav_hook, LV_EVENT_CLICKED, NULL);
    s_store_scan_hooked = true;
}

/* ============================================================================
 * 登录输入：可点击的工号/密码输入框 + 软键盘。
 * 运行时把 GUI Guider 的只读 value label 换成 lv_textarea，点击输入框弹出
 * lv_keyboard（ASCII 键盘，工号/密码够用）。✓ 或 ✗ 收起键盘；离开 Login 页
 * 自动收起。打开页面默认预填正确账号密码（可点击修改），字体优先用 OSPI
 * 动态字体（g_tiny_font），未就绪时回退内嵌字体。
 * ==========================================================================*/
#define LOGIN_STAFF_ID   "renesas"
#define LOGIN_PASSWORD   "1234"
static lv_obj_t * login_create_input(lv_obj_t * p_parent, const char * p_placeholder, bool is_pass)
{
    lv_obj_t * p_ta = lv_textarea_create(p_parent);
    lv_obj_set_size(p_ta, 304, 36);
    lv_obj_set_pos(p_ta, 14, 4);

    /* 去掉 textarea 默认背景/边框，保持与输入卡片一致的观感 */
    lv_obj_set_style_bg_opa(p_ta, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(p_ta, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(p_ta, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(p_ta, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(p_ta, 6, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(p_ta, lv_color_hex(0x10233f), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(p_ta, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(p_ta, (g_tiny_font != NULL) ? g_tiny_font : &lv_font_SourceHanSerifSC_18,
                               LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_textarea_set_placeholder_text(p_ta, p_placeholder);
    lv_obj_set_style_text_color(p_ta, lv_color_hex(0x9aa8c4), LV_PART_TEXTAREA_PLACEHOLDER | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(p_ta, (g_tiny_font != NULL) ? g_tiny_font : &lv_font_SourceHanSerifSC_16,
                               LV_PART_TEXTAREA_PLACEHOLDER | LV_STATE_DEFAULT);

    lv_textarea_set_one_line(p_ta, true);
    lv_textarea_set_max_length(p_ta, 16U);
    if (is_pass)
    {
        lv_textarea_set_password_mode(p_ta, true);
        lv_textarea_set_password_show_time(p_ta, 0);
    }
    lv_obj_add_flag(p_ta, LV_OBJ_FLAG_CLICKABLE);
    return p_ta;
}

static void login_close_keyboard(void)
{
    if (s_login_keyboard != NULL)
    {
        lv_obj_add_flag(s_login_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_textarea(s_login_keyboard, NULL);
    }
    if (s_ui != NULL)
    {
        lv_obj_t * p_staff_box = s_ui->Login.login_card_staff_box;
        lv_obj_t * p_pass_box  = s_ui->Login.login_card_password_box;
        if ((p_staff_box != NULL) && lv_obj_is_valid(p_staff_box))
        {
            lv_obj_set_style_border_color(p_staff_box, lv_color_hex(0xd8e1ef), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(p_staff_box, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        if ((p_pass_box != NULL) && lv_obj_is_valid(p_pass_box))
        {
            lv_obj_set_style_border_color(p_pass_box, lv_color_hex(0xd8e1ef), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(p_pass_box, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }
    s_login_focus = 0U;
}

static void login_open_keyboard(lv_obj_t * p_ta)
{
    if (s_login_keyboard == NULL)
    {
        return;
    }
    /* 先收起旧焦点高亮，再切到目标输入框 */
    login_close_keyboard();
    lv_keyboard_set_textarea(s_login_keyboard, p_ta);
    /* 工号 → 大写键盘（PH-001）；密码 → 数字键盘（123456） */
    lv_keyboard_set_mode(s_login_keyboard,
                         (p_ta == s_login_ta_staff) ? LV_KEYBOARD_MODE_TEXT_UPPER : LV_KEYBOARD_MODE_NUMBER);
    lv_obj_remove_flag(s_login_keyboard, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t * p_box = (p_ta == s_login_ta_staff)
        ? s_ui->Login.login_card_staff_box
        : s_ui->Login.login_card_password_box;
    if ((p_box != NULL) && lv_obj_is_valid(p_box))
    {
        lv_obj_set_style_border_color(p_box, lv_color_hex(0x7157d9), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(p_box, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    s_login_focus = (p_ta == s_login_ta_staff) ? 1U : 2U;
}

static void login_kb_event_hook(lv_event_t * e)
{
    /* ✓（LV_EVENT_READY）/ ✗（LV_EVENT_CANCEL）→ 收起软键盘 */
    (void) e;
    login_close_keyboard();
}

static void login_staff_box_click_hook(lv_event_t * e)
{
    (void) e;
    login_open_keyboard(s_login_ta_staff);
}

static void login_pass_box_click_hook(lv_event_t * e)
{
    (void) e;
    login_open_keyboard(s_login_ta_pass);
}

/* "记住用户名/记住密码"勾选状态变化 → 立即填充/清空对应输入框 */
static void login_remember_user_cb(lv_event_t * e)
{
    (void) e;
    if (s_login_cb_user == NULL)
    {
        return;
    }
    s_remember_user = lv_obj_has_state(s_login_cb_user, LV_STATE_CHECKED);
    if (s_login_ta_staff != NULL)
    {
        lv_textarea_set_text(s_login_ta_staff, s_remember_user ? LOGIN_STAFF_ID : "");
    }
}

static void login_remember_pass_cb(lv_event_t * e)
{
    (void) e;
    if (s_login_cb_pass == NULL)
    {
        return;
    }
    s_remember_pass = lv_obj_has_state(s_login_cb_pass, LV_STATE_CHECKED);
    if (s_login_ta_pass != NULL)
    {
        lv_textarea_set_text(s_login_ta_pass, s_remember_pass ? LOGIN_PASSWORD : "");
    }
}

static lv_obj_t * login_create_remember_checkbox(lv_obj_t * p_parent, lv_coord_t x, lv_coord_t y,
                                                 const char * p_text, bool checked)
{
    lv_obj_t * p_cb = lv_checkbox_create(p_parent);
    lv_obj_set_pos(p_cb, x, y);
    lv_checkbox_set_text(p_cb, p_text);
    lv_obj_set_style_text_color(p_cb, lv_color_hex(0x687b99), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(p_cb, (g_tiny_font != NULL) ? g_tiny_font : &lv_font_SourceHanSerifSC_16,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    if (checked)
    {
        lv_obj_add_state(p_cb, LV_STATE_CHECKED);
    }
    return p_cb;
}

static void login_build_inputs(gg_ui_t * ui)
{
    if (s_login_inputs_built || (ui == NULL))
    {
        return;
    }
    lv_obj_t * p_staff_box = ui->Login.login_card_staff_box;
    lv_obj_t * p_pass_box  = ui->Login.login_card_password_box;
    if ((p_staff_box == NULL) || (p_pass_box == NULL) ||
        !lv_obj_is_valid(p_staff_box) || !lv_obj_is_valid(p_pass_box))
    {
        return;
    }

    /* 隐藏只读 value label，换成可输入的 textarea */
    if ((ui->Login.staff_box_staff_value != NULL) && lv_obj_is_valid(ui->Login.staff_box_staff_value))
    {
        lv_obj_add_flag(ui->Login.staff_box_staff_value, LV_OBJ_FLAG_HIDDEN);
    }
    if ((ui->Login.password_box_password_value != NULL) && lv_obj_is_valid(ui->Login.password_box_password_value))
    {
        lv_obj_add_flag(ui->Login.password_box_password_value, LV_OBJ_FLAG_HIDDEN);
    }

    s_login_ta_staff = login_create_input(p_staff_box, "点击输入工号", false);
    s_login_ta_pass  = login_create_input(p_pass_box, "点击输入密码", true);

    /* 预填：勾选了"记住"才填（默认都勾选，免每次输入；可点击修改） */
    if (s_remember_user)
    {
        lv_textarea_set_text(s_login_ta_staff, LOGIN_STAFF_ID);
    }
    if (s_remember_pass)
    {
        lv_textarea_set_text(s_login_ta_pass, LOGIN_PASSWORD);
    }

    /* "记住用户名/记住密码"复选框：放在登录卡片下方（卡片 y=78..424） */
    s_login_cb_user = login_create_remember_checkbox(ui->Login.screen, 185, 432, "记住用户名", s_remember_user);
    s_login_cb_pass = login_create_remember_checkbox(ui->Login.screen, 355, 432, "记住密码", s_remember_pass);
    lv_obj_add_event_cb(s_login_cb_user, login_remember_user_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_login_cb_pass, login_remember_pass_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* 点击输入框/卡片 → 弹出软键盘 */
    lv_obj_add_flag(p_staff_box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(p_pass_box, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(p_staff_box, login_staff_box_click_hook, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(p_pass_box, login_pass_box_click_hook, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_login_ta_staff, login_staff_box_click_hook, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_login_ta_pass, login_pass_box_click_hook, LV_EVENT_CLICKED, NULL);

    /* 软键盘：lv_layer_top 上创建一次，初始隐藏 */
    if (s_login_keyboard == NULL)
    {
        s_login_keyboard = lv_keyboard_create(lv_layer_top());
        lv_obj_set_size(s_login_keyboard, 640, 200);
        lv_obj_set_align(s_login_keyboard, LV_ALIGN_BOTTOM_MID);
        lv_obj_add_flag(s_login_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_event_cb(s_login_keyboard, login_kb_event_hook, LV_EVENT_READY, NULL);
        lv_obj_add_event_cb(s_login_keyboard, login_kb_event_hook, LV_EVENT_CANCEL, NULL);
    }
    s_login_inputs_built = true;
}

/* ============================================================================
 * 登录会话状态：Login 提交并校验通过 → 认证；Admin 登出 → 注销。
 * 用追加事件回调实现（不修改 GUI Guider 生成的事件文件）。
 *
 * 工号/密码由软键盘输入（不再默认预填），提交时与常量比对：
 *   - 匹配：s_authenticated = true，跳转 Admin；
 *   - 不匹配：留在 Login 页并给出红色错误提示。
 * ==========================================================================*/

/* 大小写不敏感比较（工号可能输成小写，如 renesas） */
static bool login_streq_ci(const char * p_a, const char * p_b)
{
    if ((p_a == NULL) || (p_b == NULL))
    {
        return false;
    }
    while ((*p_a != '\0') && (*p_b != '\0'))
    {
        char ca = *p_a;
        char cb = *p_b;
        if ((ca >= 'A') && (ca <= 'Z')) { ca = (char) (ca - 'A' + 'a'); }
        if ((cb >= 'A') && (cb <= 'Z')) { cb = (char) (cb - 'A' + 'a'); }
        if (ca != cb)
        {
            return false;
        }
        p_a++;
        p_b++;
    }
    return (*p_a == '\0') && (*p_b == '\0');
}

static void login_submit_verify_hook(lv_event_t * e)
{
    (void) e;
    if (s_ui == NULL)
    {
        return;
    }

    /* 优先读 textarea（可输入模式），回退读原 value label */
    const char * p_staff = (s_login_ta_staff != NULL)
        ? lv_textarea_get_text(s_login_ta_staff)
        : ((s_ui->Login.staff_box_staff_value != NULL)
               ? lv_label_get_text(s_ui->Login.staff_box_staff_value) : NULL);
    const char * p_pass = (s_login_ta_pass != NULL)
        ? lv_textarea_get_text(s_login_ta_pass)
        : ((s_ui->Login.password_box_password_value != NULL)
               ? lv_label_get_text(s_ui->Login.password_box_password_value) : NULL);

    /* 校验前先收起软键盘（避免键盘遮挡提交按钮/跳转动画） */
    login_close_keyboard();

    bool const ok = (p_staff != NULL) && (p_pass != NULL) &&
                    login_streq_ci(p_staff, LOGIN_STAFF_ID) &&
                    (0 == strcmp(p_pass, LOGIN_PASSWORD));
    s_authenticated = ok;

    /* 错误提示：登录卡副标题（16px） */
    lv_obj_t * p_hint = s_ui->Login.login_card_login_hint;
    if ((NULL != p_hint) && lv_obj_is_valid(p_hint))
    {
        if (ok)
        {
            gui_custom_label_set_if_changed(p_hint, "认证后可进行存药、查阅日志和设备管理");
            lv_obj_set_style_text_color(p_hint, lv_color_hex(0x687b99),
                                        LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        else
        {
            gui_custom_label_set_if_changed(p_hint, "工号或密码错误");
            lv_obj_set_style_text_color(p_hint, lv_color_hex(0xef5350),
                                        LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }

    if (ok)
    {
        /* 校验通过 → 进入管理台（替代生成事件的无条件跳转） */
        if (s_ui->Admin.screen == NULL)
        {
            setup_Admin(s_ui);
        }
        if (s_ui->Admin.screen != NULL)
        {
            lv_screen_load_anim(s_ui->Admin.screen, LV_SCREEN_LOAD_ANIM_FADE_IN, 180, 0, false);
        }
    }
}

static void logout_auth_hook(lv_event_t * e)
{
    (void) e;
    s_authenticated = false;
}

static void install_auth_hooks(gg_ui_t * ui)
{
    if (s_auth_hooks_installed || (ui == NULL))
    {
        return;
    }

    bool installed = false;
    if (ui->Login.login_card_button_submit != NULL)
    {
        /* 不再默认预填账号密码：登录凭据由软键盘输入 */
        login_build_inputs(ui);

        /* 移除生成事件的无条件跳转处理器，改用"校验 → 跳转" */
        while (lv_obj_get_event_count(ui->Login.login_card_button_submit) > 0U)
        {
            lv_obj_remove_event(ui->Login.login_card_button_submit, 0U);
        }
        lv_obj_add_event_cb(ui->Login.login_card_button_submit, login_submit_verify_hook,
                            LV_EVENT_CLICKED, NULL);
        installed = true;
    }
    if (ui->Admin.button_logout != NULL)
    {
        lv_obj_add_event_cb(ui->Admin.button_logout, logout_auth_hook, LV_EVENT_CLICKED, NULL);
        installed = true;
    }
    if (installed)
    {
        s_auth_hooks_installed = true;
    }
}

static void refresh_auth_badge(gg_ui_t * ui)
{
    if ((ui == NULL) || (ui->Home.auth_badge_auth_badge_text == NULL) ||
        !lv_obj_is_valid(ui->Home.auth_badge_auth_badge_text))
    {
        return;
    }

    gui_custom_label_set_if_changed(ui->Home.auth_badge_auth_badge_text,
                                    s_authenticated ? "已认证" : "需要认证");
    lv_obj_set_style_text_color(ui->Home.auth_badge_auth_badge_text,
                                lv_color_hex(s_authenticated ? 0x14a66a : 0x687b99),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void refresh_admin_welcome(gg_ui_t * ui)
{
    if ((ui == NULL) || (ui->Admin.welcome == NULL) || !lv_obj_is_valid(ui->Admin.welcome))
    {
        return;
    }

    gui_custom_label_set_if_changed(ui->Admin.welcome,
                                    s_authenticated ? "当前药师：已登录" : "请先登录药师账号");
}

static void refresh_home_uptime(gg_ui_t * ui)
{
    if ((ui == NULL) || (ui->Home.welcome_card_hint == NULL) ||
        !lv_obj_is_valid(ui->Home.welcome_card_hint))
    {
        return;
    }

    /* 运行时长（开机 HH:MM:SS）：用 LVGL tick（模拟器与固件通用），
     * 仅当秒值变化时才更新文本 */
    uint32_t const sec = lv_tick_get() / 1000U;
    char buf[32];
    (void) snprintf(buf, sizeof(buf), "运行 %02lu:%02lu:%02lu",
                    (unsigned long) ((sec / 3600U) % 24U),
                    (unsigned long) ((sec / 60U) % 60U),
                    (unsigned long) (sec % 60U));
    gui_custom_label_set_if_changed(ui->Home.welcome_card_hint, buf);
}

static void network_badge_timer_cb(lv_timer_t * timer)
{
    gg_ui_t * ui = (gg_ui_t *) lv_timer_get_user_data(timer);

    /* GUI Guider creates Home/Login/Admin lazily.  Apply the label policy
     * ONCE per newly created screen set instead of every 500 ms: re-forcing
     * LV_SIZE_CONTENT on every tick overrode the GUI Guider fixed label
     * geometry and made short Chinese captions reflow into a second row /
     * overlap (docs: 双行标题 root cause).  The set only grows, so the
     * mask comparison fires exactly once per created page. */
    {
        static uint32_t s_known_screens = 0U;
        uint32_t mask = 0U;
        if (ui->Boot.screen)      { mask |= 1U << 0; }
        if (ui->Home.screen)      { mask |= 1U << 1; }
        if (ui->Pickup.screen)    { mask |= 1U << 2; }
        if (ui->Scan.screen)      { mask |= 1U << 3; }
        if (ui->Medicine.screen)  { mask |= 1U << 4; }
        if (ui->Login.screen)     { mask |= 1U << 5; }
        if (ui->Admin.screen)     { mask |= 1U << 6; }
        if (ui->Store.screen)     { mask |= 1U << 7; }
        if (ui->Logs.screen)      { mask |= 1U << 8; }
        if (ui->Device.screen)    { mask |= 1U << 9; }
        if (mask != s_known_screens)
        {
            s_known_screens = mask;
            configure_ui_label_layout(ui);
            /* configure_ui_label_layout 经 configure_single_line_label_box 把
             * Boot.title 的 text_align 重置为 LEFT，覆盖 boot_style_texts 的
             * CENTER——每次有新页面懒创建（mask 变化）都重新应用 Boot 文字
             * 样式，否则切换界面后 Boot 标题回到左对齐（"第二次加载没居中"）。 */
            boot_style_texts(ui);
        }
    }
    /* Login/Admin 页创建后补挂认证事件（懒创建页面出现一次即安装） */
    install_auth_hooks(ui);
    /* Home 页创建后补建 nuedc 图标（懒创建页面出现一次即安装） */
    home_build_images(ui);
    /* Store 页"重新识别"按钮 → Scan（模拟器+固件通用） */
    install_store_scan_hook(ui);
    /* Device 页"电机调试"入口（懒创建页面出现一次即安装） */
    install_device_motor_debug_entry(ui);
    /* Device 页"取药调试"入口（点格子取药，懒创建页面出现一次即安装） */
    install_device_pickup_debug_entry(ui);
    /* Device 页"无线调试"入口（原"运行自检"按钮，懒创建页面出现一次即安装） */
    install_device_wireless_entry(ui);
    /* Keep the GUI Guider-generated font, size and line metrics as the
     * canonical design values.  Runtime font replacement caused the small
     * panel headings to reflow differently from the Guider simulator. */
    refresh_network_badges(ui);
    refresh_arm_coordinates(ui);
    refresh_device_page_rows(ui);
    refresh_auth_badge(ui);
    refresh_admin_welcome(ui);
    refresh_home_uptime(ui);
    /* 电机调试页：脉冲值/状态实时刷新 */
    refresh_motor_debug_page();
    /* 取药调试页：选中格/流程状态实时刷新 */
    refresh_pickup_debug_page();
    /* 无线调试页：ESP-01S 配置状态实时刷新 */
    refresh_wireless_debug_page();
    /* 取药流程测试状态机推进（命令入 gantry_robot 队列，Motor 线程执行） */
    PickupTest_Service();

    /* 离开 Login 页 → 自动收起软键盘（回 Home/切 Admin 后不再遮挡） */
    if ((s_login_keyboard != NULL) && !lv_obj_has_flag(s_login_keyboard, LV_OBJ_FLAG_HIDDEN) &&
        (lv_screen_active() != ui->Login.screen))
    {
        login_close_keyboard();
    }
}

void gui_set_esp01s_state(gg_ui_t *ui, esp01s_ui_state_t state)
{
    if (ui == NULL)
    {
        return;
    }

    if (state == s_esp01s_state)
    {
        return; /* 无变化：避免每次 500ms 定时器都重设样式/文本 */
    }

    s_esp01s_state = state;
    refresh_network_badges(ui);
    refresh_device_page_rows(ui);
}

void gui_set_esp01s_online(gg_ui_t *ui, bool online)
{
    gui_set_esp01s_state(ui, online ? ESP01S_UI_ONLINE : ESP01S_UI_OFFLINE);
}

bool gui_get_esp01s_online(void)
{
    return (s_esp01s_state == ESP01S_UI_ONLINE);
}

esp01s_ui_state_t gui_get_esp01s_state(void)
{
    return s_esp01s_state;
}

bool gui_get_authenticated(void)
{
    return s_authenticated;
}

void gui_set_arm_coordinates(gg_ui_t *ui, int32_t x, int32_t y, int32_t z)
{
    if (ui == NULL)
    {
        return;
    }

    s_arm_x = x;
    s_arm_y = y;
    s_arm_z = z;
    refresh_arm_coordinates(ui);
}

/* Boot 启动页附加图片（数据在 OSPI 图标区，见 tools/gen_ospi_icons.py）：
 *  - boot_photo：背景照片铺满白色区域（brand_band 之下 148..480），最底层
 *  - 下半部分字体（title/subtitle/status）：白色 + 深色粗描边，封面背景上醒目 */
extern const lv_image_dsc_t boot_photo;
extern const lv_image_dsc_t icon_nuedc_70x42_ARGB8888;
static bool s_boot_images_built = false;

static void boot_style_texts(gg_ui_t * ui)
{
    /* 封面照片背景下：标题/副标题/状态 白色 + 深色粗描边（模拟加粗） */
    lv_obj_t * texts[3];
    texts[0] = ui->Boot.title;
    texts[1] = ui->Boot.subtitle;
    texts[2] = ui->Boot.status;
    uint8_t const outlines[3] = { 3U, 2U, 2U };
    for (int i = 0; i < 3; i++)
    {
        lv_obj_t * o = texts[i];
        if ((o == NULL) || !lv_obj_is_valid(o))
        {
            continue;
        }
        lv_obj_set_style_text_color(o, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_outline_stroke_width(o, outlines[i], LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_outline_stroke_color(o, lv_color_hex(0x10233f), LV_PART_MAIN | LV_STATE_DEFAULT);
        /* configure_ui_label_layout 把 title 框成 280x42（防换行），
         * 框内文字必须居中，否则 LV_ALIGN_TOP_MID 只居中框、文字整体左偏。 */
        lv_obj_set_style_text_align(o, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

static void boot_build_images(gg_ui_t * ui)
{
    if (s_boot_images_built || (ui == NULL) || (ui->Boot.screen == NULL) ||
        !lv_obj_is_valid(ui->Boot.screen))
    {
        return;
    }
    /* 烧录模式（OSPI 正被 loader 擦写）跳过：Boot 页渲染读 OSPI 大图会占用
     * 总线，导致 J-Link 读内存超时、烧录停滞。烧录结束（burn_mode=0）后
     * 下次正常启动再创建。 */
    if (ospi_ttf_loader_burn_mode())
    {
        return;
    }

    /* 背景照片：640x332（预处理缩放），放在屏幕最底层（brand_band 之下） */
    lv_obj_t * p_photo = lv_image_create(ui->Boot.screen);
    lv_obj_set_pos(p_photo, 0, 148);
    lv_obj_set_size(p_photo, 640, 332);
    lv_image_set_src(p_photo, &boot_photo);
    lv_obj_move_to_index(p_photo, 0);

    /* 下半部分字体：白色加粗（封面背景上醒目） */
    boot_style_texts(ui);

    s_boot_images_built = true;
}

/* Home 主界面：renesas 品牌图左侧放 nuedc 图标（相对定位，跟随 renesas） */
static bool s_home_nuedc_built = false;

static void home_build_images(gg_ui_t * ui)
{
    if (s_home_nuedc_built || (ui == NULL) || (ui->Home.screen == NULL) ||
        !lv_obj_is_valid(ui->Home.screen))
    {
        return;
    }
    if (ospi_ttf_loader_burn_mode())
    {
        return;
    }
    lv_obj_t * p_renesas = ui->Home.header_renesas_brand;
    if ((p_renesas == NULL) || !lv_obj_is_valid(p_renesas))
    {
        return;
    }
    lv_obj_t * p_nuedc = lv_image_create(ui->Home.header);
    lv_obj_set_pos(p_nuedc, lv_obj_get_x(p_renesas) - 8 - 70,
                   lv_obj_get_y(p_renesas) + (lv_obj_get_height(p_renesas) - 42) / 2);
    lv_obj_set_size(p_nuedc, 70, 42);
    lv_image_set_src(p_nuedc, &icon_nuedc_70x42_ARGB8888);
    s_home_nuedc_built = true;
}

/* ============================================================================
 * 电机调试页（手写 LVGL 页面，不进 GUI Guider 结构）
 * 四轴 X/Y/Z/抓手：设零 / 脉冲点动(+/-) / 步长切换 / 急停 / 返回。
 * 命令经 gantry_debug 队列投递，由 Motor 线程独占执行 ZDT（不改协议）。
 * 脉冲值为本地累计（ZDT 无回读），设零清零。
 * ==========================================================================*/
#define GDBG_STEP_SMALL  100L
#define GDBG_STEP_MED    1000L
#define GDBG_STEP_LARGE  10000L
#define GDBG_JOG_SPEED   400U
#define GDBG_JOG_ACC     40U

static lv_obj_t * s_dbg_screen = NULL;
static lv_obj_t * s_dbg_pulse_label[4] = {NULL, NULL, NULL, NULL};
static lv_obj_t * s_dbg_step_label = NULL;
static lv_obj_t * s_dbg_status_label = NULL;
static int32_t s_dbg_step = GDBG_STEP_MED;
static bool s_dbg_built = false;
static bool s_device_dbg_entry = false;

static const char * const s_dbg_axis_names[4] = {"X 轴", "Y 轴", "Z 轴", "抓手"};
static const uint8_t s_dbg_axis_bits[4] = {GDBG_AXIS_X, GDBG_AXIS_Y, GDBG_AXIS_Z, GDBG_AXIS_CATCH};

static void motor_debug_back_hook(lv_event_t * e)
{
    (void) e;
    if ((s_ui != NULL) && (s_ui->Device.screen != NULL) && lv_obj_is_valid(s_ui->Device.screen))
    {
        lv_screen_load_anim(s_ui->Device.screen, LV_SCREEN_LOAD_ANIM_FADE_IN, 180, 0, false);
    }
}

/* 点动：user_data = 轴索引(0..3)，按钮 "+/-" 由对象 user_data 高位置区分 */
static void motor_debug_jog_hook(lv_event_t * e)
{
    lv_obj_t * btn = lv_event_get_target_obj(e);
    intptr_t arg = (intptr_t) lv_obj_get_user_data(btn);
    uint8_t axis = (uint8_t) (arg & 0x0F);
    int32_t sign = ((arg & 0x10) != 0) ? 1 : -1;
    if (axis >= 4U)
    {
        return;
    }
    (void) GantryDebug_Jog(s_dbg_axis_bits[axis], sign * s_dbg_step, GDBG_JOG_SPEED, GDBG_JOG_ACC);
}

static void motor_debug_zero_hook(lv_event_t * e)
{
    lv_obj_t * btn = lv_event_get_target_obj(e);
    intptr_t axis = (intptr_t) lv_obj_get_user_data(btn);
    if ((axis >= 0) && (axis < 4))
    {
        (void) GantryDebug_SetZero(s_dbg_axis_bits[axis]);
    }
}

static void motor_debug_gozero_hook(lv_event_t * e)
{
    lv_obj_t * btn = lv_event_get_target_obj(e);
    intptr_t axis = (intptr_t) lv_obj_get_user_data(btn);
    if ((axis >= 0) && (axis < 4))
    {
        (void) GantryDebug_Gozero(s_dbg_axis_bits[axis]);
    }
}

static void motor_debug_step_hook(lv_event_t * e)
{
    lv_obj_t * btn = lv_event_get_target_obj(e);
    s_dbg_step = (int32_t) (intptr_t) lv_obj_get_user_data(btn);
    if (s_dbg_step_label != NULL)
    {
        char buf[32];
        (void) snprintf(buf, sizeof(buf), "步长: %ld", (long) s_dbg_step);
        gui_custom_label_set_if_changed(s_dbg_step_label, buf);
    }
}

static void motor_debug_estop_hook(lv_event_t * e)
{
    (void) e;
    (void) GantryDebug_Stop(GDBG_AXIS_ALL);
    /* 同时解除 gantry_robot 的挂起状态（自动归零等流程），调试页立即恢复可操作 */
    (void) Gantry_EmergencyStop();
    if (s_dbg_status_label != NULL)
    {
        gui_custom_label_set_if_changed(s_dbg_status_label, "已急停");
    }
}

static void motor_debug_unprotect_hook(lv_event_t * e)
{
    (void) e;
    (void) GantryDebug_Unprotect(GDBG_AXIS_ALL);
    if (s_dbg_status_label != NULL)
    {
        gui_custom_label_set_if_changed(s_dbg_status_label, "已发送解除保护");
    }
}

/* 真空泵 / 电磁阀 输出开关：关闭=低电平，打开=高电平（relay_drv 语义一致） */
static const char * const s_relay_names[RELAY_CHANNEL_COUNT] = {
    "真空泵", "电磁阀1", "电磁阀2"
};

static void motor_debug_relay_hook(lv_event_t * e)
{
    lv_obj_t * sw = lv_event_get_target_obj(e);
    intptr_t ch = (intptr_t) lv_obj_get_user_data(sw);
    if ((ch < 0) || (ch >= (intptr_t) RELAY_CHANNEL_COUNT))
    {
        return;
    }
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    fsp_err_t err = RelayDrv_Set((relay_channel_t) ch, on);
    if (s_dbg_status_label != NULL)
    {
        char buf[72];
        (void) snprintf(buf, sizeof(buf), "%s %s (%s)",
                        s_relay_names[ch], on ? "已打开" : "已关闭",
                        (err == FSP_SUCCESS) ? "输出正常" : "GPIO 写入失败");
        gui_custom_label_set_if_changed(s_dbg_status_label, buf);
    }
}

/* 取药流程测试：完整取药动作（回零→去第一层→伸Z→夹→收Z→搬运→放下） */
static void motor_debug_pickup_test_hook(lv_event_t * e)
{
    (void) e;
    PickupTest_Start();
    if (s_dbg_status_label != NULL)
    {
        gui_custom_label_set_if_changed(s_dbg_status_label, "取药测试启动...");
    }
}

static void refresh_motor_debug_page(void)
{
    if (!s_dbg_built || (s_dbg_screen == NULL) || !lv_obj_is_valid(s_dbg_screen))
    {
        return;
    }
    gdbg_status_t st;
    GantryDebug_GetStatus(&st);
    for (int i = 0; i < 4; i++)
    {
        if (s_dbg_pulse_label[i] == NULL)
        {
            continue;
        }
        char buf[24];
        (void) snprintf(buf, sizeof(buf), "%ld", (long) st.pulse[i]);
        gui_custom_label_set_if_changed(s_dbg_pulse_label[i], buf);
    }
    if (s_dbg_status_label != NULL)
    {
        /* 取药流程测试进行中/有结果时，状态行优先显示测试进度 */
        pickup_test_state_t pst;
        const char * ptext = NULL;
        PickupTest_GetStatus(&pst, &ptext);
        if ((pst != PICKUP_TEST_IDLE) && (ptext != NULL))
        {
            gui_custom_label_set_if_changed(s_dbg_status_label, ptext);
            return;
        }
        /* 机械臂故障/停止（如自动归零超时无应答）：状态行提示，方便排查 */
        gantry_status_t gst;
        Gantry_GetStatus(&gst);
        if ((gst.state == GANTRY_STATE_FAULT) || (gst.state == GANTRY_STATE_STOPPED))
        {
            char buf[72];
            (void) snprintf(buf, sizeof(buf),
                            "机械臂%s: err=%d（检查电机电源/接线，点急停后重试）",
                            (gst.state == GANTRY_STATE_FAULT) ? "故障" : "已停止",
                            (int) gst.last_error);
            gui_custom_label_set_if_changed(s_dbg_status_label, buf);
            return;
        }
        /* 到位状态：收到过 FD 9F 6B(移动到位)/9A 02 6B(回零完成) → 已到位 */
        gdbg_status_t st2;
        GantryDebug_GetStatus(&st2);
        char buf[128];
        buf[0] = '\0';
        size_t used = 0U;
        for (int i = 0; i < 4; i++)
        {
            int n = snprintf(buf + used, sizeof(buf) - used, "%s:%s  ",
                             s_dbg_axis_names[i],
                             (st2.arrived[i] > 0U) ? "已到位" : "未到位");
            if (n > 0)
            {
                used += (size_t) n;
            }
        }
        if (GantryDebug_IsIdle())
        {
            gui_custom_label_set_if_changed(s_dbg_status_label, buf);
        }
    }
}

static void motor_debug_build_page(void)
{
    if (s_dbg_built)
    {
        return;
    }
    s_dbg_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_dbg_screen, lv_color_hex(0xf6f7ff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_dbg_screen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    const lv_font_t * font = (g_tiny_font != NULL) ? g_tiny_font : &lv_font_SourceHanSerifSC_18;

    /* 顶栏：返回 + 标题 */
    lv_obj_t * header = lv_obj_create(s_dbg_screen);
    lv_obj_set_size(header, 640, 56);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(header, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(header, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(header, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t * btn_back = lv_button_create(header);
    lv_obj_set_size(btn_back, 122, 36);
    lv_obj_set_pos(btn_back, 16, 10);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0xf0edff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn_back, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn_back, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn_back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_back, motor_debug_back_hook, LV_EVENT_CLICKED, NULL);
    lv_obj_t * lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, "<  返回");
    lv_obj_set_style_text_font(lbl_back, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(lbl_back, lv_color_hex(0x7157d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(lbl_back);

    lv_obj_t * lbl_title = lv_label_create(header);
    lv_label_set_text(lbl_title, "电机调试");
    lv_obj_set_style_text_font(lbl_title, (g_tiny_font != NULL) ? g_tiny_font : &lv_font_SourceHanSerifSC_22,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0x10233f), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, 0);

    /* 顶栏右侧："参数设置"入口（药柜布局/取药动作参数） */
    lv_obj_t * btn_params = lv_button_create(header);
    lv_obj_set_size(btn_params, 108, 36);
    lv_obj_set_pos(btn_params, 516, 10);
    lv_obj_set_style_bg_color(btn_params, lv_color_hex(0xf0fbf7), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn_params, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn_params, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn_params, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_params, device_params_nav_hook, LV_EVENT_CLICKED, NULL);
    lv_obj_t * t_params = lv_label_create(btn_params);
    lv_label_set_text(t_params, "参数设置");
    lv_obj_set_style_text_font(t_params, (g_tiny_font != NULL) ? g_tiny_font : &lv_font_SourceHanSerifSC_18,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(t_params, lv_color_hex(0x14a66a), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(t_params);

    /* 四轴行卡片 */
    for (int i = 0; i < 4; i++)
    {
        lv_obj_t * card = lv_obj_create(s_dbg_screen);
        lv_obj_set_size(card, 620, 60);
        lv_obj_set_pos(card, 10, 62 + i * 68);
        lv_obj_set_style_bg_color(card, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(card, lv_color_hex(0xd8e1ef), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(card, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_radius(card, 14, LV_PART_MAIN | LV_STATE_DEFAULT);

        lv_obj_t * lbl_axis = lv_label_create(card);
        lv_label_set_text(lbl_axis, s_dbg_axis_names[i]);
        lv_obj_set_style_text_font(lbl_axis, font, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(lbl_axis, lv_color_hex(0x10233f), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_pos(lbl_axis, 20, 24);

        s_dbg_pulse_label[i] = lv_label_create(card);
        lv_label_set_text(s_dbg_pulse_label[i], "0");
        lv_obj_set_style_text_font(s_dbg_pulse_label[i],
                                   (g_tiny_font != NULL) ? g_tiny_font : &lv_font_SourceHanSerifSC_22,
                                   LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(s_dbg_pulse_label[i], lv_color_hex(0x0891b2),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_pos(s_dbg_pulse_label[i], 96, 20);
        lv_obj_set_width(s_dbg_pulse_label[i], 180);
        lv_obj_set_style_text_align(s_dbg_pulse_label[i], LV_TEXT_ALIGN_RIGHT,
                                    LV_PART_MAIN | LV_STATE_DEFAULT);

        /* [-] [+] [设零] [归零] */
        const lv_coord_t b_w = 60;
        const lv_coord_t b_h = 44;
        lv_obj_t * btn_minus = lv_button_create(card);
        lv_obj_set_size(btn_minus, b_w, b_h);
        lv_obj_set_pos(btn_minus, 292, 8);
        lv_obj_set_style_bg_color(btn_minus, lv_color_hex(0xfff0f0), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(btn_minus, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_radius(btn_minus, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(btn_minus, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_user_data(btn_minus, (void *) (intptr_t) i); /* 负向 */
        lv_obj_add_event_cb(btn_minus, motor_debug_jog_hook, LV_EVENT_CLICKED, NULL);
        lv_obj_t * t_minus = lv_label_create(btn_minus);
        lv_label_set_text(t_minus, "-");
        lv_obj_set_style_text_font(t_minus, (g_tiny_font != NULL) ? g_tiny_font : &lv_font_SourceHanSerifSC_22,
                                   LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(t_minus, lv_color_hex(0xef5350), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_center(t_minus);

        lv_obj_t * btn_plus = lv_button_create(card);
        lv_obj_set_size(btn_plus, b_w, b_h);
        lv_obj_set_pos(btn_plus, 360, 8);
        lv_obj_set_style_bg_color(btn_plus, lv_color_hex(0xf0fbf7), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(btn_plus, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_radius(btn_plus, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(btn_plus, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_user_data(btn_plus, (void *) (intptr_t) (i | 0x10)); /* 正向 */
        lv_obj_add_event_cb(btn_plus, motor_debug_jog_hook, LV_EVENT_CLICKED, NULL);
        lv_obj_t * t_plus = lv_label_create(btn_plus);
        lv_label_set_text(t_plus, "+");
        lv_obj_set_style_text_font(t_plus, (g_tiny_font != NULL) ? g_tiny_font : &lv_font_SourceHanSerifSC_22,
                                   LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(t_plus, lv_color_hex(0x14a66a), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_center(t_plus);

        lv_obj_t * btn_zero = lv_button_create(card);
        lv_obj_set_size(btn_zero, 72, 44);
        lv_obj_set_pos(btn_zero, 432, 8);
        lv_obj_set_style_bg_color(btn_zero, lv_color_hex(0xfff4dc), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(btn_zero, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_radius(btn_zero, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(btn_zero, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_user_data(btn_zero, (void *) (intptr_t) i);
        lv_obj_add_event_cb(btn_zero, motor_debug_zero_hook, LV_EVENT_CLICKED, NULL);
        lv_obj_t * t_zero = lv_label_create(btn_zero);
        lv_label_set_text(t_zero, "设零");
        lv_obj_set_style_text_font(t_zero, font, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(t_zero, lv_color_hex(0xf59e0b), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_center(t_zero);

        lv_obj_t * btn_gozero = lv_button_create(card);
        lv_obj_set_size(btn_gozero, 72, 44);
        lv_obj_set_pos(btn_gozero, 516, 8);
        lv_obj_set_style_bg_color(btn_gozero, lv_color_hex(0xe5f7fb), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(btn_gozero, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_radius(btn_gozero, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(btn_gozero, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_user_data(btn_gozero, (void *) (intptr_t) i);
        lv_obj_add_event_cb(btn_gozero, motor_debug_gozero_hook, LV_EVENT_CLICKED, NULL);
        lv_obj_t * t_gozero = lv_label_create(btn_gozero);
        lv_label_set_text(t_gozero, "归零");
        lv_obj_set_style_text_font(t_gozero, font, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(t_gozero, lv_color_hex(0x0891b2), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_center(t_gozero);
    }

    /* 真空泵 / 电磁阀 输出开关行（关闭=低电平，打开=高电平） */
    lv_obj_t * relay_card = lv_obj_create(s_dbg_screen);
    lv_obj_set_size(relay_card, 620, 56);
    lv_obj_set_pos(relay_card, 10, 334);
    lv_obj_set_style_bg_color(relay_card, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(relay_card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(relay_card, lv_color_hex(0xd8e1ef), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(relay_card, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(relay_card, 14, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t * lbl_relay_title = lv_label_create(relay_card);
    lv_label_set_text(lbl_relay_title, "输出:");
    lv_obj_set_style_text_font(lbl_relay_title, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(lbl_relay_title, lv_color_hex(0x687b99), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(lbl_relay_title, 18, 19);

    for (int i = 0; i < (int) RELAY_CHANNEL_COUNT; i++)
    {
        lv_obj_t * lbl_name = lv_label_create(relay_card);
        lv_label_set_text(lbl_name, s_relay_names[i]);
        lv_obj_set_style_text_font(lbl_name, font, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(lbl_name, lv_color_hex(0x10233f), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_pos(lbl_name, 108 + i * 158, 19);

        lv_obj_t * sw = lv_switch_create(relay_card);
        lv_obj_set_size(sw, 64, 32);
        lv_obj_set_pos(sw, 108 + i * 158 + 78, 12);
        lv_obj_set_user_data(sw, (void *) (intptr_t) i);
        lv_obj_add_event_cb(sw, motor_debug_relay_hook, LV_EVENT_VALUE_CHANGED, NULL);
    }

    /* 底部工具区：应答状态行 + 步长 + 读脉冲 + 急停 */
    s_dbg_status_label = lv_label_create(s_dbg_screen);
    lv_label_set_text(s_dbg_status_label, "就绪");
    lv_obj_set_style_text_font(s_dbg_status_label, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(s_dbg_status_label, lv_color_hex(0x14a66a), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_long_mode(s_dbg_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(s_dbg_status_label, 20, 396);
    lv_obj_set_size(s_dbg_status_label, 600, 30);

    s_dbg_step_label = lv_label_create(s_dbg_screen);
    {
        char buf[32];
        (void) snprintf(buf, sizeof(buf), "步长: %ld", (long) s_dbg_step);
        lv_label_set_text(s_dbg_step_label, buf);
    }
    lv_obj_set_style_text_font(s_dbg_step_label, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(s_dbg_step_label, lv_color_hex(0x687b99), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(s_dbg_step_label, 20, 434);

    const int32_t steps[3] = {GDBG_STEP_SMALL, GDBG_STEP_MED, GDBG_STEP_LARGE};
    for (int i = 0; i < 3; i++)
    {
        lv_obj_t * btn = lv_button_create(s_dbg_screen);
        lv_obj_set_size(btn, 60, 44);
        lv_obj_set_pos(btn, 110 + i * 66, 426);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(btn, lv_color_hex(0xd8e1ef), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_radius(btn, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_user_data(btn, (void *) (intptr_t) steps[i]);
        lv_obj_add_event_cb(btn, motor_debug_step_hook, LV_EVENT_CLICKED, NULL);
        lv_obj_t * t = lv_label_create(btn);
        char buf[12];
        (void) snprintf(buf, sizeof(buf), "%ld", (long) steps[i]);
        lv_label_set_text(t, buf);
        lv_obj_set_style_text_font(t, font, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(t, lv_color_hex(0x10233f), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_center(t);
    }

    /* 解除堵转保护：撞限位/堵转后先解除再继续点动 */
    lv_obj_t * btn_unprot = lv_button_create(s_dbg_screen);
    lv_obj_set_size(btn_unprot, 80, 44);
    lv_obj_set_pos(btn_unprot, 346, 426);
    lv_obj_set_style_bg_color(btn_unprot, lv_color_hex(0xfff4dc), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn_unprot, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn_unprot, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn_unprot, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_unprot, motor_debug_unprotect_hook, LV_EVENT_CLICKED, NULL);
    lv_obj_t * t_unprot = lv_label_create(btn_unprot);
    lv_label_set_text(t_unprot, "解除");
    lv_obj_set_style_text_font(t_unprot, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(t_unprot, lv_color_hex(0xf59e0b), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(t_unprot);

    /* 取药流程测试：完整取药动作（零点=第一个药柜，抓第一层 40mm 药盒搬到暂存区） */
    lv_obj_t * btn_pickup_test = lv_button_create(s_dbg_screen);
    lv_obj_set_size(btn_pickup_test, 80, 44);
    lv_obj_set_pos(btn_pickup_test, 428, 426);
    lv_obj_set_style_bg_color(btn_pickup_test, lv_color_hex(0xe5f7fb), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn_pickup_test, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn_pickup_test, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn_pickup_test, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_pickup_test, motor_debug_pickup_test_hook, LV_EVENT_CLICKED, NULL);
    lv_obj_t * t_pickup_test = lv_label_create(btn_pickup_test);
    lv_label_set_text(t_pickup_test, "取药测试");
    lv_obj_set_style_text_font(t_pickup_test, (g_tiny_font != NULL) ? g_tiny_font : &lv_font_SourceHanSerifSC_18,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(t_pickup_test, lv_color_hex(0x0891b2), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(t_pickup_test);

    lv_obj_t * btn_estop = lv_button_create(s_dbg_screen);
    lv_obj_set_size(btn_estop, 104, 56);
    lv_obj_set_pos(btn_estop, 516, 420);
    lv_obj_set_style_bg_color(btn_estop, lv_color_hex(0xef5350), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn_estop, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn_estop, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn_estop, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_estop, motor_debug_estop_hook, LV_EVENT_CLICKED, NULL);
    lv_obj_t * t_estop = lv_label_create(btn_estop);
    lv_label_set_text(t_estop, "急停");
    lv_obj_set_style_text_font(t_estop, (g_tiny_font != NULL) ? g_tiny_font : &lv_font_SourceHanSerifSC_22,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(t_estop, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(t_estop);

    s_dbg_built = true;
}

/* Device 页入口按钮："电机调试"（自检按钮旁） */
static void device_motor_debug_nav_hook(lv_event_t * e)
{
    (void) e;
    motor_debug_build_page();
    if (s_dbg_screen != NULL)
    {
        lv_screen_load_anim(s_dbg_screen, LV_SCREEN_LOAD_ANIM_FADE_IN, 180, 0, false);
    }
}

static void install_device_motor_debug_entry(gg_ui_t * ui)
{
    if (s_device_dbg_entry || (ui == NULL) || (ui->Device.screen == NULL) ||
        !lv_obj_is_valid(ui->Device.screen))
    {
        return;
    }
    lv_obj_t * btn = lv_button_create(ui->Device.screen);
    lv_obj_set_size(btn, 168, 42);
    lv_obj_set_pos(btn, 20, 416);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x7157d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn, device_motor_debug_nav_hook, LV_EVENT_CLICKED, NULL);
    lv_obj_t * t = lv_label_create(btn);
    lv_label_set_text(t, "电机调试");
    lv_obj_set_style_text_font(t, (g_tiny_font != NULL) ? g_tiny_font : &lv_font_SourceHanSerifSC_18,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(t, lv_color_hex(0x7157d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(t);
    s_device_dbg_entry = true;
}

/* ============================================================================
 * 取药调试页（手写 LVGL，不进 GUI Guider 结构）
 * 按药柜布局（层数×格数，参数页可设）生成格子矩阵，点击某个格子即对该格
 * 执行完整取药流程（XY→该格 → Z伸出 → 夹爪闭合 → Z收回 → 暂存区 → 放下）。
 * 布局数据来自 PickupParams_Get()：shelf_count 层、slots_per_row 格/层。
 * ==========================================================================*/
static lv_obj_t * s_pk_screen = NULL;
static bool s_pk_built = false;
static lv_obj_t * s_pk_status_label = NULL;
static lv_obj_t * s_pk_current_label = NULL;   /* 当前选中格提示 */
static lv_obj_t * s_pk_slot_btn[8] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
static lv_obj_t * s_pk_shelf_btn[4] = {NULL, NULL, NULL, NULL};

static void pickup_debug_back_hook(lv_event_t * e)
{
    (void) e;
    if ((s_ui != NULL) && (s_ui->Device.screen != NULL) && lv_obj_is_valid(s_ui->Device.screen))
    {
        lv_screen_load_anim(s_ui->Device.screen, LV_SCREEN_LOAD_ANIM_FADE_IN, 180, 0, false);
    }
}

/* 点击某个格子：设置测试目标 → 启动取药流程 */
static void pickup_debug_slot_hook(lv_event_t * e)
{
    lv_obj_t * btn = lv_event_get_target_obj(e);
    intptr_t arg = (intptr_t) lv_obj_get_user_data(btn);
    uint8_t shelf = (uint8_t) ((arg >> 8) & 0xFFU);
    uint8_t slot  = (uint8_t) (arg & 0xFFU);
    const pickup_params_t * pp = PickupParams_Get();
    if ((shelf >= pp->shelf_count) || (slot >= pp->slots_per_row))
    {
        if (s_pk_status_label != NULL)
        {
            lv_label_set_text(s_pk_status_label, "该格超出药柜范围");
        }
        return;
    }
    /* 量程保护：目标 X/Y 换算必须落在机械臂实测行程内（X≤396.7mm(17000脉冲)、Y≤291.7mm(12500脉冲)），
     * 否则发命令会软限位失败/可能损坏电机——直接提示不启动。
     * 调试页格子 X = 第1列70mm/第2列折中233.3mm/第3列X最大396.7mm。 */
    float tx = PickupParams_TestSlotX(slot);
    float ty = PickupParams_ShelfY(shelf);
    if ((tx < 0.0f) || (tx > 396.7f) || (ty < 0.0f) || (ty > 291.7f))
    {
        if (s_pk_status_label != NULL)
        {
            char tmp[72];
            (void) snprintf(tmp, sizeof(tmp), "第%u层第%u格坐标(%d,%d)超量程，未启动",
                            (unsigned) shelf + 1U, (unsigned) slot + 1U,
                            (int) tx, (int) ty);
            lv_label_set_text(s_pk_status_label, tmp);
        }
        return;
    }
    (void) PickupParams_SetTestTarget(shelf, slot);
    PickupTest_Start();
    if (s_pk_status_label != NULL)
    {
        char tmp[64];
        (void) snprintf(tmp, sizeof(tmp), "已选 第%u层第%u格，取药启动...",
                        (unsigned) shelf + 1U, (unsigned) slot + 1U);
        lv_label_set_text(s_pk_status_label, tmp);
    }
}

static void pickup_debug_estop_hook(lv_event_t * e)
{
    (void) e;
    (void) Gantry_EmergencyStop();
}

/* 全体归零：Z 先回零 → X/Y 回零（gantry_robot 固定时序，回到第一个药柜零点）。
 * 注意：抓手(CATCH)不回零——用户约束"抓手只在 上电 或 电机调试界面 归零"，
 * 取药/调试流程一律用 move 指令；抓手零点由上电自动归零或调试页抓手"归零"确定。 */
static void pickup_debug_home_all_hook(lv_event_t * e)
{
    (void) e;
    (void) Gantry_ClearFault();
    (void) Gantry_Home(GANTRY_AXIS_MASK_ALL);
}

static void refresh_pickup_debug_page(void)
{
    if (!s_pk_built || (s_pk_screen == NULL) || !lv_obj_is_valid(s_pk_screen))
    {
        return;
    }
    const pickup_params_t * pp = PickupParams_Get();
    char buf[64];
    if (s_pk_current_label != NULL)
    {
        (void) snprintf(buf, sizeof(buf), "当前选中: 第%u层 第%u格",
                        (unsigned) pp->test_shelf + 1U, (unsigned) pp->test_slot + 1U);
        gui_custom_label_set_if_changed(s_pk_current_label, buf);
    }
    if (s_pk_status_label != NULL)
    {
        /* 优先显示取药流程进度/结果 */
        pickup_test_state_t pst;
        const char * ptext = NULL;
        PickupTest_GetStatus(&pst, &ptext);
        if ((pst != PICKUP_TEST_IDLE) && (ptext != NULL))
        {
            gui_custom_label_set_if_changed(s_pk_status_label, ptext);
            return;
        }
        gantry_status_t gst;
        Gantry_GetStatus(&gst);
        if ((gst.state == GANTRY_STATE_FAULT) || (gst.state == GANTRY_STATE_STOPPED))
        {
            (void) snprintf(buf, sizeof(buf), "机械臂%s: err=%d", 
                            (gst.state == GANTRY_STATE_FAULT) ? "故障" : "已停止",
                            (int) gst.last_error);
            gui_custom_label_set_if_changed(s_pk_status_label, buf);
            return;
        }
        gui_custom_label_set_if_changed(s_pk_status_label, "点击格子开始取药");
    }
}

static void pickup_debug_build_page(void)
{
    if (s_pk_built)
    {
        return;
    }
    const pickup_params_t * pp = PickupParams_Get();
    s_pk_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_pk_screen, lv_color_hex(0xf6f7ff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_pk_screen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    const lv_font_t * font = (g_tiny_font != NULL) ? g_tiny_font : &lv_font_SourceHanSerifSC_18;

    /* 顶栏：返回 + 标题 + 急停 */
    lv_obj_t * header = lv_obj_create(s_pk_screen);
    lv_obj_set_size(header, 640, 56);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(header, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(header, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(header, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t * btn_back = lv_button_create(header);
    lv_obj_set_size(btn_back, 122, 36);
    lv_obj_set_pos(btn_back, 16, 10);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0xf0edff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn_back, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn_back, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn_back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_back, pickup_debug_back_hook, LV_EVENT_CLICKED, NULL);
    lv_obj_t * lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, "<  返回");
    lv_obj_set_style_text_font(lbl_back, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(lbl_back, lv_color_hex(0x7157d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(lbl_back);

    lv_obj_t * lbl_title = lv_label_create(header);
    lv_label_set_text(lbl_title, "取药调试");
    lv_obj_set_style_text_font(lbl_title, (g_tiny_font != NULL) ? g_tiny_font : &lv_font_SourceHanSerifSC_22,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0x10233f), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t * btn_estop = lv_button_create(header);
    lv_obj_set_size(btn_estop, 96, 36);
    lv_obj_set_pos(btn_estop, 528, 10);
    lv_obj_set_style_bg_color(btn_estop, lv_color_hex(0xef5350), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn_estop, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn_estop, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn_estop, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_estop, pickup_debug_estop_hook, LV_EVENT_CLICKED, NULL);
    lv_obj_t * t_estop = lv_label_create(btn_estop);
    lv_label_set_text(t_estop, "急停");
    lv_obj_set_style_text_font(t_estop, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(t_estop, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(t_estop);

    /* 药柜格子矩阵：shelf_count 层，每层 slots_per_row 格。
     * 屏幕宽 640：左侧 88px 放层号标签，右侧 540px 分格（最大 8 格）。 */
    const lv_coord_t grid_x = 88;
    const lv_coord_t grid_w = 540;
    const lv_coord_t grid_y = 70;
    const lv_coord_t shelf_h = 76;
    uint8_t rows = (pp->shelf_count > 0U) ? pp->shelf_count : 1U;
    uint8_t cols = (pp->slots_per_row > 0U) ? pp->slots_per_row : 1U;
    if (cols > 8U) cols = 8U;
    if (rows > 4U) rows = 4U;
    const lv_coord_t slot_w = grid_w / (lv_coord_t) cols;

    for (uint8_t s = 0U; s < rows; s++)
    {
        /* 布局镜像（对应药柜实际视角）：
         * 第1层放最下面（row 坐标 = (rows-1-s)），第1格放最右边（col = (cols-1-c)）。 */
        lv_coord_t row_y = grid_y + (lv_coord_t) (rows - 1U - s) * shelf_h;
        if (s < 4U)
        {
            lv_obj_t * lbl = lv_label_create(s_pk_screen);
            char tmp[16];
            (void) snprintf(tmp, sizeof(tmp), "第%u层", (unsigned) s + 1U);
            lv_label_set_text(lbl, tmp);
            lv_obj_set_style_text_font(lbl, font, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(lbl, lv_color_hex(0x687b99), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_pos(lbl, 8, row_y + 26);
            lv_obj_set_size(lbl, 76, 24);
            lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        for (uint8_t c = 0U; c < cols; c++)
        {
            if ((s >= 4U) || (c >= 8U))
            {
                continue;
            }
            lv_coord_t col_x = grid_x + (lv_coord_t) (cols - 1U - c) * slot_w;
            lv_obj_t * btn = lv_button_create(s_pk_screen);
            lv_obj_set_size(btn, slot_w - 6, shelf_h - 8);
            lv_obj_set_pos(btn, col_x + 3, row_y + 4);
            lv_obj_set_style_bg_color(btn, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_color(btn, lv_color_hex(0x0891b2), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_radius(btn, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_user_data(btn, (void *) (intptr_t) (((intptr_t) s << 8) | (intptr_t) c));
            lv_obj_add_event_cb(btn, pickup_debug_slot_hook, LV_EVENT_CLICKED, NULL);
            lv_obj_t * t = lv_label_create(btn);
            char tmp[12];
            (void) snprintf(tmp, sizeof(tmp), "%u", (unsigned) c + 1U);
            lv_label_set_text(t, tmp);
            lv_obj_set_style_text_font(t, (g_tiny_font != NULL) ? g_tiny_font : &lv_font_SourceHanSerifSC_22,
                                       LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(t, lv_color_hex(0x0891b2), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_center(t);
        }
    }

    /* 底部信息区 */
    lv_coord_t info_y = grid_y + (lv_coord_t) rows * shelf_h;

    /* 全体归零按钮（回到第一个药柜零点） */
    lv_obj_t * btn_home = lv_button_create(s_pk_screen);
    lv_obj_set_size(btn_home, 130, 44);
    lv_obj_set_pos(btn_home, 20, info_y + 8);
    lv_obj_set_style_bg_color(btn_home, lv_color_hex(0xe5f7fb), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn_home, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn_home, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn_home, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_home, pickup_debug_home_all_hook, LV_EVENT_CLICKED, NULL);
    lv_obj_t * t_home = lv_label_create(btn_home);
    lv_label_set_text(t_home, "全体归零");
    lv_obj_set_style_text_font(t_home, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(t_home, lv_color_hex(0x0891b2), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(t_home);

    s_pk_current_label = lv_label_create(s_pk_screen);
    lv_obj_set_style_text_font(s_pk_current_label, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(s_pk_current_label, lv_color_hex(0x10233f), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(s_pk_current_label, 170, info_y + 20);

    s_pk_status_label = lv_label_create(s_pk_screen);
    lv_label_set_text(s_pk_status_label, "点击格子开始取药");
    lv_obj_set_style_text_font(s_pk_status_label, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(s_pk_status_label, lv_color_hex(0x14a66a), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_long_mode(s_pk_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(s_pk_status_label, 170, info_y + 44);
    lv_obj_set_size(s_pk_status_label, 450, 40);

    s_pk_built = true;
}

static void device_pickup_debug_nav_hook(lv_event_t * e)
{
    (void) e;
    pickup_debug_build_page();
    if (s_pk_screen != NULL)
    {
        lv_screen_load_anim(s_pk_screen, LV_SCREEN_LOAD_ANIM_FADE_IN, 180, 0, false);
    }
}

static void install_device_pickup_debug_entry(gg_ui_t * ui)
{
    if ((ui == NULL) || (ui->Device.screen == NULL) || !lv_obj_is_valid(ui->Device.screen))
    {
        return;
    }
    /* 电机调试按钮在 (20,416)、自检按钮在 (236,416)，取药调试放自检右侧 */
    lv_obj_t * btn = lv_button_create(ui->Device.screen);
    lv_obj_set_size(btn, 168, 42);
    lv_obj_set_pos(btn, 452, 416);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x0891b2), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn, device_pickup_debug_nav_hook, LV_EVENT_CLICKED, NULL);
    lv_obj_t * t = lv_label_create(btn);
    lv_label_set_text(t, "取药调试");
    lv_obj_set_style_text_font(t, (g_tiny_font != NULL) ? g_tiny_font : &lv_font_SourceHanSerifSC_18,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(t, lv_color_hex(0x0891b2), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(t);
}

/* ============================================================================
 * 取药参数设置页（手写 LVGL，不进 GUI Guider 结构）
 * 药柜布局（层高/格子宽度）与取药动作参数全部可改，保存到板载 OSPI。
 * 点"改"弹数字键盘编辑；[保存] 写 Flash 并应用速度；[恢复默认] 回退默认。
 * ==========================================================================*/
enum
{
    P_SHELF_COUNT = 0, P_SHELF_Y0, P_SHELF_Y1, P_SHELF_Y2, P_SLOTS_PER_ROW,
    P_BOX_WIDTH, P_SLOT_WIDTH, P_Z_REACH, P_GRIP_PULSES, P_GRIP_MARGIN,
    P_STORE_X, P_STORE_Y,
    P_XY_SPEED, P_XY_ACC, P_Z_SPEED, P_Z_ACC, P_GRIP_SPEED, P_GRIP_ACC,
    P_TEST_SHELF, P_TEST_SLOT, P_AUTO_HOME, P_COUNT
};

static const char * const s_pnames[P_COUNT] = {
    "层数", "第一层Y", "第二层Y", "第三层Y", "每层格数", "药盒宽度", "格子宽度",
    "Z伸出量", "夹爪闭合", "夹紧余量", "暂存区X", "暂存区Y",
    "XY速度", "XY加速度", "Z速度", "Z加速度", "夹爪速度", "夹爪加速度",
    "测试层号", "测试格号", "上电自动归零"
};
static const char * const s_punits[P_COUNT] = {
    "层", "mm", "mm", "mm", "格", "mm", "mm", "mm", "脉冲", "mm", "mm", "mm",
    "", "", "", "", "", "", "", "", ""
};

static lv_obj_t * s_pscreen = NULL;
static bool s_pbuilt = false;
static pickup_params_t s_params;                 /* 编辑副本（保存才生效） */
static lv_obj_t * s_pvalue_label[P_COUNT];
static lv_obj_t * s_pstatus_label = NULL;
static lv_obj_t * s_pedit_modal = NULL;          /* 编辑弹层（lv_layer_top） */
static lv_obj_t * s_pedit_name_label = NULL;
static lv_obj_t * s_pedit_ta = NULL;
static int8_t s_pedit_idx = -1;
static lv_obj_t * s_params_keyboard = NULL;

static void params_value_text(int idx, char * buf, size_t n)
{
    buf[0] = '\0';
    switch (idx)
    {
        case P_SHELF_COUNT:   (void) snprintf(buf, n, "%u", (unsigned) s_params.shelf_count); break;
        case P_SHELF_Y0:      (void) snprintf(buf, n, "%d", (int) s_params.shelf_y_mm[0]); break;
        case P_SHELF_Y1:      (void) snprintf(buf, n, "%d", (int) s_params.shelf_y_mm[1]); break;
        case P_SHELF_Y2:      (void) snprintf(buf, n, "%d", (int) s_params.shelf_y_mm[2]); break;
        case P_SLOTS_PER_ROW: (void) snprintf(buf, n, "%u", (unsigned) s_params.slots_per_row); break;
        case P_BOX_WIDTH:
            (void) snprintf(buf, n, "%d", (int) s_params.box_width_mm);
            break;
        case P_SLOT_WIDTH:
            /* 0 = 自动规划：药盒宽+10mm 余量（下限夹爪最小间距 35mm） */
            if (s_params.slot_width_mm <= 0.0f)
            {
                float auto_w = s_params.box_width_mm + 10.0f;
                if (auto_w < 35.0f) auto_w = 35.0f;
                float maxw = (s_params.slots_per_row > 0U) ? (396.7f / (float) s_params.slots_per_row) : 396.7f;
                if (auto_w > maxw) auto_w = maxw;
                (void) snprintf(buf, n, "自动(%d)", (int) auto_w);
            }
            else
            {
                (void) snprintf(buf, n, "%d", (int) s_params.slot_width_mm);
            }
            break;
        case P_Z_REACH:       (void) snprintf(buf, n, "%d", (int) s_params.z_reach_mm); break;
        case P_GRIP_PULSES:   (void) snprintf(buf, n, "%ld", (long) s_params.grip_pulses); break;
        /* 夹紧余量：0=刚好贴住、1=轻夹、越大越紧（闭合中间宽度 = 物品宽度 - 余量） */
        case P_GRIP_MARGIN:   (void) snprintf(buf, n, "%0.1f", s_params.grip_margin_mm); break;
        case P_STORE_X:       (void) snprintf(buf, n, "%d", (int) s_params.store_x_mm); break;
        case P_STORE_Y:       (void) snprintf(buf, n, "%d", (int) s_params.store_y_mm); break;
        case P_XY_SPEED:      (void) snprintf(buf, n, "%u", (unsigned) s_params.xy_speed); break;
        case P_XY_ACC:        (void) snprintf(buf, n, "%u", (unsigned) s_params.xy_acc); break;
        case P_Z_SPEED:       (void) snprintf(buf, n, "%u", (unsigned) s_params.z_speed); break;
        case P_Z_ACC:         (void) snprintf(buf, n, "%u", (unsigned) s_params.z_acc); break;
        case P_GRIP_SPEED:    (void) snprintf(buf, n, "%u", (unsigned) s_params.grip_speed); break;
        case P_GRIP_ACC:      (void) snprintf(buf, n, "%u", (unsigned) s_params.grip_acc); break;
        case P_TEST_SHELF:    (void) snprintf(buf, n, "%u", (unsigned) s_params.test_shelf + 1U); break;
        case P_TEST_SLOT:     (void) snprintf(buf, n, "%u", (unsigned) s_params.test_slot + 1U); break;
        case P_AUTO_HOME:     (void) snprintf(buf, n, "%s", s_params.auto_home_on_boot ? "开" : "关"); break;
        default: break;
    }
}

static bool params_parse_set(int idx, const char * text)
{
    if (idx == P_AUTO_HOME)
    {
        s_params.auto_home_on_boot = (uint8_t) (!s_params.auto_home_on_boot);
        return true;
    }
    if ((text == NULL) || (text[0] == '\0'))
    {
        return false;
    }
    switch (idx)
    {
        case P_SHELF_COUNT: { long v = strtol(text, NULL, 10); if ((v < 1L) || (v > 4L)) return false; s_params.shelf_count = (uint8_t) v; return true; }
        /* 层 Y 坐标：不得超过 Y 轴实测最大量程 291.7mm(12500脉冲)，超量程点击会软限位失败/损坏电机 */
        case P_SHELF_Y0:    { float v = strtof(text, NULL); if ((v < 0.0f) || (v > 291.7f)) return false; s_params.shelf_y_mm[0] = v; return true; }
        case P_SHELF_Y1:    { float v = strtof(text, NULL); if ((v < 0.0f) || (v > 291.7f)) return false; s_params.shelf_y_mm[1] = v; return true; }
        case P_SHELF_Y2:    { float v = strtof(text, NULL); if ((v < 0.0f) || (v > 291.7f)) return false; s_params.shelf_y_mm[2] = v; return true; }
        case P_SLOTS_PER_ROW: { long v = strtol(text, NULL, 10); if ((v < 1L) || (v > 8L)) return false; s_params.slots_per_row = (uint8_t) v; return true; }
        /* 药盒宽度（每种药不一样）：10~200mm，格子宽度自动 = 盒宽+10mm 余量 */
        case P_BOX_WIDTH:   { float v = strtof(text, NULL); if ((v < 10.0f) || (v > 200.0f)) return false; s_params.box_width_mm = v; return true; }
        /* 格宽：0=自动(药盒宽+10，下限夹爪最小间距35mm)；非 0 固定值 35mm~396.7/格数 */
        case P_SLOT_WIDTH:
        {
            float v = strtof(text, NULL);
            float maxw = (s_params.slots_per_row > 0U) ? (396.7f / (float) s_params.slots_per_row) : 396.7f;
            if (v == 0.0f) { s_params.slot_width_mm = 0.0f; return true; } /* 自动 */
            if ((v < 35.0f) || (v > maxw)) return false;
            s_params.slot_width_mm = v;
            return true;
        }
        case P_Z_REACH:     { float v = strtof(text, NULL); if ((v < 0.0f) || (v > 151.8f)) return false; s_params.z_reach_mm = v; return true; }
        case P_GRIP_PULSES: { long v = strtol(text, NULL, 10); if ((v < 1L) || (v > 1550L)) return false; s_params.grip_pulses = (int32_t) v; return true; }
        /* 夹紧余量 0~5mm（0=贴住不夹、越大夹得越紧/力度越大） */
        case P_GRIP_MARGIN: { float v = strtof(text, NULL); if ((v < 0.0f) || (v > 5.0f)) return false; s_params.grip_margin_mm = v; return true; }
        case P_STORE_X:     { float v = strtof(text, NULL); if ((v < 0.0f) || (v > 396.7f)) return false; s_params.store_x_mm = v; return true; }
        case P_STORE_Y:     { float v = strtof(text, NULL); if ((v < 0.0f) || (v > 291.7f)) return false; s_params.store_y_mm = v; return true; }
        case P_XY_SPEED:    { long v = strtol(text, NULL, 10); if ((v < 1L) || (v > 60000L)) return false; s_params.xy_speed = (uint16_t) v; return true; }
        case P_XY_ACC:      { long v = strtol(text, NULL, 10); if ((v < 1L) || (v > 255L)) return false; s_params.xy_acc = (uint8_t) v; return true; }
        case P_Z_SPEED:     { long v = strtol(text, NULL, 10); if ((v < 1L) || (v > 60000L)) return false; s_params.z_speed = (uint16_t) v; return true; }
        case P_Z_ACC:       { long v = strtol(text, NULL, 10); if ((v < 1L) || (v > 255L)) return false; s_params.z_acc = (uint8_t) v; return true; }
        case P_GRIP_SPEED:  { long v = strtol(text, NULL, 10); if ((v < 1L) || (v > 60000L)) return false; s_params.grip_speed = (uint16_t) v; return true; }
        case P_GRIP_ACC:    { long v = strtol(text, NULL, 10); if ((v < 1L) || (v > 255L)) return false; s_params.grip_acc = (uint8_t) v; return true; }
        case P_TEST_SHELF:  { long v = strtol(text, NULL, 10); if ((v < 1L) || (v > (long) s_params.shelf_count)) return false; s_params.test_shelf = (uint8_t) (v - 1L); return true; }
        case P_TEST_SLOT:   { long v = strtol(text, NULL, 10); if ((v < 1L) || (v > (long) s_params.slots_per_row)) return false; s_params.test_slot = (uint8_t) (v - 1L); return true; }
        default: return false;
    }
}

static void params_refresh_rows(void)
{
    if (!s_pbuilt || (s_pscreen == NULL) || !lv_obj_is_valid(s_pscreen))
    {
        return;
    }
    char buf[24];
    for (int i = 0; i < P_COUNT; i++)
    {
        if (s_pvalue_label[i] == NULL)
        {
            continue;
        }
        params_value_text(i, buf, sizeof(buf));
        gui_custom_label_set_if_changed(s_pvalue_label[i], buf);
    }
}

static void params_close_edit(void)
{
    s_pedit_idx = -1;
    if (s_pedit_modal != NULL)
    {
        lv_obj_add_flag(s_pedit_modal, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_params_keyboard != NULL)
    {
        lv_keyboard_set_textarea(s_params_keyboard, NULL);
        lv_obj_add_flag(s_params_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

static void params_edit_confirm(void)
{
    if (s_pedit_idx < 0)
    {
        params_close_edit();
        return;
    }
    const char * txt = (s_pedit_ta != NULL) ? lv_textarea_get_text(s_pedit_ta) : "";
    if (params_parse_set(s_pedit_idx, txt))
    {
        params_refresh_rows();
        if (s_pstatus_label != NULL)
        {
            gui_custom_label_set_if_changed(s_pstatus_label, "已修改（点保存生效）");
        }
    }
    else if (s_pstatus_label != NULL)
    {
        gui_custom_label_set_if_changed(s_pstatus_label, "输入无效，未修改");
    }
    params_close_edit();
}

static void params_kb_event_hook(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY)
    {
        params_edit_confirm();
    }
    else if (code == LV_EVENT_CANCEL)
    {
        params_close_edit();
    }
}

static void params_cancel_hook(lv_event_t * e)
{
    (void) e;
    params_close_edit();
}

static void params_confirm_hook(lv_event_t * e)
{
    (void) e;
    params_edit_confirm();
}

static void params_open_edit(int idx)
{
    if ((idx < 0) || (idx >= P_COUNT))
    {
        return;
    }
    /* 开关类：直接切换，不弹键盘 */
    if (idx == P_AUTO_HOME)
    {
        params_parse_set(idx, NULL);
        params_refresh_rows();
        if (s_pstatus_label != NULL)
        {
            gui_custom_label_set_if_changed(s_pstatus_label, "已修改（点保存生效）");
        }
        return;
    }

    s_pedit_idx = (int8_t) idx;

    if (s_pedit_modal == NULL)
    {
        s_pedit_modal = lv_obj_create(lv_layer_top());
        lv_obj_set_size(s_pedit_modal, 620, 176);
        lv_obj_set_pos(s_pedit_modal, 10, 12);
        lv_obj_set_style_bg_color(s_pedit_modal, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(s_pedit_modal, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_radius(s_pedit_modal, 14, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(s_pedit_modal, lv_color_hex(0x7157d9), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(s_pedit_modal, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_add_flag(s_pedit_modal, LV_OBJ_FLAG_HIDDEN);

        s_pedit_name_label = lv_label_create(s_pedit_modal);
        lv_obj_set_pos(s_pedit_name_label, 20, 12);
        lv_obj_set_style_text_font(s_pedit_name_label, (g_tiny_font != NULL) ? g_tiny_font : &lv_font_SourceHanSerifSC_18,
                                   LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(s_pedit_name_label, lv_color_hex(0x10233f), LV_PART_MAIN | LV_STATE_DEFAULT);

        s_pedit_ta = lv_textarea_create(s_pedit_modal);
        lv_obj_set_size(s_pedit_ta, 560, 48);
        lv_obj_set_pos(s_pedit_ta, 30, 48);
        lv_textarea_set_one_line(s_pedit_ta, true);
        lv_textarea_set_max_length(s_pedit_ta, 12U);
        lv_textarea_set_placeholder_text(s_pedit_ta, "输入数值");
        lv_obj_set_style_text_font(s_pedit_ta, (g_tiny_font != NULL) ? g_tiny_font : &lv_font_SourceHanSerifSC_18,
                                   LV_PART_MAIN | LV_STATE_DEFAULT);

        lv_obj_t * btn_ok = lv_button_create(s_pedit_modal);
        lv_obj_set_size(btn_ok, 110, 44);
        lv_obj_set_pos(btn_ok, 360, 116);
        lv_obj_set_style_bg_color(btn_ok, lv_color_hex(0x14a66a), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(btn_ok, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_radius(btn_ok, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(btn_ok, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_add_event_cb(btn_ok, params_confirm_hook, LV_EVENT_CLICKED, NULL);
        lv_obj_t * t_ok = lv_label_create(btn_ok);
        lv_label_set_text(t_ok, "确定");
        lv_obj_set_style_text_font(t_ok, (g_tiny_font != NULL) ? g_tiny_font : &lv_font_SourceHanSerifSC_18,
                                   LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(t_ok, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_center(t_ok);

        lv_obj_t * btn_cancel = lv_button_create(s_pedit_modal);
        lv_obj_set_size(btn_cancel, 110, 44);
        lv_obj_set_pos(btn_cancel, 480, 116);
        lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(0xe5e7f2), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(btn_cancel, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_radius(btn_cancel, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(btn_cancel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_add_event_cb(btn_cancel, params_cancel_hook, LV_EVENT_CLICKED, NULL);
        lv_obj_t * t_cancel = lv_label_create(btn_cancel);
        lv_label_set_text(t_cancel, "取消");
        lv_obj_set_style_text_font(t_cancel, (g_tiny_font != NULL) ? g_tiny_font : &lv_font_SourceHanSerifSC_18,
                                   LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(t_cancel, lv_color_hex(0x687b99), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_center(t_cancel);
    }

    if (s_pedit_name_label != NULL)
    {
        char buf[48];
        (void) snprintf(buf, sizeof(buf), "%s (%s)", s_pnames[idx], s_punits[idx]);
        gui_custom_label_set_if_changed(s_pedit_name_label, buf);
    }
    char val[24];
    params_value_text(idx, val, sizeof(val));
    if (s_pedit_ta != NULL)
    {
        lv_textarea_set_text(s_pedit_ta, val);
    }
    lv_obj_remove_flag(s_pedit_modal, LV_OBJ_FLAG_HIDDEN);

    if (s_params_keyboard == NULL)
    {
        s_params_keyboard = lv_keyboard_create(lv_layer_top());
        lv_obj_set_size(s_params_keyboard, 640, 200);
        lv_obj_set_align(s_params_keyboard, LV_ALIGN_BOTTOM_MID);
        lv_obj_add_flag(s_params_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_keyboard_set_mode(s_params_keyboard, LV_KEYBOARD_MODE_NUMBER);
        lv_obj_add_event_cb(s_params_keyboard, params_kb_event_hook, LV_EVENT_READY, NULL);
        lv_obj_add_event_cb(s_params_keyboard, params_kb_event_hook, LV_EVENT_CANCEL, NULL);
    }
    lv_keyboard_set_textarea(s_params_keyboard, s_pedit_ta);
    lv_obj_remove_flag(s_params_keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void params_edit_hook(lv_event_t * e)
{
    lv_obj_t * btn = lv_event_get_target_obj(e);
    params_open_edit((int) (intptr_t) lv_obj_get_user_data(btn));
}

static void params_save_hook(lv_event_t * e)
{
    (void) e;
    fsp_err_t err = PickupParams_Save(&s_params);
    if (FSP_SUCCESS == err)
    {
        (void) PickupParams_ApplyToGantry();
        if (s_pstatus_label != NULL)
        {
            gui_custom_label_set_if_changed(s_pstatus_label, "已保存到Flash，速度已生效");
        }
    }
    else
    {
        char buf[48];
        (void) snprintf(buf, sizeof(buf), "保存失败: %d", (int) err);
        if (s_pstatus_label != NULL)
        {
            gui_custom_label_set_if_changed(s_pstatus_label, buf);
        }
    }
}

static void params_default_hook(lv_event_t * e)
{
    (void) e;
    PickupParams_LoadDefault(&s_params);
    params_refresh_rows();
    if (s_pstatus_label != NULL)
    {
        gui_custom_label_set_if_changed(s_pstatus_label, "已恢复默认（点保存生效）");
    }
}

static void params_back_hook(lv_event_t * e)
{
    (void) e;
    params_close_edit();
    if ((s_ui != NULL) && (s_ui->Device.screen != NULL) && lv_obj_is_valid(s_ui->Device.screen))
    {
        lv_screen_load_anim(s_ui->Device.screen, LV_SCREEN_LOAD_ANIM_FADE_IN, 180, 0, false);
    }
}

static void params_build_page(void)
{
    if (s_pbuilt)
    {
        return;
    }
    s_pscreen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_pscreen, lv_color_hex(0xf6f7ff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_pscreen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    const lv_font_t * font = (g_tiny_font != NULL) ? g_tiny_font : &lv_font_SourceHanSerifSC_18;

    /* 顶栏 */
    lv_obj_t * header = lv_obj_create(s_pscreen);
    lv_obj_set_size(header, 640, 56);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(header, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(header, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(header, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t * btn_back = lv_button_create(header);
    lv_obj_set_size(btn_back, 122, 36);
    lv_obj_set_pos(btn_back, 16, 10);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0xf0edff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn_back, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn_back, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn_back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_back, params_back_hook, LV_EVENT_CLICKED, NULL);
    lv_obj_t * lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, "<  返回");
    lv_obj_set_style_text_font(lbl_back, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(lbl_back, lv_color_hex(0x7157d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(lbl_back);

    lv_obj_t * lbl_title = lv_label_create(header);
    lv_label_set_text(lbl_title, "参数设置");
    lv_obj_set_style_text_font(lbl_title, (g_tiny_font != NULL) ? g_tiny_font : &lv_font_SourceHanSerifSC_22,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0x10233f), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, 0);

    /* 状态行 */
    s_pstatus_label = lv_label_create(s_pscreen);
    lv_label_set_text(s_pstatus_label, "默认=之前的药柜设置");
    lv_obj_set_style_text_font(s_pstatus_label, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(s_pstatus_label, lv_color_hex(0x14a66a), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_long_mode(s_pstatus_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(s_pstatus_label, 20, 384);
    lv_obj_set_size(s_pstatus_label, 600, 36);

    /* 参数滚动列表 */
    lv_obj_t * scroll = lv_obj_create(s_pscreen);
    lv_obj_set_size(scroll, 620, 314);
    lv_obj_set_pos(scroll, 10, 62);
    lv_obj_set_style_bg_color(scroll, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(scroll, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(scroll, lv_color_hex(0xd8e1ef), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(scroll, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(scroll, 14, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_scroll_dir(scroll, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(scroll, LV_SCROLLBAR_MODE_AUTO);

    for (int i = 0; i < P_COUNT; i++)
    {
        lv_obj_t * row = lv_obj_create(scroll);
        lv_obj_set_size(row, 600, 34);
        lv_obj_set_pos(row, 0, i * 34);
        lv_obj_set_style_bg_color(row, lv_color_hex(0xfafbff), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(row, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_radius(row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

        lv_obj_t * name = lv_label_create(row);
        lv_label_set_text(name, s_pnames[i]);
        lv_obj_set_style_text_font(name, font, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(name, lv_color_hex(0x10233f), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_pos(name, 14, 8);

        lv_obj_t * unit = lv_label_create(row);
        lv_label_set_text(unit, s_punits[i]);
        lv_obj_set_style_text_font(unit, font, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(unit, lv_color_hex(0x9aa8bf), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_pos(unit, 216, 8);

        s_pvalue_label[i] = lv_label_create(row);
        char buf[24];
        params_value_text(i, buf, sizeof(buf));
        lv_label_set_text(s_pvalue_label[i], buf);
        lv_obj_set_style_text_font(s_pvalue_label[i], font, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(s_pvalue_label[i], lv_color_hex(0x0891b2), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_pos(s_pvalue_label[i], 300, 8);
        lv_obj_set_width(s_pvalue_label[i], 110);
        lv_obj_set_style_text_align(s_pvalue_label[i], LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);

        lv_obj_t * btn_edit = lv_button_create(row);
        lv_obj_set_size(btn_edit, 64, 28);
        lv_obj_set_pos(btn_edit, 522, 3);
        lv_obj_set_style_bg_color(btn_edit, lv_color_hex(0xe5f7fb), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(btn_edit, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_radius(btn_edit, 8, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(btn_edit, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_user_data(btn_edit, (void *) (intptr_t) i);
        lv_obj_add_event_cb(btn_edit, params_edit_hook, LV_EVENT_CLICKED, NULL);
        lv_obj_t * t_edit = lv_label_create(btn_edit);
        lv_label_set_text(t_edit, "改");
        lv_obj_set_style_text_font(t_edit, font, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(t_edit, lv_color_hex(0x0891b2), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_center(t_edit);
    }

    /* 底部按钮：保存 / 恢复默认 */
    lv_obj_t * btn_save = lv_button_create(s_pscreen);
    lv_obj_set_size(btn_save, 168, 44);
    lv_obj_set_pos(btn_save, 110, 424);
    lv_obj_set_style_bg_color(btn_save, lv_color_hex(0x14a66a), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn_save, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn_save, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn_save, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_save, params_save_hook, LV_EVENT_CLICKED, NULL);
    lv_obj_t * t_save = lv_label_create(btn_save);
    lv_label_set_text(t_save, "保存");
    lv_obj_set_style_text_font(t_save, (g_tiny_font != NULL) ? g_tiny_font : &lv_font_SourceHanSerifSC_18,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(t_save, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(t_save);

    lv_obj_t * btn_default = lv_button_create(s_pscreen);
    lv_obj_set_size(btn_default, 168, 44);
    lv_obj_set_pos(btn_default, 320, 424);
    lv_obj_set_style_bg_color(btn_default, lv_color_hex(0xfff4dc), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn_default, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn_default, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn_default, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_default, params_default_hook, LV_EVENT_CLICKED, NULL);
    lv_obj_t * t_default = lv_label_create(btn_default);
    lv_label_set_text(t_default, "恢复默认");
    lv_obj_set_style_text_font(t_default, (g_tiny_font != NULL) ? g_tiny_font : &lv_font_SourceHanSerifSC_18,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(t_default, lv_color_hex(0xf59e0b), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(t_default);

    s_pbuilt = true;
}

/* Device 页入口按钮："参数设置"（电机调试按钮旁） */
static void device_params_nav_hook(lv_event_t * e)
{
    (void) e;
    s_params = *PickupParams_Get();   /* 每次进入从生效参数拷贝 */
    params_build_page();
    if (s_pscreen != NULL)
    {
        lv_screen_load_anim(s_pscreen, LV_SCREEN_LOAD_ANIM_FADE_IN, 180, 0, false);
    }
}

/* ============================================================================
 * ESP-01S 无线调试页（手写 LVGL，不进 GUI Guider 结构）
 * 配置"透传主机地址"（TCP 透传目标）：输入 主机:端口 → 读取当前 / 保存并应用。
 * AT 序列（+++ 退出透传 → AT+SAVETRANSLINK → AT+RST 重启）由 esp01s_cfg 在
 * Network 线程执行，本页只发起请求并轮询状态（约 5~15 秒完成）。
 * ==========================================================================*/
static lv_obj_t * s_wlan_screen = NULL;
static bool s_wlan_built = false;
static bool s_device_wlan_entry = false;       /* Device 页按钮已挂事件 */
static lv_obj_t * s_wlan_ta = NULL;            /* 主机:端口 输入框 */
static lv_obj_t * s_wlan_kb = NULL;            /* 软键盘 */
static lv_obj_t * s_wlan_status_label = NULL;  /* 状态行 */
static lv_obj_t * s_wlan_current_label = NULL; /* 当前配置行 */
static lv_obj_t * s_wlan_conn_label = NULL;    /* 云端连接状态行（三态） */

static void wlan_close_keyboard(void)
{
    if (s_wlan_kb != NULL)
    {
        lv_keyboard_set_textarea(s_wlan_kb, NULL);
        lv_obj_add_flag(s_wlan_kb, LV_OBJ_FLAG_HIDDEN);
    }
}

static void wlan_kb_event_hook(lv_event_t * e)
{
    /* ✓（LV_EVENT_READY）/ ✗（LV_EVENT_CANCEL）→ 收起软键盘 */
    (void) e;
    wlan_close_keyboard();
}

static void wlan_ta_click_hook(lv_event_t * e)
{
    (void) e;
    if ((s_wlan_ta == NULL) || (s_wlan_kb == NULL))
    {
        return;
    }
    lv_keyboard_set_textarea(s_wlan_kb, s_wlan_ta);
    /* 小写键盘含 . 与 :（第三行），"1#" 键切数字/符号页，满足 主机:端口 输入 */
    lv_keyboard_set_mode(s_wlan_kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_remove_flag(s_wlan_kb, LV_OBJ_FLAG_HIDDEN);
}

static void wlan_back_hook(lv_event_t * e)
{
    (void) e;
    wlan_close_keyboard();
    if ((s_ui != NULL) && (s_ui->Device.screen != NULL) && lv_obj_is_valid(s_ui->Device.screen))
    {
        lv_screen_load_anim(s_ui->Device.screen, LV_SCREEN_LOAD_ANIM_FADE_IN, 180, 0, false);
    }
}

/* 解析 "主机:端口"（IPv4：取最后一个 ':' 分隔；端口 1~65535） */
static bool wlan_parse_host_port(const char * p_text, char * p_host, size_t host_n, uint16_t * p_port)
{
    if ((NULL == p_text) || (NULL == p_host) || (NULL == p_port) || (host_n == 0U))
    {
        return false;
    }
    const char * p_colon = strrchr(p_text, ':');
    if (NULL == p_colon)
    {
        return false;
    }
    size_t const n = (size_t) (p_colon - p_text);
    if ((n == 0U) || (n >= host_n))
    {
        return false;
    }
    long const v = strtol(p_colon + 1, NULL, 10);
    if ((v <= 0L) || (v > 65535L))
    {
        return false;
    }
    memcpy(p_host, p_text, n);
    p_host[n] = '\0';
    *p_port = (uint16_t) v;
    return true;
}

static void wlan_read_hook(lv_event_t * e)
{
    (void) e;
    if (s_wlan_status_label == NULL)
    {
        return;
    }
    if (!esp01s_cfg_read())
    {
        gui_custom_label_set_if_changed(s_wlan_status_label, "正在执行上一步，请稍候");
        return;
    }
    gui_custom_label_set_if_changed(s_wlan_status_label, "正在读取模块配置...");
}

static void wlan_save_hook(lv_event_t * e)
{
    (void) e;
    wlan_close_keyboard();
    if ((s_wlan_ta == NULL) || (s_wlan_status_label == NULL))
    {
        return;
    }
    const char * p_text = lv_textarea_get_text(s_wlan_ta);
    char host[64];
    uint16_t port = 0U;
    if (!wlan_parse_host_port(p_text, host, sizeof(host), &port))
    {
        gui_custom_label_set_if_changed(s_wlan_status_label,
                                        "格式错误：请输入 主机:端口（如 192.168.1.100:8080）");
        return;
    }
    if (!esp01s_cfg_set(host, port))
    {
        gui_custom_label_set_if_changed(s_wlan_status_label, "正在执行上一步，请稍候");
        return;
    }
    gui_custom_label_set_if_changed(s_wlan_status_label, "正在保存并重启模块（约 5~15 秒）...");
}

static void wlan_build_page(void)
{
    if (s_wlan_built)
    {
        return;
    }
    s_wlan_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_wlan_screen, lv_color_hex(0xf6f7ff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_wlan_screen, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    const lv_font_t * font = (g_tiny_font != NULL) ? g_tiny_font : &lv_font_SourceHanSerifSC_18;

    /* 顶栏：返回 + 标题 */
    lv_obj_t * header = lv_obj_create(s_wlan_screen);
    lv_obj_set_size(header, 640, 56);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_bg_color(header, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(header, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(header, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(header, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t * btn_back = lv_button_create(header);
    lv_obj_set_size(btn_back, 122, 36);
    lv_obj_set_pos(btn_back, 16, 10);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(0xf0edff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn_back, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn_back, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn_back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_back, wlan_back_hook, LV_EVENT_CLICKED, NULL);
    lv_obj_t * lbl_back = lv_label_create(btn_back);
    lv_label_set_text(lbl_back, "<  返回");
    lv_obj_set_style_text_font(lbl_back, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(lbl_back, lv_color_hex(0x7157d9), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(lbl_back);

    lv_obj_t * lbl_title = lv_label_create(header);
    lv_label_set_text(lbl_title, "无线调试");
    lv_obj_set_style_text_font(lbl_title, (g_tiny_font != NULL) ? g_tiny_font : &lv_font_SourceHanSerifSC_22,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(0x10233f), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, 0);

    /* 配置卡片 */
    lv_obj_t * card = lv_obj_create(s_wlan_screen);
    lv_obj_set_size(card, 600, 186);
    lv_obj_set_pos(card, 20, 66);
    lv_obj_set_style_bg_color(card, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(card, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(card, lv_color_hex(0xd8e1ef), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(card, 14, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t * lbl_card_title = lv_label_create(card);
    lv_label_set_text(lbl_card_title, "ESP-01S 透传主机");
    lv_obj_set_style_text_font(lbl_card_title, (g_tiny_font != NULL) ? g_tiny_font : &lv_font_SourceHanSerifSC_22,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(lbl_card_title, lv_color_hex(0x10233f), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(lbl_card_title, 20, 12);

    lv_obj_t * lbl_field = lv_label_create(card);
    lv_label_set_text(lbl_field, "主机:端口");
    lv_obj_set_style_text_font(lbl_field, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(lbl_field, lv_color_hex(0x10233f), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(lbl_field, 20, 56);

    s_wlan_ta = lv_textarea_create(card);
    lv_obj_set_size(s_wlan_ta, 380, 40);
    lv_obj_set_pos(s_wlan_ta, 116, 48);
    lv_textarea_set_one_line(s_wlan_ta, true);
    lv_textarea_set_max_length(s_wlan_ta, ESP01S_CFG_HOST_MAX);
    lv_textarea_set_placeholder_text(s_wlan_ta, "192.168.1.100:8080");
    lv_obj_set_style_text_font(s_wlan_ta, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(s_wlan_ta, lv_color_hex(0x10233f), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(s_wlan_ta, lv_color_hex(0x9aa8c4), LV_PART_TEXTAREA_PLACEHOLDER | LV_STATE_DEFAULT);
    lv_obj_add_flag(s_wlan_ta, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_wlan_ta, wlan_ta_click_hook, LV_EVENT_CLICKED, NULL);

    lv_obj_t * lbl_hint = lv_label_create(card);
    lv_label_set_text(lbl_hint, "格式：192.168.1.100:8080　保存后模块重启并自动透传连接");
    lv_obj_set_style_text_font(lbl_hint, (g_tiny_font != NULL) ? g_tiny_font : &lv_font_SourceHanSerifSC_15,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(lbl_hint, lv_color_hex(0x9aa8bf), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(lbl_hint, 20, 98);

    s_wlan_current_label = lv_label_create(card);
    lv_label_set_text(s_wlan_current_label, "当前配置: 未读取");
    lv_obj_set_style_text_font(s_wlan_current_label, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(s_wlan_current_label, lv_color_hex(0x0891b2), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(s_wlan_current_label, 20, 128);

    s_wlan_status_label = lv_label_create(card);
    lv_label_set_text(s_wlan_status_label, "就绪");
    lv_obj_set_style_text_font(s_wlan_status_label, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(s_wlan_status_label, lv_color_hex(0x14a66a), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_long_mode(s_wlan_status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(s_wlan_status_label, 20, 156);
    lv_obj_set_size(s_wlan_status_label, 560, 30);

    /* 云端连接状态行（手册 §5 三态：已连接/连接中/离线）。
     * 卡片底部 (66+186=252) 与按钮 (280) 之间的间隙放置。 */
    s_wlan_conn_label = lv_label_create(s_wlan_screen);
    lv_label_set_text(s_wlan_conn_label, "云端连接: 连接中");
    lv_obj_set_style_text_font(s_wlan_conn_label, (g_tiny_font != NULL) ? g_tiny_font : &lv_font_SourceHanSerifSC_18,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(s_wlan_conn_label, lv_color_hex(0xf59e0b), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_pos(s_wlan_conn_label, 20, 258);
    lv_obj_set_size(s_wlan_conn_label, 600, 18);

    /* 底部按钮：读取当前 / 保存并应用 */
    lv_obj_t * btn_read = lv_button_create(s_wlan_screen);
    lv_obj_set_size(btn_read, 150, 42);
    lv_obj_set_pos(btn_read, 150, 280);
    lv_obj_set_style_bg_color(btn_read, lv_color_hex(0xe5f7fb), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn_read, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn_read, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn_read, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_read, wlan_read_hook, LV_EVENT_CLICKED, NULL);
    lv_obj_t * t_read = lv_label_create(btn_read);
    lv_label_set_text(t_read, "读取当前");
    lv_obj_set_style_text_font(t_read, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(t_read, lv_color_hex(0x0891b2), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(t_read);

    lv_obj_t * btn_save = lv_button_create(s_wlan_screen);
    lv_obj_set_size(btn_save, 160, 42);
    lv_obj_set_pos(btn_save, 330, 280);
    lv_obj_set_style_bg_color(btn_save, lv_color_hex(0x14a66a), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(btn_save, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn_save, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn_save, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_save, wlan_save_hook, LV_EVENT_CLICKED, NULL);
    lv_obj_t * t_save = lv_label_create(btn_save);
    lv_label_set_text(t_save, "保存并应用");
    lv_obj_set_style_text_font(t_save, font, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(t_save, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(t_save);

    /* 软键盘：lv_layer_top 上创建一次，初始隐藏 */
    s_wlan_kb = lv_keyboard_create(lv_layer_top());
    lv_obj_set_size(s_wlan_kb, 640, 200);
    lv_obj_set_align(s_wlan_kb, LV_ALIGN_BOTTOM_MID);
    lv_obj_add_flag(s_wlan_kb, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_mode(s_wlan_kb, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_add_event_cb(s_wlan_kb, wlan_kb_event_hook, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_wlan_kb, wlan_kb_event_hook, LV_EVENT_CANCEL, NULL);

    s_wlan_built = true;
}

static void refresh_wireless_debug_page(void)
{
    if (!s_wlan_built || (s_wlan_screen == NULL) || !lv_obj_is_valid(s_wlan_screen))
    {
        return;
    }
    esp01s_cfg_status_t st;
    esp01s_cfg_get_status(&st);

    /* 云端连接状态行（三态；与 Home/Admin 徽章、Device 行同源） */
    if (s_wlan_conn_label != NULL)
    {
        gui_custom_label_set_if_changed(s_wlan_conn_label,
                                        (s_esp01s_state == ESP01S_UI_ONLINE) ? "云端连接: 已连接" :
                                        (s_esp01s_state == ESP01S_UI_CONNECTING) ? "云端连接: 连接中" :
                                        "云端连接: 离线");
        lv_obj_set_style_text_color(s_wlan_conn_label,
                                    (s_esp01s_state == ESP01S_UI_ONLINE) ? lv_color_hex(0x14a66a) :
                                    (s_esp01s_state == ESP01S_UI_CONNECTING) ? lv_color_hex(0xf59e0b) :
                                    lv_color_hex(0xef5350),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    /* 当前配置行 */
    if (s_wlan_current_label != NULL)
    {
        char buf[80];
        if (st.has_cfg && (st.host[0] != '\0'))
        {
            (void) snprintf(buf, sizeof(buf), "当前配置: %s:%u", st.host, (unsigned) st.port);
        }
        else
        {
            (void) snprintf(buf, sizeof(buf), "当前配置: 未读取");
        }
        gui_custom_label_set_if_changed(s_wlan_current_label, buf);
    }

    if (s_wlan_status_label == NULL)
    {
        return;
    }
    /* 状态行：状态或文案变化才刷新（500ms 定时器驱动） */
    static int s_last_state = -1;
    static char s_last_detail[sizeof(st.detail)];
    if ((st.state != (esp01s_cfg_state_t) s_last_state) ||
        (0 != strcmp(st.detail, s_last_detail)))
    {
        s_last_state = (int) st.state;
        (void) snprintf(s_last_detail, sizeof(s_last_detail), "%s", st.detail);
        switch (st.state)
        {
            case ESP01S_CFG_BUSY:
                gui_custom_label_set_if_changed(s_wlan_status_label,
                                                "正在执行配置（模块重启约 5~15 秒）...");
                lv_obj_set_style_text_color(s_wlan_status_label, lv_color_hex(0xf59e0b),
                                            LV_PART_MAIN | LV_STATE_DEFAULT);
                break;
            case ESP01S_CFG_OK:
                gui_custom_label_set_if_changed(s_wlan_status_label, st.detail);
                lv_obj_set_style_text_color(s_wlan_status_label, lv_color_hex(0x14a66a),
                                            LV_PART_MAIN | LV_STATE_DEFAULT);
                break;
            case ESP01S_CFG_ERR:
                gui_custom_label_set_if_changed(s_wlan_status_label, st.detail);
                lv_obj_set_style_text_color(s_wlan_status_label, lv_color_hex(0xef5350),
                                            LV_PART_MAIN | LV_STATE_DEFAULT);
                break;
            default:
                gui_custom_label_set_if_changed(s_wlan_status_label, "就绪");
                lv_obj_set_style_text_color(s_wlan_status_label, lv_color_hex(0x14a66a),
                                            LV_PART_MAIN | LV_STATE_DEFAULT);
                break;
        }
    }
}

/* Device 页入口按钮："无线调试"（原"运行自检"按钮，位置 (236,416)） */
static void device_wlan_nav_hook(lv_event_t * e)
{
    (void) e;
    wlan_build_page();
    if (s_wlan_screen != NULL)
    {
        lv_screen_load_anim(s_wlan_screen, LV_SCREEN_LOAD_ANIM_FADE_IN, 180, 0, false);
    }
}

static void install_device_wireless_entry(gg_ui_t * ui)
{
    if (s_device_wlan_entry || (ui == NULL) || (ui->Device.screen == NULL) ||
        !lv_obj_is_valid(ui->Device.screen))
    {
        return;
    }
    lv_obj_t * btn = ui->Device.button_self_test;
    if ((btn == NULL) || !lv_obj_is_valid(btn))
    {
        return;
    }
    /* 原"运行自检"按钮 → "无线调试"：显式设置文案+字体（生成文件文本已同步，
     * 这里再兜底一次，优先 tiny 字体保证字形完整） */
    lv_obj_t * lbl = ui->Device.button_self_test_button_self_test_text;
    if ((lbl != NULL) && lv_obj_is_valid(lbl))
    {
        lv_label_set_text(lbl, "无线调试");
        lv_obj_set_style_text_font(lbl, (g_tiny_font != NULL) ? g_tiny_font : &lv_font_SourceHanSerifSC_18,
                                   LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    lv_obj_add_event_cb(btn, device_wlan_nav_hook, LV_EVENT_CLICKED, NULL);
    s_device_wlan_entry = true;
}

void custom_init(gg_ui_t *ui)
{
    /* The editable GUI Guider screens are now the single UI source of truth. */
    s_ui = ui;
    gui_set_esp01s_state(ui, ESP01S_UI_CONNECTING);   /* 上电初始：正在建连 */
    gui_set_arm_coordinates(ui, 0, 0, 0);
    configure_ui_label_layout(ui);
    boot_build_images(ui);

    if (s_network_badge_timer == NULL)
    {
        /* Home/Admin are created lazily, so reapply the latest link state after page creation. */
        s_network_badge_timer = lv_timer_create(network_badge_timer_cb, 500, s_ui);
    }
}

void slider_adjust_img_cb(lv_obj_t * img, int32_t brightValue, int16_t hueValue)
{
    static lv_color_t recolor;

    recolor = lv_color_hsv_to_rgb(hueValue, 100, brightValue);

    lv_obj_set_style_image_recolor(img, recolor, LV_PART_MAIN|LV_STATE_DEFAULT);
    lv_obj_set_style_image_recolor_opa(img, 50, LV_PART_MAIN|LV_STATE_DEFAULT);
}

void label_progress_cb(void * var, int32_t v)
{
    lv_label_set_text_fmt((lv_obj_t *) var, "%d", (int)v);
}
