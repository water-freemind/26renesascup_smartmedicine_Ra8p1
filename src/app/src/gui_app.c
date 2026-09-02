#include "gui_app.h"

#if defined(GUI_GUIDER_AVAILABLE)

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "custom.h"
#include "gg_utils.h"
#include "gui_guider.h"
#include "camera_app.h"
#include "camera_preview.h"
#include "qr_decoder.h"
#include "qr_selftest.h"       /* 二维码板端自检（嵌入已知 QR 验证 quirc） */
#include "sys_log.h"
#include "drug_db.h"
#include "inventory.h"
#include "cst816s_touch.h"
#include "ospi_storage.h"
#include "ospi_ttf_loader.h"   /* 烧录模式：烧录期间跳过 tiny_ttf，防残缺字体渲染崩溃 */
#include "pickup_log.h"        /* 取药记录（与系统日志分离的独立环形缓冲） */
#include "pickup_test.h"       /* 取药单多药取药流程状态机 */
#include "pickup_params.h"     /* 层Y坐标（layer → ShelfY，手册 §6 机械定位） */
#include "esp01s_proto.h"      /* 云端协议：取药单识别后按 drugId 上报 PICKUP_SCANNED */
#include "lvgl.h"          /* LV_USE_TINY_TTF 使能时导出 lv_tiny_ttf_* API */

/* 设备自检/状态刷新用（LVGL 线程入口定义） */
extern volatile bool s_lvgl_ready;

/* GUI Guider declares this instance in gui_guider.h but leaves ownership to
 * the platform entry point.  Keep it in the RA8P1 application layer instead
 * of editing an auto-generated source file. */
gg_ui_t guider_ui;
static lv_obj_t * s_pickup_preview;
static lv_obj_t * s_scan_preview;
static bool       s_capture_requested;
static uint32_t   s_last_fps_refresh_ms;

/* ============================================================================
 * tiny_ttf 运行时字体（外部 Flash 动态渲染）：
 * TTF 已烧录在 OSPI 偏移 0（内存映射 0x80000000），零拷贝直接作为字体数据源。
 * 与编译期字库互补：可渲染任意 GB2312 字符，不再受内部 flash 1MB 限制。
 * ==========================================================================*/
#define TINY_TTF_SIZE  (9745792U)   /* 黑体_simhei.ttf 烧入 OSPI 的大小（GB2312 全覆盖粗体） */
lv_font_t * g_tiny_font = NULL;     /* 非 static：供自检/其他模块引用（20px 别名） */
/* 多字号 tiny 字体：界面各字号，全面替换编译期字库（编译字库仅 164 中文且缺失，
 * tiny_ttf 黑体含完整 GB2312 6763 汉字，笔画粗于仿宋） */
lv_font_t * g_tiny_fonts[6] = { NULL, NULL, NULL, NULL, NULL, NULL };
static const int s_tiny_sizes[6] = { 17, 19, 20, 24, 26, 30 };

/* LVGL 堆监控（J-Link 直读，诊断内存泄漏/碎片化导致 HardFault） */
volatile uint32_t s_lvgl_mem_free = 0U;
volatile uint32_t s_lvgl_mem_big  = 0U;
volatile uint32_t s_lvgl_mem_frag = 0U;

/* 节流：每 500 ms 刷新一次 FPS 标签，避免每个 LVGL tick 都重建字符串 */
#define FPS_REFRESH_PERIOD_MS   (500U)

/* LVGL 9.3 的 lv_label_set_text() 对相同文本不做短路：每次都 malloc+strcpy
 * +invalidate。周期刷新（2Hz 节流后）仍会持续分配/释放并触发重绘，长时间
 * 运行会碎片化 LVGL 内存池。以下辅助函数仅在文本真正变化时才设置。 */
static void gui_app_label_set_if_changed(lv_obj_t * p_label, const char * p_text)
{
    if ((NULL == p_label) || !lv_obj_is_valid(p_label))
    {
        return;
    }
    const char * p_cur = lv_label_get_text(p_label);
    if ((NULL != p_cur) && (NULL != p_text) && (0 == strcmp(p_cur, p_text)))
    {
        return;
    }
    lv_label_set_text(p_label, p_text);
}

static void gui_app_label_set_fmt_if_changed(lv_obj_t * p_label, const char * p_fmt, ...)
{
    if ((NULL == p_label) || !lv_obj_is_valid(p_label))
    {
        return;
    }

    char buf[96];
    va_list args;
    va_start(args, p_fmt);
    (void) vsnprintf(buf, sizeof(buf), p_fmt, args);
    va_end(args);
    gui_app_label_set_if_changed(p_label, buf);
}

/* 页面状态/文本刷新节流闸门（每函数独立计数）。
 * 关键：gui_app_poll 每 5ms 运行一次，若各 refresh 函数无条件
 * lv_label_set_text()，Pickup/Scan 激活时每 5ms 触发十几个 label 重绘，
 * LVGL 渲染线程被占满 → 触摸读取饿死（无法触摸）+ 摄像头预览 invalidate
 * 被排挤（预览不显示）。统一节流后状态刷新降为 2Hz，负载可忽略。 */
static bool gui_app_status_throttle(uint32_t * p_last_ms)
{
    uint32_t const now_ms = lv_tick_get();
    if ((now_ms - *p_last_ms) < FPS_REFRESH_PERIOD_MS)
    {
        return false;
    }
    *p_last_ms = now_ms;
    return true;
}

/***********************************************************************************************************************
 * 摄像头帧率标签刷新（Scan 识别页 + Device 管理页）
 *  - 采集激活：显示实时实测帧率（如 "9.9 FPS"）；
 *  - 采集停止：Scan 页显示 "待机"，Device 页保留最近一次实测值（"上次 9.9 FPS"）。
 **********************************************************************************************************************/
static void gui_app_refresh_camera_fps(void)
{
    uint32_t const now_ms = lv_tick_get();
    if ((now_ms - s_last_fps_refresh_ms) < FPS_REFRESH_PERIOD_MS)
    {
        return;
    }
    s_last_fps_refresh_ms = now_ms;

    uint32_t const fps_x10 = camera_app_get_fps_x10();
    bool const active = camera_app_capture_active();

    if ((NULL != guider_ui.Scan.control_panel_fps) && lv_obj_is_valid(guider_ui.Scan.control_panel_fps))
    {
        if (active && (fps_x10 > 0U))
        {
            gui_app_label_set_fmt_if_changed(guider_ui.Scan.control_panel_fps,
                                             "%lu.%lu FPS",
                                             (unsigned long) (fps_x10 / 10U),
                                             (unsigned long) (fps_x10 % 10U));
        }
        else
        {
            gui_app_label_set_if_changed(guider_ui.Scan.control_panel_fps, "待机");
        }
    }

    if ((NULL != guider_ui.Device.device_camera_row_value) &&
        lv_obj_is_valid(guider_ui.Device.device_camera_row_value))
    {
        if (active && (fps_x10 > 0U))
        {
            gui_app_label_set_fmt_if_changed(guider_ui.Device.device_camera_row_value,
                                             "就绪 / %lu.%lu FPS",
                                             (unsigned long) (fps_x10 / 10U),
                                             (unsigned long) (fps_x10 % 10U));
        }
        else
        {
            uint32_t const last_fps_x10 = camera_app_get_last_fps_x10();
            if (last_fps_x10 > 0U)
            {
                gui_app_label_set_fmt_if_changed(guider_ui.Device.device_camera_row_value,
                                                 "就绪 / 上次 %lu.%lu FPS",
                                                 (unsigned long) (last_fps_x10 / 10U),
                                                 (unsigned long) (last_fps_x10 % 10U));
            }
            else
            {
                gui_app_label_set_if_changed(guider_ui.Device.device_camera_row_value, "就绪 / 待机");
            }
        }
    }
}

static void gui_app_refresh_qr_result(void);     /* 前置声明（定义在 gui_app_poll 之后） */
static void gui_app_poll_pickup_robot(void);     /* 前置声明（定义在 gui_app_poll 之后） */
static void gui_app_refresh_logs_page(void);     /* 前置声明 */
static void gui_app_refresh_scan_badge(void);    /* 前置声明 */
static void gui_app_refresh_scan_button(void);   /* 前置声明 */
static void gui_app_refresh_medicine_page(void); /* 前置声明 */
static void gui_app_refresh_pickup_list(void);   /* 前置声明 */
static void gui_app_refresh_store_page(void);    /* 前置声明 */
static void gui_app_install_store_hooks(void);   /* 前置声明 */
static void gui_app_refresh_boot_status(void);   /* 前置声明 */
static void gui_app_install_logs_hook(void);     /* 前置声明 */
static void gui_app_install_pickup_hook(void);   /* 前置声明 */
static void gui_app_install_pickup_rescan_hook(void); /* 前置声明 */
static void gui_app_tiny_font_service(void);     /* 前置声明 */
static void gui_app_apply_tiny_to_screens(void); /* 前置声明 */
static bool s_store_hooks_hooked;                /* Store 按钮事件是否已挂载 */
static bool s_logs_hook_hooked;                  /* Logs 清空按钮事件是否已挂载 */
static bool s_pickup_hook_hooked;                /* Pickup 开始取药按钮事件是否已挂载 */
static bool s_pickup_rescan_hook_hooked;         /* Pickup 重新扫描按钮事件是否已挂载 */
static bool s_pickup_dispensed;                  /* 本单是否已取药（状态行展示用） */
static bool s_pickup_order_scanned;              /* 取药单是否已在取药页扫码识别（每进页复位） */
static char s_pickup_order_number[QR_DECODER_PAYLOAD_MAX + 1U]; /* 取药页识别的单号 */
static bool s_pickup_page_active_prev;           /* 上一帧是否在取药页（进页时复位本单） */
static bool s_scan_has_result;                   /* Scan 页是否已展示过扫码结果 */
static char s_last_scan_text[QR_DECODER_PAYLOAD_MAX + 1U]; /* 最近一次扫码 payload */
static bool s_scan_btn_hooked;                   /* "开始扫描/查看详情"按钮事件是否已挂载 */

/* ============================================================================
 * 取药单 JSON 解析（轻量 strstr，针对云端取药二维码三种格式）：
 *   v2 精简码（《硬件修改指南_二维码精简v2.md》）：{"t":"P","v":1,"q":"PU-001",
 *                          "i":[{"d":"DRG-1","l":1,"x":66,"w":72},...]}
 *     → 提取 q（整单标识）、i[].d（核销核心字段）与 l/x（机械定位：
 *       l 1=A → 层Y，x = 药位中心 X mm）；容错 L，内容短、模块少；
 *   v1（手册 §6，兼容）：{"t":"PICK","v":1,"taskId":"PICK-PU-001","items":[
 *                          {"drugId":"DRG-1","layer":1,"x":66,"w":72},...]}
 *   v0 旧格式（兼容保留）：{"oid":"RX-...","i":[{"id":"MED-001","n":2},...]}
 *     → 提取 oid 与 i[].id/n（机械定位回退 drug_db position）。
 * 原始 JSON 直接显示会在窄栏换行成一团（乱码），且药品清单需按单内项目
 * 填充（可超过 3 种滚动）。
 * ==========================================================================*/
#define PICKUP_JSON_MAX_ITEMS    (16U)
static char     s_pickup_items[PICKUP_JSON_MAX_ITEMS][32]; /* 药品 code（云端 drugId） */
static uint32_t s_pickup_qty[PICKUP_JSON_MAX_ITEMS];       /* 数量 */
static float    s_pickup_x_mm[PICKUP_JSON_MAX_ITEMS];      /* 显式坐标 X（mm，手册 §6 items[].x） */
static float    s_pickup_y_mm[PICKUP_JSON_MAX_ITEMS];      /* 显式坐标 Y（mm，layer → 层Y） */
static float    s_pickup_w_mm[PICKUP_JSON_MAX_ITEMS];      /* 药品宽度（mm，items[].w；夹爪按宽度自动闭合） */
static bool     s_pickup_has_xy;                           /* 新格式自带坐标（layer/x） */
static bool     s_pickup_has_w;                            /* 新格式自带宽度（w） */
static uint32_t s_pickup_item_count;                       /* 有效项数 */
static char     s_pickup_oid[64];                          /* 取药单号 */

