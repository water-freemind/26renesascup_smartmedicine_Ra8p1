#include "qr_decoder.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "quirc.h"

#include "qr_selftest.h"

/* SCB_CleanDCache_by_Addr：J-Link 经 SWD 直读内存（绕过 M85 D-Cache），
 * 状态变量更新后需 clean 才能被探针读到一致值。 */
#include "hal_data.h"

/* 中央 ROI（扫码时码通常在画面中央）：主解码只解中央 400x400 —— 像素从
 * 307K 降到 160K（~1.9x 提速），且是**原分辨率裁剪不缩放**，码模块像素
 * 不变（实测 49x49 码 4.4px/模块在 ROI 内可正常解出，降采样反而解不出）。
 * 码不在中央时（ROI 内 NO_CODE）回退全画面 640x480 兜底。 */
#define QR_ROI_X                 (120U)
#define QR_ROI_Y                 (40U)
#define QR_ROI_WIDTH             (400U)
#define QR_ROI_HEIGHT            (400U)

static struct quirc * gp_quirc;        /* 全画面 640x480（码不在中央时兜底 / 自检） */
static struct quirc * gp_quirc_roi;    /* 中央 ROI 400x400（主解码，提速 ~1.9x） */
static uint16_t       g_width;
static uint16_t       g_height;

bool qr_decoder_init(uint16_t width, uint16_t height)
{
    if ((0U == width) || (0U == height))
    {
        return false;
    }

    if ((NULL != gp_quirc) && (width == g_width) && (height == g_height) &&
        (NULL != gp_quirc_roi))
    {
        return true;
    }

    qr_decoder_deinit();
    gp_quirc = quirc_new();
    if (NULL == gp_quirc)
    {
        return false;
    }

    if (0 != quirc_resize(gp_quirc, width, height))
    {
        qr_decoder_deinit();
        return false;
    }

    gp_quirc_roi = quirc_new();
    if (NULL == gp_quirc_roi)
    {
        qr_decoder_deinit();
        return false;
    }

    if (0 != quirc_resize(gp_quirc_roi, QR_ROI_WIDTH, QR_ROI_HEIGHT))
    {
        qr_decoder_deinit();
        return false;
    }

    g_width = width;
    g_height = height;
    return true;
}

void qr_decoder_deinit(void)
{
    if (NULL != gp_quirc)
    {
        quirc_destroy(gp_quirc);
        gp_quirc = NULL;
    }

    if (NULL != gp_quirc_roi)
    {
        quirc_destroy(gp_quirc_roi);
        gp_quirc_roi = NULL;
    }

    g_width = 0U;
    g_height = 0U;
}

/* 内部解码：在指定 quirc 实例上解一帧灰度（ROI 实例或全画面实例共用） */
static qr_decoder_status_t qr_decode_gray_into(struct quirc * p_q,
                                               const uint8_t * p_gray,
                                               uint16_t width,
                                               uint16_t height,
                                               uint8_t * p_payload,
                                               uint16_t payload_capacity,
                                               uint16_t * p_payload_length)
{
    int      image_width;
    int      image_height;
    int      count;
    uint8_t * p_image;

    if ((NULL == p_gray) || (NULL == p_payload) || (NULL == p_payload_length) ||
        (0U == payload_capacity))
    {
        return QR_DECODER_INVALID_ARGUMENT;
    }

    *p_payload_length = 0U;
    if (NULL == p_q)
    {
        return QR_DECODER_NOT_READY;
    }

    p_image = quirc_begin(p_q, &image_width, &image_height);
    if ((NULL == p_image) || ((int) width != image_width) || ((int) height != image_height))
    {
        return QR_DECODER_NOT_READY;
    }

    memcpy(p_image, p_gray, (size_t) width * height);
    quirc_end(p_q);

    count = quirc_count(p_q);
    if (count <= 0)
    {
        return QR_DECODER_NO_CODE;
    }

    for (int index = 0; index < count; index++)
    {
        struct quirc_code code;
        struct quirc_data data;

        quirc_extract(p_q, index, &code);
        quirc_decode_error_t err = quirc_decode(&code, &data);
        if (QUIRC_ERROR_DATA_ECC == err)
        {
            /* 镜像二维码重试：OV7725 COM3=0x50 配置 HFLIP，实拍画面水平
             * 镜像，quirc 定位角检测不受镜像影响（对称），但数据位流反序
             * → ECC 失败；quirc_flip 翻转码图后再解（qrtest 同款处理）。 */
            quirc_flip(&code);
            err = quirc_decode(&code, &data);
        }

        if (QUIRC_SUCCESS != err)
        {
            continue;
        }

        if ((data.payload_len < 0) || ((uint32_t) data.payload_len > payload_capacity))
        {
            return QR_DECODER_OUTPUT_TOO_SMALL;
        }

        memcpy(p_payload, data.payload, (size_t) data.payload_len);
        *p_payload_length = (uint16_t) data.payload_len;
        return QR_DECODER_OK;
    }

    return QR_DECODER_DECODE_FAILED;
}

