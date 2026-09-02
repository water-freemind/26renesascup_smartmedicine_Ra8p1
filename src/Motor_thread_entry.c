#include "Motor_thread.h"
#include "ZDT_drv.h"
#include "ZDT_app.h"
#include "gantry_robot.h"
#include "gantry_debug.h"
#include "pickup_params.h"
#include "relay_drv.h"

/* LVGL 线程在 GUI 初始化完成后置位（屏幕完全就绪），Motor 线程等待该标志
 * 后再执行上电自动归零——避免系统启动初期 CAN/总线未稳定导致命令丢失。 */
extern volatile bool s_lvgl_ready;

/* 上电启动诊断（J-Link 直读）：
 *   0=初始化中  1=参数已加载  2=等待参数超时(用默认)  3=归零命令已入队
 *   4=参数关闭自动归零  5=自动归零完成 */
volatile uint8_t s_dbg_motor_boot_state = 0U;

/* ===== 上电自动归零 =====
 * 采用用户原 ZDT_Gozero_ALL() 的固定时序（ZDT_app.c 未链接进固件，
 * 此处按原逻辑内联，与用户手写代码一致）：
 *   Z 轴先单独回零 → 等待 4s → X+Y 一起回零 → 抓手回零。
 * 固定时序、不依赖应答帧（实测各轴"回零完成"应答不可靠：只有 Z 有应答）。
 * 完成后用 Gantry_SetLogicalPosition 标记全部已回零（取药流程需要 homed）。 */
#define AUTO_HOME_Z_GAP_MS (2000U) /* Z 回零后到 X/Y 的间隔（用户确认 2s） */

/* 执行上电自动归零（阻塞约 4s+，仅在启动阶段调用一次） */
static void auto_home_run(void)
{
    s_dbg_motor_boot_state = 3U; /* 归零命令已发出 */
    ZDT_Gozero(ZDT_ID_Z, false);     /* Z 先单独回零 */
    vTaskDelay(pdMS_TO_TICKS(AUTO_HOME_Z_GAP_MS));
    ZDT_Gozero(ZDT_ID_X, false);     /* X+Y 一起回零 */
    ZDT_Gozero(ZDT_ID_Y, false);
    ZDT_Gozero(ZDT_ID_CATCH, false); /* 抓手回零 */
    /* 标记 gantry_robot 全部已回零（后续 MoveTo/MoveXYTo 需要 homed） */
    gantry_position_t zero = {0.0f, 0.0f, 0.0f, 0.0f};
    (void) Gantry_SetLogicalPosition(&zero, (uint8_t) (GANTRY_AXIS_MASK_ALL | GANTRY_AXIS_MASK_CATCH));
    s_dbg_motor_boot_state = 5U; /* 自动归零完成 */
}

/*
 * CAN 无电机台架发送测试
 * 1: 每秒发送一帧固定扩展帧，供 CANgaroo 观察；
 * 0: 关闭测试发送。
 *
 * 该测试帧不是 ZDT 电机协议命令，不会使能、停止或移动电机。
 */
#ifndef CAN_TEST_TX_ENABLE
#define CAN_TEST_TX_ENABLE    (0)
#endif

#define CAN_TEST_TX_PERIOD_MS (1000U)
#define CAN_TEST_TX_ID        (0x18FF5001U)
#define CAN_TEST_TX_MB_COUNT  (4U) /* RA8P1 CANFD Lite: TX mailbox 0..3 */
#define CAN_TEST_ECHO_REQ_ID  (0x123U)
#define CAN_TEST_ECHO_RSP_ID  (0x321U)

#if CAN_TEST_TX_ENABLE
static volatile bool s_can_test_echo_pending = false;

