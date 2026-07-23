/* generated thread source file - do not edit */
#include "Motor_thread.h"

#if 1
                static StaticTask_t Motor_thread_memory;
                #if defined(__ARMCC_VERSION)           /* AC6 compiler */
                static uint8_t Motor_thread_stack[2048] BSP_PLACE_IN_SECTION(BSP_UNINIT_SECTION_PREFIX ".stack.thread") BSP_ALIGN_VARIABLE(BSP_STACK_ALIGNMENT);
                #else
                static uint8_t Motor_thread_stack[2048] BSP_PLACE_IN_SECTION(BSP_UNINIT_SECTION_PREFIX ".stack.Motor_thread") BSP_ALIGN_VARIABLE(BSP_STACK_ALIGNMENT);
                #endif
                #endif
                TaskHandle_t Motor_thread;
                void Motor_thread_create(void);
                static void Motor_thread_func(void * pvParameters);
                void rtos_startup_err_callback(void * p_instance, void * p_data);
                void rtos_startup_common_init(void);
/* Nominal and Data bit timing configuration */

can_bit_timing_cfg_t g_canfd0_bit_timing_cfg =
{
    /* Actual bitrate: 500000 Hz. Actual sample point: 75 %. */
    .baud_rate_prescaler = 1,
    .time_segment_1 = 11,
    .time_segment_2 = 4,
    .synchronization_jump_width = 1
};

#if BSP_FEATURE_CANFD_FD_SUPPORT
can_bit_timing_cfg_t g_canfd0_data_timing_cfg =
{
    /* Actual bitrate: 0 Hz. Actual sample point: 0 %. */
    .baud_rate_prescaler = 0,
    .time_segment_1 = 0,
    .time_segment_2 = 0,
    .synchronization_jump_width = 0
};
#endif


extern const canfd_afl_entry_t p_canfd0_afl[CANFD_CFG_AFL_CH0_RULE_NUM];


#define CANFD_CFG_COMMONFIFO0 (((0) << R_CANFD_CFDCFCC_CFE_Pos) | \
                                        ((0) << R_CANFD_CFDCFCC_CFRXIE_Pos) | \
                                        ((0) << R_CANFD_CFDCFCC_CFTXIE_Pos) | \
                                        ((0) << R_CANFD_CFDCFCC_CFPLS_Pos) | \
                                        ((0) << R_CANFD_CFDCFCC_CFM_Pos) | \
                                        ((0) << R_CANFD_CFDCFCC_CFITSS_Pos) | \
                                        ((0) << R_CANFD_CFDCFCC_CFITR_Pos) | \
                                        ((0)  << R_CANFD_CFDCFCC_CFIM_Pos) | \
                                        ((3U) << R_CANFD_CFDCFCC_CFIGCV_Pos) | \
                                        ((0) << R_CANFD_CFDCFCC_CFTML_Pos) | \
                                        ((3) << R_CANFD_CFDCFCC_CFDC_Pos) | \
                                        (0 << R_CANFD_CFDCFCC_CFITT_Pos))


/* Buffer RAM used: 320 bytes */
canfd_global_cfg_t g_canfd0_global_cfg =
{
    .global_interrupts = ( 0x3),
    .global_config = ((R_CANFD_CFDGCFG_TPRI_Msk) | (0) | (BSP_CFG_CANFDCLK_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_MAIN_OSC ? R_CANFD_CFDGCFG_DCS_Msk : 0U) | (0) |
                      ((0) << R_CANFD_CFDGCFG_ITRCP_Pos)),
    .rx_mb_config = (0 | ((0) << R_CANFD_CFDRMNB_RMPLS_Pos)),
    .global_err_ipl = CANFD_CFG_GLOBAL_ERR_IPL,
    .rx_fifo_ipl = CANFD_CFG_RX_FIFO_IPL,
    .rx_fifo_config =
    {
        ((3U) << R_CANFD_CFDRFCC_RFIGCV_Pos) | ((3) << R_CANFD_CFDRFCC_RFDC_Pos) | ((0) << R_CANFD_CFDRFCC_RFPLS_Pos) | ((R_CANFD_CFDRFCC_RFIE_Msk | R_CANFD_CFDRFCC_RFIM_Msk)) | ((1)),
        ((3U) << R_CANFD_CFDRFCC_RFIGCV_Pos) | ((3) << R_CANFD_CFDRFCC_RFDC_Pos) | ((0) << R_CANFD_CFDRFCC_RFPLS_Pos) | ((R_CANFD_CFDRFCC_RFIE_Msk | R_CANFD_CFDRFCC_RFIM_Msk)) | ((0))
    },
    .common_fifo_config =
    {
        CANFD_CFG_COMMONFIFO0
    }
};

