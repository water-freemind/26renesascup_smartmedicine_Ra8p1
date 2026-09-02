#include "camera_convert.h"

/**********************************************************************************************************************
 * 局部工具：YCbCr(有限范围 BT.601) 单像素 → RGB 565
 **********************************************************************************************************************/
static inline uint16_t ycbcr_to_rgb565(uint8_t y, uint8_t cb, uint8_t cr)
{
    /* BT.601 limited range 整数近似：
     *   C = Y - 16, D = Cb - 128, E = Cr - 128
     *   R = (298*C + 409*E + 128) >> 8
     *   G = (298*C - 100*D - 208*E + 128) >> 8
     *   B = (298*C + 516*D + 128) >> 8   */
    int32_t c  = (int32_t) y  - 16;
    int32_t d  = (int32_t) cb - 128;
    int32_t e  = (int32_t) cr - 128;

    int32_t r = (298 * c + 409 * e + 128) >> 8;
    int32_t g = (298 * c - 100 * d - 208 * e + 128) >> 8;
    int32_t b = (298 * c + 516 * d + 128) >> 8;

    if (r < 0) { r = 0; } else if (r > 255) { r = 255; }
    if (g < 0) { g = 0; } else if (g > 255) { g = 255; }
    if (b < 0) { b = 0; } else if (b > 255) { b = 255; }

    return (uint16_t) ((((uint16_t) r & 0xF8U) << 8) |
                       (((uint16_t) g & 0xFCU) << 3) |
                       (((uint16_t) b & 0xF8U) >> 3));
}

/**********************************************************************************************************************
 * CEU SRAM 中的 YCbCr422(YUYV) → RGB565（单行）
 * 每 4 字节 = Y0, Cb, Y1, Cr → 输出 2 个像素。
 * OV7725 的总线输出本身为 UYVY；此处的顺序是 CEU 16 位小端写入后的内存视图。
 **********************************************************************************************************************/
static void convert_row(const uint8_t * p_src, uint16_t * p_dst, uint32_t width)
{
    uint32_t i;

    for (i = 0; i < width; i += 2)
    {
        uint8_t y0 = p_src[0];
        uint8_t cb = p_src[1];
        uint8_t y1 = p_src[2];
        uint8_t cr = p_src[3];
        p_src += 4;

        p_dst[0] = ycbcr_to_rgb565(y0, cb, cr);
        p_dst[1] = ycbcr_to_rgb565(y1, cb, cr);
        p_dst += 2;
    }
}

/**********************************************************************************************************************
 * YCbCr422(UYVY) → RGB565（整帧）
 **********************************************************************************************************************/
void camera_ycbcr422_to_rgb565(const uint8_t * p_src, uint16_t * p_dst,
                               uint32_t width, uint32_t height)
{
    uint32_t row;
    uint32_t row_bytes = width * 2U;   /* YCbCr422: 2 字节/像素 */

    for (row = 0; row < height; row++)
    {
        convert_row(p_src, p_dst, width);
        p_src += row_bytes;
        p_dst += width;
    }
}

/**********************************************************************************************************************
 * 最近邻缩放（RGB565）
 **********************************************************************************************************************/
void camera_scale_rgb565_nearest(const uint16_t * p_src,
                                 uint32_t src_w, uint32_t src_h,
                                 uint16_t * p_dst,
                                 uint32_t dst_w, uint32_t dst_h)
{
    uint32_t dy, dx;

    if ((0U == src_w) || (0U == src_h) || (0U == dst_w) || (0U == dst_h))
    {
        return;
    }

    for (dy = 0; dy < dst_h; dy++)
    {
        uint32_t src_y = (dy * src_h) / dst_h;
        const uint16_t * p_src_row = p_src + (src_y * src_w);

        for (dx = 0; dx < dst_w; dx++)
        {
            uint32_t src_x = (dx * src_w) / dst_w;
            p_dst[(dy * dst_w) + dx] = p_src_row[src_x];
        }
    }
}

/**********************************************************************************************************************
 * 组合：YCbCr422 → RGB565 → 缩放（按行处理，仅需一行临时缓冲）
 **********************************************************************************************************************/
void camera_frame_to_rgb565_scaled(const uint8_t * p_ycbcr422,
                                   uint32_t src_w, uint32_t src_h,
                                   uint16_t * p_rgb565,
                                   uint32_t dst_w, uint32_t dst_h)
{
    static uint16_t s_row_scratch[640U];   /* 单行 RGB565（最大 VGA 宽 640） */
    uint32_t         row_bytes = src_w * 2U;
    uint32_t         dy;

    if ((0U == src_w) || (0U == src_h) || (0U == dst_w) || (0U == dst_h))
    {
        return;
    }

    for (dy = 0; dy < dst_h; dy++)
    {
        uint32_t src_y   = (dy * src_h) / dst_h;
        const uint8_t * p_src_row = p_ycbcr422 + (src_y * row_bytes);
        uint16_t      * p_dst_row = p_rgb565   + (dy * dst_w);
        uint32_t        dx;

        /* 1. 源行 YCbCr422 → RGB565 */
        convert_row(p_src_row, s_row_scratch, src_w);

        /* 2. 行内最近邻缩放 */
        for (dx = 0; dx < dst_w; dx++)
        {
            uint32_t src_x = (dx * src_w) / dst_w;
            p_dst_row[dx] = s_row_scratch[src_x];
        }
    }
}

void camera_frame_to_gray_rgb565_scaled(const uint8_t * p_ycbcr422,
                                        uint32_t src_w, uint32_t src_h,
                                        uint16_t * p_rgb565,
                                        uint32_t dst_w, uint32_t dst_h)
{
    uint32_t row_bytes;
    uint32_t dy;

    if ((NULL == p_ycbcr422) || (NULL == p_rgb565) ||
        (0U == src_w) || (0U == src_h) || (0U == dst_w) || (0U == dst_h))
    {
        return;
    }

    row_bytes = src_w * 2U;
    for (dy = 0U; dy < dst_h; dy++)
    {
        uint32_t src_y = (dy * src_h) / dst_h;
        const uint8_t * p_src_row = p_ycbcr422 + (src_y * row_bytes);
        uint16_t * p_dst_row = p_rgb565 + (dy * dst_w);

        for (uint32_t dx = 0U; dx < dst_w; dx++)
        {
            uint32_t src_x = (dx * src_w) / dst_w;
            /* CEU stores UYVY bus data as little-endian half words in SRAM:
             * memory byte order is Y0, Cb, Y1, Cr.  Y is therefore byte 0
             * of each two-byte pixel slot; selecting byte 1 samples Cb/Cr
             * and produces the observed one-column-on/one-column-off stripes. */
            uint8_t y = p_src_row[src_x * 2U];
            p_dst_row[dx] = (uint16_t) ((((uint16_t) y & 0xF8U) << 8) |
                                        (((uint16_t) y & 0xFCU) << 3) |
                                        (((uint16_t) y & 0xF8U) >> 3));
        }
    }
}
