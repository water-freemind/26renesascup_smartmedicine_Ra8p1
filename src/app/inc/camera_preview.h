#ifndef CAMERA_PREVIEW_H
#define CAMERA_PREVIEW_H

#include <stdint.h>
#include <stdbool.h>

#include "lvgl.h"

/**********************************************************************************************************************
 * 摄像头 → LVGL 屏幕预览共享模块
 *
 * 线程模型：
 *   Camera 线程：camera_preview_put_frame() 写入缩放后的 RGB565 帧
 *   LVGL 线程：  camera_preview_get_image() 获取 lv_image 描述，lv_timer_handler 前调用
 *                camera_preview_refresh() 检查新帧并触发重绘
 *
 * 缓冲：480x360 RGB565（对应 320x240 源按 4:3 缩放，居中显示在 480x640 屏上）
 **********************************************************************************************************************/

#define CAMERA_PREVIEW_WIDTH     (480U)
#define CAMERA_PREVIEW_HEIGHT    (360U)

/* 返回 lv_image_dsc_t 指针（静态，用于 lv_image_set_src）。 */
lv_image_dsc_t * camera_preview_get_image(void);

/* Camera 线程写入新帧（缩放后 RGB565）。非阻塞，内部带简单锁。 */
void camera_preview_put_frame(const uint16_t * p_rgb565, uint32_t w, uint32_t h);

/* 是否有新帧待刷新（LVGL 线程轮询）。 */
bool camera_preview_has_new_frame(void);

/* 刷新完成标记（LVGL 线程在重绘后调用）。 */
void camera_preview_mark_flushed(void);

/* LVGL 显示是否已就绪（Camera 线程在填屏前查询）。 */
bool camera_preview_lvgl_ready(void);

#endif /* CAMERA_PREVIEW_H */
