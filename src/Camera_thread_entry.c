/*
 * Copyright (c) 2025 Renesas Electronics Corporation and/or its affiliates
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "Camera_thread.h"
#include "camera_app.h"
#include "camera_convert.h"
#include "camera_preview.h"
#include "qr_decoder.h"
#include "sys_log.h"
#include "usb_cdc.h"
#include "rtt_preview.h"
#include "ospi_storage.h"
#include "ospi_ttf_loader.h"
#include "pickup_params.h"

/* 灰度串流线程入口（camera_usb_thread_entry.c） */
void camera_usb_thread_entry(void * pvParameters);
extern volatile uint32_t s_dbg_ceu_cap_err;

/* 诊断：Camera 线程栈余量（高水位，J-Link 可读）。
 * 4096B 栈 + quirc 解码（otsu 直方图 1KB 等局部数组）需要实测余量。 */
volatile uint32_t s_dbg_camera_stack_high_water;

/* ============================================================================
 * 模式选择：
 *   CAMERA_USB_GRAY_MODE == 1  -> OV7725 -> CEU -> 320x240 灰度 -> USBHS CDC
 *   CAMERA_USB_GRAY_MODE == 0  -> 原有 RGB565 预览（MIPI 屏 + USB）
 * ==========================================================================*/
#ifndef CAMERA_USB_GRAY_MODE
#define CAMERA_USB_GRAY_MODE (0)
#endif

#if (CAMERA_USB_GRAY_MODE == 1)

/* ---- 灰度串流模式：直接委托给 camera_usb_thread_entry.c ---- */
void Camera_thread_entry(void * pvParameters)
{
    camera_usb_thread_entry(pvParameters);

    /* 正常情况下不会到达；若返回则挂起任务 */
    vTaskSuspend(NULL);
}

#else /* CAMERA_USB_GRAY_MODE == 0：RGB565 预览模式 */

/* USB 预览输出分辨率（320x240 RGB565 = 153.6KB/帧，USBHS 高速下带宽充足） */
#define USB_PREVIEW_W    (320U)
#define USB_PREVIEW_H    (240U)
#define RTT_PREVIEW_W    (320U)
#define RTT_PREVIEW_H    (240U)

#ifndef CAMERA_USB_CDC_PREVIEW_ENABLE
#define CAMERA_USB_CDC_PREVIEW_ENABLE (1)
#endif

#ifndef CAMERA_RTT_PREVIEW_ENABLE
#define CAMERA_RTT_PREVIEW_ENABLE (0)
#endif

/* The scanner workflow benefits from luminance-only preview. Set to 0 only
 * when colour inspection of the sensor is explicitly needed. */
#ifndef CAMERA_SCREEN_GRAY_MODE
#define CAMERA_SCREEN_GRAY_MODE (1U)
#endif

/* MIPI 屏预览缓冲（480x360 RGB565，与 camera_preview 一致）。
 * 放在 SDRAM：RAM 已接近满载（LVGL 渲染缓冲/CEU/RTT 均在 RAM），
 * 大块预览缓冲放 SDRAM（128MB 富余）释放 RAM 给 tiny_ttf 字形缓存等。 */
static uint16_t s_screen_preview[CAMERA_PREVIEW_WIDTH * CAMERA_PREVIEW_HEIGHT]
    __attribute__((section(".sdram_noinit"), aligned(32)));
static uint16_t s_rtt_preview[RTT_PREVIEW_W * RTT_PREVIEW_H]
    __attribute__((section(".sdram_noinit"), aligned(32)));

/* 双缓冲 ping-pong：当前正在处理的帧缓冲指针。
 * CEU 采集到 g_ceu_buffer_0/1（32 字节对齐），主循环处理当前帧时
 * 下一帧采集到另一缓冲，互不干扰。 */
static uint8_t * s_cur_frame = g_ceu_buffer_0;

/* 摄像头初始化失败时发送的测试帧内容（每行一个颜色，可区分 USB 通/不通） */
static void fill_test_pattern(uint16_t * p_buf, uint32_t w, uint32_t h)
{
    /* 垂直色条：红 绿 蓝 白 黑，验证 RGB 通道与颜色正确性 */
    static const uint16_t colors[5] = { 0xF800, 0x07E0, 0x001F, 0xFFFF, 0x0000 };
    uint32_t y, x;

    for (y = 0; y < h; y++)
    {
        for (x = 0; x < w; x++)
        {
            p_buf[y * w + x] = colors[(x * 5) / w];
        }
    }
}

