/* generated thread source file - do not edit */
#include "Camera_thread.h"


#if 1
                static StaticTask_t Camera_thread_memory;
                #if defined(__ARMCC_VERSION)           /* AC6 compiler */
                static uint8_t Camera_thread_stack[1024] BSP_PLACE_IN_SECTION(BSP_UNINIT_SECTION_PREFIX ".stack.thread") BSP_ALIGN_VARIABLE(BSP_STACK_ALIGNMENT);
                #else
                static uint8_t Camera_thread_stack[1024] BSP_PLACE_IN_SECTION(BSP_UNINIT_SECTION_PREFIX ".stack.Camera_thread") BSP_ALIGN_VARIABLE(BSP_STACK_ALIGNMENT);
                #endif
                #endif
                TaskHandle_t Camera_thread;
                void Camera_thread_create(void);
                static void Camera_thread_func(void * pvParameters);
                void rtos_startup_err_callback(void * p_instance, void * p_data);
                void rtos_startup_common_init(void);
gpt_instance_ctrl_t g_timer_xclk_ctrl;
#if 0
const gpt_extended_pwm_cfg_t g_timer_xclk_pwm_extend =
{
    .trough_ipl             = (BSP_IRQ_DISABLED),
#if defined(VECTOR_NUMBER_GPT10_COUNTER_UNDERFLOW)
    .trough_irq             = VECTOR_NUMBER_GPT10_COUNTER_UNDERFLOW,
#else
    .trough_irq             = FSP_INVALID_VECTOR,
#endif
    .poeg_link              = GPT_POEG_LINK_POEG0,
    .output_disable         = (gpt_output_disable_t) ( GPT_OUTPUT_DISABLE_NONE),
    .adc_trigger            = (gpt_adc_trigger_t) ( GPT_ADC_TRIGGER_NONE),
    .dead_time_count_up     = 0,
    .dead_time_count_down   = 0,
    .adc_a_compare_match    = 0,
    .adc_b_compare_match    = 0,
    .interrupt_skip_source  = GPT_INTERRUPT_SKIP_SOURCE_NONE,
    .interrupt_skip_count   = GPT_INTERRUPT_SKIP_COUNT_0,
    .interrupt_skip_adc     = GPT_INTERRUPT_SKIP_ADC_NONE,
    .gtioca_disable_setting = GPT_GTIOC_DISABLE_PROHIBITED,
    .gtiocb_disable_setting = GPT_GTIOC_DISABLE_PROHIBITED,
};
#endif
const gpt_extended_cfg_t g_timer_xclk_extend =
{
    .gtioca = { .output_enabled = true,
                .stop_level     = GPT_PIN_LEVEL_LOW
              },
    .gtiocb = { .output_enabled = false,
                .stop_level     = GPT_PIN_LEVEL_LOW
              },
    .start_source        = (gpt_source_t) ( GPT_SOURCE_NONE),
    .stop_source         = (gpt_source_t) ( GPT_SOURCE_NONE),
    .clear_source        = (gpt_source_t) ( GPT_SOURCE_NONE),
    .count_up_source     = (gpt_source_t) ( GPT_SOURCE_NONE),
    .count_down_source   = (gpt_source_t) ( GPT_SOURCE_NONE),
    .capture_a_source    = (gpt_source_t) ( GPT_SOURCE_NONE),
    .capture_b_source    = (gpt_source_t) ( GPT_SOURCE_NONE),
    .capture_a_ipl       = (BSP_IRQ_DISABLED),
    .capture_b_ipl       = (BSP_IRQ_DISABLED),
    .compare_match_c_ipl = (BSP_IRQ_DISABLED),
    .compare_match_d_ipl = (BSP_IRQ_DISABLED),
    .compare_match_e_ipl = (BSP_IRQ_DISABLED),
    .compare_match_f_ipl = (BSP_IRQ_DISABLED),
#if defined(VECTOR_NUMBER_GPT10_CAPTURE_COMPARE_A)
    .capture_a_irq         = VECTOR_NUMBER_GPT10_CAPTURE_COMPARE_A,
#else
    .capture_a_irq         = FSP_INVALID_VECTOR,
#endif
#if defined(VECTOR_NUMBER_GPT10_CAPTURE_COMPARE_B)
    .capture_b_irq         = VECTOR_NUMBER_GPT10_CAPTURE_COMPARE_B,
#else
    .capture_b_irq         = FSP_INVALID_VECTOR,
#endif
#if defined(VECTOR_NUMBER_GPT10_COMPARE_C)
    .compare_match_c_irq   = VECTOR_NUMBER_GPT10_COMPARE_C,
#else
    .compare_match_c_irq   = FSP_INVALID_VECTOR,
#endif
#if defined(VECTOR_NUMBER_GPT10_COMPARE_D)
    .compare_match_d_irq   = VECTOR_NUMBER_GPT10_COMPARE_D,
#else
    .compare_match_d_irq   = FSP_INVALID_VECTOR,
#endif
#if defined(VECTOR_NUMBER_GPT10_COMPARE_E)
    .compare_match_e_irq   = VECTOR_NUMBER_GPT10_COMPARE_E,
#else
    .compare_match_e_irq   = FSP_INVALID_VECTOR,
#endif
#if defined(VECTOR_NUMBER_GPT10_COMPARE_F)
    .compare_match_f_irq   = VECTOR_NUMBER_GPT10_COMPARE_F,
#else
    .compare_match_f_irq   = FSP_INVALID_VECTOR,
#endif
     .compare_match_value = { (uint32_t)0x0, /* CMP_A */(uint32_t)0x0, /* CMP_B */(uint32_t)0x0, /* CMP_C */(uint32_t)0x0, /* CMP_D */(uint32_t)0x0, /* CMP_E */(uint32_t)0x0, /* CMP_F */ }, .compare_match_status = ((0U << 5U) | (0U << 4U) | (0U << 3U) | (0U << 2U) | (0U << 1U) | 0U),
    .capture_filter_gtioca = GPT_CAPTURE_FILTER_NONE,
    .capture_filter_gtiocb = GPT_CAPTURE_FILTER_NONE,
#if 0
    .p_pwm_cfg             = &g_timer_xclk_pwm_extend,
#else
    .p_pwm_cfg             = NULL,
#endif
#if 0
    .gtior_setting.gtior_b.gtioa  = (0U << 4U) | (0U << 2U) | (0U << 0U),
    .gtior_setting.gtior_b.oadflt = (uint32_t) GPT_PIN_LEVEL_LOW,
    .gtior_setting.gtior_b.oahld  = 0U,
    .gtior_setting.gtior_b.oae    = (uint32_t) true,
    .gtior_setting.gtior_b.oadf   = (uint32_t) GPT_GTIOC_DISABLE_PROHIBITED,
    .gtior_setting.gtior_b.nfaen  = ((uint32_t) GPT_CAPTURE_FILTER_NONE & 1U),
    .gtior_setting.gtior_b.nfcsa  = ((uint32_t) GPT_CAPTURE_FILTER_NONE >> 1U),
    .gtior_setting.gtior_b.gtiob  = (0U << 4U) | (0U << 2U) | (0U << 0U),
    .gtior_setting.gtior_b.obdflt = (uint32_t) GPT_PIN_LEVEL_LOW,
    .gtior_setting.gtior_b.obhld  = 0U,
    .gtior_setting.gtior_b.obe    = (uint32_t) false,
    .gtior_setting.gtior_b.obdf   = (uint32_t) GPT_GTIOC_DISABLE_PROHIBITED,
    .gtior_setting.gtior_b.nfben  = ((uint32_t) GPT_CAPTURE_FILTER_NONE & 1U),
    .gtior_setting.gtior_b.nfcsb  = ((uint32_t) GPT_CAPTURE_FILTER_NONE >> 1U),
#else
    .gtior_setting.gtior = 0U,
#endif

    .gtioca_polarity = GPT_GTIOC_POLARITY_NORMAL,
    .gtiocb_polarity = GPT_GTIOC_POLARITY_NORMAL,
};