qr_decoder_status_t qr_decoder_decode_gray(const uint8_t * p_gray,
                                           uint16_t width,
                                           uint16_t height,
                                           uint8_t * p_payload,
                                           uint16_t payload_capacity,
                                           uint16_t * p_payload_length)
{
    return qr_decode_gray_into(gp_quirc, p_gray, width, height,
                               p_payload, payload_capacity, p_payload_length);
}

/* ============================================================================
 * 流式解码：Camera 线程只喂帧，独立低优先级线程执行 quirc 解码
 *
 * 重要：quirc_end() 在 M85 上首次解码可能耗时极长甚至卡死（已实测
 * decode_count 停 0）。若解码仍在 Camera 线程（优先级 2）同步执行，
 * 会占死 CPU 并饿死更低优先级的 LVGL 线程（触摸/画面全停）。
 * 因此解码移到独立的优先级 0 线程：即使 quirc 卡死，也只卡住该
 * 线程自身，触摸与采集不受影响。
 * ==========================================================================*/

/* 诊断：最近一次 quirc_end() 解码耗时（ms）与解码次数（J-Link 可读） */
volatile uint32_t s_dbg_qr_decode_ms;
volatile uint32_t s_dbg_qr_decode_count;

/* 解码限频：100ms 一次（10Hz），由解码线程控制。解码线程优先级 0
 * （低于 LVGL/Camera），quirc 卡死也不会饿死触摸/采集；限频仅为
 * 约束 CPU 占用。原 200ms 下从"码入画面"到识别最快也要 ~200ms，
 * 减半后首帧命中延迟更低。 */
#define QR_DECODE_INTERVAL_TICKS   (pdMS_TO_TICKS(100U))
/* 连续 N 帧解码到相同 payload 才确认。实测取药单大码（45x45）多数帧
 * NO_CODE、偶尔成功且不连续，2 帧去抖导致"扫到了但不发布"——quirc 的
 * OK 是完整 ECC 校验后的成功（几乎无误报），1 帧即发布，手持抖动也不怕。 */
#define QR_CONFIRM_FRAMES          (1U)
/* 解码线程栈：qr_decoder_decode_gray 的局部 struct quirc_code（~3.9KB
 * cell_bitmap）+ struct quirc_data（~8.9KB payload）≈ 12.9KB 栈帧，叠加
 * quirc_end 内 otsu 直方图 ~1KB —— 8KB 栈会溢出（SP 下探 → HardFault，曾被
 * 误判为"quirc 卡死"）。24KB（6144 words）留足余量；任务栈来自 FreeRTOS
 * heap（256KB），对 BSS 无影响。 */
#define QR_DECODE_TASK_STACK       (6144U)
#define QR_DECODE_TASK_PRIORITY    (0U)

/* 摄像头源尺寸 = 解码尺寸：OV7725 VGA 640x480（CEU 采集 640x480）。
 * VGA 下 45x45 取药单 JSON 码模块像素翻倍，无需软件放大，直接解码。 */
#define QR_SOURCE_WIDTH          (640U)
#define QR_SOURCE_HEIGHT         (480U)
#define QR_DECODE_WIDTH          (640U)
#define QR_DECODE_HEIGHT         (480U)

static bool           s_enabled;
static bool           s_quirc_ready;
static TickType_t     s_last_decode_tick;
/* UYVY -> Y 灰度缓冲（Camera 线程写/解码线程读）。640x480 = 307KB，
 * SRAM 放不下 → 下沉 SDRAM（.sdram_noinit） */