/*******************************************************************************************************************//**
 * @brief  Camera 线程入口函数
 **********************************************************************************************************************/
void Camera_thread_entry(void * pvParameters)
{
    FSP_PARAMETER_NOT_USED(pvParameters);

    /* 0. 先初始化 USB PCDC（保证 PC 端虚拟串口出现，便于诊断） */
    usb_cdc_init();
    if (CAMERA_RTT_PREVIEW_ENABLE)
    {
        rtt_preview_init();
    }
    rtt_down_init();
    sys_log_add(SYS_LOG_INFO, "Camera 线程启动");

    /* 板载串行 Flash（OSPI0/CS0）初始化：读 JEDEC ID 确认芯片型号 */
    {
        fsp_err_t ospi_err = ospi_storage_init();
        if (FSP_SUCCESS == ospi_err)
        {
            uint8_t jedec[3];
            ospi_storage_get_jedec(jedec);
            sys_log_add(SYS_LOG_OK, "OSPI Flash 就绪 (JEDEC %02X %02X %02X, SR2=%02X)",
                        (unsigned) jedec[0], (unsigned) jedec[1], (unsigned) jedec[2],
                        (unsigned) ospi_storage_sr2());

            /* 擦/写/映射读 自检：验证 4 字节地址模式与整条数据路径 */
            fsp_err_t st_err = ospi_storage_selftest();
            if (FSP_SUCCESS == st_err)
            {
                sys_log_add(SYS_LOG_OK, "OSPI 自检通过（擦除/编程/映射读 OK）");
            }
            else
            {
                sys_log_add(SYS_LOG_ERR, "OSPI 自检失败: %d (result=%08X, mis=%d)",
                            (int) st_err,
                            (unsigned) ospi_storage_selftest_result(),
                            (int) ospi_storage_selftest_mismatch());
            }
        }
        else
        {
            sys_log_add(SYS_LOG_ERR, "OSPI Flash 初始化失败: %d", (int) ospi_err);
        }
    }

    /* 取药系统参数：从 OSPI 加载（失败回退默认；无论成败都置 ready） */
    {
        fsp_err_t p_err = PickupParams_Init();
        if (FSP_SUCCESS == p_err)
        {
            sys_log_add(SYS_LOG_OK, "取药参数已加载 (层=%u 格=%u Z=%.0fmm)",
                        (unsigned) PickupParams_Get()->shelf_count,
                        (unsigned) PickupParams_Get()->slots_per_row,
                        (unsigned) PickupParams_Get()->z_reach_mm);
        }
        else
        {
            sys_log_add(SYS_LOG_ERR, "取药参数回退默认值: %d", (int) p_err);
        }
    }

    /* 独立二维码解码线程（优先级 0，低于 LVGL/Camera；quirc 卡死不影响触摸） */
    if (qr_decoder_start_task())
    {
        sys_log_add(SYS_LOG_OK, "二维码解码线程已创建");
    }

    /* 1. 摄像头初始化（失败不阻塞，主循环继续；可通过 USB 串口判断状态） */
    /* RTT-only diagnostic firmware deliberately follows the previously
     * validated camera baseline: initialize and start CEU immediately. */
    bool cam_ok = false;
#if defined(CAMERA_CAPTURE_ON_DEMAND)
    bool capture_was_active = false;
    /* RTT is an explicit diagnostic mode.  The normal GUI build keeps camera
     * capture strictly on demand: the camera stays OFF (no XCLK, no CEU, no
     * shared-IIC0 traffic) until gui_app_poll() reports that the user
     * entered the Pickup/Scan screen, and stops when the user leaves it. */
    if (CAMERA_RTT_PREVIEW_ENABLE)
    {
        cam_ok = camera_app_init();
        capture_was_active = cam_ok;
    }
    else
    {
        /* Do NOT request capture at boot: that would initialize the sensor
         * and drive the shared IIC0 even while the user is on Home/Admin.
         * The GUI application owns the capture request.
         *
         * Keep the OV7725 POWERED DOWN (PWDN=1/RST=0).  Measured: in its
         * idle powered-on state (PWDN=0/RST=1, XCLK running, no SCCB init)
         * the uninitialized module pulls the shared IIC0 SDA low and kills
         * CST816S (SDAI=0, BBSY=1); forcing PWDN=1 releases SDA instantly.
         * Power-down also gives zero module idle draw.  Power-up + full
         * init happen inside camera_app_init() only when Pickup/Scan
         * requests capture.  Never start XCLK here: camera_app_init()
         * calls camera_xclk_init() again and R_GPT_Open would return
         * FSP_ERR_IN_USE on a second open. */
        camera_power_off();
        camera_app_request_capture(false);
    }
#else
    cam_ok = camera_app_init();
#endif

    /* 主循环：处理帧数据（双缓冲 ping-pong） */
    while (1)
    {
        /* TTF 字体烧录：轮询 RTT down 通道（空闲时处理，不阻塞摄像头） */
        ospi_ttf_loader_poll();

        /* 栈余量诊断：每 2s 记录一次高水位（uxTaskGetStackHighWaterMark 返回
         * 自任务启动以来最少的未用栈空间，单位 word） */
        {
            static TickType_t s_next_watermark_tick;
            TickType_t const now = xTaskGetTickCount();
            if (now >= s_next_watermark_tick)
            {
                s_next_watermark_tick = now + pdMS_TO_TICKS(2000U);
                uint32_t const high_water = (uint32_t) uxTaskGetStackHighWaterMark(NULL);
                if ((0U == s_dbg_camera_stack_high_water) ||
                    (high_water < s_dbg_camera_stack_high_water))
                {
                    s_dbg_camera_stack_high_water = high_water;
                }
            }
        }
#if defined(CAMERA_AUTOTEST)
        /* Headless on-target verification of the on-demand chain: simulate
         * the GUI entering Pickup/Scan at t+5 s and leaving at t+35 s while
         * the service loop keeps running.  The 30 s capture window also
         * covers the CST816S latch-up latency seen with the un-protected
         * power-on sequence. */
        {
            static bool s_auto_entered = false;
            static bool s_auto_left    = false;
            TickType_t const now = xTaskGetTickCount();
            if (!s_auto_entered && (now >= pdMS_TO_TICKS(5000U)))
            {
                camera_app_request_capture(true);
                s_auto_entered = true;
            }
            if (!s_auto_left && (now >= pdMS_TO_TICKS(35000U)))
            {
                camera_app_request_capture(false);
                s_auto_left = true;
            }
        }
#endif
        /* SerialState 通知轮询：usbser 打开端口后等待 DSR/DCD 上报，
         * 否则 Open() 永久阻塞（任务上下文调用，避免 ISR 中 vTaskSuspendAll） */
        usb_cdc_poll_serial_notify();
        usb_cdc_preview_poll();

        bool capture_active = CAMERA_RTT_PREVIEW_ENABLE ? cam_ok : camera_app_service_capture();
#if defined(CAMERA_CAPTURE_ON_DEMAND)
        if (capture_active != capture_was_active)
        {
            s_cur_frame = g_ceu_buffer_0;
            g_frame_ready = false;
            capture_was_active = capture_active;
        }
#endif

        if (capture_active && g_frame_ready)
        {
            g_frame_ready = false;

            /* A CEU frame-end interrupt proves that the sensor, timing and
             * DMA path are live, even if a preceding SCCB probe timed out. */
            cam_ok = true;

            /* 取当前完成的帧缓冲（ping-pong 切换前的活跃缓冲） */
            uint8_t * p_done = s_cur_frame;

            /* D-Cache 失效：CEU DMA 直接写物理 SRAM（绕过缓存），
             * CPU 读取前使该缓冲的缓存行失效，确保读到 DMA 写入的新数据。
             * （D-Cache 未使能时此调用为 no-op，安全无害） */
            SCB_InvalidateDCache_by_Addr((uint32_t *) p_done, (int32_t) CAMERA_IMAGE_SIZE);

            /* 1. MIPI 屏预览：320x240 -> 480x360 RGB565 -> LVGL 缓冲。 */
            if ((cam_ok) && (camera_preview_lvgl_ready()))
            {
#if (CAMERA_SCREEN_GRAY_MODE == 1U)
                camera_frame_to_gray_rgb565_scaled(p_done,
                                                    CAMERA_IMAGE_W, CAMERA_IMAGE_H,
                                                    s_screen_preview,
                                                    CAMERA_PREVIEW_WIDTH, CAMERA_PREVIEW_HEIGHT);
#else
                camera_frame_to_rgb565_scaled(p_done,
                                              CAMERA_IMAGE_W, CAMERA_IMAGE_H,
                                              s_screen_preview,
                                              CAMERA_PREVIEW_WIDTH, CAMERA_PREVIEW_HEIGHT);
#endif
                camera_preview_put_frame(s_screen_preview, CAMERA_PREVIEW_WIDTH, CAMERA_PREVIEW_HEIGHT);
            }

            /* 2. PC 预览（真实摄像头帧）：
             *    - cam_ok 且 USB 已连接: 发送真实摄像头帧 */
            if ((CAMERA_USB_CDC_PREVIEW_ENABLE) && (cam_ok) && (usb_cdc_is_connected()) && !usb_cdc_preview_busy())
            {
                camera_frame_to_gray_rgb565_scaled(p_done,
                                                   CAMERA_IMAGE_W, CAMERA_IMAGE_H,
                                                   s_rtt_preview,
                                                   USB_PREVIEW_W, USB_PREVIEW_H);

                /* RTT/J-Link reads SRAM directly, bypassing the M85 D-Cache.
                 * Publish every CPU-written cache line before the host reads
                 * the preview buffer, otherwise old/new lines appear as bands. */
                SCB_CleanDCache_by_Addr((uint32_t *) s_rtt_preview,
                                         (int32_t) (USB_PREVIEW_W * USB_PREVIEW_H * sizeof(uint16_t)));
                if (usb_cdc_preview_submit(s_rtt_preview, USB_PREVIEW_W, USB_PREVIEW_H))
                {
                    usb_cdc_preview_poll();
                }
            }

            /* 2b. 二维码喂帧（仅 Scan/Pickup 页启用时解码）。
             *     **只提取 Y 灰度**，quirc 解码在独立的低优先级线程执行，
             *     避免解码卡死拖垮本线程（CEU 停帧）与 LVGL（触摸/画面）。 */
            qr_decoder_feed_frame(p_done, CAMERA_IMAGE_W, CAMERA_IMAGE_H);

            /* RTT does not use the Windows USB serial driver. */
            if ((CAMERA_RTT_PREVIEW_ENABLE) && cam_ok)
            {
                /* CEU captures YCbCr422 (Y0 U0 Y1 V0); extract the luma
                 * component like the USB preview path.  Feeding the raw
                 * YUV422 words into the RGB565 scaler renders colored noise
                 * ("花花绿绿") even though the captured scene is fine. */
                camera_frame_to_gray_rgb565_scaled(p_done,
                                                    CAMERA_IMAGE_W, CAMERA_IMAGE_H,
                                                    s_rtt_preview,
                                                    RTT_PREVIEW_W, RTT_PREVIEW_H);
                (void) rtt_preview_send_rgb565(s_rtt_preview, RTT_PREVIEW_W, RTT_PREVIEW_H);
            }

            /* 3. 双缓冲 ping-pong：切换缓冲并立即启动下一帧采集。
             *    当前帧(p_done)正在被处理，下一帧采集到另一缓冲。 */
            if (cam_ok)
            {
                uint8_t * p_next = (s_cur_frame == g_ceu_buffer_0) ? g_ceu_buffer_1 : g_ceu_buffer_0;
                fsp_err_t err = R_CEU_CaptureStart(g_ceu0.p_ctrl, p_next);
                if (FSP_SUCCESS == err)
                {
                    s_cur_frame = p_next;   /* 切换活跃缓冲指针 */
                }
                else
                {
                    /* CEU may already be running in continuous capture mode.
                     * A transient/in-use return here must not discard a live
                     * camera stream and replace it with the USB test pattern. */
                    s_dbg_ceu_cap_err = (uint32_t) err;
                }
            }
        }
        else
        {
            /* 4. 降级模式：摄像头未出帧（CEU 无 VSYNC/OV7725 无 XCLK）时，
             *    只要 USB 已连接就持续发送测试彩条帧。 */
            if ((CAMERA_USB_CDC_PREVIEW_ENABLE) && !cam_ok &&
                (usb_cdc_is_connected()) && !usb_cdc_preview_busy())
            {
                fill_test_pattern(s_rtt_preview, USB_PREVIEW_W, USB_PREVIEW_H);
                if (usb_cdc_preview_submit(s_rtt_preview, USB_PREVIEW_W, USB_PREVIEW_H))
                {
                    usb_cdc_preview_poll();
                }
                vTaskDelay(5);   /* 降级模式限速，避免 USB 满载 */
            }
        }

        vTaskDelay(1);
    }
}

#endif /* CAMERA_USB_GRAY_MODE */