static fsp_err_t can_test_write(can_frame_t * p_frame)
{
    static uint8_t mailbox = 0;
    fsp_err_t err;
    uint32_t retry = 1000U;

    do
    {
        err = g_canfd0.p_api->write(g_canfd0.p_ctrl, mailbox, p_frame);
        if ((FSP_ERR_IN_USE == err) || (FSP_ERR_CAN_TRANSMIT_NOT_READY == err))
        {
            mailbox = (uint8_t)((mailbox + 1U) % CAN_TEST_TX_MB_COUNT);
            R_BSP_SoftwareDelay(10U, BSP_DELAY_UNITS_MICROSECONDS);
            retry--;
        }
    } while (((FSP_ERR_IN_USE == err) || (FSP_ERR_CAN_TRANSMIT_NOT_READY == err)) && (retry > 0U));

    if (FSP_SUCCESS == err)
    {
        mailbox = (uint8_t)((mailbox + 1U) % CAN_TEST_TX_MB_COUNT);
    }

    return err;
}

static fsp_err_t can_test_send_periodic(void)
{
    static uint8_t sequence = 0;
    can_frame_t frame = {0};

    frame.id = CAN_TEST_TX_ID;
    frame.id_mode = CAN_ID_MODE_EXTENDED;
    frame.type = CAN_FRAME_TYPE_DATA;
    frame.data_length_code = 8U;
    frame.data[0] = 0xA5U;
    frame.data[1] = 0x5AU;
    frame.data[2] = sequence++;
    frame.data[3] = 0x01U;
    frame.data[4] = 0x02U;
    frame.data[5] = 0x03U;
    frame.data[6] = 0x04U;
    frame.data[7] = 0x6BU;

    return can_test_write(&frame);
}

static fsp_err_t can_test_send_echo(void)
{
    can_frame_t frame = {0};

    frame.id = CAN_TEST_ECHO_RSP_ID;
    frame.id_mode = CAN_ID_MODE_STANDARD;
    frame.type = CAN_FRAME_TYPE_DATA;
    frame.data_length_code = 8U;
    frame.data[0] = 0xC3U;
    frame.data[1] = 0x3CU;
    frame.data[2] = 0x12U;
    frame.data[3] = 0x30U;
    frame.data[4] = 0x52U;
    frame.data[5] = 0x41U;
    frame.data[6] = 0x38U;
    frame.data[7] = 0x50U;

    return can_test_write(&frame);
}
#endif

/* CANFD0 接收滤波列表：接收所有帧到 RX FIFO0，供台架握手测试使用。 */
const canfd_afl_entry_t p_canfd0_afl[CANFD_CFG_AFL_CH0_RULE_NUM] =
{
    {
        .destination =
        {
            .fifo_select_flags = CANFD_RX_FIFO_0,
        },
    },
};

/* CAN 发送/错误诊断计数（J-Link 直读）：
 *   s_dbg_can_tx_count      发送完成(TX_COMPLETE)次数
 *   s_dbg_can_tx_err_count  总线警告/被动/离线 错误事件次数 */
volatile uint32_t s_dbg_can_tx_count = 0U;
volatile uint32_t s_dbg_can_tx_err_count = 0U;

/* CANFD0 中断回调 - 暂为空 */
void canfd0_callback(can_callback_args_t * p_args)
{
#if CAN_TEST_TX_ENABLE
    if ((CAN_EVENT_RX_COMPLETE == p_args->event) &&
        (CAN_ID_MODE_STANDARD == p_args->frame.id_mode) &&
        (CAN_TEST_ECHO_REQ_ID == p_args->frame.id))
    {
        s_can_test_echo_pending = true;
    }
#endif
    if (CAN_EVENT_TX_COMPLETE == p_args->event)
    {
        s_dbg_can_tx_count++;
    }
    if ((CAN_EVENT_ERR_WARNING == p_args->event) || (CAN_EVENT_ERR_PASSIVE == p_args->event) ||
        (CAN_EVENT_ERR_BUS_OFF == p_args->event))
    {
        s_dbg_can_tx_err_count++;
    }
    /* 电机调试：ZDT 应答帧（扩展帧 0x0100..0x0400）→ gantry_debug 原始缓冲 */
    if (CAN_EVENT_RX_COMPLETE == p_args->event)
    {
        GantryDebug_OnCanRxFromISR(p_args->frame.id, p_args->frame.data,
                                   (uint8_t) p_args->frame.data_length_code);
    }
}

