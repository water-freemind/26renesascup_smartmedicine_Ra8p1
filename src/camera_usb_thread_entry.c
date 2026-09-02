/*
 * camera_usb_thread_entry.c
 *
 * OV7725 -> CEU -> Grayscale (320x240) -> USBHS CDC-ACM 串流线程实现
 *
 * 数据流：
 *   OV7725 (VGA 640x480 YCbCr422)                 [sccb 配置: g_i2c_master0]
 *     -> CEU 双缓冲采集 (g_ceu0, 640x480*2 B/帧)   [VSYNC 中断 -> 信号量]
 *     -> Y 通道提取 + 2x2 下采样 -> 320x240 灰度    [76800 B/帧]
 *     -> USBHS CDC-ACM 分包发送 (g_basic0)         [usb_cdc_send_raw]
 *
 * 依赖（RASC 生成）：
 *   - g_timer_xclk  (GPT10, P109, 24MHz PWM)
 *   - g_i2c_master0 (IIC0,  SCL=P410 SDA=P409, 从机 0x21)
 *   - g_ceu0        (CEU,   PCLK=P414 HD=P415 VD=P708 D0-D7=P400/P401/P405/P406/P700-P703)
 *   - g_basic0      (USBHS CDC-ACM, 480Mbps)
 *
 * GPIO：
 *   - P709 (PWDN) 低有效唤醒
 *   - P710 (RST)  高有效（释放）
 */

#include "Camera_thread.h"
#include "camera_drv.h"
#include "usb_cdc.h"

#include "FreeRTOS.h"
#include "semphr.h"

/**********************************************************************************************************************
 * 配置宏
 **********************************************************************************************************************/
#define CAM_W                  (640U)   /* CEU 采集宽度（VGA） */
#define CAM_H                  (480U)   /* CEU 采集高度（VGA） */
#define CAM_STRIDE             (CAM_W * 2U)   /* YCbCr422: 每像素 2 字节 */
#define CAM_FRAME_BYTES        (CAM_W * CAM_H * 2U)

#define GRAY_W                 (320U)   /* 输出灰度宽 */
#define GRAY_H                 (240U)   /* 输出灰度高 */
#define GRAY_BYTES             (GRAY_W * GRAY_H)

/* 双缓冲（8 字节对齐满足 CEU DMA 要求） */
static uint8_t s_ceu_buf[CAM_FRAME_BYTES] __attribute__((aligned(16)));
static uint8_t s_ceu_buf2[CAM_FRAME_BYTES] __attribute__((aligned(16)));

static uint8_t s_gray[GRAY_BYTES] __attribute__((aligned(4)));

/* 帧同步信号量（CEU VSYNC 中断 give，线程 take） */
static SemaphoreHandle_t s_frame_sem;

/* 当前采集缓冲（双缓冲切换） */
static uint8_t * s_cur_buf = s_ceu_buf;

/* 诊断计数（J-Link 可读） */
volatile uint32_t s_dbg_gr_frame_cnt;    /* 已发送灰度帧数 */
volatile uint32_t s_dbg_gr_usb_fail;     /* USB 发送失败次数 */
volatile uint32_t s_dbg_gr_ceu_start_fail;

/**********************************************************************************************************************
 * OV7725 寄存器初始化表（VGA 640x480，YCbCr422 输出）
 **********************************************************************************************************************/