/* Pickup 滚动清单（超过 3 种药品时上下滚动） */
static lv_obj_t * s_pickup_scroll;                         /* 滚动容器（懒创建） */
static lv_obj_t * s_pickup_row_objs[PICKUP_JSON_MAX_ITEMS];/* 每行容器 */
static lv_obj_t * s_pickup_row_name[PICKUP_JSON_MAX_ITEMS];/* 行内 药品名 */
static lv_obj_t * s_pickup_row_qty[PICKUP_JSON_MAX_ITEMS]; /* 行内 数量 */
static lv_obj_t * s_pickup_row_pos[PICKUP_JSON_MAX_ITEMS]; /* 行内 仓位 */
static lv_obj_t * s_pickup_row_state[PICKUP_JSON_MAX_ITEMS];/* 行内 状态 */
static bool       s_pickup_rows_built;                     /* 动态行是否已创建 */

/* 本单已上报核销（PICKUP_SCANNED）的药品数；重新扫描时复位 */
static uint32_t   s_pickup_reported_count;

/* 复位本单已上报计数（进入取药页/重新扫描时调用） */
static void gui_app_pickup_reported_reset(void)
{
    s_pickup_reported_count = 0U;
}

/* 解析取药单 JSON（p_json 为 NUL 结尾的 payload）。
 * 兼容新格式（taskId + items[].drugId + layer/x）与旧格式（oid + i[].id/n）。
 * 返回解析到的药品项数；单号/items/qty/显式坐标写入 s_pickup_*。 */
static uint32_t pickup_json_parse(const char * p_json, uint32_t len)
{
    s_pickup_item_count = 0U;
    s_pickup_oid[0] = '\0';
    s_pickup_has_xy = false;
    s_pickup_has_w = false;
    if ((NULL == p_json) || (0U == len))
    {
        return 0U;
    }
    const char * p_end = p_json + len;

    /* 码类型：v2 精简码 t="P"；v1 手册码 t="PICK"；v0 旧旧格式无 t 键。
     * 注意匹配 "P\""（含引号）防止把 "PICK" 误判为 "P"；p_json 为 NUL
     * 结尾，strncmp 越界安全。 */
    const char * t_key = strstr(p_json, "\"t\":\"");
    bool const is_v2 = ((NULL != t_key) && (t_key + 5 < p_end) &&
                        (0 == strncmp(t_key + 5, "P\"", 2)));
    bool const is_v1 = ((NULL != t_key) && (t_key + 5 < p_end) &&
                        (0 == strncmp(t_key + 5, "PICK", 4)));
    bool const is_pick = is_v2 || is_v1;

    /* 药品项键：v2 "d":" → v1 "drugId":" → v0 "id":"。
     * 键互不误匹配："d":" 要求 d 后紧跟 引号冒号引号（"drugId":" 的 d 后是
     * r；"id":" 的 d 前是 i），strstr 精确。 */
    const char * item_key;
    size_t item_key_len;
    bool has_drug_id;
    if (is_v2)
    {
        item_key = "\"d\":\"";
        item_key_len = 5U;
        has_drug_id = true;
    }
    else if (NULL != strstr(p_json, "\"drugId\":\""))
    {
        item_key = "\"drugId\":\"";
        item_key_len = 10U;
        has_drug_id = true;
    }
    else
    {
        item_key = "\"id\":\"";
        item_key_len = 6U;
        has_drug_id = false;
    }

    /* 单号：v2 q → v1 taskId → v0 oid */
    const char * key = NULL;
    size_t key_len = 0U;
    if (is_v2)
    {
        key = strstr(p_json, "\"q\":\"");
        key_len = 5U; /* "q":" */
    }
    if ((NULL == key) || (key >= p_end))
    {
        key = strstr(p_json, "\"taskId\":\"");
        key_len = 10U; /* "taskId":" */
    }
    if ((NULL == key) || (key >= p_end))
    {
        key = strstr(p_json, "\"oid\":\"");
        key_len = 7U; /* "oid":" */
    }
    if ((NULL != key) && (key < p_end))
    {
        const char * v = key + key_len;
        const char * e = strchr(v, '"');
        uint32_t n = ((e != NULL) && (e < p_end)) ? (uint32_t) (e - v) : 0U;
        if ((n > 0U) && (n < sizeof(s_pickup_oid)))
        {
            memcpy(s_pickup_oid, v, n);
            s_pickup_oid[n] = '\0';
        }
    }

    if (has_drug_id && !is_pick)
    {
        /* 含 d/drugId 但非 P/PICK 码：可能为其他业务码，不解析为取药单 */
        return 0U;
    }

    /* 药品项：v2/v1 每药一个（无数量字段，qty=1）；
     * v0 旧格式 "id":"..."（n: 数量，缺省 1） */
    const char * scan = p_json;
    while (s_pickup_item_count < PICKUP_JSON_MAX_ITEMS)
    {
        const char * idk = strstr(scan, item_key);
        if ((NULL == idk) || (idk >= p_end))
        {
            break;
        }
        const char * v = idk + item_key_len;
        const char * e = strchr(v, '"');
        if ((NULL == e) || (e >= p_end))
        {
            break;
        }
        uint32_t n = (uint32_t) (e - v);
        if ((n > 0U) && (n < 32U))
        {
            memcpy(s_pickup_items[s_pickup_item_count], v, n);
            s_pickup_items[s_pickup_item_count][n] = '\0';
            uint32_t qty = 1U;
            if (!has_drug_id)
            {
                const char * nk = strstr(e, "\"n\":");
                if ((NULL != nk) && (nk < p_end))
                {
                    qty = (uint32_t) strtoul(nk + 4, NULL, 10);
                    if (0U == qty)
                    {
                        qty = 1U;
                    }
                }
            }
            s_pickup_qty[s_pickup_item_count] = qty;
            s_pickup_item_count++;
        }
        scan = e;
    }

    /* 显式坐标（v2/v1：l/layer 1=A→层Y、x = 药位中心 X mm；手册 §6/精简v2）。
     * 顺序扫描每个层键与 "x" 键：与药品项一一对应。v0 旧格式无坐标。 */
    if ((is_v2 || is_v1) && (s_pickup_item_count > 0U))
    {
        bool all_ok = true;
        /* v2 层键 "l":（4 字符）；v1 "layer":（8 字符）——互不误匹配 */
        const char * layer_k = is_v2 ? "\"l\":" : "\"layer\":";
        size_t const layer_k_len = is_v2 ? 4U : 8U;
        const char * p = p_json;
        for (uint32_t i = 0U; i < s_pickup_item_count; i++)
        {
            const char * lk = strstr(p, layer_k);
            const char * xk = strstr(p, "\"x\":");
            if ((NULL == lk) || (NULL == xk) || (lk >= p_end) || (xk >= p_end))
            {
                all_ok = false;
                break;
            }
            long layer = 0L;
            {
                char * endp = NULL;
                layer = strtol(lk + layer_k_len, &endp, 10);
                if (endp == lk + layer_k_len)
                {
                    all_ok = false;
                    break;
                }
            }
            long x_mm = 0L;
            {
                char * endp = NULL;
                x_mm = strtol(xk + 4, &endp, 10);
                if (endp == xk + 4)
                {
                    all_ok = false;
                    break;
                }
            }
            /* layer 1=A → shelf 0 起；越界按最近有效层 */
            uint8_t shelf = (layer > 0L) ? (uint8_t) (layer - 1L) : 0U;
            s_pickup_x_mm[i] = (float) x_mm;
            s_pickup_y_mm[i] = PickupParams_ShelfY(shelf);
            /* 药品宽度 items[].w（mm）：夹爪按宽度自动闭合（留 1mm 夹紧）；
             * 缺省 0 = 回退参数 grip_pulses */
            const char * wk = strstr(xk + 4, "\"w\":");
            if ((NULL != wk) && (wk < p_end))
            {
                char * endp = NULL;
                long w_mm = strtol(wk + 4, &endp, 10);
                if (endp != wk + 4)
                {
                    s_pickup_w_mm[i] = (float) w_mm;
                    s_pickup_has_w = true;
                }
            }
            p = xk + 4; /* 继续找下一项 */
        }
        s_pickup_has_xy = all_ok;
    }
    return s_pickup_item_count;
}

static lv_obj_t * gui_app_create_camera_image(lv_obj_t * p_parent)
{
    lv_obj_t * p_image = lv_image_create(p_parent);
    lv_obj_set_size(p_image, lv_pct(100), lv_pct(100));
    lv_obj_center(p_image);
    lv_image_set_src(p_image, camera_preview_get_image());
    lv_image_set_inner_align(p_image, LV_IMAGE_ALIGN_STRETCH);
    lv_obj_move_to_index(p_image, 0);
    return p_image;
}

/* 进入取药页：复位本单状态为"待扫描"（不沿用全局最近扫码结果）。
 * 取药任务页与 Scan 识别页共用一条解码链路，但任务状态必须独立：
 * 在 Scan 页扫过的码不应让取药页一进来就显示"已识别"。 */