const timer_cfg_t g_timer_xclk_cfg =
{
    .mode                = TIMER_MODE_PERIODIC,
    /* Actual period: 4e-8 seconds. Actual duty: 50%. */ .period_counts = (uint32_t) 0xa, .duty_cycle_counts = 0x5, .source_div = (timer_source_div_t)0,
    .channel             = 10,
    .p_callback          = NULL,
    /** If NULL then do not add & */
#if defined(NULL)
    .p_context           = NULL,
#else
    .p_context           = (void *) &NULL,
#endif
    .p_extend            = &g_timer_xclk_extend,
    .cycle_end_ipl       = (BSP_IRQ_DISABLED),
#if defined(VECTOR_NUMBER_GPT10_COUNTER_OVERFLOW)
    .cycle_end_irq       = VECTOR_NUMBER_GPT10_COUNTER_OVERFLOW,
#else
    .cycle_end_irq       = FSP_INVALID_VECTOR,
#endif
};
/* Instance structure to use this module. */
const timer_instance_t g_timer_xclk =
{
    .p_ctrl        = &g_timer_xclk_ctrl,
    .p_cfg         = &g_timer_xclk_cfg,
    .p_api         = &g_timer_on_gpt
};
ceu_instance_ctrl_t g_ceu0_ctrl;
            const ceu_extended_cfg_t g_ceu0_extended_cfg =
            {
                .capture_format       = CEU_CAPTURE_FORMAT_DATA_SYNCHRONOUS,
                .input_order          = CEU_INPUT_ORDER_CB0Y0CR0Y1,
                .output_format        = CEU_OUTPUT_FORMAT_YCBCR422,
                .data_bus_width       = CEU_DATA_BUS_SIZE_8_BIT,
                .edge_info.dsel       = 0,
                .edge_info.hdsel      = 0,
                .edge_info.vdsel      = 0,
                .hsync_polarity       = CEU_HSYNC_POLARITY_HIGH,
                .vsync_polarity       = CEU_VSYNC_POLARITY_HIGH,
                .byte_swapping        = {
                                        .swap_8bit_units  = ( 0x0) >> 0x00 & 0x01,
                                        .swap_16bit_units = ( 0x0) >> 0x01 & 0x01,
                                        .swap_32bit_units = ( 0x0) >> 0x02 & 0x01,
                                        },
                .burst_mode           = CEU_BURST_TRANSFER_MODE_X8,
                .scale_down_factor    = 0x0U,
                .h_output_size        = 0,
                .v_output_size        = 0,
                .image_area_size      = 640 * 480 * 2,
                .interrupts_enabled   = 0 | \
                                        R_CEU_CEIER_CPEIE_Msk | \
                                        0 | \
                                        R_CEU_CEIER_VDIE_Msk | \
                                        R_CEU_CEIER_CDTOFIE_Msk | \
                                        0 | \
                                        0 | \
                                        R_CEU_CEIER_VBPIE_Msk | \
                                        R_CEU_CEIER_NHDIE_Msk | \
                                        R_CEU_CEIER_NVDIE_Msk,
                .ceu_ipl              = (12),
                .ceu_irq              = VECTOR_NUMBER_CEU_CEUI,
            };

            const capture_cfg_t g_ceu0_cfg =
            {
                .x_capture_pixels      = 640,
                .y_capture_pixels      = 480,
                .x_capture_start_pixel = 0,
                .y_capture_start_pixel = 0,
                .bytes_per_pixel       = 2,
                .p_callback            = g_ceu0_user_callback,
                .p_context             = (void *) NULL,
                .p_extend              = &g_ceu0_extended_cfg,
            };

            const capture_instance_t g_ceu0 =
            {
                .p_ctrl = &g_ceu0_ctrl,
                .p_cfg =  &g_ceu0_cfg,
                .p_api =  &g_ceu_on_capture,
            };