static const uint8_t s_ov7725_regs[][2] =
{
    {0x12, 0x80}, /* [Reset] Software Reset (Wait 2ms after writing this!) */
    {0x3d, 0x03}, {0x17, 0x22}, {0x18, 0xa4}, {0x19, 0x07}, {0x1a, 0xf0}, {0x32, 0x00}, /* [Windowing] */
    {0x29, 0xa0}, {0x2c, 0xf0}, {0x2a, 0x00}, {0x11, 0x01}, /* [Clock & Frame Rate 15fps @ 24Mhz] */
    {0x42, 0x7f}, {0x4d, 0x09}, {0x63, 0xe0}, {0x64, 0xff}, {0x65, 0x20}, {0x66, 0x00}, {0x67, 0x48}, /* [AEC/AGC] */
    {0x13, 0xf0}, {0x0d, 0x41}, {0x0f, 0xc5}, {0x14, 0x11},
    {0x22, 0x7f}, {0x23, 0x03}, {0x24, 0x40}, {0x25, 0x30}, {0x26, 0xa1}, /* [Banding Filter 60Hz] */
    {0x2b, 0x00}, {0x6b, 0xaa}, {0x13, 0xff}, /* [AWB, AEC, AGC Enable] */
    {0x90, 0x05}, {0x91, 0x01}, {0x92, 0x03}, {0x93, 0x00}, {0x94, 0xb0}, {0x95, 0x9d},
    {0x96, 0x13}, {0x97, 0x16}, {0x98, 0x7b}, {0x99, 0x91}, {0x9a, 0x1e}, {0x9b, 0x08},
    {0x9c, 0x20}, {0x9e, 0x81}, {0xa6, 0x06},
    /* [Gamma Curves for High Contrast] */
    {0x7e, 0x0c}, {0x7f, 0x16}, {0x80, 0x2a}, {0x81, 0x4e}, {0x82, 0x61}, {0x83, 0x6f},
    {0x84, 0x7b}, {0x85, 0x86}, {0x86, 0x8e}, {0x87, 0x97}, {0x88, 0xa4}, {0x89, 0xaf},
    {0x8a, 0xc5}, {0x8b, 0xd7}, {0x8c, 0xe8}, {0x8d, 0x20},
    {0x0e, 0x65}  /* [COM5] */
};

/**********************************************************************************************************************
 * SCCB 写寄存器（FSP I2C Master）
 **********************************************************************************************************************/
static bool sccb_write(uint8_t reg, uint8_t val)
{
    return camera_i2c_write(reg, val);
}

/**********************************************************************************************************************
 * SCCB 寄存器初始化
 **********************************************************************************************************************/
