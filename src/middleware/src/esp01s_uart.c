#include "esp01s_uart.h"

#include <string.h>

#include "Network_thread.h" /* g_uart8 / g_uart8_ctrl / g_uart8_cfg */
#include "hal_data.h"       /* g_ioport_ctrl */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/* ============================================================================
 * 内部状态
 * ==========================================================================*/
static bool                    s_init_done;      /* UART 是否已 open */
static SemaphoreHandle_t       s_tx_done_sem;    /* TX_COMPLETE 信号量 */
static SemaphoreHandle_t       s_tx_mutex;       /* 串行化发送（Network 线程 / 协议 / LVGL 多线程发送） */
static uint8_t                 s_rx_buf[ESP01S_UART_RX_BUF_SIZE];
static volatile uint32_t       s_rx_head;        /* 生产者（RXI ISR）写指针 */
static volatile uint32_t       s_rx_tail;        /* 消费者（Network 线程）读指针 */

/* 诊断计数（J-Link 可读，SCB clean 后可见） */
volatile uint32_t s_dbg_esp_tx_ok;
volatile uint32_t s_dbg_esp_tx_err;
volatile uint32_t s_dbg_esp_rx_bytes;
volatile uint32_t s_dbg_esp_rx_dropped;

/* ============================================================================
 * 内部控制：环形缓冲
 * ==========================================================================*/
static uint32_t esp01s_uart_rx_count(void)
{
    uint32_t const head = s_rx_head;
    uint32_t const tail = s_rx_tail;
    return (uint32_t) ((head + ESP01S_UART_RX_BUF_SIZE - tail) % ESP01S_UART_RX_BUF_SIZE);
}

static void esp01s_uart_rx_put(uint8_t byte)
{
    uint32_t const next = (uint32_t) ((s_rx_head + 1U) % ESP01S_UART_RX_BUF_SIZE);
    if (next == s_rx_tail)
    {
        /* 缓冲满：丢弃最旧一字节（移动 tail），保留最新数据 */
        s_rx_tail = (uint32_t) ((s_rx_tail + 1U) % ESP01S_UART_RX_BUF_SIZE);
        s_dbg_esp_rx_dropped++;
    }
    s_rx_buf[s_rx_head] = byte;
    s_rx_head = next;
    s_dbg_esp_rx_bytes++;
}

/* ============================================================================
 * 回调（中断上下文）：RXI 逐字节 / TX_COMPLETE / 错误
 * ==========================================================================*/
void esp01s_uart_callback(uart_callback_args_t * p_args)
{
    if (NULL == p_args)
    {
        return;
    }

    switch (p_args->event)
    {
        case UART_EVENT_RX_CHAR:
            esp01s_uart_rx_put(p_args->data);
            break;

        case UART_EVENT_TX_COMPLETE:
        {
            BaseType_t higher_priority_task_woken = pdFALSE;
            if (s_tx_done_sem != NULL)
            {
                (void) xSemaphoreGiveFromISR(s_tx_done_sem, &higher_priority_task_woken);
            }
            portYIELD_FROM_ISR(higher_priority_task_woken);
            break;
        }

        case UART_EVENT_ERR_PARITY:
        case UART_EVENT_ERR_FRAMING:
        case UART_EVENT_ERR_OVERFLOW:
        case UART_EVENT_BREAK_DETECT:
        default:
            /* 错误事件：丢弃该字节（data 无效时无需处理）。 */
            break;
    }
}

/* ============================================================================
 * 初始化
 * ==========================================================================*/
bool esp01s_uart_init(void)
{
    if (s_init_done)
    {
        return true;
    }

    if (s_tx_done_sem == NULL)
    {
        s_tx_done_sem = xSemaphoreCreateBinary();
        if (s_tx_done_sem == NULL)
        {
            return false;
        }
    }
    if (s_tx_mutex == NULL)
    {
        s_tx_mutex = xSemaphoreCreateMutex();
        if (s_tx_mutex == NULL)
        {
            return false;
        }
    }

    fsp_err_t const err = g_uart8.p_api->open(g_uart8.p_ctrl, g_uart8.p_cfg);
    if (FSP_SUCCESS != err)
    {
        return false;
    }

    s_init_done = true;
    return true;
}

