#include "LVGL_thread.h"
#include "rm_lvgl_port.h"
#include "lvgl.h"
#include "hal_data.h"
#include "r_glcdc.h"

#include "camera_preview.h"
#include "camera_convert.h"
#include "gui_app.h"
#include "st7701s_panel.h"
#include "cst816s_touch.h"
#include "sys_log.h"

/* ==========================================================================
 * MIPI 屏显开关
 *   CAMERA_PREVIEW_SCREEN_ENABLE = 1 : 初始化 MIPI 屏，摄像头画面显示在屏上
 *   CAMERA_PREVIEW_SCREEN_ENABLE = 0 : 禁用屏显（仅用 USB 在 PC 上预览）
 * 修改后需重新编译。
 * ========================================================================== */
#ifndef CAMERA_PREVIEW_SCREEN_ENABLE
#define CAMERA_PREVIEW_SCREEN_ENABLE   (1U)
#endif

/* LVGL 初始化完成标志（供 Camera 线程查询是否可填屏） */
volatile bool s_lvgl_ready = false;
volatile uint32_t g_lvgl_port_error;
volatile uint32_t g_gui_init_error;
volatile uint32_t g_lvgl_flush_count;
volatile uint32_t g_lvgl_last_flush_error;
volatile uint32_t g_lvgl_last_scanout_index;
/* Runtime display state for J-Link diagnosis: logical W/H, physical W/H,
 * rotation, matrix-rotation flag. */
volatile uint32_t g_lvgl_display_diag[6];
/*
 * MIPI bring-up snapshot.  The array is intentionally kept in RAM so it can
 * be read with J-Link even when the display task stops at the FSP error path.
 * [0] stage, [1] LINKSR, [2] ISR, [3] TXSETR, [4] HSCLKSETR, [5] PLSR,
 * [6] SQCH0SR, [7] SQCH1SR, [8] VMSR, [9] FERRSR, [10] RXSR.
 */
volatile uint32_t g_mipi_dsi_diag[11];
volatile uint32_t g_mipi_dsi_callback_count;
/* Per-event counters make the DSI start path observable without a debugger.
 * Index is mipi_dsi_event_t; slots outside the current FSP event range remain
 * unused.  They are read by the short J-Link diagnostic script. */
volatile uint32_t g_mipi_dsi_event_count[8];
volatile uint32_t g_mipi_dsi_last_video_status;
volatile uint32_t g_mipi_dsi_video_status_or;

static lv_indev_t * s_cst816s_indev;
static cst816s_touch_state_t s_cst816s_touch_state;
static TickType_t s_cst816s_next_probe;

