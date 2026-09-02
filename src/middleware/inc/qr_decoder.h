#ifndef QR_DECODER_H
#define QR_DECODER_H

#include <stdbool.h>
#include <stdint.h>

/* Application-facing QR decoder wrapper.  quirc remains an implementation
 * detail in third_party and must not be included by application code. */
typedef enum e_qr_decoder_status
{
    QR_DECODER_OK = 0,
    QR_DECODER_NO_CODE,
    QR_DECODER_DECODE_FAILED,
    QR_DECODER_NOT_READY,
    QR_DECODER_INVALID_ARGUMENT,
    QR_DECODER_IMAGE_SIZE_MISMATCH,
    QR_DECODER_OUTPUT_TOO_SMALL,
    QR_DECODER_NO_MEMORY,
} qr_decoder_status_t;

/* ============================================================================
 * 同步单帧解码 API（底层，供需要直接解一帧灰度的调用方使用）
 * ==========================================================================*/
bool qr_decoder_init(uint16_t width, uint16_t height);
void qr_decoder_deinit(void);

/* Decode the first valid code in one 8-bit grayscale frame.  Payload is
 * binary-safe: use p_payload_length rather than assuming a trailing NUL. */
qr_decoder_status_t qr_decoder_decode_gray(const uint8_t * p_gray,
                                           uint16_t width,
                                           uint16_t height,
                                           uint8_t * p_payload,
                                           uint16_t payload_capacity,
                                           uint16_t * p_payload_length);

/* ============================================================================
 * 流式解码 API（Camera 线程喂帧 + 独立解码线程 + LVGL 线程取结果）
 *
 * 线程模型：
 *  - Camera 线程：qr_decoder_set_enabled(true) 后每帧调用
 *    qr_decoder_feed_frame()（只提取 Y 灰度，不执行 quirc）；
 *  - 解码线程（优先级 0，qr_decoder_start_task 创建）：轮询新帧 → 限频
 *    100ms → quirc 解码 → 连续帧去抖后发布；
 *  - LVGL 线程：qr_decoder_get_result() 读取最近一次稳定结果。
 *
 * 内存：quirc 的 malloc/calloc/free 已重定向到专用 SDRAM bump 池
 * （third_party/quirc 内 640KB 预留区，见 quirc.c QUIRC_SDRAM_POOL_*），
 * 640x480 图像 + flood-fill 工作区不占用 FreeRTOS heap 与 SRAM。
 * ==========================================================================*/
/* 解码 payload 上限。实测取药单二维码 JSON 已达 142 字节（2 药），多药单
 * 更长——128 会导致 OUTPUT_TOO_SMALL 丢弃（早期"识别不到"根因之一）。
 * 512 按 8 药估算留余量；s_pending/s_result 在 BSS，解码线程栈 24KB 足够。 */
#define QR_DECODER_PAYLOAD_MAX   (512U)

/* 解码结果类型：当前为 QR-ONLY（按需求已移除 EAN-13 一维条码预扫描，
 * 每帧直接进 quirc）。EAN13 枚举保留仅为兼容，实际不会再产生。 */
typedef enum e_qr_decoder_code_type
{
    QR_DECODER_TYPE_QR = 0,   /* quirc 二维码 */
    QR_DECODER_TYPE_EAN13,    /* 一维 EAN-13 商品条码 */
} qr_decoder_code_type_t;

typedef struct
{
    bool     valid;        /* 是否存在有效解码结果 */
    uint8_t  type;         /* qr_decoder_code_type_t：QR / EAN13 */
    char     payload[QR_DECODER_PAYLOAD_MAX]; /* 解码内容（原始字节，可含 UTF-8） */
    uint32_t payload_len;  /* 实际字节数 */
    uint32_t update_tick;  /* 结果发布时间（FreeRTOS tick），变化即新结果 */
} qr_decoder_result_t;

/* 启用/停用解码（仅 Scan/Pickup 页启用，其它页面不占 CPU）。 */
void qr_decoder_set_enabled(bool enable);

/* 重置去抖/发布状态：清空待确认与已发布记录，允许重新解码同一二维码
 * （Pickup"重新扫描"按钮调用）。不影响 quirc 实例与灰度缓冲。 */
void qr_decoder_reset(void);

/* Camera 线程：喂入一帧 UYVY422（w*h*2 字节），只提取 Y 灰度到共享缓冲并
 * 置帧就绪标志。**绝不执行 quirc 解码**——解码由独立的低优先级线程完成，
 * 避免 quirc 卡死拖垮高优先级的 Camera/LVGL 线程（触摸/预览）。 */
void qr_decoder_feed_frame(const uint8_t * p_uyvy422, uint32_t w, uint32_t h);

/* 创建独立解码线程（优先级 0，低于 LVGL/Camera；quirc 卡死不影响触摸与采集）。
 * @return true 创建成功；重复调用返回 false。 */
bool qr_decoder_start_task(void);

/* 解码线程入口（FreeRTOS 任务）。 */
void qr_decoder_task_entry(void * pv_parameter);

/* LVGL 线程：读取最近一次稳定结果。 */
bool qr_decoder_get_result(qr_decoder_result_t * p_out);

/* ============================================================================
 * 板端自检（无需摄像头/光学）：解码线程渲染嵌入的已知二维码并解码，
 * 验证 quirc 在 M85 上可用性与耗时（Device 页"运行自检"触发，或 J-Link
 * 写 s_dbg_qr_selftest_trigger）。触发不阻塞，结果异步就绪。
 * ==========================================================================*/
typedef struct
{
    bool     done;       /* 本次自检是否已完成 */
    uint8_t  which;      /* qr_selftest_id_t */
    uint8_t  status;     /* qr_decoder_status_t */
    uint32_t decode_ms;  /* quirc 解码耗时（decode_gray 全程） */
    char     payload[QR_DECODER_PAYLOAD_MAX]; /* 解码内容（status==OK 时有效） */
    uint16_t payload_len;
} qr_selftest_result_t;

/* 请求执行一次自检（which = qr_selftest_id_t）。立即返回；解码线程异步执行，
 * 完成后 qr_decoder_selftest_get() 返回 true 一次。 */
void qr_decoder_selftest_request(uint8_t which);

/* 读取最近一次自检结果；返回 true 表示有新的已完成结果（读取后清除）。 */
bool qr_decoder_selftest_get(qr_selftest_result_t * p_out);

/* J-Link 可写诊断触发：写 1 = 自检 MED001，写 2 = 自检 RXORDER，写 0 = 空闲。
 * 解码线程处理完毕后清零。 */
extern volatile uint32_t s_dbg_qr_selftest_trigger;

#endif /* QR_DECODER_H */
