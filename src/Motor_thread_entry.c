#include "Motor_thread.h"
#include "ZDT_drv.h"
#include "ZDT_app.h"

/* CANFD0 接收滤波列表 (AFL) - 默认全接收 */
const canfd_afl_entry_t p_canfd0_afl[CANFD_CFG_AFL_CH0_RULE_NUM] = {0};

/* CANFD0 中断回调 - 暂为空 */
void canfd0_callback(can_callback_args_t * p_args)
{
    (void)p_args;
}

/* Motor entry function */
/* pvParameters contains TaskHandle_t */
void Motor_thread_entry(void * pvParameters)
{
    FSP_PARAMETER_NOT_USED(pvParameters);

    /* 初始化 ZDT 张大头 CAN 电机驱动 */
    fsp_err_t err = ZDT_Driver_Init();
    if (err != FSP_SUCCESS)
    {
        while (1)
        {
            vTaskDelay(1); // 初始化失败，停留在此
        }
    }

    /* 使能所有电机 */
    // ZDT_Enable_ALL();

    while(1)
    {
        vTaskDelay(1);
    }
}