static bool ov7725_reg_init(void)
{
    for (uint32_t i = 0; i < (sizeof(s_ov7725_regs) / sizeof(s_ov7725_regs[0])); i++)
    {
        uint8_t reg = s_ov7725_regs[i][0];
        uint8_t val = s_ov7725_regs[i][1];

        if (false == sccb_write(reg, val))
        {
            return false;
        }

        /* 复位后需等待 2ms（任务规范要求 5ms，取较大值更稳） */
        if (reg == 0x12 && val == 0x80)
        {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }

    return true;
}

/**********************************************************************************************************************
 * 硬件复位时序
 *   PWDN(P709) 低有效：拉低唤醒
 *   RST(P710)  高有效：先拉低 10ms，再拉高释放
 **********************************************************************************************************************/
static void camera_hw_reset(void)
{
    /* PWDN=1(掉电), RST=0(复位) —— 初始态 */
    R_PORT7->POSR = (1U << 9);   /* P709 = 1 */
    R_PORT7->PORR = (1U << 10);  /* P710 = 0 */

    vTaskDelay(pdMS_TO_TICKS(10));

    /* 释放复位：RST=1 */
    R_PORT7->POSR = (1U << 10);

    vTaskDelay(pdMS_TO_TICKS(20));

    /* 唤醒：PWDN=0 */
    R_PORT7->PORR = (1U << 9);

    vTaskDelay(pdMS_TO_TICKS(20));
}

/**********************************************************************************************************************
 * CEU 帧完成回调（ISR 上下文）
 **********************************************************************************************************************/
static void ceu_frame_cb(capture_callback_args_t * p_args)
{
    if (NULL == p_args)
    {
        return;
    }

    if (CEU_EVENT_FRAME_END == p_args->event)
    {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xSemaphoreGiveFromISR(s_frame_sem, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

/**********************************************************************************************************************
 * YCbCr422 (UYVY) -> 320x240 灰度（Y 通道 + 2x2 下采样）
 *
 * UYVY 布局：每 4 字节 = 2 像素 = [Cb, Y0, Cr, Y1]
 * Y 通道位于每像素的第 1 字节（偶数偏移）。
 * 2x2 下采样：取 (2i, 2j) 处像素的 Y 值。
 **********************************************************************************************************************/
static void extract_gray_qvga(const uint8_t * p_yuv, uint8_t * p_gray)
{
    for (uint32_t gy = 0; gy < GRAY_H; gy++)
    {
        uint32_t src_row = gy * 2;                 /* 源行 = 目标行 * 2 */
        const uint8_t * p_row = &p_yuv[src_row * CAM_STRIDE];

        for (uint32_t gx = 0; gx < GRAY_W; gx++)
        {
            uint32_t src_col = gx * 2;             /* 源列 = 目标列 * 2 */
            p_gray[gy * GRAY_W + gx] = p_row[src_col * 2U + 1U];  /* Y0 在 UYVY 中偏移 +1 */
        }
    }
}

/**********************************************************************************************************************
 * CEU 采集初始化 + 双缓冲启动
 **********************************************************************************************************************/
static bool ceu_capture_init(void)
{
    fsp_err_t err;

    err = R_CEU_Open(g_ceu0.p_ctrl, g_ceu0.p_cfg);
    if (FSP_SUCCESS != err)
    {
        return false;
    }

    /* 将回调替换为帧同步回调（保留原回调用于诊断事件） */
    /* 注：g_ceu0_cfg.p_callback 由 RASC 生成，指向 g_ceu0_user_callback；
     *     此处使用 FSP 的 callbackSet API 重定向到 ceu_frame_cb。 */
    err = R_CEU_CallbackSet(g_ceu0.p_ctrl, ceu_frame_cb, NULL, NULL);
    if (FSP_SUCCESS != err)
    {
        return false;
    }

    /* 启动第一帧采集到缓冲 1 */
    err = R_CEU_CaptureStart(g_ceu0.p_ctrl, s_ceu_buf);
    if (FSP_SUCCESS != err)
    {
        return false;
    }
    s_cur_buf = s_ceu_buf;

    return true;
}

/**********************************************************************************************************************
 * 线程入口
 **********************************************************************************************************************/
void camera_usb_thread_entry(void * pvParameters)
{
    FSP_PARAMETER_NOT_USED(pvParameters);

    /* 1. 启动 XCLK（GPT10, 24MHz PWM） */
    camera_xclk_init();

    /* 2. 硬件复位（PWDN/RST 时序） */
    camera_hw_reset();

    /* 3. 初始化 I2C 并配置 OV7725 寄存器 */
    camera_i2c_init();
    if (false == ov7725_reg_init())
    {
        /* 寄存器写入失败：降级为测试帧模式（仍走 USB） */
        s_dbg_gr_ceu_start_fail = 1U;
    }

    /* 4. 初始化 USB PCDC */
    (void) usb_cdc_init();

    /* 5. 创建帧同步信号量 */
    s_frame_sem = xSemaphoreCreateBinary();
    if (NULL == s_frame_sem)
    {
        vTaskSuspend(NULL);
    }

    /* 6. CEU 双缓冲采集初始化 */
    if (false == ceu_capture_init())
    {
        s_dbg_gr_ceu_start_fail = 2U;
    }

    /* 主循环：等待帧同步 -> 灰度提取 -> USB 发送 */
    for (;;)
    {
        /* SerialState 通知轮询（usbser 打开端口依赖） */
        usb_cdc_poll_serial_notify();

        /* 等待 CEU 帧完成（10s 超时保护） */
        if (pdTRUE == xSemaphoreTake(s_frame_sem, pdMS_TO_TICKS(10000U)))
        {
            uint8_t * p_done = s_cur_buf;

            /* 立即重启下一帧采集到另一缓冲（双缓冲） */
            uint8_t * p_next = (s_cur_buf == s_ceu_buf) ? s_ceu_buf2 : s_ceu_buf;
            fsp_err_t err = R_CEU_CaptureStart(g_ceu0.p_ctrl, p_next);
            if (FSP_SUCCESS == err)
            {
                s_cur_buf = p_next;
            }
            else
            {
                s_dbg_gr_ceu_start_fail++;
            }

            /* 灰度提取 + USB 发送（不阻塞采集） */
            extract_gray_qvga(p_done, s_gray);

            if (usb_cdc_is_connected())
            {
                if (false == usb_cdc_send_raw(s_gray, GRAY_BYTES))
                {
                    s_dbg_gr_usb_fail++;
                }
                else
                {
                    s_dbg_gr_frame_cnt++;
                }
            }
        }

        vTaskDelay(1);
    }
}