canfd_extended_cfg_t g_canfd0_extended_cfg =
{
    .p_afl              = p_canfd0_afl,
    .txmb_txi_enable    = ( 0ULL),
    .error_interrupts   = ( 0U),
#if BSP_FEATURE_CANFD_FD_SUPPORT
    .p_data_timing      = &g_canfd0_data_timing_cfg,
#else
    .p_data_timing      = NULL,
#endif
    .delay_compensation = (1),
    .p_global_cfg       = &g_canfd0_global_cfg,
};

canfd_instance_ctrl_t g_canfd0_ctrl;
const can_cfg_t g_canfd0_cfg =
{
    .channel                = 0,
    .p_bit_timing           = &g_canfd0_bit_timing_cfg,
    .p_callback             = canfd0_callback,
    .p_extend               = &g_canfd0_extended_cfg,
    .p_context              = NULL,
    .ipl                    = (12),
#if defined(VECTOR_NUMBER_CAN0_COMFRX)
    .rx_irq             = VECTOR_NUMBER_CAN0_COMFRX,
#else
    .rx_irq             = FSP_INVALID_VECTOR,
#endif
#if defined(VECTOR_NUMBER_CAN0_TX)
    .tx_irq             = VECTOR_NUMBER_CAN0_TX,
#else
    .tx_irq             = FSP_INVALID_VECTOR,
#endif
#if defined(VECTOR_NUMBER_CAN0_CHERR)
    .error_irq             = VECTOR_NUMBER_CAN0_CHERR,
#else
    .error_irq             = FSP_INVALID_VECTOR,
#endif
};
/* Instance structure to use this module. */
const can_instance_t g_canfd0 =
{
    .p_ctrl        = &g_canfd0_ctrl,
    .p_cfg         = &g_canfd0_cfg,
    .p_api         = &g_canfd_on_canfd
};
extern uint32_t g_fsp_common_thread_count;

                const rm_freertos_port_parameters_t Motor_thread_parameters =
                {
                    .p_context = (void *) NULL,
                };

                void Motor_thread_create (void)
                {
                    /* Increment count so we will know the number of threads created in the RA Configuration editor. */
                    g_fsp_common_thread_count++;

                    /* Initialize each kernel object. */
                    

                    #if 1
                    Motor_thread = xTaskCreateStatic(
                    #else
                    BaseType_t Motor_thread_create_err = xTaskCreate(
                    #endif
                        Motor_thread_func,
                        (const char *)"Motor",
                        2048/4, // In words, not bytes
                        (void *) &Motor_thread_parameters, //pvParameters
                        1,
                        #if 1
                        (StackType_t *)&Motor_thread_stack,
                        (StaticTask_t *)&Motor_thread_memory
                        #else
                        & Motor_thread
                        #endif
                    );

                    #if 1
                    if (NULL == Motor_thread)
                    {
                        rtos_startup_err_callback(Motor_thread, 0);
                    }
                    #else
                    if (pdPASS != Motor_thread_create_err)
                    {
                        rtos_startup_err_callback(Motor_thread, 0);
                    }
                    #endif
                }
                static void Motor_thread_func (void * pvParameters)
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
                    Motor_thread_entry(pvParameters);
                }
