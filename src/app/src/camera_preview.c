#include "camera_preview.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

/**********************************************************************************************************************
 * 局部状态
 **********************************************************************************************************************/
static uint16_t s_preview_buf[CAMERA_PREVIEW_WIDTH * CAMERA_PREVIEW_HEIGHT];
static volatile bool s_new_frame;

static lv_image_dsc_t s_image_desc =
{
    .header =
    {
        .cf = LV_COLOR_FORMAT_RGB565,
        .w  = CAMERA_PREVIEW_WIDTH,
        .h  = CAMERA_PREVIEW_HEIGHT,
    },
    .data_size = CAMERA_PREVIEW_WIDTH * CAMERA_PREVIEW_HEIGHT * 2U,
    .data      = (const uint8_t *) s_preview_buf,
};

/**********************************************************************************************************************
 * Camera 线程：写入新帧
 **********************************************************************************************************************/
void camera_preview_put_frame(const uint16_t * p_rgb565, uint32_t w, uint32_t h)
{
    uint32_t copy_w = (w < CAMERA_PREVIEW_WIDTH) ? w : CAMERA_PREVIEW_WIDTH;
    uint32_t copy_h = (h < CAMERA_PREVIEW_HEIGHT) ? h : CAMERA_PREVIEW_HEIGHT;
    uint32_t row;

    if (NULL == p_rgb565)
    {
        return;
    }

    /* NO taskENTER_CRITICAL around this copy: on Cortex-M85 it masks all
     * interrupts for the ~1-2 ms needed to memcpy 345 KB, which stalls any
     * in-flight IIC0 transaction with SDA held low and kills the shared-bus
     * CST816S reads (measured: SDAI=0/BBSY=1, read_fail floods while the
     * camera preview runs).  A torn frame during the copy is acceptable
     * for a live preview; the s_new_frame flag orders the consumer. */
    for (row = 0; row < copy_h; row++)
    {
        memcpy(&s_preview_buf[row * CAMERA_PREVIEW_WIDTH],
               &p_rgb565[row * w],
               copy_w * 2U);
    }
    s_new_frame = true;
}

/**********************************************************************************************************************
 * LVGL 线程：状态查询
 **********************************************************************************************************************/
bool camera_preview_has_new_frame(void)
{
    return s_new_frame;
}

void camera_preview_mark_flushed(void)
{
    s_new_frame = false;
}

lv_image_dsc_t * camera_preview_get_image(void)
{
    return &s_image_desc;
}

/* LVGL 就绪标志（由 LVGL 线程设置） */
extern volatile bool s_lvgl_ready;

bool camera_preview_lvgl_ready(void)
{
    return s_lvgl_ready;
}
