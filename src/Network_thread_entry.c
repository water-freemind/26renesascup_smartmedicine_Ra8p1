#include "Network_thread.h"
#include "esp01s_uart.h"
#include "esp01s_cfg.h"
#include "esp01s_proto.h"
#include "sys_log.h"

/* ============================================================================
 * Network 线程入口（ESP-01S 透传链路 + 云端数据协议）
 *
 * 职责：
 *   1. 使能 ESP-01S（EN=P514 拉高）→ 打开 SCI8 UART；
 *   2. 周期把 UART RX 缓冲的内容读出：按行转发到系统日志（透传下行），
 *      并把每一行送入协议解析（esp01s_proto_process_line：PING /
 *      DISPENSE_ACTION）；
 *   3. 协议层调度（esp01s_proto_service）：心跳定时发送（注册/保活）+
 *      云端出药任务推进；
 *   4. ESP-01S 透传主机配置（esp01s_cfg）：无线调试页发起的 AT 序列
 *      （读取/保存 SAVETRANSLINK 链路）在此线程执行，期间暂停透传转发/
 *      协议发送（串口被 cfg 独占）。
 *
 * 优先级 1（与 LVGL 同级，不高于 LVGL）。
 * 栈 8192B（RASC configuration.xml 已配，容纳行缓冲与协议解析）。
 *
 * 注意：ESP-01S 常态为 TCP 透传（AT+CIPMODE=1 + AT+CIPSEND 或
 * AT+SAVETRANSLINK 自动透传）。仅当无线调试页发起配置时才回到 AT 命令模式，
 * 配置完成后 AT+RST 重启模块恢复透传。
 * ==========================================================================*/

/* 行缓冲：协议单条消息 ≤1KB，这里按最大行 + 终止符预留 */
#define ESP_NET_LINE_MAX   (1024U)
#define ESP_NET_POLL_MS    (20U)

static char s_line_buf[ESP_NET_LINE_MAX];
static uint32_t s_line_len;

/* 把缓冲中已积累的行（以 \n 结尾）逐条打印到系统日志，并送入协议解析 */
static void esp_net_drain_lines(void)
{
    uint8_t tmp[ESP_NET_LINE_MAX];
    uint32_t const got = esp01s_uart_rx_read(tmp, sizeof(tmp));
    if (0U == got)
    {
        return;
    }

    for (uint32_t i = 0U; i < got; i++)
    {
        uint8_t const ch = tmp[i];
        if ((ch == '\n') || (s_line_len >= (ESP_NET_LINE_MAX - 1U)))
        {
            if (s_line_len > 0U)
            {
                s_line_buf[s_line_len] = '\0';
                /* 去掉行尾 \r（透传 TCP 下行可能带 CRLF） */
                while ((s_line_len > 0U) &&
                       (('\r' == s_line_buf[s_line_len - 1U]) ||
                        ('\n' == s_line_buf[s_line_len - 1U])))
                {
                    s_line_buf[--s_line_len] = '\0';
                }
                if (s_line_len > 0U)
                {
                    sys_log_add(SYS_LOG_INFO, "ESP: %s", s_line_buf);
                    /* 送入云端协议解析（PING / DISPENSE_ACTION 等） */
                    esp01s_proto_process_line(s_line_buf);
                }
                s_line_len = 0U;
            }
        }
        else
        {
            s_line_buf[s_line_len++] = (char) ch;
        }
    }
}

/* Network 线程入口函数 */
/* pvParameters 包含 TaskHandle_t */
void Network_thread_entry(void * pvParameters)
{
    FSP_PARAMETER_NOT_USED(pvParameters);

    /* 1. 使能 ESP-01S 并打开 SCI8 UART */
    esp01s_uart_enable(true);
    vTaskDelay(pdMS_TO_TICKS(100U)); /* EN 稳定后模块才上电自检 */

    if (!esp01s_uart_init())
    {
        sys_log_add(SYS_LOG_ERR, "ESP: SCI8 UART 打开失败");
        while (1)
        {
            vTaskDelay(pdMS_TO_TICKS(1000U)); /* 初始化失败，停留在此 */
        }
    }

    sys_log_add(SYS_LOG_OK, "ESP: SCI8 UART 已打开 (115200 8N1)");

    /* 透传主机配置引擎（无线调试页发起 AT 序列） */
    esp01s_cfg_init();
    /* 云端数据协议层（心跳定时 + 下行解析 + 云端出药推进） */
    esp01s_proto_init();

    /* 2. 主循环：轮询 RX 缓冲（透传下行/协议解析）+ 心跳/出药调度 + AT 配置状态机 */
    while (1)
    {
        /* AT 配置进行中：串口应答由 esp01s_cfg 独占解析，暂停透传转发与协议发送 */
        if (!esp01s_cfg_busy())
        {
            esp_net_drain_lines();
            esp01s_proto_service();
        }
        esp01s_cfg_service();
        vTaskDelay(pdMS_TO_TICKS(ESP_NET_POLL_MS));
    }
}
