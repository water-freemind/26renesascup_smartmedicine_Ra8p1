/* generated thread header file - do not edit */
#ifndef CAMERA_THREAD_H_
#define CAMERA_THREAD_H_
#include "bsp_api.h"
                #include "FreeRTOS.h"
                #include "task.h"
                #include "semphr.h"
                #include "hal_data.h"
                #ifdef __cplusplus
                extern "C" void Camera_thread_entry(void * pvParameters);
                #else
                extern void Camera_thread_entry(void * pvParameters);
                #endif
#include "r_ospi_b.h"
#include "r_spi_flash_api.h"
#include "r_iic_master.h"
#include "r_i2c_master_api.h"
#include "r_usb_basic.h"
#include "r_usb_basic_api.h"
#include "r_usb_pcdc_api.h"
#include "r_gpt.h"
#include "r_timer_api.h"
#include "r_capture_api.h"
            #include "r_ceu.h"
FSP_HEADER
#if OSPI_B_CFG_DMAC_SUPPORT_ENABLE
    #include "r_dmac.h"
#endif
#if OSPI_CFG_DOTF_SUPPORT_ENABLE
    #include "r_sce_if.h"
#endif

extern const spi_flash_instance_t g_ospi0;
extern ospi_b_instance_ctrl_t g_ospi0_ctrl;
extern const spi_flash_cfg_t g_ospi0_cfg;
/* I2C Master on IIC Instance. */
extern const i2c_master_instance_t g_i2c_master0;

/** Access the I2C Master instance using these structures when calling API functions directly (::p_api is not used). */
extern iic_master_instance_ctrl_t g_i2c_master0_ctrl;
extern const i2c_master_cfg_t g_i2c_master0_cfg;

#ifndef camera_i2c_callback
void camera_i2c_callback(i2c_master_callback_args_t * p_args);
#endif
/* Basic on USB Instance. */
extern const usb_instance_t g_basic0;

/** Access the USB instance using these structures when calling API functions directly (::p_api is not used). */
extern usb_instance_ctrl_t g_basic0_ctrl;
extern const usb_cfg_t g_basic0_cfg;

#ifndef NULL
void NULL(void *);
#endif

#if 0 == BSP_CFG_RTOS
#ifndef usb_pcdc_callback
void usb_pcdc_callback(usb_callback_args_t *);
#endif
#endif

#if 2 == BSP_CFG_RTOS
#ifndef usb_pcdc_callback
void usb_pcdc_callback(usb_event_info_t *, usb_hdl_t, usb_onoff_t);
#endif
#endif
/** CDC Driver on USB Instance. */
/** Timer on GPT Instance. */
extern const timer_instance_t g_timer_xclk;

/** Access the GPT instance using these structures when calling API functions directly (::p_api is not used). */
extern gpt_instance_ctrl_t g_timer_xclk_ctrl;
extern const timer_cfg_t g_timer_xclk_cfg;

#ifndef NULL
void NULL(timer_callback_args_t * p_args);
#endif
/* CEU on CAPTURE instance */
            extern const capture_instance_t g_ceu0;
            /* Access the CEU instance using these structures when calling API functions directly (::p_api is not used). */
            extern ceu_instance_ctrl_t g_ceu0_ctrl;
            extern const capture_cfg_t g_ceu0_cfg;
            #ifndef g_ceu0_user_callback
            void g_ceu0_user_callback(capture_callback_args_t * p_args);
            #endif
FSP_FOOTER
#endif /* CAMERA_THREAD_H_ */