static uint8_t        s_gray[QR_SOURCE_WIDTH * QR_SOURCE_HEIGHT]
    __attribute__((section(".sdram_noinit"), aligned(32)));
static volatile bool  s_frame_ready;           /* 新帧就绪标志 */
static char           s_pending[QR_DECODER_PAYLOAD_MAX];
static uint32_t       s_pending_len;
static uint32_t       s_pending_match;
static uint8_t        s_pending_type;          /* qr_decoder_code_type_t（去抖对象类型） */
static bool           s_published;             /* 当前 payload 是否已发布（内容变化才重置） */

/* 板端自检状态 */
static volatile bool  s_selftest_pending;      /* 有自检请求待处理（解码线程消费） */
static uint8_t        s_selftest_which;        /* 请求的 qr_selftest_id_t */
static volatile bool  s_selftest_active;       /* 自检渲染/解码进行中（Camera 线程喂帧跳过） */
static qr_selftest_result_t s_selftest_result; /* 最近一次自检结果（临界区复制） */
static volatile bool  s_selftest_result_dirty;

/* J-Link 诊断触发：写 1/2 触发自检（见 qr_decoder.h）。
 * 放 .noinit（复位不清零，同 s_ttf_burn_mode 惯例）：PC 在复位后冷缓存
 * 下直写可见；普通 .bss 变量会被启动清零代码写进缓存，J-Link 直写被掩蔽。 */
volatile uint32_t s_dbg_qr_selftest_trigger __attribute__((section(".noinit")));

/* 自检阶段（J-Link 可读）：0=空闲 1=quirc init 2=渲染 3=解码 4=已发布 */
volatile uint32_t s_dbg_qr_selftest_stage;

/* 流式解码诊断（J-Link 可读，解码线程更新后 clean）：
 *  - s_dbg_qr_last_status：最近一次 decode_gray 返回（0=OK 1=NO_CODE 2=DECODE_FAILED）
 *  - s_dbg_qr_frame_mean：最近一帧 s_gray 平均亮度（0=黑屏 ~235=过曝/全白）
 *  - s_dbg_qr_stat_ok / _no_code / _failed：扫码期间累计各结果次数（区分
 *    "画面里没码" vs "有码但解码失败"）
 *  - s_qr_frame_snapshot：最近一帧**解码输入**（640x480，SDRAM，J-Link 读看实拍）
 *  - s_qr_frame_snapshot：s_gray 的稳定副本（320x240，防 Camera 并发写撕裂） */
volatile uint32_t s_dbg_qr_last_status;
volatile uint32_t s_dbg_qr_frame_mean;
volatile uint32_t s_dbg_qr_stat_ok;
volatile uint32_t s_dbg_qr_stat_no_code;
volatile uint32_t s_dbg_qr_stat_failed;
static uint8_t s_qr_frame_snapshot[QR_SOURCE_WIDTH * QR_SOURCE_HEIGHT]
    __attribute__((section(".sdram_noinit"), aligned(32)));

/* 平滑重试缓冲：3x3 均值去噪（高密度码 ECC 失败时重试用，SDRAM 160KB，
 * 只对 ROI 尺寸做——全画面平滑太贵，兜底失败直接放弃等下一帧） */
static uint8_t s_smooth_buf[QR_ROI_WIDTH * QR_ROI_HEIGHT]
    __attribute__((section(".sdram_noinit"), aligned(32)));

/* 中央 ROI 裁剪缓冲（解码线程独占，SDRAM 160KB） */
static uint8_t s_roi_buf[QR_ROI_WIDTH * QR_ROI_HEIGHT]
    __attribute__((section(".sdram_noinit"), aligned(32)));

/* 3x3 均值平滑（去单像素噪声/屏幕颗粒，改善高密度码 ECC 采样）。
 * 仅在主解码 DECODE_FAILED 后重试用，避免对清晰帧引入模糊。 */