static void cst816s_lvgl_read_cb(lv_indev_t * indev, lv_indev_data_t * data)
{
    FSP_PARAMETER_NOT_USED(indev);

    if (cst816s_touch_read(&s_cst816s_touch_state))
    {
        /* CST816S reports native portrait coordinates (480x640).  The
         * display is presented as a 640x480 landscape canvas, so rotate the
         * touch point in the same direction as the panel image. */
        /* The panel is physically mounted opposite to the previous
         * portrait-to-landscape assumption.  Invert both axes after the
         * quarter-turn: raw is 480x640, LVGL is 640x480. */
        int32_t logical_x = 639 - (int32_t) s_cst816s_touch_state.y;
        int32_t logical_y = (int32_t) s_cst816s_touch_state.x;

        if (logical_x < 0) { logical_x = 0; }
        if (logical_x > 639) { logical_x = 639; }
        if (logical_y < 0) { logical_y = 0; }
        if (logical_y > 479) { logical_y = 479; }

        data->point.x = (lv_coord_t) logical_x;
        data->point.y = (lv_coord_t) logical_y;
        data->state = s_cst816s_touch_state.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    }
    else
    {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

/*
 * LVGL uses the GUI Guider canvas (640 x 480), while the panel scan-out is
 * physically 480 x 640.  The stock RM_LVGL_PORT direct path assumes that
 * both geometries are identical.  Keep two logical render buffers in SDRAM
 * and rotate the completed frame into the two native GLCDC frame buffers
 * before BufferChange().  This makes the transform explicit and prevents a
 * mixed-stride/double-buffer frame from producing ghosting.
 */
#define LVGL_LOGICAL_WIDTH       (640U)
#define LVGL_LOGICAL_HEIGHT      (480U)
#define PANEL_SCAN_WIDTH         (480U)
#define PANEL_SCAN_HEIGHT        (640U)
#define LVGL_RENDER_STRIDE_BYTES (LVGL_LOGICAL_WIDTH * sizeof(uint16_t))
#define LVGL_RENDER_BUFFER_BYTES (LVGL_RENDER_STRIDE_BYTES * LVGL_LOGICAL_HEIGHT)
#define PANEL_SCAN_STRIDE_BYTES  (DISPLAY_BUFFER_STRIDE_BYTES_INPUT0)
#define PANEL_SCAN_BUFFER_BYTES  (PANEL_SCAN_STRIDE_BYTES * PANEL_SCAN_HEIGHT)

static uint8_t s_lvgl_render_buffer[2][LVGL_RENDER_BUFFER_BYTES]
    BSP_ALIGN_VARIABLE(64) BSP_PLACE_IN_SECTION(BSP_UNINIT_SECTION_PREFIX ".sdram_noinit");
/* A third native scan buffer prevents GLCDC from reusing a buffer which is
 * still pending in the MIPI/GLCDC pipeline. */
static uint8_t s_scanout_buffer_2[PANEL_SCAN_BUFFER_BYTES]
    BSP_ALIGN_VARIABLE(64) BSP_PLACE_IN_SECTION(BSP_UNINIT_SECTION_PREFIX ".sdram_noinit");
/* The RASC LVGL port may select either configured framebuffer while opening
 * GLCDC.  Start with the independent third buffer so the first frame cannot
 * overwrite the buffer currently being scanned. */
static uint8_t s_scanout_index = 2U;

static void lvgl_flush_rotated(lv_display_t * p_display,
                               const lv_area_t * p_area,
                               uint8_t * p_px_map)
{
    FSP_PARAMETER_NOT_USED(p_area);

    if ((NULL != p_area) && !lv_display_flush_is_last(p_display))
    {
        return;
    }

    volatile uint16_t const * p_src = (volatile uint16_t const *) p_px_map;
    volatile uint16_t * p_dst0 = (volatile uint16_t *) &fb_background[0][0];
    volatile uint16_t * p_dst1 = (volatile uint16_t *) &fb_background[1][0];
    volatile uint16_t * p_dst2 = (volatile uint16_t *) &s_scanout_buffer_2[0];
    volatile uint16_t * p_dst;

    if (0U == s_scanout_index)
    {
        p_dst = p_dst0;
    }
    else if (1U == s_scanout_index)
    {
        p_dst = p_dst1;
    }
    else
    {
        p_dst = p_dst2;
    }

    /* Clockwise 90-degree rotation: 640x480 -> 480x640. */
    for (uint32_t y = 0U; y < PANEL_SCAN_HEIGHT; y++)
    {
        for (uint32_t x = 0U; x < PANEL_SCAN_WIDTH; x++)
        {
            uint16_t const pixel =
                p_src[((LVGL_LOGICAL_HEIGHT - 1U - x) * LVGL_LOGICAL_WIDTH) + y];
            uint32_t const index = (y * DISPLAY_BUFFER_STRIDE_PIXELS_INPUT0) + x;
            /* Only update the buffer which will be submitted next.  Never
             * touch the buffer currently being fetched by GLCDC. */
            p_dst[index] = pixel;
        }
    }

    /* Make every CPU store visible before GLCDC fetches the scan-out buffer. */
    __DSB();

    /* The scan-out frame is produced by the CPU and consumed by GLCDC DMA.
     * Clean is sufficient here; invalidating a just-written frame is not. */
    SCB_CleanDCache_by_Addr((uint32_t *) p_dst,
                            (int32_t) (DISPLAY_BUFFER_STRIDE_BYTES_INPUT0 * DISPLAY_VSIZE_INPUT0));
    __DSB();

    /* BufferChange is only submitted at a safe GLCDC line boundary.  Without
     * this wait, the controller can display the old and new scan buffers in
     * one frame, which appears as ghosting on the panel. */
    RM_LVGL_PORT_WaitForVpos();

    fsp_err_t error;
    do
    {
        error = R_GLCDC_BufferChange(&g_display0_ctrl,
                                     (uint8_t *) p_dst,
                                     g_lvgl_port_ctrl.inherit_frame_layer);
    } while (FSP_ERR_INVALID_UPDATE_TIMING == error);

    g_lvgl_last_flush_error = (uint32_t) error;
    g_lvgl_last_scanout_index = s_scanout_index;
    g_lvgl_flush_count++;

    /* This callback owns the flush-completion handshake.  The stock port's
     * wait callback is disabled below, so LVGL must be released explicitly
     * after the rotated frame has been submitted to GLCDC. */
    if (NULL != p_area)
    {
        lv_display_flush_ready(p_display);
    }

    /* The next frame is rendered into a separate LVGL buffer, so the GLCDC
     * scan buffer can be alternated without reading from the active buffer. */
    s_scanout_index = (uint8_t) ((s_scanout_index + 1U) % 3U);
}

static void mipi_dsi_diag_capture(uint32_t stage)
{
    g_mipi_dsi_diag[0]  = stage;
    g_mipi_dsi_diag[1]  = R_MIPI_DSI->LINKSR;
    g_mipi_dsi_diag[2]  = R_MIPI_DSI->ISR;
    g_mipi_dsi_diag[3]  = R_MIPI_DSI->TXSETR;
    g_mipi_dsi_diag[4]  = R_MIPI_DSI->HSCLKSETR;
    g_mipi_dsi_diag[5]  = R_MIPI_DSI->PLSR;
    g_mipi_dsi_diag[6]  = R_MIPI_DSI->SQCH0SR;
    g_mipi_dsi_diag[7]  = R_MIPI_DSI->SQCH1SR;
    g_mipi_dsi_diag[8]  = R_MIPI_DSI->VMSR;
    g_mipi_dsi_diag[9]  = R_MIPI_DSI->FERRSR;
    g_mipi_dsi_diag[10] = R_MIPI_DSI->RXSR;
}

/*******************************************************************************************************************//**
 * @brief  MIPI DSI 事件回调（RASC 生成的 g_mipi_dsi0_cfg 引用，需用户提供实现）
 **********************************************************************************************************************/
void mipi_dsi0_callback(mipi_dsi_callback_args_t * p_args)
{
#if (CAMERA_PREVIEW_SCREEN_ENABLE == 1U)
    if (NULL == p_args)
    {
        return;
    }

    g_mipi_dsi_callback_count++;
    if ((uint32_t) p_args->event < (sizeof(g_mipi_dsi_event_count) / sizeof(g_mipi_dsi_event_count[0])))
    {
        g_mipi_dsi_event_count[p_args->event]++;
    }
    g_st7701s_last_mipi_event = 0xA5000000UL | (uint32_t) p_args->event;
    mipi_dsi_diag_capture(0x100U | (uint32_t) p_args->event);

    if (MIPI_DSI_EVENT_POST_OPEN == p_args->event)
    {
        /* Send the ST7701S low-power initialization while DSI is open but
         * before GLCDC asks the DSI link to enter video mode. */
        fsp_err_t err = st7701s_panel_init();
        if (FSP_SUCCESS != err)
        {
            g_st7701s_init_error = (uint32_t) err;
        }
    }
    else if (MIPI_DSI_EVENT_PRE_START == p_args->event)
    {
        /* FSP 6.3.0 headless generation currently retains the default VMIE
         * bitmap even when the matching configuration.xml property changes.
         * Enable video lifecycle status before R_MIPI_DSI_Start writes VSTART;
         * this is deliberately application-side, never a generated-file edit. */
        R_MIPI_DSI->VMIER |= R_MIPI_DSI_VMIER_START_Msk |
                             R_MIPI_DSI_VMIER_STOP_Msk |
                             R_MIPI_DSI_VMIER_VIRDY_Msk |
                             R_MIPI_DSI_VMIER_TIMERR_Msk;
        mipi_dsi_diag_capture(0x180U | (uint32_t) p_args->event);
    }
    else if (MIPI_DSI_EVENT_SEQUENCE_0 == p_args->event || MIPI_DSI_EVENT_SEQUENCE_1 == p_args->event)
    {
        if (MIPI_DSI_EVENT_SEQUENCE_0 == p_args->event)
        {
            g_st7701s_sequence0_count++;
        }
        else
        {
            g_st7701s_sequence1_count++;
        }
        g_st7701s_last_mipi_status = (uint32_t) p_args->tx_status;
    }
    else if (MIPI_DSI_EVENT_VIDEO == p_args->event)
    {
        g_mipi_dsi_last_video_status = (uint32_t) p_args->video_status;
        g_mipi_dsi_video_status_or |= (uint32_t) p_args->video_status;
        g_st7701s_last_mipi_status = (uint32_t) p_args->video_status;
    }
    else if (MIPI_DSI_EVENT_RECEIVE == p_args->event)
    {
        g_st7701s_last_mipi_status = (uint32_t) p_args->rx_status;
    }
    else if (MIPI_DSI_EVENT_FATAL == p_args->event)
    {
        g_st7701s_last_mipi_status = (uint32_t) p_args->fatal_status;
        g_st7701s_init_error = ST7701S_PANEL_ERROR_MIPI_EVENT;
    }
    else if (MIPI_DSI_EVENT_PHY == p_args->event)
    {
        g_st7701s_last_mipi_status = (uint32_t) p_args->phy_status;
    }
#else
    FSP_PARAMETER_NOT_USED(p_args);
#endif
}

/*******************************************************************************************************************//**
 * @brief  LVGL 线程入口：初始化显示 -> 创建摄像头预览 -> 循环刷新
 *         若 CAMERA_PREVIEW_SCREEN_ENABLE=0，仅空转（Camera 线程会自动只走 USB 路径）
 **********************************************************************************************************************/
void LVGL_thread_entry(void * pvParameters)
{
    FSP_PARAMETER_NOT_USED(pvParameters);

#if (CAMERA_PREVIEW_SCREEN_ENABLE == 1U)

    /* 1. LVGL 核心初始化（必须先于 RM_LVGL_PORT_Open） */
    lv_init();
    /* A failed previous DSI transaction can survive a CPU-only reset.  Stop
     * the block once before FSP opens it so the pure-color bring-up test does
     * not inherit stale SQ0/SQ1 state from the previous image. */
    R_BSP_MODULE_STOP(FSP_IP_MIPI_DSI, 0);
    mipi_dsi_diag_capture(1U);

    /* 2. 打开 GLCDC 显示（内部完成 R_GLCDC_Open + Start + lv_display 创建） */
    fsp_err_t err = RM_LVGL_PORT_Open(&g_lvgl_port_ctrl, &g_lvgl_port_cfg);
    g_lvgl_port_error = (uint32_t) err;
    mipi_dsi_diag_capture(2U | ((uint32_t) err << 8U));
    if (FSP_SUCCESS != err)
    {
        while (1) { vTaskDelay(1); }
    }

    if (FSP_SUCCESS != (fsp_err_t) g_st7701s_init_error)
    {
        while (1) { vTaskDelay(1); }
    }

    /* Render the GUI Guider canvas as a real 640x480 image.  The completed
     * frame is rotated exactly once by lvgl_flush_rotated() below. */
    lv_display_set_resolution(g_lvgl_port_ctrl.p_lv_display,
                              LVGL_LOGICAL_WIDTH,
                              LVGL_LOGICAL_HEIGHT);
    lv_display_set_physical_resolution(g_lvgl_port_ctrl.p_lv_display,
                                       PANEL_SCAN_WIDTH,
                                       PANEL_SCAN_HEIGHT);
    lv_display_set_rotation(g_lvgl_port_ctrl.p_lv_display, LV_DISPLAY_ROTATION_0);
    lv_display_set_matrix_rotation(g_lvgl_port_ctrl.p_lv_display, false);
    lv_display_set_color_format(g_lvgl_port_ctrl.p_lv_display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers_with_stride(g_lvgl_port_ctrl.p_lv_display,
                                       &s_lvgl_render_buffer[0][0],
                                       &s_lvgl_render_buffer[1][0],
                                       LVGL_RENDER_BUFFER_BYTES,
                                       LVGL_RENDER_STRIDE_BYTES,
                                       LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(g_lvgl_port_ctrl.p_lv_display, lvgl_flush_rotated);
    /* RM_LVGL_PORT installs a wait callback for its original direct path.
     * The custom callback above performs its own VPOS wait and explicitly
     * calls lv_display_flush_ready(), so keeping both handshakes can clear
     * LVGL's state at the wrong time and cause stale/ghosted frames. */
    lv_display_set_flush_wait_cb(g_lvgl_port_ctrl.p_lv_display, NULL);
    g_lvgl_display_diag[0] = (uint32_t) lv_display_get_horizontal_resolution(g_lvgl_port_ctrl.p_lv_display);
    g_lvgl_display_diag[1] = (uint32_t) lv_display_get_vertical_resolution(g_lvgl_port_ctrl.p_lv_display);
    g_lvgl_display_diag[2] = (uint32_t) lv_display_get_physical_horizontal_resolution(g_lvgl_port_ctrl.p_lv_display);
    g_lvgl_display_diag[3] = (uint32_t) lv_display_get_physical_vertical_resolution(g_lvgl_port_ctrl.p_lv_display);
    g_lvgl_display_diag[4] = (uint32_t) lv_display_get_rotation(g_lvgl_port_ctrl.p_lv_display);
    g_lvgl_display_diag[5] = lv_display_get_matrix_rotation(g_lvgl_port_ctrl.p_lv_display) ? 1U : 0U;

    /* 3. 优先加载GUI Guider界面；未导入生成代码时保留摄像头预览。 */
#if defined(GUI_GUIDER_AVAILABLE)
    if (!gui_app_init())
    {
        g_gui_init_error = 1U;
        sys_log_add(SYS_LOG_ERR, "GUI 初始化失败");
        while (1) { vTaskDelay(1); }
    }
    g_gui_init_error = 0U;
    s_lvgl_ready = true;
    sys_log_add(SYS_LOG_OK, "界面启动完成");

    /* Camera RTT-only firmware must not touch the shared IIC0 at all. */
#if !defined(CAMERA_RTT_ONLY)
    /* TP_INT/TP_RST are intentionally not required for the first bring-up.
     * Probe CST816S by polling over the shared IIC0 bus. */
    (void) cst816s_touch_init();
    if (0U != g_cst816s_probe_ok)
    {
        sys_log_add(SYS_LOG_OK, "触摸屏初始化成功");
    }
    else
    {
        sys_log_add(SYS_LOG_WARN, "触摸屏初始化失败，重试中");
    }
    s_cst816s_indev = lv_indev_create();
    lv_indev_set_type(s_cst816s_indev, LV_INDEV_TYPE_POINTER);
    /* Bind explicitly to the rotated GUI display.  Relying on the current
     * default display is fragile when the GUI Guider page creates or switches
     * displays during startup. */
    lv_indev_set_display(s_cst816s_indev, g_lvgl_port_ctrl.p_lv_display);
    lv_indev_set_read_cb(s_cst816s_indev, cst816s_lvgl_read_cb);
    s_cst816s_next_probe = xTaskGetTickCount() + pdMS_TO_TICKS(1000U);
#else
    s_cst816s_indev = NULL;
#endif
#else
    lv_obj_t * p_img = lv_image_create(lv_screen_active());
    lv_image_set_src(p_img, camera_preview_get_image());
    lv_obj_align(p_img, LV_ALIGN_CENTER, 0, 0);

    /* 4. 通知 Camera 线程可以填屏 */
    s_lvgl_ready = true;
#endif

    /* 5. LVGL 主循环 */
    while (1)
    {
        /* 若使用旧摄像头预览，标记 image 需要重绘。GUI Guider界面不直接
         * 读取Camera线程缓冲区，后续通过消息接口接入二维码结果。 */
#if !defined(GUI_GUIDER_AVAILABLE)
        if (camera_preview_has_new_frame())
        {
            lv_obj_invalidate(p_img);
            camera_preview_mark_flushed();
        }
#else
#if !defined(CAMERA_RTT_ONLY)
        gui_app_poll();
#endif
#endif

        if ((NULL != s_cst816s_indev) &&
            (xTaskGetTickCount() >= s_cst816s_next_probe) &&
            (0U == g_cst816s_probe_ok))
        {
            (void) cst816s_touch_init();
            if (0U != g_cst816s_probe_ok)
            {
                sys_log_add(SYS_LOG_OK, "触摸屏重连成功");
            }
            s_cst816s_next_probe = xTaskGetTickCount() + pdMS_TO_TICKS(1000U);
        }

        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }

#else /* CAMERA_PREVIEW_SCREEN_ENABLE == 0：屏显禁用，空转等待 */

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

#endif /* CAMERA_PREVIEW_SCREEN_ENABLE */
}