extern uint32_t g_fsp_common_thread_count;

                const rm_freertos_port_parameters_t Camera_thread_parameters =
                {
                    .p_context = (void *) NULL,
                };

                void Camera_thread_create (void)
                {
                    /* Increment count so we will know the number of threads created in the RA Configuration editor. */
                    g_fsp_common_thread_count++;

                    /* Initialize each kernel object. */
                    

                    #if 1
                    Camera_thread = xTaskCreateStatic(
                    #else
                    BaseType_t Camera_thread_create_err = xTaskCreate(
                    #endif
                        Camera_thread_func,
                        (const char *)"Camera",
                        1024/4, // In words, not bytes
                        (void *) &Camera_thread_parameters, //pvParameters
                        1,
                        #if 1
                        (StackType_t *)&Camera_thread_stack,
                        (StaticTask_t *)&Camera_thread_memory
                        #else
                        & Camera_thread
                        #endif
                    );

                    #if 1
                    if (NULL == Camera_thread)
                    {
                        rtos_startup_err_callback(Camera_thread, 0);
                    }
                    #else
                    if (pdPASS != Camera_thread_create_err)
                    {
                        rtos_startup_err_callback(Camera_thread, 0);
                    }
                    #endif
                }
                static void Camera_thread_func (void * pvParameters)
                {
                    /* Initialize common components */
                    rtos_startup_common_init();

                    /* Initialize each module instance. */
                    

                    #if (1 == BSP_TZ_NONSECURE_BUILD) && (1 == 1)
                    /* When FreeRTOS is used in a non-secure TrustZone application, portALLOCATE_SECURE_CONTEXT must be called prior
                     * to calling any non-secure callable function in a thread. The parameter is unused in the FSP implementation.
                     * If no slots are available then configASSERT() will be called from vPortSVCHandler_C(). If this occurs, the
                     * application will need to either increase the value of the "Process Stack Slots" Property in the rm_tz_context
                     * module in the secure project or decrease the number of threads in the non-secure project that are allocating
                     * a secure context. Users can control which threads allocate a secure context via the Properties tab when
                     * selecting each thread. Note that the idle thread in FreeRTOS requires a secure context so the application
                     * will need at least 1 secure context even if no user threads make secure calls. */
                     portALLOCATE_SECURE_CONTEXT(0);
                    #endif

                    /* Enter user code for this thread. Pass task handle. */
                    Camera_thread_entry(pvParameters);
                }