/* Motor 线程入口函数 */
/* pvParameters 包含 TaskHandle_t */
void Motor_thread_entry(void * pvParameters)
{
    FSP_PARAMETER_NOT_USED(pvParameters);

    /* 初始化 ZDT 张大头 CAN 电机驱动 */
    fsp_err_t err = ZDT_Driver_Init();
    if (err != FSP_SUCCESS)
    {
        while (1)
        {
            vTaskDelay(1); /* 初始化失败，停留在此 */
        }
    }

    /* 继电器输出初始化（真空泵 PIN904 / 电磁阀 PIN807 / 电磁阀 PA07）：
     * pin_data.c 已配置 GPIO 输出初始低电平，此处幂等兜底 + 确保关闭 */
    (void) RelayDrv_Init();

    gantry_config_t gantry_config;
    Gantry_GetDefaultConfig(&gantry_config);
    if (GANTRY_OK != Gantry_Init(&gantry_config))
    {
        while (1)
        {
            vTaskDelay(1);
        }
    }

    /* 电机调试模块（GUI 电机调试页：设零/点动/急停） */
    if (GDBG_OK != GantryDebug_Init())
    {
        while (1)
        {
            vTaskDelay(1);
        }
    }

    /* 取药参数：等待 Camera 线程加载（OSPI 初始化+自检后执行），最多 10s；
     * 超时未加载时 pickup_params 保证返回编译期默认值（上电自动归零=开）。 */
    {
        uint32_t waited_ms = 0U;
        while (!PickupParams_Ready() && (waited_ms < 10000U))
        {
            vTaskDelay(pdMS_TO_TICKS(100U));
            waited_ms += 100U;
        }
    }
    s_dbg_motor_boot_state = PickupParams_Ready() ? 1U : 2U;
    /* 参数中的速度/加速度应用到 gantry_robot（机械臂空闲，可配置） */
    (void) PickupParams_ApplyToGantry();

    /* 上电自动归零（参数可关）：
     * 1. 等屏幕完全初始化（s_lvgl_ready，最多 30s；超时也继续）；
     * 2. 再等 2s 缓冲（显示/触摸/总线稳定）；
     * 3. 按用户原 ZDT_Gozero_ALL() 时序回零：Z → 4s → X+Y → 抓手（auto_home_run）。 */
    if (PickupParams_Get()->auto_home_on_boot)
    {
        {
            uint32_t waited_ms = 0U;
            while (!s_lvgl_ready && (waited_ms < 30000U))
            {
                vTaskDelay(pdMS_TO_TICKS(100U));
                waited_ms += 100U;
            }
            vTaskDelay(pdMS_TO_TICKS(2000U));
        }
        auto_home_run();
    }
    else
    {
        s_dbg_motor_boot_state = 4U; /* 参数已关闭自动归零 */
    }

    /* 使能所有电机 */
    // ZDT_Enable_ALL();

#if CAN_TEST_TX_ENABLE
    TickType_t last_periodic_tick = xTaskGetTickCount();
#endif
    while(1)
    {
        Gantry_Service();
        GantryDebug_Service();
#if CAN_TEST_TX_ENABLE
        if (s_can_test_echo_pending)
        {
            s_can_test_echo_pending = false;
            (void) can_test_send_echo();
        }

        if ((xTaskGetTickCount() - last_periodic_tick) >= pdMS_TO_TICKS(CAN_TEST_TX_PERIOD_MS))
        {
            last_periodic_tick = xTaskGetTickCount();
            (void) can_test_send_periodic();
        }

        vTaskDelay(pdMS_TO_TICKS(10U));
#else
        vTaskDelay(pdMS_TO_TICKS(5U));
#endif
    }
}