static void qr_smooth3x3(const uint8_t * p_src, uint8_t * p_dst, uint32_t w, uint32_t h)
{
    for (uint32_t y = 0U; y < h; y++)
    {
        uint32_t const y0 = (y > 0U) ? y - 1U : 0U;
        uint32_t const y1 = (y + 1U < h) ? y + 1U : y;
        for (uint32_t x = 0U; x < w; x++)
        {
            uint32_t const x0 = (x > 0U) ? x - 1U : 0U;
            uint32_t const x1 = (x + 1U < w) ? x + 1U : x;
            uint32_t sum = (uint32_t) p_src[y0 * w + x0] + p_src[y0 * w + x] + p_src[y0 * w + x1] +
                           p_src[y * w + x0] + p_src[y * w + x] + p_src[y * w + x1] +
                           p_src[y1 * w + x0] + p_src[y1 * w + x] + p_src[y1 * w + x1];
            p_dst[y * w + x] = (uint8_t) (sum / 9U);
        }
    }
}

static qr_decoder_result_t s_result;           /* LVGL 线程读取（短临界区复制） */
static volatile bool       s_result_dirty;

void qr_decoder_reset(void)
{
    s_pending_len = 0U;
    s_pending_match = 0U;
    s_pending_type = QR_DECODER_TYPE_QR;
    s_published = false;
    s_result_dirty = false;
    s_frame_ready = false;
}

void qr_decoder_set_enabled(bool enable)
{
    s_enabled = enable;
    if (!enable)
    {
        /* 离开识别页：清空待确认状态，避免残留旧码在下次进入时立即发布 */
        s_pending_len = 0U;
        s_pending_match = 0U;
        s_pending_type = QR_DECODER_TYPE_QR;
        s_published = false;
        s_result_dirty = false;
        s_frame_ready = false;
    }
}

/***********************************************************************************************************************
 * Camera 线程：只提取 Y 灰度到共享缓冲（快速，无解码），置帧就绪标志
 **********************************************************************************************************************/
void qr_decoder_feed_frame(const uint8_t * p_uyvy422, uint32_t w, uint32_t h)
{
    if ((NULL == p_uyvy422) || (w != QR_SOURCE_WIDTH) || (h != QR_SOURCE_HEIGHT))
    {
        return;
    }

    /* 板端自检渲染/解码进行中：跳过本帧，避免覆盖自检灰度缓冲 */
    if (s_selftest_active)
    {
        return;
    }

    /* UYVY422 内存序为 Y0 Cb Y1 Cr（每 4 字节 2 像素），Y 在偶数偏移 */
    for (uint32_t y = 0U; y < QR_SOURCE_HEIGHT; y++)
    {
        uint8_t const * p_row = p_uyvy422 + ((size_t) y * QR_SOURCE_WIDTH * 2U);
        uint8_t * p_dst = &s_gray[(size_t) y * QR_SOURCE_WIDTH];
        for (uint32_t x = 0U; x < QR_SOURCE_WIDTH; x++)
        {
            p_dst[x] = p_row[x * 2U];
        }
    }
    s_frame_ready = true;
}

/***********************************************************************************************************************
 * 独立解码线程：轮询新帧 → 限频 quirc 解码 → 去抖发布
 **********************************************************************************************************************/