/* ============================================================================
 * ESP-01S 控制
 * ==========================================================================*/
void esp01s_uart_enable(bool on)
{
    (void) R_IOPORT_PinWrite(&g_ioport_ctrl,
                             (bsp_io_port_pin_t) BSP_IO_PORT_05_PIN_14,
                             on ? BSP_IO_LEVEL_HIGH : BSP_IO_LEVEL_LOW);
}

void esp01s_uart_reset(void)
{
    (void) R_IOPORT_PinWrite(&g_ioport_ctrl,
                             (bsp_io_port_pin_t) BSP_IO_PORT_05_PIN_15,
                             BSP_IO_LEVEL_LOW);
    vTaskDelay(pdMS_TO_TICKS(50U));
    (void) R_IOPORT_PinWrite(&g_ioport_ctrl,
                             (bsp_io_port_pin_t) BSP_IO_PORT_05_PIN_15,
                             BSP_IO_LEVEL_HIGH);
}

/* ============================================================================
 * 发送（阻塞，等待 TX_COMPLETE）
 * ==========================================================================*/
bool esp01s_uart_send(const uint8_t * p_data, uint32_t len)
{
    if ((!s_init_done) || (NULL == p_data) || (0U == len))
    {
        return false;
    }

    /* 多线程发送串行化：Network 线程（心跳/透传）、协议层（LVGL 上报事件）
     * 可能同时调用；持有互斥锁期间等待 TX_COMPLETE（≤100ms），其他调用方
     * 最多阻塞 300ms，超时返回失败由调用方决定重试/丢弃。 */
    if (s_tx_mutex != NULL)
    {
        BaseType_t const got = xSemaphoreTake(s_tx_mutex, pdMS_TO_TICKS(300U));
        if (pdPASS != got)
        {
            s_dbg_esp_tx_err++;
            return false;
        }
    }

    /* 清掉上次遗留的完成信号，避免误判 */
    (void) xSemaphoreTake(s_tx_done_sem, 0U);

    fsp_err_t const err = g_uart8.p_api->write(g_uart8.p_ctrl, (uint8_t *) p_data, len);
    bool ok = false;
    if (FSP_SUCCESS == err)
    {
        /* 等待 TX_COMPLETE（整帧发完） */
        BaseType_t const taken = xSemaphoreTake(s_tx_done_sem,
                                                pdMS_TO_TICKS(ESP01S_UART_TX_TIMEOUT_MS));
        if (pdPASS == taken)
        {
            s_dbg_esp_tx_ok++;
            ok = true;
        }
        else
        {
            /* 超时：中断可能已丢失，继续尝试；返回失败由调用方决定重试 */
            s_dbg_esp_tx_err++;
        }
    }
    else
    {
        s_dbg_esp_tx_err++;
    }

    if (s_tx_mutex != NULL)
    {
        (void) xSemaphoreGive(s_tx_mutex);
    }
    return ok;
}

bool esp01s_uart_send_str(const char * p_str)
{
    if (NULL == p_str)
    {
        return false;
    }
    return esp01s_uart_send((const uint8_t *) p_str, (uint32_t) strlen(p_str));
}

/* ============================================================================
 * 接收
 * ==========================================================================*/
uint32_t esp01s_uart_rx_available(void)
{
    return esp01s_uart_rx_count();
}

uint32_t esp01s_uart_rx_read(uint8_t * p_dst, uint32_t max)
{
    if ((NULL == p_dst) || (0U == max))
    {
        return 0U;
    }

    uint32_t got = 0U;
    while ((got < max) && (s_rx_tail != s_rx_head))
    {
        p_dst[got++] = s_rx_buf[s_rx_tail];
        s_rx_tail = (uint32_t) ((s_rx_tail + 1U) % ESP01S_UART_RX_BUF_SIZE);
    }
    return got;
}

void esp01s_uart_rx_flush(void)
{
    s_rx_tail = s_rx_head; /* 丢弃全部 */
}
