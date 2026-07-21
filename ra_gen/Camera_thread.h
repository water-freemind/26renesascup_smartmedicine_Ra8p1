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
#include "r_gpt.h"
#include "r_timer_api.h"
#include "r_capture_api.h"
            #include "r_ceu.h"
FSP_HEADER
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