static void gui_app_pickup_reset_order_card(void)
{
    s_pickup_order_scanned = false;
    s_pickup_order_number[0] = '\0';
    s_pickup_dispensed = false;
    gui_app_pickup_reported_reset();

    if ((NULL != guider_ui.Pickup.order_card_order_title) &&
        lv_obj_is_valid(guider_ui.Pickup.order_card_order_title))
    {
        gui_app_label_set_if_changed(guider_ui.Pickup.order_card_order_title, "取药单待扫描");
    }
    if ((NULL != guider_ui.Pickup.order_badge_order_badge_text) &&
        lv_obj_is_valid(guider_ui.Pickup.order_badge_order_badge_text))
    {
        gui_app_label_set_if_changed(guider_ui.Pickup.order_badge_order_badge_text, "待扫描");
    }
    if ((NULL != guider_ui.Pickup.order_card_order_number) &&
        lv_obj_is_valid(guider_ui.Pickup.order_card_order_number))
    {
        gui_app_label_set_if_changed(guider_ui.Pickup.order_card_order_number, "等待扫码…");
    }
    /* 按钮文字恢复"开始取药"（颜色恢复白色） */
    lv_obj_t * p_btn_text = guider_ui.Pickup.button_start_button_start_text;
    if ((NULL != p_btn_text) && lv_obj_is_valid(p_btn_text))
    {
        gui_app_label_set_if_changed(p_btn_text, "开始取药");
        lv_obj_set_style_text_color(p_btn_text, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

bool gui_app_init(void)
{
    setup_ui(&guider_ui);
    custom_init(&guider_ui);

    /* GUI Guider initially creates only Boot.  Home, Pickup and Scan are
     * lazily constructed by their navigation events, so they must not be
     * treated as an initialization failure here. */
    if (NULL == guider_ui.Boot.screen)
    {
        return false;
    }

    s_pickup_preview = NULL;
    s_scan_preview = NULL;
    s_capture_requested = false;
    s_scan_has_result = false;
    s_scan_btn_hooked = false;
    s_store_hooks_hooked = false;
    s_logs_hook_hooked = false;
    s_pickup_hook_hooked = false;
    s_pickup_rescan_hook_hooked = false;
    s_pickup_dispensed = false;
    s_pickup_order_scanned = false;
    s_pickup_order_number[0] = '\0';
    s_pickup_page_active_prev = false;
    s_last_scan_text[0] = '\0';
    inventory_init();
    sys_log_add(SYS_LOG_OK, "库存台账初始化完成");
#if !defined(CAMERA_RTT_ONLY)
    camera_app_request_capture(false);
#endif
    return true;
}

void gui_app_poll(void)
{
    lv_obj_t * p_active_screen = lv_screen_active();
    bool pickup_active = (p_active_screen == guider_ui.Pickup.screen);
    bool scan_active = (p_active_screen == guider_ui.Scan.screen);

    /* 进入取药页：复位本单状态为"待扫描"。避免 Scan 页扫过的码/上次会话残留
     * 让取药页一进来就显示"已识别"。仅当页面由非激活变为激活时执行一次。 */
    if (pickup_active && !s_pickup_page_active_prev)
    {
        gui_app_pickup_reset_order_card();
    }
    s_pickup_page_active_prev = pickup_active;

    /* tiny_ttf 运行时字体：OSPI 就绪后创建（一次性），各 screen 出现后应用 tiny 字体 */
    gui_app_tiny_font_service();
    gui_app_apply_tiny_to_screens();

    /* LVGL 堆监控：每 2s 记录（J-Link 读 s_lvgl_mem_* 诊断内存泄漏/碎片） */
    {
        static uint32_t s_mem_log_ms;
        uint32_t const now_ms = lv_tick_get();
        if ((now_ms - s_mem_log_ms) >= 2000U)
        {
            s_mem_log_ms = now_ms;
            lv_mem_monitor_t mon;
            lv_mem_monitor(&mon);
            s_lvgl_mem_free = (uint32_t) mon.free_size;
            s_lvgl_mem_big  = (uint32_t) mon.free_size - ((uint32_t) mon.free_size * mon.frag_pct / 100U);
            s_lvgl_mem_frag = (uint32_t) mon.frag_pct;
            SCB_CleanDCache_by_Addr((uint32_t *) &s_lvgl_mem_free, 12);
        }
    }

    if (pickup_active && (NULL == s_pickup_preview) &&
        (NULL != guider_ui.Pickup.camera_card_camera_frame))
    {
        s_pickup_preview = gui_app_create_camera_image(guider_ui.Pickup.camera_card_camera_frame);
    }

    if (scan_active && (NULL == s_scan_preview) && (NULL != guider_ui.Scan.camera_preview))
    {
        s_scan_preview = gui_app_create_camera_image(guider_ui.Scan.camera_preview);
    }

    bool capture_requested = (pickup_active && (NULL != s_pickup_preview)) ||
                             (scan_active && (NULL != s_scan_preview));

    if (capture_requested != s_capture_requested)
    {
        camera_app_request_capture(capture_requested);
        s_capture_requested = capture_requested;
    }

    if (capture_requested && camera_preview_has_new_frame())
    {
        lv_obj_invalidate(pickup_active ? s_pickup_preview : s_scan_preview);
        camera_preview_mark_flushed();
    }

    /* 二维码解码：Scan 识别页 + Pickup 取药页均启用（取药单/药品码共用一条链路） */
    qr_decoder_set_enabled(((scan_active && (NULL != s_scan_preview)) ||
                            (pickup_active && (NULL != s_pickup_preview))));
    /* s_scan_has_result 保留最近一次识别结果（不随页面切换清空），
     * 供 Medicine 详情页在 Scan→Medicine 跳转后仍能显示内容。 */
    (void) scan_active;

    /* Scan 页"开始扫描/查看详情"按钮（扫码成功后点击跳转 Medicine 详情页） */
    gui_app_refresh_scan_button();

    /* Medicine 页：药品识别结果（名称/编码/验证徽章由最近一次扫码填充） */
    gui_app_refresh_medicine_page();

    /* Pickup 页：药品清单由药品库填充（进入页面时刷新一次） */
    gui_app_refresh_pickup_list();

    /* Store 存药页：药品信息由 drug_db + 最近扫码填充；按钮事件挂载 */
    gui_app_refresh_store_page();
    gui_app_install_store_hooks();

    /* Boot 启动页：状态文本真实化（触摸/摄像头/LVGL 摘要） */
    gui_app_refresh_boot_status();

    /* Logs 页"日志详情"按钮 → 清空日志 */
    gui_app_install_logs_hook();

    /* Pickup 页"开始取药"按钮 → 模拟取药（库存扣减 + 状态行更新） */
    gui_app_install_pickup_hook();

    /* 取药流程轮询：多药取药状态机完成/失败 → 扣库存 + 按钮/日志反馈 */
    gui_app_poll_pickup_robot();

    /* Pickup 页"重新扫描"按钮 → 重置取药单与解码状态 */
    gui_app_install_pickup_rescan_hook();

    /* 实时帧率标签（Scan/Device 页） */
    gui_app_refresh_camera_fps();

    /* 解码结果发布到 Scan 页结果标签 */
    gui_app_refresh_qr_result();

    /* 二维码板端自检结果（Device 页"运行自检"异步触发）→ 系统日志 */
    {
        qr_selftest_result_t selftest;
        if (qr_decoder_selftest_get(&selftest))
        {
            if (QR_DECODER_OK == selftest.status)
            {
                char stext[QR_DECODER_PAYLOAD_MAX + 1U];
                uint32_t slen = selftest.payload_len;
                if (slen > QR_DECODER_PAYLOAD_MAX)
                {
                    slen = QR_DECODER_PAYLOAD_MAX;
                }
                memcpy(stext, selftest.payload, slen);
                stext[slen] = '\0';
                sys_log_add(SYS_LOG_OK, "二维码自检通过(%u): %s (%lu ms)",
                            (unsigned) selftest.which, stext,
                            (unsigned long) selftest.decode_ms);
            }
            else
            {
                sys_log_add(SYS_LOG_ERR, "二维码自检失败(%u): status=%u (%lu ms)",
                            (unsigned) selftest.which, (unsigned) selftest.status,
                            (unsigned long) selftest.decode_ms);
            }
        }
    }

    /* Scan 页状态徽章（就绪 → 采集中 → 已识别） */
    gui_app_refresh_scan_badge();

    /* 云端连接状态桥接（手册 §5）：协议层三态（CONNECTED/PING/DISPENSE 刷新
     * 断线基线）→ LVGL 徽章/Device 行。500ms 节流轮询（gui_set_esp01s_state
     * 内部无变化短路，避免每 tick 重设样式）。 */
    {
        static uint32_t s_last_conn_ms;
        uint32_t const now_ms = lv_tick_get();
        if ((now_ms - s_last_conn_ms) >= 500U)
        {
            s_last_conn_ms = now_ms;
            esp01s_conn_state_t const conn = esp01s_proto_get_conn_state();
            esp01s_ui_state_t ui_state =
                (conn == ESP01S_CONN_ONLINE) ? ESP01S_UI_ONLINE :
                (conn == ESP01S_CONN_OFFLINE) ? ESP01S_UI_OFFLINE :
                                                ESP01S_UI_CONNECTING;
            gui_set_esp01s_state(&guider_ui, ui_state);
        }
    }

    /* Logs 页：真实系统日志 + 统计指标（仅页面激活时刷新） */
    gui_app_refresh_logs_page();
}

/***********************************************************************************************************************
 * 二维码解码结果显示（Scan 页 control_panel_result）
 **********************************************************************************************************************/
static void gui_app_refresh_qr_result(void)
{
    qr_decoder_result_t result;

    if (!qr_decoder_get_result(&result))
    {
        return;
    }

    /* 无效结果：Scan 页结果栏显示占位（若页面已懒创建）。此分支不再
     * 提前 return —— 此前 Scan.control_panel_result 为 NULL（用户从未进
     * 过 Scan 页）时整个函数退出，Pickup 分支永远执行不到（取药页扫码
     * 无任何显示）。 */
    if (!result.valid || (result.payload_len == 0U))
    {
        if ((NULL != guider_ui.Scan.control_panel_result) &&
            lv_obj_is_valid(guider_ui.Scan.control_panel_result))
        {
            lv_label_set_text(guider_ui.Scan.control_panel_result, "等待\n二维码");
        }
        return;
    }

    /* payload 是原始字节（可能含 UTF-8 中文）。保证 NUL 结尾后再交给 LVGL。 */
    char text[QR_DECODER_PAYLOAD_MAX + 1U];
    uint32_t len = result.payload_len;
    if (len > QR_DECODER_PAYLOAD_MAX)
    {
        len = QR_DECODER_PAYLOAD_MAX;
    }
    memcpy(text, result.payload, len);
    text[len] = '\0';
    s_scan_has_result = true;
    memcpy(s_last_scan_text, text, len + 1U);
    sys_log_add(SYS_LOG_OK, "扫码成功: %s", text);

    /* Scan 页结果栏（页面未懒创建时为 NULL，跳过即可，不影响 Pickup）：
     * 固定宽度 + WRAP，防止长 payload 撑破 136px 侧栏 */
    if ((NULL != guider_ui.Scan.control_panel_result) &&
        lv_obj_is_valid(guider_ui.Scan.control_panel_result))
    {
        lv_label_set_long_mode(guider_ui.Scan.control_panel_result, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(guider_ui.Scan.control_panel_result, 112);
        lv_obj_set_style_text_align(guider_ui.Scan.control_panel_result, LV_TEXT_ALIGN_LEFT,
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(guider_ui.Scan.control_panel_result, text);
    }

    /* Pickup 取药页：解码结果 → 取药单号/徽章/标题。
     * 仅当在取药页本页解码成功才置位本单状态（页内扫码才算识别）。 */
    if (lv_screen_active() == guider_ui.Pickup.screen)
    {
        s_pickup_order_scanned = true;
        /* 新一单：复位"已完成"状态与已上报计数，允许再次取药
         * （此前只在进入页面时复位，停留页内重新扫码会被
         * s_pickup_dispensed 拦截，按钮停留在"取药完成"） */
        s_pickup_dispensed = false;
        gui_app_pickup_reported_reset();
        uint32_t copy_len = len;
        if (copy_len > (QR_DECODER_PAYLOAD_MAX))
        {
            copy_len = QR_DECODER_PAYLOAD_MAX;
        }
        memcpy(s_pickup_order_number, text, copy_len);
        s_pickup_order_number[copy_len] = '\0';
        /* 解析取药单 JSON：提取单号/药品项/显式坐标（供单号栏与滚动清单） */
        pickup_json_parse(text, len);
        /* 云端协议（手册 §6/§7）：PICKUP_SCANNED 在"取到某药后"上报核销，
         * 不在扫码瞬间上报（避免云端提前核销而机械尚未取药）。
         * 上报由取药流程完成回调（gui_app_poll_pickup_robot）逐药触发。 */
        /* 按钮恢复"开始取药"（上次完成后是"取药完成"，新单可再取） */
        lv_obj_t * p_btn_text = guider_ui.Pickup.button_start_button_start_text;
        if ((NULL != p_btn_text) && lv_obj_is_valid(p_btn_text))
        {
            gui_app_label_set_if_changed(p_btn_text, "开始取药");
            lv_obj_set_style_text_color(p_btn_text, lv_color_hex(0xffffff),
                                        LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        if ((NULL != guider_ui.Pickup.order_card_order_number) &&
            lv_obj_is_valid(guider_ui.Pickup.order_card_order_number))
        {
            lv_label_set_long_mode(guider_ui.Pickup.order_card_order_number,
                                   (s_pickup_oid[0] != '\0') ? LV_LABEL_LONG_CLIP : LV_LABEL_LONG_WRAP);
            lv_obj_set_width(guider_ui.Pickup.order_card_order_number, 190);
            /* 标准取药单 JSON：显示解析后的单号（原始 JSON 在窄栏换行成一团
             * =乱码）；非 JSON 内容（短单号）原样显示 */
            lv_label_set_text(guider_ui.Pickup.order_card_order_number,
                              (s_pickup_oid[0] != '\0') ? s_pickup_oid : text);
        }
        if ((NULL != guider_ui.Pickup.order_badge_order_badge_text) &&
            lv_obj_is_valid(guider_ui.Pickup.order_badge_order_badge_text))
        {
            lv_label_set_text(guider_ui.Pickup.order_badge_order_badge_text, "扫描成功");
        }
        if ((NULL != guider_ui.Pickup.order_card_order_title) &&
            lv_obj_is_valid(guider_ui.Pickup.order_card_order_title))
        {
            lv_label_set_text(guider_ui.Pickup.order_card_order_title, "取药单已识别");
        }
    }
}

/***********************************************************************************************************************
 * Scan 页"开始扫描"按钮：扫码成功后变为"查看详情"，点击跳转 Medicine 详情页
 **********************************************************************************************************************/
static void scan_detail_button_click_hook(lv_event_t * e)
{
    (void) e;

    if (!s_scan_has_result)
    {
        return;
    }

    gg_screen_load_cfg_t cfg =
    {
        .scr       = &guider_ui.Medicine.screen,
        .setup_fn  = setup_Medicine,
        .anim_type = LV_SCR_LOAD_ANIM_FADE_IN,
        .time      = 180,
        .delay     = 0,
        .auto_del  = false,
    };
    gg_load_screen_animation(&guider_ui, &cfg);
}

static void gui_app_refresh_scan_button(void)
{
    static uint32_t s_last_ms;
    lv_obj_t * p_btn = guider_ui.Scan.control_panel_button_start;
    if ((NULL == p_btn) || !lv_obj_is_valid(p_btn))
    {
        return;
    }
    if (!gui_app_status_throttle(&s_last_ms))
    {
        return;
    }

    /* 事件钩子只挂一次（GUI Guider 生成的事件文件不含此跳转） */
    if (!s_scan_btn_hooked)
    {
        lv_obj_add_event_cb(p_btn, scan_detail_button_click_hook, LV_EVENT_CLICKED, NULL);
        s_scan_btn_hooked = true;
    }

    lv_obj_t * p_text = guider_ui.Scan.button_start_button_start_text;
    if ((NULL != p_text) && lv_obj_is_valid(p_text))
    {
        gui_app_label_set_if_changed(p_text, s_scan_has_result ? "查看详情" : "开始扫描");
    }
}

/***********************************************************************************************************************
 * Medicine 页：药品识别结果（名称/编码/验证徽章填充最近一次扫码内容）
 **********************************************************************************************************************/
static void gui_app_refresh_medicine_page(void)
{
    static uint32_t s_last_ms;
    if (lv_screen_active() != guider_ui.Medicine.screen)
    {
        return;
    }
    if (!gui_app_status_throttle(&s_last_ms))
    {
        return;
    }

    /* 名称：优先用扫码 payload；无结果显示占位 */
    lv_obj_t * p_name = guider_ui.Medicine.medicine_card_medicine_name;
    if ((NULL != p_name) && lv_obj_is_valid(p_name))
    {
        lv_label_set_long_mode(p_name, LV_LABEL_LONG_CLIP);
        lv_obj_set_width(p_name, 420);
        if (s_scan_has_result && (s_last_scan_text[0] != '\0'))
        {
            gui_app_label_set_if_changed(p_name, s_last_scan_text);
        }
        else
        {
            gui_app_label_set_if_changed(p_name, "等待扫码…");
        }
    }

    /* 编码 */
    lv_obj_t * p_code = guider_ui.Medicine.medicine_card_medicine_code;
    if ((NULL != p_code) && lv_obj_is_valid(p_code))
    {
        if (s_scan_has_result && (s_last_scan_text[0] != '\0'))
        {
            gui_app_label_set_fmt_if_changed(p_code, "编码：%s", s_last_scan_text);
        }
        else
        {
            gui_app_label_set_if_changed(p_code, "编码：--");
        }
    }

    /* 验证徽章：已识别 → 已验证（绿）；否则 → 未验证（灰） */
    lv_obj_t * p_badge = guider_ui.Medicine.valid_badge_valid_text;
    if ((NULL != p_badge) && lv_obj_is_valid(p_badge))
    {
        if (s_scan_has_result)
        {
            gui_app_label_set_if_changed(p_badge, "已验证");
            lv_obj_set_style_text_color(p_badge, lv_color_hex(0x14a66a), LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        else
        {
            gui_app_label_set_if_changed(p_badge, "未验证");
            lv_obj_set_style_text_color(p_badge, lv_color_hex(0x687b99), LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }

    /* 剂量/批次/有效期：由药品库查表填充（未命中显示 "--"） */
    const drug_db_entry_t * p_drug = s_scan_has_result ? drug_db_lookup(s_last_scan_text) : NULL;

    lv_obj_t * p_dose = guider_ui.Medicine.medicine_card_dose;
    if ((NULL != p_dose) && lv_obj_is_valid(p_dose))
    {
        gui_app_label_set_if_changed(p_dose, (p_drug != NULL) ? p_drug->dose : "--");
    }
    lv_obj_t * p_batch = guider_ui.Medicine.medicine_card_batch;
    if ((NULL != p_batch) && lv_obj_is_valid(p_batch))
    {
        gui_app_label_set_if_changed(p_batch, (p_drug != NULL) ? p_drug->batch : "--");
    }
    lv_obj_t * p_expiry = guider_ui.Medicine.medicine_card_expiry;
    if ((NULL != p_expiry) && lv_obj_is_valid(p_expiry))
    {
        gui_app_label_set_if_changed(p_expiry, (p_drug != NULL) ? p_drug->expiry : "--");
    }
}

/***********************************************************************************************************************
 * Scan 页状态徽章：就绪（绿）→ 采集中（蓝）→ 已识别（绿/橙）
 **********************************************************************************************************************/
static void gui_app_refresh_scan_badge(void)
{
    static uint32_t s_last_ms;
    lv_obj_t * p_badge = guider_ui.Scan.state_badge_state;
    if ((NULL == p_badge) || !lv_obj_is_valid(p_badge))
    {
        return;
    }
    if (!gui_app_status_throttle(&s_last_ms))
    {
        return;
    }

    lv_color_t color;
    const char * p_text;

    if (s_scan_has_result)
    {
        color = lv_color_hex(0x14a66a);
        p_text = "已识别";
    }
    else if (camera_app_capture_active())
    {
        color = lv_color_hex(0x1677ff);
        p_text = "采集中";
    }
    else
    {
        color = lv_color_hex(0x14a66a);
        p_text = "就绪";
    }

    /* 徽章文本 + 颜色仅在状态机变化时更新（文本已由 label helper 比较，
     * 颜色用 lv_color_eq 比较，避免每 500ms 无条件触发样式重算） */
    gui_app_label_set_if_changed(p_badge, p_text);
    static lv_color_t s_last_color;
    static bool s_color_init;
    if (!s_color_init || !lv_color_eq(s_last_color, color))
    {
        s_last_color = color;
        s_color_init = true;
        lv_obj_set_style_text_color(p_badge, color, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

/***********************************************************************************************************************
 * Logs 页：真实系统日志 + 统计指标（500ms 节流，仅页面激活时刷新）
 *  - 12 行可滚动日志列表：时间 / 级别 / 内容（最新在前）
 *  - 指标卡：日志总数 / 成功数 / 异常数
 **********************************************************************************************************************/
#define LOGS_VISIBLE_ROWS   (12U)

typedef struct
{
    lv_obj_t * row;
    lv_obj_t * time_label;
    lv_obj_t * text_label;
    lv_obj_t * level_label;
} logs_row_t;

static logs_row_t s_logs_rows[LOGS_VISIBLE_ROWS];
static bool s_logs_rows_ready;

/* 取药记录行（Logs 页表格卡下方，与系统日志分离展示） */
#define PICKUP_VISIBLE_ROWS  (6U)
static logs_row_t s_pickup_rows[PICKUP_VISIBLE_ROWS];
static bool s_pickup_rows_ready;

/* 在可滚动日志卡片内追加"取药记录"区块（系统日志 12 行之下） */
static void gui_app_pickup_build_rows(void)
{
    if (s_pickup_rows_ready)
    {
        return;
    }
    lv_obj_t * p_card = guider_ui.Logs.log_table_card;
    if ((NULL == p_card) || !lv_obj_is_valid(p_card))
    {
        return;
    }

    /* 区块标题 */
    lv_obj_t * p_head = lv_label_create(p_card);
    lv_obj_set_pos(p_head, 14, 90 + (lv_coord_t) LOGS_VISIBLE_ROWS * 44 + 10);
    lv_obj_set_style_text_color(p_head, lv_color_hex(0x1677ff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(p_head,
                               (NULL != g_tiny_font) ? g_tiny_font : &lv_font_SourceHanSerifSC_15,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text(p_head, "取药记录");

    for (uint32_t i = 0U; i < PICKUP_VISIBLE_ROWS; i++)
    {
        lv_obj_t * p_row = lv_obj_create(p_card);
        lv_obj_set_size(p_row, 580, 36);
        lv_obj_set_pos(p_row, 14, 90 + (lv_coord_t) LOGS_VISIBLE_ROWS * 44 + 36 + (lv_coord_t) i * 40);
        lv_obj_set_style_bg_color(p_row, lv_color_hex(0xf3f9ff), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(p_row, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(p_row, lv_color_hex(0xdceafa), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_opa(p_row, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(p_row, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_radius(p_row, 7, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_all(p_row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

        lv_obj_t * p_time = lv_label_create(p_row);
        lv_obj_set_pos(p_time, 10, 9);
        lv_obj_set_style_text_color(p_time, lv_color_hex(0x687b99), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(p_time,
                                   (NULL != g_tiny_font) ? g_tiny_font : &lv_font_SourceHanSerifSC_15,
                                   LV_PART_MAIN | LV_STATE_DEFAULT);

        lv_obj_t * p_text = lv_label_create(p_row);
        lv_obj_set_pos(p_text, 96, 9);
        lv_label_set_long_mode(p_text, LV_LABEL_LONG_CLIP);
        lv_obj_set_width(p_text, 310);
        lv_obj_set_style_text_color(p_text, lv_color_hex(0x10233f), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(p_text,
                                   (NULL != g_tiny_font) ? g_tiny_font : &lv_font_SourceHanSerifSC_15,
                                   LV_PART_MAIN | LV_STATE_DEFAULT);

        lv_obj_t * p_tag = lv_label_create(p_row);
        lv_obj_set_pos(p_tag, 420, 9);
        lv_obj_set_style_text_color(p_tag, lv_color_hex(0x14a66a), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(p_tag,
                                   (NULL != g_tiny_font) ? g_tiny_font : &lv_font_SourceHanSerifSC_15,
                                   LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_label_set_text(p_tag, "取药");

        s_pickup_rows[i].row = p_row;
        s_pickup_rows[i].time_label = p_time;
        s_pickup_rows[i].text_label = p_text;
        s_pickup_rows[i].level_label = p_tag;
    }
    s_pickup_rows_ready = true;
}

static void gui_app_logs_build_rows(void)
{
    if (s_logs_rows_ready)
    {
        return;
    }
    lv_obj_t * p_card = guider_ui.Logs.log_table_card;
    if ((NULL == p_card) || !lv_obj_is_valid(p_card))
    {
        return;
    }

    /* 隐藏 GUI Guider 生成的 4 个静态行（内容由动态行接管） */
    lv_obj_t * static_rows[4] =
    {
        guider_ui.Logs.log_table_card_detail_log_row,
        guider_ui.Logs.log_table_card_detail_log_row_1,
        guider_ui.Logs.log_table_card_detail_log_row_2,
        guider_ui.Logs.log_table_card_detail_log_row_3,
    };
    for (uint32_t i = 0U; i < 4U; i++)
    {
        if ((NULL != static_rows[i]) && lv_obj_is_valid(static_rows[i]))
        {
            lv_obj_add_flag(static_rows[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    for (uint32_t i = 0U; i < LOGS_VISIBLE_ROWS; i++)
    {
        lv_obj_t * p_row = lv_obj_create(p_card);
        lv_obj_set_size(p_row, 580, 40);
        lv_obj_set_pos(p_row, 14, 90 + (lv_coord_t) i * 44);
        lv_obj_set_style_bg_color(p_row, lv_color_hex(0xfbfcff), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(p_row, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(p_row, lv_color_hex(0xedf1f7), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_opa(p_row, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(p_row, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_radius(p_row, 7, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_all(p_row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

        lv_obj_t * p_time = lv_label_create(p_row);
        lv_obj_set_pos(p_time, 10, 12);
        lv_obj_set_style_text_color(p_time, lv_color_hex(0x687b99), LV_PART_MAIN | LV_STATE_DEFAULT);
        /* 日志是运行态动态文本，用 tiny_ttf（黑体）渲染任意中文，避免编译字库缺字方块 */
        lv_obj_set_style_text_font(p_time,
                                   (NULL != g_tiny_font) ? g_tiny_font : &lv_font_SourceHanSerifSC_15,
                                   LV_PART_MAIN | LV_STATE_DEFAULT);

        lv_obj_t * p_text = lv_label_create(p_row);
        lv_obj_set_pos(p_text, 96, 12);
        lv_label_set_long_mode(p_text, LV_LABEL_LONG_CLIP);
        lv_obj_set_width(p_text, 300);
        lv_obj_set_style_text_color(p_text, lv_color_hex(0x10233f), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(p_text,
                                   (NULL != g_tiny_font) ? g_tiny_font : &lv_font_SourceHanSerifSC_15,
                                   LV_PART_MAIN | LV_STATE_DEFAULT);

        lv_obj_t * p_level = lv_label_create(p_row);
        lv_obj_set_pos(p_level, 420, 12);
        lv_obj_set_style_text_font(p_level,
                                   (NULL != g_tiny_font) ? g_tiny_font : &lv_font_SourceHanSerifSC_15,
                                   LV_PART_MAIN | LV_STATE_DEFAULT);

        s_logs_rows[i].row = p_row;
        s_logs_rows[i].time_label = p_time;
        s_logs_rows[i].text_label = p_text;
        s_logs_rows[i].level_label = p_level;
    }

    /* 卡片启用垂直滚动（内容超高时） */
    lv_obj_set_scroll_dir(p_card, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(p_card, LV_SCROLLBAR_MODE_AUTO);

    s_logs_rows_ready = true;
}

static void gui_app_refresh_logs_page(void)
{
    static uint32_t s_last_logs_refresh_ms;

    if (lv_screen_active() != guider_ui.Logs.screen)
    {
        return;
    }

    gui_app_logs_build_rows();
    if (!s_logs_rows_ready)
    {
        return;
    }
    gui_app_pickup_build_rows();

    uint32_t const now_ms = lv_tick_get();
    if ((now_ms - s_last_logs_refresh_ms) < FPS_REFRESH_PERIOD_MS)
    {
        return;
    }
    s_last_logs_refresh_ms = now_ms;

    static const char * const level_text[SYS_LOG_LEVELS] = { "信息", "成功", "警告", "错误" };
    static const uint32_t level_color[SYS_LOG_LEVELS] =
    {
        0x687b99,   /* INFO  灰蓝 */
        0x14a66a,   /* OK    绿 */
        0xf59e0b,   /* WARN  橙 */
        0xef5350,   /* ERR   红 */
    };

    for (uint32_t i = 0U; i < LOGS_VISIBLE_ROWS; i++)
    {
        logs_row_t * p = &s_logs_rows[i];
        if ((NULL == p->row) || !lv_obj_is_valid(p->row))
        {
            continue;
        }

        sys_log_entry_t entry;
        if (!sys_log_peek(i, &entry))
        {
            lv_obj_add_flag(p->row, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(p->row, LV_OBJ_FLAG_HIDDEN);

        uint32_t const sec = entry.tick / 1000U;
        if ((NULL != p->time_label) && lv_obj_is_valid(p->time_label))
        {
            gui_app_label_set_fmt_if_changed(p->time_label, "%02lu:%02lu:%02lu",
                                             (unsigned long) ((sec / 3600U) % 24U),
                                             (unsigned long) ((sec / 60U) % 60U),
                                             (unsigned long) (sec % 60U));
        }
        if ((NULL != p->text_label) && lv_obj_is_valid(p->text_label))
        {
            gui_app_label_set_if_changed(p->text_label, entry.text);
        }
        if ((NULL != p->level_label) && lv_obj_is_valid(p->level_label))
        {
            gui_app_label_set_if_changed(p->level_label, level_text[entry.level]);
            lv_obj_set_style_text_color(p->level_label, lv_color_hex(level_color[entry.level]),
                                        LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }

    /* 指标卡：日志总数 / 成功数 / 异常数（WARN+ERR） */
    if ((NULL != guider_ui.Logs.metric_card_metric_value_2) &&
        lv_obj_is_valid(guider_ui.Logs.metric_card_metric_value_2))
    {
        gui_app_label_set_fmt_if_changed(guider_ui.Logs.metric_card_metric_value_2, "%lu 条",
                                         (unsigned long) sys_log_total());
    }
    if ((NULL != guider_ui.Logs.metric_card_metric_value_1) &&
        lv_obj_is_valid(guider_ui.Logs.metric_card_metric_value_1))
    {
        gui_app_label_set_fmt_if_changed(guider_ui.Logs.metric_card_metric_value_1, "%lu 条",
                                         (unsigned long) sys_log_count_level(SYS_LOG_OK));
    }
    if ((NULL != guider_ui.Logs.metric_card_metric_value) &&
        lv_obj_is_valid(guider_ui.Logs.metric_card_metric_value))
    {
        gui_app_label_set_fmt_if_changed(guider_ui.Logs.metric_card_metric_value, "%lu 条",
                                         (unsigned long) (sys_log_count_level(SYS_LOG_WARN) +
                                                          sys_log_count_level(SYS_LOG_ERR)));
    }

    /* 取药记录（独立于系统日志的区块，无记录时隐藏） */
    uint32_t const pickup_count = pickup_log_count();
    for (uint32_t i = 0U; i < PICKUP_VISIBLE_ROWS; i++)
    {
        logs_row_t * p = &s_pickup_rows[i];
        if ((NULL == p->row) || !lv_obj_is_valid(p->row))
        {
            continue;
        }
        pickup_log_entry_t entry;
        if (!pickup_log_peek(i, &entry))
        {
            lv_obj_add_flag(p->row, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(p->row, LV_OBJ_FLAG_HIDDEN);
        uint32_t const sec = entry.tick / 1000U;
        if ((NULL != p->time_label) && lv_obj_is_valid(p->time_label))
        {
            gui_app_label_set_fmt_if_changed(p->time_label, "%02lu:%02lu:%02lu",
                                             (unsigned long) ((sec / 3600U) % 24U),
                                             (unsigned long) ((sec / 60U) % 60U),
                                             (unsigned long) (sec % 60U));
        }
        if ((NULL != p->text_label) && lv_obj_is_valid(p->text_label))
        {
            gui_app_label_set_if_changed(p->text_label, entry.text);
        }
    }
    (void) pickup_count;
}

/***********************************************************************************************************************
 * Pickup 页药品清单：由药品库填充（进入页面时刷新，替换生成界面的硬编码示例）
 **********************************************************************************************************************/
static void gui_app_refresh_pickup_list(void)
{
    static uint32_t s_last_ms;
    if (lv_screen_active() != guider_ui.Pickup.screen)
    {
        return;
    }
    if (!gui_app_status_throttle(&s_last_ms))
    {
        return;
    }

    lv_obj_t * p_card = guider_ui.Pickup.medicine_list_card;
    if ((NULL == p_card) || !lv_obj_is_valid(p_card))
    {
        return;
    }

    /* 首次：隐藏静态示例行，建滚动容器 + 动态行（支持 >3 种药品滚动） */
    if (!s_pickup_rows_built)
    {
        s_pickup_rows_built = true;
        lv_obj_t * static_labels[12] =
        {
            guider_ui.Pickup.medicine_list_card_drug_name,
            guider_ui.Pickup.medicine_list_card_drug_name_1,
            guider_ui.Pickup.medicine_list_card_drug_name_2,
            guider_ui.Pickup.medicine_list_card_quantity,
            guider_ui.Pickup.medicine_list_card_quantity_1,
            guider_ui.Pickup.medicine_list_card_quantity_2,
            guider_ui.Pickup.medicine_list_card_position,
            guider_ui.Pickup.medicine_list_card_position_1,
            guider_ui.Pickup.medicine_list_card_position_2,
            guider_ui.Pickup.medicine_list_card_state,
            guider_ui.Pickup.medicine_list_card_state_1,
            guider_ui.Pickup.medicine_list_card_state_2,
        };
        for (uint32_t i = 0U; i < 12U; i++)
        {
            if ((NULL != static_labels[i]) && lv_obj_is_valid(static_labels[i]))
            {
                lv_obj_add_flag(static_labels[i], LV_OBJ_FLAG_HIDDEN);
            }
        }

        /* 滚动容器：table_header(y52 h28) 之下，可上下滚动 */
        s_pickup_scroll = lv_obj_create(p_card);
        lv_obj_set_pos(s_pickup_scroll, 14, 84);
        lv_obj_set_size(s_pickup_scroll, 580, 104);
        lv_obj_set_style_bg_opa(s_pickup_scroll, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(s_pickup_scroll, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_all(s_pickup_scroll, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_radius(s_pickup_scroll, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_scroll_dir(s_pickup_scroll, LV_DIR_VER);
        lv_obj_add_flag(s_pickup_scroll, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scrollbar_mode(s_pickup_scroll, LV_SCROLLBAR_MODE_AUTO);

        for (uint32_t i = 0U; i < PICKUP_JSON_MAX_ITEMS; i++)
        {
            lv_obj_t * p_row = lv_obj_create(s_pickup_scroll);
            lv_obj_set_pos(p_row, 0, (lv_coord_t) i * 32);
            lv_obj_set_size(p_row, 580, 30);
            lv_obj_set_style_bg_opa(p_row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_border_width(p_row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_pad_all(p_row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_radius(p_row, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

            lv_obj_t * p_name = lv_label_create(p_row);
            lv_obj_set_pos(p_name, 24, 6);
            lv_obj_set_style_text_color(p_name, lv_color_hex(0x10233f), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(p_name, &lv_font_SourceHanSerifSC_17, LV_PART_MAIN | LV_STATE_DEFAULT);

            lv_obj_t * p_qty = lv_label_create(p_row);
            lv_obj_set_pos(p_qty, 318, 6);
            lv_obj_set_style_text_color(p_qty, lv_color_hex(0x10233f), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(p_qty, &lv_font_SourceHanSerifSC_17, LV_PART_MAIN | LV_STATE_DEFAULT);

            lv_obj_t * p_pos = lv_label_create(p_row);
            lv_obj_set_pos(p_pos, 400, 6);
            lv_obj_set_style_text_color(p_pos, lv_color_hex(0x10233f), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(p_pos, &lv_font_SourceHanSerifSC_17, LV_PART_MAIN | LV_STATE_DEFAULT);

            lv_obj_t * p_state = lv_label_create(p_row);
            lv_obj_set_pos(p_state, 504, 6);
            lv_obj_set_style_text_color(p_state, lv_color_hex(0xf59e0b), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(p_state, &lv_font_SourceHanSerifSC_17, LV_PART_MAIN | LV_STATE_DEFAULT);

            s_pickup_row_objs[i] = p_row;
            s_pickup_row_name[i] = p_name;
            s_pickup_row_qty[i] = p_qty;
            s_pickup_row_pos[i] = p_pos;
            s_pickup_row_state[i] = p_state;
            lv_obj_add_flag(p_row, LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* 未扫码：隐藏全部动态行，合计占位 */
    if (!(s_pickup_order_scanned && (s_pickup_order_number[0] != '\0')))
    {
        for (uint32_t i = 0U; i < PICKUP_JSON_MAX_ITEMS; i++)
        {
            if ((NULL != s_pickup_row_objs[i]) && lv_obj_is_valid(s_pickup_row_objs[i]))
            {
                lv_obj_add_flag(s_pickup_row_objs[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
        lv_obj_t * p_total = guider_ui.Pickup.order_card_order_total;
        if ((NULL != p_total) && lv_obj_is_valid(p_total))
        {
            gui_app_label_set_if_changed(p_total, "--");
        }
        return;
    }

    /* 已扫码：按解析的取药单药品填充（可 >3 种，滚动查看） */
    for (uint32_t i = 0U; i < PICKUP_JSON_MAX_ITEMS; i++)
    {
        lv_obj_t * p_row = s_pickup_row_objs[i];
        if ((NULL == p_row) || !lv_obj_is_valid(p_row))
        {
            continue;
        }
        if (i < s_pickup_item_count)
        {
            lv_obj_remove_flag(p_row, LV_OBJ_FLAG_HIDDEN);
            const drug_db_entry_t * p_drug = drug_db_lookup(s_pickup_items[i]);
            if ((NULL != s_pickup_row_name[i]) && lv_obj_is_valid(s_pickup_row_name[i]))
            {
                gui_app_label_set_if_changed(s_pickup_row_name[i],
                                             (p_drug != NULL) ? p_drug->name : s_pickup_items[i]);
            }
            if ((NULL != s_pickup_row_qty[i]) && lv_obj_is_valid(s_pickup_row_qty[i]))
            {
                gui_app_label_set_fmt_if_changed(s_pickup_row_qty[i], "%lu盒",
                                                 (unsigned long) s_pickup_qty[i]);
            }
            if ((NULL != s_pickup_row_pos[i]) && lv_obj_is_valid(s_pickup_row_pos[i]))
            {
                gui_app_label_set_if_changed(s_pickup_row_pos[i],
                                             (p_drug != NULL) ? p_drug->position : "--");
            }
            if ((NULL != s_pickup_row_state[i]) && lv_obj_is_valid(s_pickup_row_state[i]))
            {
                /* 取药流程进度：当前行"取药中"、已取行"已取"、未取行"待取" */
                uint32_t r_idx = 0U, r_cnt = 0U;
                PickupTest_GetProgress(&r_idx, &r_cnt);
                bool running = PickupTest_IsRunning();
                if (s_pickup_dispensed)
                {
                    gui_app_label_set_if_changed(s_pickup_row_state[i], "已取");
                    lv_obj_set_style_text_color(s_pickup_row_state[i], lv_color_hex(0x14a66a),
                                                LV_PART_MAIN | LV_STATE_DEFAULT);
                }
                else if (running && (i < r_idx))
                {
                    gui_app_label_set_if_changed(s_pickup_row_state[i], "已取");
                    lv_obj_set_style_text_color(s_pickup_row_state[i], lv_color_hex(0x14a66a),
                                                LV_PART_MAIN | LV_STATE_DEFAULT);
                }
                else if (running && (i == r_idx))
                {
                    gui_app_label_set_if_changed(s_pickup_row_state[i], "取药中");
                    lv_obj_set_style_text_color(s_pickup_row_state[i], lv_color_hex(0x0891b2),
                                                LV_PART_MAIN | LV_STATE_DEFAULT);
                }
                else
                {
                    gui_app_label_set_if_changed(s_pickup_row_state[i], "待取");
                    lv_obj_set_style_text_color(s_pickup_row_state[i], lv_color_hex(0xf59e0b),
                                                LV_PART_MAIN | LV_STATE_DEFAULT);
                }
            }
        }
        else
        {
            lv_obj_add_flag(p_row, LV_OBJ_FLAG_HIDDEN);
        }
    }

    /* 合计：单内药品种类 / 总盒数 */
    {
        uint32_t total = 0U;
        for (uint32_t i = 0U; i < s_pickup_item_count; i++)
        {
            total += s_pickup_qty[i];
        }
        lv_obj_t * p_total = guider_ui.Pickup.order_card_order_total;
        if ((NULL != p_total) && lv_obj_is_valid(p_total))
        {
            gui_app_label_set_fmt_if_changed(p_total, "%lu种 / %lu盒",
                                             (unsigned long) s_pickup_item_count,
                                             (unsigned long) total);
        }
    }
}

/***********************************************************************************************************************
 * tiny_ttf 运行时字体服务：
 *  - OSPI Flash 就绪后创建多字号运行时字体（数据 = OSPI 内存映射区，零拷贝）
 *  - 遍历各 screen 的 label，按原字号匹配替换为 tiny 字体（编译字库仅 164 中文，
 *    大量运行态文本缺字；tiny_ttf 覆盖完整 GB2312）
 *  - Device/Home 页挂演示 label 验证生僻字渲染
 **********************************************************************************************************************/

/* 按原字体行高匹配最接近的 tiny 字号字体 */
static const lv_font_t * tiny_font_match(const lv_font_t * p_orig)
{
    if (NULL == g_tiny_fonts[0])
    {
        return p_orig;
    }
    if (NULL == p_orig)
    {
        return g_tiny_fonts[2];
    }
    int const orig_lh = (int) lv_font_get_line_height(p_orig);
    int best = 0;
    int best_d = 0x7FFFFFFF;
    for (int i = 0; i < 6; i++)
    {
        if (NULL == g_tiny_fonts[i])
        {
            continue;
        }
        int const d = (int) lv_font_get_line_height(g_tiny_fonts[i]) - orig_lh;
        int const ad = (d < 0) ? -d : d;
        if (ad < best_d)
        {
            best_d = ad;
            best = i;
        }
    }
    return g_tiny_fonts[best];
}

/* 递归替换子对象树的 label 字体（黑体 tiny_ttf，笔画已够粗无需描边） */
static void tiny_font_apply_tree(lv_obj_t * p_obj)
{
    if ((NULL == p_obj) || !lv_obj_is_valid(p_obj))
    {
        return;
    }
    uint32_t const cnt = lv_obj_get_child_count(p_obj);
    for (uint32_t i = 0U; i < cnt; i++)
    {
        lv_obj_t * p_child = lv_obj_get_child(p_obj, i);
        if ((NULL == p_child) || !lv_obj_is_valid(p_child))
        {
            continue;
        }
        if (lv_obj_check_type(p_child, &lv_label_class))
        {
            const lv_font_t * p_f = lv_obj_get_style_text_font(p_child, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_font(p_child, tiny_font_match(p_f), LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        tiny_font_apply_tree(p_child);
    }
}

/* screen 集合（懒创建：出现后一次性应用 tiny 字体） */
typedef struct
{
    lv_obj_t ** pp_screen;
    bool     * p_applied;
} tiny_screen_item_t;

static void gui_app_apply_tiny_to_screens(void)
{
    if (ospi_ttf_loader_burn_mode())
    {
        return;
    }
    if (NULL == g_tiny_fonts[0])
    {
        return;
    }
    lv_obj_t * screens[] = {
        guider_ui.Boot.screen, guider_ui.Home.screen, guider_ui.Pickup.screen,
        guider_ui.Scan.screen, guider_ui.Medicine.screen, guider_ui.Login.screen,
        guider_ui.Admin.screen, guider_ui.Store.screen, guider_ui.Logs.screen,
        guider_ui.Device.screen,
    };
    static bool applied[10];
    for (int i = 0; i < 10; i++)
    {
        if (applied[i])
        {
            continue;
        }
        if ((NULL == screens[i]) || !lv_obj_is_valid(screens[i]))
        {
            continue;
        }
        tiny_font_apply_tree(screens[i]);
        applied[i] = true;
    }
}

static void gui_app_tiny_font_service(void)
{
    /* 烧录模式（loader RESET/DONE 或 PC 直写 s_ttf_burn_mode）：跳过字体创建。
     * 残缺字体数据（烧录中断）会让 tiny_ttf 创建出坏字体 → 渲染触发 stbtt 断言
     * → HardFault → loader 停摆 → 烧录永远停滞。 */
    if (ospi_ttf_loader_burn_mode())
    {
        return;
    }
    if (NULL == g_tiny_font)
    {
        if (ospi_storage_get_ready())
        {
            /* 失败节流：创建失败时 2s 内不重试（避免每 5ms 刷屏日志） */
            static uint32_t s_next_attempt_ms;
            uint32_t const now_ms = lv_tick_get();
            if (now_ms < s_next_attempt_ms)
            {
                return;
            }

            lv_mem_monitor_t mon;
            lv_mem_monitor(&mon);

            /* 诊断：OSPI 映射区 TTF 头部（magic/tables/tag0） */
            {
                const uint8_t * p = ospi_storage_mmap_base();
                uint32_t magic = ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) |
                                 ((uint32_t) p[2] << 8) | (uint32_t) p[3];
                uint32_t num_tables = ((uint32_t) p[4] << 8) | (uint32_t) p[5];
                sys_log_add(SYS_LOG_INFO, "TTF head: magic=%08X tables=%lu",
                            (unsigned) magic, (unsigned long) num_tables);
            }

            /* 数据完整性校验：magic 必须为 TrueType(0x00010000)、表数合理、
             * 字体区末尾不得为 0xFF（烧录中断时末尾是擦除态 0xFF）。
             * 校验失败视为"字体未烧完"，跳过创建，避免坏字体渲染崩溃。 */
            {
                const uint8_t * p = ospi_storage_mmap_base();
                uint32_t magic = ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) |
                                 ((uint32_t) p[2] << 8) | (uint32_t) p[3];
                uint32_t num_tables = ((uint32_t) p[4] << 8) | (uint32_t) p[5];
                uint8_t last_byte = p[TINY_TTF_SIZE - 1U];
                if ((0x00010000U != magic) || (num_tables == 0U) || (num_tables > 64U) ||
                    (0xFFU == last_byte))
                {
                    sys_log_add(SYS_LOG_ERR, "TTF 数据不完整(magic=%08X tbls=%lu last=%02X)，跳过字体",
                                (unsigned) magic, (unsigned long) num_tables, (unsigned) last_byte);
                    s_next_attempt_ms = now_ms + 2000U;
                    return;
                }
            }

            /* 创建 6 个字号 tiny 字体（15/17/18/22/24/28px），cache=64 */
            for (int i = 0; i < 6; i++)
            {
                g_tiny_fonts[i] = lv_tiny_ttf_create_data_ex(ospi_storage_mmap_base(), TINY_TTF_SIZE,
                                                             s_tiny_sizes[i], LV_FONT_KERNING_NONE, 64);
            }
            g_tiny_font = g_tiny_fonts[2];   /* 18px 别名（demo/日志等） */
            /* J-Link 经 SWD 直读内存绕过 D-Cache，发布变量前需 clean */
            SCB_CleanDCache_by_Addr((uint32_t *) &g_tiny_font, (int32_t) sizeof(g_tiny_font));
            SCB_CleanDCache_by_Addr((uint32_t *) &g_tiny_fonts, (int32_t) sizeof(g_tiny_fonts));
            if (NULL != g_tiny_font)
            {
                sys_log_add(SYS_LOG_OK, "tiny_ttf 运行时字体创建成功 (15~28px, OSPI 映射)");
            }
            else
            {
                sys_log_add(SYS_LOG_ERR, "tiny_ttf 字体创建失败 (LVGL free=%luB frag=%u%%)",
                            (unsigned long) mon.free_size, (unsigned) mon.frag_pct);
                s_next_attempt_ms = now_ms + 2000U;
            }
        }
    }
}

/***********************************************************************************************************************
 * Store 存药页：药品信息由 drug_db + 最近扫码填充（识别 → 存药流程）；
 * 云端 STORAGE_PLACE 任务进行中/刚完成（10s 内）时优先显示任务内容
 * （药品名/目标仓/机械臂与仓位状态），保证"网页下发 → 设备执行"闭环在
 * 设备端界面可见、逻辑不打架。
 **********************************************************************************************************************/
static void gui_app_refresh_store_page(void)
{
    static uint32_t s_last_ms;
    if (lv_screen_active() != guider_ui.Store.screen)
    {
        return;
    }
    if (!gui_app_status_throttle(&s_last_ms))
    {
        return;
    }

    /* 云端存药任务快照（esp01s_proto 层，Network 线程更新） */
    esp01s_place_info_t pinfo;
    esp01s_proto_get_place_info(&pinfo);
    bool const task_active = pinfo.active;
    bool const recent_result = (!task_active) && (pinfo.last_result != 0U) &&
                               ((lv_tick_get() - pinfo.last_tick) < 10000U);
    bool const show_task = task_active || recent_result;
    const char * p_task_drug = (pinfo.drug_name[0] != '\0') ? pinfo.drug_name : pinfo.drug_id;

    const drug_db_entry_t * p_drug = s_scan_has_result ? drug_db_lookup(s_last_scan_text) : NULL;

    /* 药品名：云端任务优先，否则本地扫码 */
    lv_obj_t * p_name = guider_ui.Store.drug_card_drug_name;
    if ((NULL != p_name) && lv_obj_is_valid(p_name))
    {
        if (show_task && (p_task_drug[0] != '\0'))
        {
            lv_label_set_long_mode(p_name, LV_LABEL_LONG_CLIP);
            lv_obj_set_width(p_name, 360);
            gui_app_label_set_if_changed(p_name, p_task_drug);
        }
        else if (p_drug != NULL)
        {
            gui_app_label_set_if_changed(p_name, p_drug->name);
        }
        else if (s_scan_has_result)
        {
            /* 药品商品条码（EAN-13）：drug_db 未收录时直接显示 13 位序列号，
             * 便于药师核对药盒印刷码 */
            lv_label_set_long_mode(p_name, LV_LABEL_LONG_CLIP);
            lv_obj_set_width(p_name, 360);
            gui_app_label_set_if_changed(p_name, s_last_scan_text);
        }
        else
        {
            gui_app_label_set_if_changed(p_name, "等待扫码…");
        }
    }

    /* 条码序列号（存药页专用）：识别到 EAN-13 时在 meta 区显示"序列号 xxxxx"；
     * 云端任务显示任务号 PLACE-xxx，方便与网页任务对应 */
    lv_obj_t * p_serial = guider_ui.Store.drug_card_drug_meta;
    if ((NULL != p_serial) && lv_obj_is_valid(p_serial))
    {
        if (show_task && (pinfo.task_id[0] != '\0'))
        {
            gui_app_label_set_fmt_if_changed(p_serial, "任务 %s  ·  %s",
                                             pinfo.task_id, pinfo.coord);
        }
        else if (s_scan_has_result && (p_drug != NULL))
        {
            gui_app_label_set_fmt_if_changed(p_serial, "序列号 %s  ·  %s/%s",
                                             s_last_scan_text,
                                             p_drug->batch, p_drug->expiry);
        }
        else if (s_scan_has_result)
        {
            gui_app_label_set_fmt_if_changed(p_serial, "序列号 %s", s_last_scan_text);
        }
        else
        {
            gui_app_label_set_if_changed(p_serial, "批次 --  ·  有效期 --");
        }
    }

    /* 目标药仓：云端任务 → coord；本地扫码 → drug_db 仓位 */
    lv_obj_t * p_pos = guider_ui.Store.parameter_card_position;
    if ((NULL != p_pos) && lv_obj_is_valid(p_pos))
    {
        if (show_task && (pinfo.coord[0] != '\0'))
        {
            gui_app_label_set_if_changed(p_pos, pinfo.coord);
        }
        else
        {
            gui_app_label_set_if_changed(p_pos, (p_drug != NULL) ? p_drug->position : "--");
        }
    }

    /* 核验徽章 */
    lv_obj_t * p_badge = guider_ui.Store.verified_badge_verified_text;
    if ((NULL != p_badge) && lv_obj_is_valid(p_badge))
    {
        if (s_scan_has_result)
        {
            gui_app_label_set_if_changed(p_badge, "药品已核验");
            lv_obj_set_style_text_color(p_badge, lv_color_hex(0x14a66a), LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        else
        {
            gui_app_label_set_if_changed(p_badge, "待扫码");
            lv_obj_set_style_text_color(p_badge, lv_color_hex(0x687b99), LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }

    /* 仓位容量：显示库存台账真实数量（如 "8 / 30 盒"） */
    lv_obj_t * p_cap = guider_ui.Store.warehouse_card_capacity;
    if ((NULL != p_cap) && lv_obj_is_valid(p_cap))
    {
        if (p_drug != NULL)
        {
            gui_app_label_set_fmt_if_changed(p_cap, "%lu / %lu 盒",
                                             (unsigned long) inventory_get(p_drug->code),
                                             (unsigned long) inventory_capacity(p_drug->code));
        }
        else
        {
            gui_app_label_set_if_changed(p_cap, "--");
        }
    }

    /* 入库数量：显示可入库余量（容量 - 当前库存），替代生成示例的假数量 */
    lv_obj_t * p_qty = guider_ui.Store.parameter_card_quantity;
    if ((NULL != p_qty) && lv_obj_is_valid(p_qty))
    {
        if (p_drug != NULL)
        {
            uint32_t const cur = inventory_get(p_drug->code);
            uint32_t const cap = inventory_capacity(p_drug->code);
            uint32_t const avail = (cap > cur) ? (cap - cur) : 0U;
            gui_app_label_set_fmt_if_changed(p_qty, "余 %lu 盒", (unsigned long) avail);
        }
        else
        {
            gui_app_label_set_if_changed(p_qty, "--");
        }
    }

    /* 操作药师：登录会话状态（已登录 / 未登录） */
    lv_obj_t * p_operator = guider_ui.Store.parameter_card_operator;
    if ((NULL != p_operator) && lv_obj_is_valid(p_operator))
    {
        gui_app_label_set_if_changed(p_operator,
                                     gui_get_authenticated() ? "已登录" : "未登录");
    }

    /* 机械臂状态：云端任务执行中/刚完成（10s 内）优先，否则待命 */
    lv_obj_t * p_motor = guider_ui.Store.warehouse_card_motor_state;
    if ((NULL != p_motor) && lv_obj_is_valid(p_motor))
    {
        if (task_active)
        {
            gui_app_label_set_if_changed(p_motor, "执行中");
            lv_obj_set_style_text_color(p_motor, lv_color_hex(0xf59e0b), LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        else if (recent_result)
        {
            gui_app_label_set_if_changed(p_motor, (pinfo.last_result == 1U) ? "已完成" : "失败");
            lv_obj_set_style_text_color(p_motor,
                                        lv_color_hex((pinfo.last_result == 1U) ? 0x14a66a : 0xef5350),
                                        LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        else
        {
            gui_app_label_set_if_changed(p_motor, "待命");
            lv_obj_set_style_text_color(p_motor, lv_color_hex(0x14a66a), LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }

    /* 仓位状态：云端任务占用中/已入库/失败，否则可用 */
    lv_obj_t * p_slot = guider_ui.Store.warehouse_card_slot_state;
    if ((NULL != p_slot) && lv_obj_is_valid(p_slot))
    {
        if (task_active)
        {
            gui_app_label_set_if_changed(p_slot, "占用中");
            lv_obj_set_style_text_color(p_slot, lv_color_hex(0xf59e0b), LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        else if (recent_result)
        {
            gui_app_label_set_if_changed(p_slot, (pinfo.last_result == 1U) ? "已入库" : "失败");
            lv_obj_set_style_text_color(p_slot,
                                        lv_color_hex((pinfo.last_result == 1U) ? 0x14a66a : 0xef5350),
                                        LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        else
        {
            gui_app_label_set_if_changed(p_slot, "可用");
            lv_obj_set_style_text_color(p_slot, lv_color_hex(0x14a66a), LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }

    /* 安全提示：云端任务执行中/结果提示，否则默认文案 */
    lv_obj_t * p_note = guider_ui.Store.safety_note_safety_text;
    if ((NULL != p_note) && lv_obj_is_valid(p_note))
    {
        if (task_active)
        {
            gui_app_label_set_if_changed(p_note, "机械臂正在执行存药…");
        }
        else if (recent_result)
        {
            gui_app_label_set_if_changed(p_note,
                                         (pinfo.last_result == 1U) ? "药品已入库，云端核销任务" : "存药失败，请检查设备");
        }
        else
        {
            gui_app_label_set_if_changed(p_note, "确认后机械臂将执行入库");
        }
    }

    /* 确认按钮：云端任务执行中禁用（防与机械臂动作冲突），结束后恢复 */
    lv_obj_t * p_confirm = guider_ui.Store.parameter_card_button_confirm;
    lv_obj_t * p_btn_text = guider_ui.Store.button_confirm_button_confirm_text;
    if ((NULL != p_confirm) && lv_obj_is_valid(p_confirm))
    {
        if (task_active)
        {
            lv_obj_add_state(p_confirm, LV_STATE_DISABLED);
            if ((NULL != p_btn_text) && lv_obj_is_valid(p_btn_text))
            {
                gui_app_label_set_if_changed(p_btn_text, "存药执行中");
            }
        }
        else
        {
            lv_obj_remove_state(p_confirm, LV_STATE_DISABLED);
            /* 仅当按钮文字停在"存药执行中"时恢复，避免覆盖"已确认/确认失败"反馈 */
            if ((NULL != p_btn_text) && lv_obj_is_valid(p_btn_text) &&
                (0 == strcmp(lv_label_get_text(p_btn_text), "存药执行中")))
            {
                gui_app_label_set_if_changed(p_btn_text, "确认存药");
                lv_obj_set_style_text_color(p_btn_text, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
            }
        }
    }
}

/***********************************************************************************************************************
 * Store 页按钮：确认存药 → 写日志（机械臂动作待接入）；重新识别 → 跳 Scan
 **********************************************************************************************************************/
static void store_confirm_click_hook(lv_event_t * e)
{
    (void) e;

    /* 云端 STORAGE_PLACE 任务执行中：按钮已禁用，双保险拦截（防与机械臂
     * 动作冲突——手动确认只做库存台账，不驱动机械臂） */
    esp01s_place_info_t pinfo;
    esp01s_proto_get_place_info(&pinfo);
    if (pinfo.active)
    {
        sys_log_add(SYS_LOG_WARN, "存药确认忽略: 云端存药任务执行中 (%s)", pinfo.task_id);
        return;
    }

    const drug_db_entry_t * p_drug = s_scan_has_result ? drug_db_lookup(s_last_scan_text) : NULL;
    bool const ok = (p_drug != NULL);
    if (ok)
    {
        uint32_t const after = inventory_add(p_drug->code, 1U);
        sys_log_add(SYS_LOG_OK, "存药确认: %s -> 仓位 %s (库存 %lu/%lu)",
                    p_drug->name, p_drug->position,
                    (unsigned long) after,
                    (unsigned long) inventory_capacity(p_drug->code));
    }
    else if (s_scan_has_result)
    {
        sys_log_add(SYS_LOG_OK, "存药确认: %s（未入库表，仅记录）", s_last_scan_text);
    }
    else
    {
        sys_log_add(SYS_LOG_WARN, "存药确认失败: 未识别药品");
    }

    /* 操作反馈：按钮文字与颜色变化（内容变化才更新，防重复重绘） */
    lv_obj_t * p_btn_text = guider_ui.Store.button_confirm_button_confirm_text;
    if ((NULL != p_btn_text) && lv_obj_is_valid(p_btn_text))
    {
        gui_app_label_set_if_changed(p_btn_text, ok ? "已确认" : "确认失败");
        lv_obj_set_style_text_color(p_btn_text,
                                    lv_color_hex(ok ? 0x14a66a : 0xef5350),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

static void gui_app_install_store_hooks(void)
{
    if (s_store_hooks_hooked)
    {
        return;
    }

    /* 注：Store"重新识别"按钮 → Scan 的导航已移到 custom.c（install_store_scan_hook），
     * 使模拟器（不跑 gui_app_poll）同样可点击进入扫码页。这里只保留确认存药。 */
    bool hooked = false;
    lv_obj_t * p_confirm = guider_ui.Store.parameter_card_button_confirm;
    if ((NULL != p_confirm) && lv_obj_is_valid(p_confirm))
    {
        lv_obj_add_event_cb(p_confirm, store_confirm_click_hook, LV_EVENT_CLICKED, NULL);
        hooked = true;
    }
    if (hooked)
    {
        s_store_hooks_hooked = true;
    }
}

/***********************************************************************************************************************
 * Boot 启动页：状态文本真实化（启动完成 → 触摸/摄像头/LVGL 摘要）
 **********************************************************************************************************************/
static void gui_app_refresh_boot_status(void)
{
    static uint32_t s_last_ms;
    if (lv_screen_active() != guider_ui.Boot.screen)
    {
        return;
    }
    if (!gui_app_status_throttle(&s_last_ms))
    {
        return;
    }

    lv_obj_t * p_status = guider_ui.Boot.status;
    if ((NULL == p_status) || !lv_obj_is_valid(p_status))
    {
        return;
    }

    /* 启动已完成（本函数在 GUI 就绪后才运行），显示真实状态摘要 */
    char text[64];
    uint32_t n = (uint32_t) snprintf(text, sizeof(text), "启动完成 · 触摸屏%s",
                                     (g_cst816s_probe_ok != 0U) ? "已就绪" : "异常");
    if (camera_app_capture_active())
    {
        n += (uint32_t) snprintf(text + n, (size_t) (sizeof(text) - n), " · 摄像头采集");
    }
    gui_app_label_set_if_changed(p_status, text);

    /* 进度条：全满 */
    lv_obj_t * p_fill = guider_ui.Boot.progress_track_progress_fill;
    if ((NULL != p_fill) && lv_obj_is_valid(p_fill))
    {
        lv_obj_set_width(p_fill, lv_pct(100));
    }
}

/***********************************************************************************************************************
 * Logs 页"日志详情"按钮 → 清空日志（点击后计数归零并留一条提示）
 **********************************************************************************************************************/
static void logs_clear_button_click_hook(lv_event_t * e)
{
    (void) e;

    sys_log_clear();
    sys_log_add(SYS_LOG_INFO, "日志已清空");
}

static void gui_app_install_logs_hook(void)
{
    if (s_logs_hook_hooked)
    {
        return;
    }
    lv_obj_t * p_btn = guider_ui.Logs.log_table_card_button_export;
    if ((NULL == p_btn) || !lv_obj_is_valid(p_btn))
    {
        return;
    }
    lv_obj_add_event_cb(p_btn, logs_clear_button_click_hook, LV_EVENT_CLICKED, NULL);

    /* 按钮文案"日志详情"改为"清空日志"（运行时覆盖生成文本） */
    lv_obj_t * p_text = guider_ui.Logs.button_export_button_export_text;
    if ((NULL != p_text) && lv_obj_is_valid(p_text))
    {
        lv_label_set_text(p_text, "清空日志");
    }
    s_logs_hook_hooked = true;
}

/***********************************************************************************************************************
 * Pickup 页"开始取药"：启动取药单多药取药流程（真实机械臂动作，非阻塞状态机）
 * 流程：使能→回零→逐药 [XY→该药仓位→Z伸→夹爪合→Z收→XY→取药区→Z伸放下→夹爪张→Z收] → 完成
 * 硬约束：一种药放到取药区并放下收回 Z 之后，才移动去下一个药柜。
 **********************************************************************************************************************/
static void pickup_start_click_hook(lv_event_t * e)
{
    (void) e;

    if (s_pickup_dispensed)
    {
        pickup_log_add("取药重复执行，已忽略");
        return;
    }

    /* 未在取药页扫码识别前不允许执行取药（任务流程约束） */
    if (!s_pickup_order_scanned || (s_pickup_order_number[0] == '\0'))
    {
        pickup_log_add("请先扫描取药单");
        return;
    }
    if (0U == s_pickup_item_count)
    {
        pickup_log_add("取药单无药品项");
        return;
    }
    if (PickupTest_IsRunning())
    {
        pickup_log_add("取药流程进行中，请等待");
        return;
    }

    /* 启动多药取药：把本单药品 code 列表交给取药状态机（每种取 1 盒）。
     * 新格式二维码自带 layer/x 坐标 → 显式坐标模式（不依赖本地 drug_db，
     * 手册 §6：layer/x 供机械定位）；旧格式无坐标 → drug_db 定位。 */
    if (s_pickup_has_xy)
    {
        /* 新格式码自带 items[].w（药品宽度）→ 夹爪按宽度自动闭合 */
        PickupTest_StartOrderXY((const char (*)[32]) s_pickup_items,
                                s_pickup_x_mm, s_pickup_y_mm,
                                s_pickup_has_w ? s_pickup_w_mm : NULL,
                                s_pickup_item_count);
    }
    else
    {
        PickupTest_StartOrder((const char (*)[32]) s_pickup_items, s_pickup_item_count);
    }
    pickup_log_add("取药单开始：共 %lu 种药", (unsigned long) s_pickup_item_count);

    /* 操作反馈：按钮文字"开始取药"→"取药中…" */
    lv_obj_t * p_btn_text = guider_ui.Pickup.button_start_button_start_text;
    if ((NULL != p_btn_text) && lv_obj_is_valid(p_btn_text))
    {
        gui_app_label_set_if_changed(p_btn_text, "取药中…");
        lv_obj_set_style_text_color(p_btn_text, lv_color_hex(0xf59e0b),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

static void gui_app_install_pickup_hook(void)
{
    if (s_pickup_hook_hooked)
    {
        return;
    }
    lv_obj_t * p_btn = guider_ui.Pickup.medicine_list_card_button_start;
    if ((NULL == p_btn) || !lv_obj_is_valid(p_btn))
    {
        return;
    }
    lv_obj_add_event_cb(p_btn, pickup_start_click_hook, LV_EVENT_CLICKED, NULL);
    s_pickup_hook_hooked = true;
}

/***********************************************************************************************************************
 * Pickup 页"重新扫描"：重置取药单 UI + 解码状态（允许重新扫同一单）
 **********************************************************************************************************************/
static void pickup_rescan_click_hook(lv_event_t * e)
{
    (void) e;

    s_pickup_dispensed = false;
    qr_decoder_reset();
    pickup_log_add("取药单重新扫描");

    /* 复位本单状态为"待扫描"（标题/徽章/单号/按钮） */
    gui_app_pickup_reset_order_card();
}

static void gui_app_install_pickup_rescan_hook(void)
{
    if (s_pickup_rescan_hook_hooked)
    {
        return;
    }
    lv_obj_t * p_btn = guider_ui.Pickup.order_card_button_scan_order;
    if ((NULL == p_btn) || !lv_obj_is_valid(p_btn))
    {
        return;
    }
    lv_obj_add_event_cb(p_btn, pickup_rescan_click_hook, LV_EVENT_CLICKED, NULL);
    s_pickup_rescan_hook_hooked = true;
}

/***********************************************************************************************************************
 * 取药流程轮询（gui_app_poll 内周期调用）：
 *  - 多药取药状态机完成（DONE_OK）→ 扣库存 + 按钮"取药完成" + 日志；
 *  - 失败（DONE_FAIL）→ 按钮恢复"开始取药" + 日志。
 *  - 云端协议（手册 §7）：每取到一药即上报 PICKUP_SCANNED（核销），
 *    全部完成后补报剩余项；扫码瞬间不再上报。
 **********************************************************************************************************************/
static void gui_app_poll_pickup_robot(void)
{
    if (s_pickup_dispensed)
    {
        return;
    }
    /* 云端出药（DISPENSE_ACTION）/存药（STORAGE_PLACE）模式：状态机完成态
     * 由协议层（esp01s_proto_service）回报并复位，本函数只服务取药单/测试
     * 流程——云端任务期间直接让位，避免误把 DONE 当取药消费（重复扣库存/
     * 误发 PICKUP_SCANNED/漏发 ACTION_FINISHED/PLACE_FINISHED）。 */
    if (PickupTest_IsCloudMode() || PickupTest_IsPlaceMode())
    {
        return;
    }
    pickup_test_state_t pst;
    const char * ptext = NULL;
    PickupTest_GetStatus(&pst, &ptext);
    lv_obj_t * p_btn_text = guider_ui.Pickup.button_start_button_start_text;

    if (pst == PICKUP_TEST_RUNNING)
    {
        /* 进度推进：当前处理 index 已从 n 变为 n+1 → 第 n 味药已取放完成，
         * 上报其 drugId 核销（仅限取药单模式；云端出药模式 s_order_count=0）。 */
        uint32_t idx = 0U, cnt = 0U;
        PickupTest_GetProgress(&idx, &cnt);
        if ((cnt > 0U) && (idx > s_pickup_reported_count) && (idx <= cnt))
        {
            while (s_pickup_reported_count < idx)
            {
                esp01s_proto_send_pickup_scanned(s_pickup_items[s_pickup_reported_count]);
                s_pickup_reported_count++;
            }
        }
        return;
    }

    if (pst == PICKUP_TEST_DONE_OK)
    {
        /* 每种药出库 1 盒（每种只取 1 盒，用户确认） */
        uint32_t total_removed = 0U;
        for (uint32_t i = 0U; i < s_pickup_item_count; i++)
        {
            uint32_t const before = inventory_get(s_pickup_items[i]);
            uint32_t const after = inventory_remove(s_pickup_items[i], 1U);
            total_removed += (before > after) ? (before - after) : 0U;
            const drug_db_entry_t * p_drug = drug_db_lookup(s_pickup_items[i]);
            pickup_log_add("%s: %lu -> %lu 盒", (p_drug != NULL) ? p_drug->name : s_pickup_items[i],
                           (unsigned long) before, (unsigned long) after);
        }
        s_pickup_dispensed = true;

        /* 全部取放完成：补报剩余未核销药品（手册 §7：取到药后上报） */
        while (s_pickup_reported_count < s_pickup_item_count)
        {
            esp01s_proto_send_pickup_scanned(s_pickup_items[s_pickup_reported_count]);
            s_pickup_reported_count++;
        }

        pickup_log_add("取药单完成：%lu 种药已放到取药区，共出库 %lu 盒",
                       (unsigned long) s_pickup_item_count, (unsigned long) total_removed);

        /* 完成态已消费：复位状态机（避免 DONE_OK 被下次轮询重复执行，
         * 把按钮/行状态反复改写）。按钮恢复"开始取药"待下一单。 */
        PickupTest_Reset();
        if ((NULL != p_btn_text) && lv_obj_is_valid(p_btn_text))
        {
            gui_app_label_set_if_changed(p_btn_text, "开始取药");
            lv_obj_set_style_text_color(p_btn_text, lv_color_hex(0xffffff),
                                        LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }
    else if (pst == PICKUP_TEST_DONE_FAIL)
    {
        /* 失败：已取到的药仍核销（已完成部分），未取的不报 */
        uint32_t idx = 0U, cnt = 0U;
        PickupTest_GetProgress(&idx, &cnt);
        if ((cnt > 0U) && (idx > s_pickup_reported_count))
        {
            while ((s_pickup_reported_count < idx) && (s_pickup_reported_count < s_pickup_item_count))
            {
                esp01s_proto_send_pickup_scanned(s_pickup_items[s_pickup_reported_count]);
                s_pickup_reported_count++;
            }
        }
        pickup_log_add("取药失败：%s", (ptext != NULL) ? ptext : "未知错误");
        /* 失败态已消费：复位状态机，按钮恢复"开始取药" */
        PickupTest_Reset();
        if ((NULL != p_btn_text) && lv_obj_is_valid(p_btn_text))
        {
            gui_app_label_set_if_changed(p_btn_text, "开始取药");
            lv_obj_set_style_text_color(p_btn_text, lv_color_hex(0xffffff),
                                        LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }
}

#else

bool gui_app_init(void)
{
    return false;
}

void gui_app_poll(void)
{
}

#endif