void qr_decoder_task_entry(void * pv_parameter)
{
    FSP_PARAMETER_NOT_USED(pv_parameter);

    for (;;)
    {
        /* 板端自检：任何页面可触发（Device 页"运行自检"/J-Link 写触发变量），
         * 渲染嵌入的已知二维码并解码。与页面启用状态无关，也不阻塞其它线程。 */
        uint32_t const selftest_trigger = s_dbg_qr_selftest_trigger;
        if (s_selftest_pending || (0U != selftest_trigger))
        {
            s_selftest_pending = false;
            s_dbg_qr_selftest_trigger = 0U;
            /* J-Link 触发：1=MED001, 2=RXORDER；按钮触发：s_selftest_which */
            uint8_t const which = (0U != selftest_trigger)
                                      ? (uint8_t) (selftest_trigger - 1U)
                                      : s_selftest_which;

            qr_selftest_result_t r;
            memset(&r, 0, sizeof(r));
            r.done = true;
            r.which = which;

            /* 懒初始化 quirc（640x480，图像缓冲从 SDRAM 池分配） */
            s_dbg_qr_selftest_stage = 1U;
            if (!s_quirc_ready)
            {
                if (qr_decoder_init(QR_DECODE_WIDTH, QR_DECODE_HEIGHT))
                {
                    s_quirc_ready = true;
                }
            }

            if (s_quirc_ready)
            {
                uint8_t payload[QR_DECODER_PAYLOAD_MAX];
                uint16_t payload_len = 0U;

                s_dbg_qr_selftest_stage = 2U;
                s_selftest_active = true;
                qr_selftest_render(which, s_gray, QR_SOURCE_WIDTH, QR_SOURCE_HEIGHT);
                s_dbg_qr_selftest_stage = 3U;
                SCB_CleanDCache_by_Addr((uint32_t *) &s_dbg_qr_selftest_stage,
                                        (int32_t) sizeof(s_dbg_qr_selftest_stage));
                TickType_t const t0 = xTaskGetTickCount();
                qr_decoder_status_t const status = qr_decoder_decode_gray(
                    s_gray, QR_DECODE_WIDTH, QR_DECODE_HEIGHT,
                    payload, sizeof(payload), &payload_len);
                TickType_t const t1 = xTaskGetTickCount();
                s_selftest_active = false;
                s_dbg_qr_selftest_stage = 4U;

                r.status = (uint8_t) status;
                r.decode_ms = (uint32_t) ((t1 - t0) * portTICK_PERIOD_MS);
                if ((QR_DECODER_OK == status) && (payload_len <= QR_DECODER_PAYLOAD_MAX))
                {
                    memcpy(r.payload, payload, payload_len);
                    r.payload_len = payload_len;
                }
                s_dbg_qr_decode_ms = r.decode_ms;
                s_dbg_qr_decode_count++;
            }
            else
            {
                r.status = (uint8_t) QR_DECODER_NOT_READY;
            }

            taskENTER_CRITICAL();
            memcpy(&s_selftest_result, &r, sizeof(r));
            s_selftest_result_dirty = true;
            taskEXIT_CRITICAL();

            /* J-Link 探针可读：clean 缓存行（SWD 直读绕过 D-Cache） */
            SCB_CleanDCache_by_Addr((uint32_t *) &s_dbg_qr_selftest_trigger,
                                    (int32_t) sizeof(s_dbg_qr_selftest_trigger));
            SCB_CleanDCache_by_Addr((uint32_t *) &s_dbg_qr_decode_ms,
                                    (int32_t) sizeof(s_dbg_qr_decode_ms));
            SCB_CleanDCache_by_Addr((uint32_t *) &s_dbg_qr_decode_count,
                                    (int32_t) sizeof(s_dbg_qr_decode_count));
            SCB_CleanDCache_by_Addr((uint32_t *) &s_dbg_qr_selftest_stage,
                                    (int32_t) sizeof(s_dbg_qr_selftest_stage));
            SCB_CleanDCache_by_Addr((uint32_t *) &s_selftest_result,
                                    (int32_t) sizeof(s_selftest_result));
        }

        if (s_enabled && s_frame_ready)
        {
            s_frame_ready = false;

            /* 懒初始化 quirc（640x480，图像缓冲从 SDRAM 池分配） */
            if (!s_quirc_ready)
            {
                if (qr_decoder_init(QR_DECODE_WIDTH, QR_DECODE_HEIGHT))
                {
                    s_quirc_ready = true;
                }
            }

            if (s_quirc_ready)
            {
                /* 限频：距上次解码不足 100ms 跳过 */
                TickType_t const now = xTaskGetTickCount();
                if ((now - s_last_decode_tick) >= QR_DECODE_INTERVAL_TICKS)
                {
                    s_last_decode_tick = now;

                    uint8_t payload[QR_DECODER_PAYLOAD_MAX];
                    uint16_t payload_len = 0U;
                    uint8_t  decode_type = QR_DECODER_TYPE_QR;
                    bool     have_result = false;

                    /* 1) 稳定帧副本：Camera 线程每帧覆盖 s_gray，直接读取可能
                     * 在复制/解码期间被并发写坏（撕裂帧）→ 先 memcpy 到 SDRAM
                     * 快照（解码线程独占）。VGA 640x480 源直接解码（不再放大）。 */
                    memcpy(s_qr_frame_snapshot, s_gray,
                           (size_t) QR_SOURCE_WIDTH * QR_SOURCE_HEIGHT);
                    TickType_t const t0 = xTaskGetTickCount();

                    /* 2) 中央 ROI 裁剪（原分辨率，不缩放）：主解码输入 */
                    for (uint32_t y = 0U; y < QR_ROI_HEIGHT; y++)
                    {
                        memcpy(&s_roi_buf[y * QR_ROI_WIDTH],
                               &s_qr_frame_snapshot[(QR_ROI_Y + y) * QR_SOURCE_WIDTH + QR_ROI_X],
                               QR_ROI_WIDTH);
                    }

                    /* 3) 二维码解码（QR-ONLY：已移除 EAN-13 一维条码预扫描——
                     * 二维码画面本身黑白过渡密集，会被条码行扫描误判为"候选
                     * 条码段"并反复空转，拖慢每帧。现在直接进 quirc。）
                     * 决策树：
                     *   a) ROI 解出 OK        → 发布
                     *   b) ROI 无码（NO_CODE）→ 码可能不在中央，全画面兜底
                     *   c) ROI 有候选但解不出 → ROI 3x3 平滑重试；平滑后仍
                     *      无码（候选是噪声）→ 全画面兜底 */
                    qr_decoder_status_t status = qr_decode_gray_into(
                        gp_quirc_roi, s_roi_buf, QR_ROI_WIDTH, QR_ROI_HEIGHT,
                        payload, sizeof(payload), &payload_len);
                    if (QR_DECODER_NO_CODE == status)
                    {
                        status = qr_decoder_decode_gray(
                            s_qr_frame_snapshot, QR_DECODE_WIDTH, QR_DECODE_HEIGHT,
                            payload, sizeof(payload), &payload_len);
                    }
                    else if ((QR_DECODER_OK != status) && (QR_DECODER_NO_CODE != status))
                    {
                        /* 高密度码（45x45 取药单 JSON）ECC 失败重试：3x3 平滑
                         * 去噪后再解，消除屏幕颗粒/单像素噪声导致的模块采样
                         * 错位。仅在失败时重试，避免清晰帧被模糊化。 */
                        qr_smooth3x3(s_roi_buf, s_smooth_buf,
                                     QR_ROI_WIDTH, QR_ROI_HEIGHT);
                        status = qr_decode_gray_into(
                            gp_quirc_roi, s_smooth_buf, QR_ROI_WIDTH, QR_ROI_HEIGHT,
                            payload, sizeof(payload), &payload_len);
                        if (QR_DECODER_NO_CODE == status)
                        {
                            status = qr_decoder_decode_gray(
                                s_qr_frame_snapshot, QR_DECODE_WIDTH, QR_DECODE_HEIGHT,
                                payload, sizeof(payload), &payload_len);
                        }
                    }
                    TickType_t const t1 = xTaskGetTickCount();
                    s_dbg_qr_decode_ms = (uint32_t) ((t1 - t0) * portTICK_PERIOD_MS);
                    s_dbg_qr_decode_count++;
                    s_dbg_qr_last_status = (uint32_t) status;
                    /* 累计统计（扫码期间各结果次数） */
                    if (QR_DECODER_OK == status)
                    {
                        s_dbg_qr_stat_ok++;
                        have_result = true;
                    }
                    else if (QR_DECODER_NO_CODE == status)
                    {
                        s_dbg_qr_stat_no_code++;
                    }
                    else
                    {
                        s_dbg_qr_stat_failed++;
                    }
                    /* 帧亮度统计：采样 s_gray（每 64 像素取 1），判断画面是否
                     * 黑屏/过曝/正常（0=全黑 ~235=全白） */
                    {
                        uint32_t sum = 0U;
                        uint32_t const step = 64U;
                        uint32_t const samples =
                            (QR_SOURCE_WIDTH * QR_SOURCE_HEIGHT + step - 1U) / step;
                        for (uint32_t i = 0U; i < QR_SOURCE_WIDTH * QR_SOURCE_HEIGHT; i += step)
                        {
                            sum += s_gray[i];
                        }
                        s_dbg_qr_frame_mean = sum / (samples > 0U ? samples : 1U);
                    }
                    /* J-Link 探针可读（SWD 直读绕过 D-Cache） */
                    SCB_CleanDCache_by_Addr((uint32_t *) &s_dbg_qr_decode_ms,
                                            (int32_t) sizeof(s_dbg_qr_decode_ms));
                    SCB_CleanDCache_by_Addr((uint32_t *) &s_dbg_qr_decode_count,
                                            (int32_t) sizeof(s_dbg_qr_decode_count));
                    SCB_CleanDCache_by_Addr((uint32_t *) &s_dbg_qr_last_status,
                                            (int32_t) sizeof(s_dbg_qr_last_status));
                    SCB_CleanDCache_by_Addr((uint32_t *) &s_dbg_qr_frame_mean,
                                            (int32_t) sizeof(s_dbg_qr_frame_mean));
                    SCB_CleanDCache_by_Addr((uint32_t *) &s_dbg_qr_stat_ok,
                                            (int32_t) sizeof(s_dbg_qr_stat_ok));
                    SCB_CleanDCache_by_Addr((uint32_t *) &s_dbg_qr_stat_no_code,
                                            (int32_t) sizeof(s_dbg_qr_stat_no_code));
                    SCB_CleanDCache_by_Addr((uint32_t *) &s_dbg_qr_stat_failed,
                                            (int32_t) sizeof(s_dbg_qr_stat_failed));
                    /* 快照 clean：s_qr_frame_snapshot（解码输入 640x480）在 SDRAM
                     * （写回缓存），J-Link 经 SWD 直读绕过 D-Cache，需 clean 才能
                     * 读到最新一帧（诊断/实拍分析用） */
                    SCB_CleanDCache_by_Addr((uint32_t *) s_qr_frame_snapshot,
                                            (int32_t) (QR_DECODE_WIDTH * QR_DECODE_HEIGHT));

                    if (have_result)
                    {
                        /* 去抖：相同类型+payload 连续 QR_CONFIRM_FRAMES 帧才发布；
                         * 发布后保持 s_published，内容变化才允许再次发布 */
                        if ((s_pending_type == decode_type) &&
                            (s_pending_len == payload_len) &&
                            (0U == memcmp(s_pending, payload, payload_len)))
                        {
                            s_pending_match++;
                        }
                        else
                        {
                            s_pending_type = decode_type;
                            s_pending_len = payload_len;
                            memcpy(s_pending, payload, payload_len);
                            s_pending_match = 1U;
                            s_published = false;
                        }

                        if (!s_published && (s_pending_match >= QR_CONFIRM_FRAMES))
                        {
                            taskENTER_CRITICAL();
                            s_result.valid = true;
                            s_result.type = s_pending_type;
                            s_result.payload_len = s_pending_len;
                            memcpy(s_result.payload, s_pending, s_pending_len);
                            s_result.update_tick = (uint32_t) xTaskGetTickCount();
                            s_result_dirty = true;
                            taskEXIT_CRITICAL();
                            s_published = true;
                        }
                    }
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(20U));
    }
}

bool qr_decoder_start_task(void)
{
    static bool s_task_started;

    if (s_task_started)
    {
        return false;
    }

    BaseType_t const err = xTaskCreate(qr_decoder_task_entry,
                                       "QRDecode",
                                       QR_DECODE_TASK_STACK,
                                       NULL,
                                       QR_DECODE_TASK_PRIORITY,
                                       NULL);
    s_task_started = (pdPASS == err);
    return s_task_started;
}

bool qr_decoder_get_result(qr_decoder_result_t * p_out)
{
    if (NULL == p_out)
    {
        return false;
    }

    taskENTER_CRITICAL();
    bool const dirty = s_result_dirty;
    if (dirty)
    {
        memcpy(p_out, &s_result, sizeof(s_result));
        s_result_dirty = false;
    }
    taskEXIT_CRITICAL();

    return dirty;
}

void qr_decoder_selftest_request(uint8_t which)
{
    if (which >= QR_SELFTEST_COUNT)
    {
        return;
    }
    s_selftest_which = which;
    s_selftest_pending = true;
}

bool qr_decoder_selftest_get(qr_selftest_result_t * p_out)
{
    if (NULL == p_out)
    {
        return false;
    }

    taskENTER_CRITICAL();
    bool const dirty = s_selftest_result_dirty;
    if (dirty)
    {
        memcpy(p_out, &s_selftest_result, sizeof(s_selftest_result));
        s_selftest_result_dirty = false;
    }
    taskEXIT_CRITICAL();

    return dirty;
}
