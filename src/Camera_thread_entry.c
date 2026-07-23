/*
 * Copyright (c) 2025 Renesas Electronics Corporation and/or its affiliates
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "Camera_thread.h"
#include "camera_app.h"

/*******************************************************************************************************************//**
 * @brief  Camera thread entry function
 **********************************************************************************************************************/
void Camera_thread_entry(void * pvParameters)
{
    FSP_PARAMETER_NOT_USED(pvParameters);

    /* Full camera init: XCLK → Power → I2C → OV7725 → CEU */
    camera_app_init();

    /* Main loop: process frames */
    while (1)
    {
        if (g_frame_ready)
        {
            g_frame_ready = false;

            /* TODO: Process g_camera_frame_buffer (640x480 YCbCr422) */

            /* Restart capture for next frame */
            fsp_err_t err = R_CEU_CaptureStart(g_ceu0.p_ctrl, g_camera_frame_buffer);
            if (FSP_SUCCESS != err)
            {
                while (1) { vTaskDelay(1); }
            }
        }

        vTaskDelay(1);
    }
}