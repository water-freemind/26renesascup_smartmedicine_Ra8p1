#ifndef CAMERA_CONVERT_H
#define CAMERA_CONVERT_H

#include <stdint.h>
#include <stddef.h>

/**********************************************************************************************************************
 * 摄像头帧格式转换工具（纯函数，无外设依赖，可在宿主机单元测试）
 *
 * 传感器到 CEU 的总线顺序为 UYVY；但 CEU 以 16 位小端单元写入 SRAM，
 * 因而 CPU 按字节读取的内存顺序为 Y0, Cb, Y1, Cr（YUYV）。
 * 目标格式：RGB565（GLCDC / LVGL / USB 上位机通用）
 **********************************************************************************************************************/

/* 转换一帧 YCbCr422(UYVY) → RGB565。dst 大小 = w * h * 2 字节。 */
void camera_ycbcr422_to_rgb565(const uint8_t * p_src, uint16_t * p_dst,
                               uint32_t width, uint32_t height);

/* 最近邻缩放 RGB565。dst 大小 = dst_w * dst_h * 2 字节。 */
void camera_scale_rgb565_nearest(const uint16_t * p_src,
                                 uint32_t src_w, uint32_t src_h,
                                 uint16_t * p_dst,
                                 uint32_t dst_w, uint32_t dst_h);

/* 组合：YCbCr422 → 缩放后的 RGB565（先转换整帧再缩放，输出 dst_w*dst_h*2 字节）。 */
void camera_frame_to_rgb565_scaled(const uint8_t * p_ycbcr422,
                                   uint32_t src_w, uint32_t src_h,
                                   uint16_t * p_rgb565,
                                   uint32_t dst_w, uint32_t dst_h);

/* 从 UYVY 的 Y（亮度）分量生成灰度 RGB565 并缩放。
 * 用于二维码预览：忽略不可靠的色度，仅保留黑白边缘与细节。 */
void camera_frame_to_gray_rgb565_scaled(const uint8_t * p_ycbcr422,
                                        uint32_t src_w, uint32_t src_h,
                                        uint16_t * p_rgb565,
                                        uint32_t dst_w, uint32_t dst_h);

#endif /* CAMERA_CONVERT_H */
