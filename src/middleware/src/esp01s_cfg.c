/*
 * esp01s_cfg.c
 *
 * ESP-01S 透传主机配置实现：非阻塞 AT 状态机（运行于 Network 线程）。
 *
 * 流程（每次操作）：
 *   静默 1.5s → 发送 "+++" 退出透传 → 静默 1.5s → 清空 RX →
 *   发送 AT 命令（SAVETRANSLINK=1 保存 / SAVETRANSLINK? 读取）→ 等待应答 →
 *   发送 AT+RST 重启（使保存的链路生效并回到透传模式）→ 等待就绪 → 结算。
 *
 * 应答按行解析（RX 环形缓冲由 esp01s_uart 提供），识别 OK / ERROR /
 * "+SAVETRANSLINK:..." / "ready"。超时统一按失败处理（模块无响应）。
 */

#include "esp01s_cfg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp01s_uart.h"
#include "sys_log.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/* ============================================================================
 * 时序参数
 * ==========================================================================*/
#define CFG_EXIT_SILENCE_MS    (1500U)   /* "+++" 前后静默（ESP8266 要求 >=1s） */
#define CFG_CMD_TIMEOUT_MS     (6000U)   /* AT 命令应答超时 */
#define CFG_RST_TIMEOUT_MS     (12000U)  /* 重启后等待就绪超时 */
#define CFG_LINE_MAX           (160U)    /* 应答行缓冲 */

/* ============================================================================
 * 内部状态
 * ==========================================================================*/
typedef enum e_cfg_step
{
    CFG_STEP_IDLE = 0,
    CFG_STEP_EXIT_BEFORE,   /* 发送 "+++" 前静默 */
    CFG_STEP_EXIT_SEND,     /* 发送 "+++" */
    CFG_STEP_EXIT_AFTER,    /* 发送后静默（随后清空 RX） */
    CFG_STEP_CMD_SEND,      /* 发送 AT 命令 */
    CFG_STEP_CMD_WAIT,      /* 等待 OK/ERROR/应答 */
    CFG_STEP_RST_SEND,      /* 发送 AT+RST */
    CFG_STEP_RST_WAIT,      /* 等待重启就绪 */
    CFG_STEP_DONE,          /* 结算结果 */
} esp01s_cfg_step_t;

typedef enum e_cfg_req
{
    CFG_REQ_NONE = 0,
    CFG_REQ_SET,
    CFG_REQ_READ,
} esp01s_cfg_req_t;

static SemaphoreHandle_t   s_mutex;
static esp01s_cfg_req_t    s_req;                 /* GUI 请求（互斥保护） */
static char                s_req_host[ESP01S_CFG_HOST_MAX + 1U];
static uint16_t            s_req_port;

/* 当前正在执行的操作（service 启动序列时从 s_req_* 拷贝，之后独享） */
static bool                s_cur_is_set;
static char                s_cur_host[ESP01S_CFG_HOST_MAX + 1U];
static uint16_t            s_cur_port;

static esp01s_cfg_step_t   s_step;                /* 状态机（service 独享） */
static uint32_t            s_step_start;          /* 当前步开始 tick */
static esp01s_cfg_status_t s_status;              /* 互斥保护 */

/* 应答解析（service 独享） */
static char                s_line[CFG_LINE_MAX];
static uint16_t            s_line_len;
static bool                s_rx_ok;
static bool                s_rx_err;
static bool                s_rx_ready;
static bool                s_read_valid;          /* 本次读回是否解析到链路 */
static bool                s_read_not_saved;      /* 读到 +SAVETRANSLINK:0 */
static char                s_read_host[ESP01S_CFG_HOST_MAX + 1U];
static uint16_t            s_read_port;

static uint32_t cfg_now_ticks(void)
{
    return (uint32_t) xTaskGetTickCount();
}

static bool cfg_elapsed(uint32_t start, uint32_t ms)
{
    return ((uint32_t) xTaskGetTickCount() - start) >= pdMS_TO_TICKS(ms);
}

/* ============================================================================
 * 应答解析
 * ==========================================================================*/
/* 解析 "+SAVETRANSLINK:1,"host",port,"TCP" */
static void cfg_parse_translink(const char * p_line)
{
    const char * p = strchr(p_line, ',');
    if (NULL == p)
    {
        return;
    }
    p++;
    while ((*p == ' ') || (*p == '\t'))
    {
        p++;
    }
    if (*p != '"')
    {
        return;
    }
    const char * p_hs = p + 1;
    const char * p_he = strchr(p_hs, '"');
    if ((NULL == p_he) || (p_he == p_hs))
    {
        return;
    }
    uint32_t const n = (uint32_t) (p_he - p_hs);
    if (n > ESP01S_CFG_HOST_MAX)
    {
        return;
    }
    memcpy(s_read_host, p_hs, n);
    s_read_host[n] = '\0';
    const char * p_port = strchr(p_he + 1, ',');
    if (NULL == p_port)
    {
        return;
    }
    p_port++;
    long const v = strtol(p_port, NULL, 10);
    if ((v <= 0L) || (v > 65535L))
    {
        return;
    }
    s_read_port = (uint16_t) v;
    s_read_valid = true;
}

static void cfg_process_line(const char * p_line)
{
    if ((NULL == p_line) || ('\0' == p_line[0]))
    {
        return;
    }
    /* 去行首空白 */
    while ((*p_line == ' ') || (*p_line == '\t'))
    {
        p_line++;
    }

    if (0 == strcmp(p_line, "OK"))
    {
        s_rx_ok = true;
        return;
    }
    if (0 == strcmp(p_line, "ERROR"))
    {
        s_rx_err = true;
        return;
    }
    if (0 == strncmp(p_line, "+SAVETRANSLINK:", 15U))
    {
        if ((p_line[15] == '0') && (('\0' == p_line[16]) || (',' == p_line[16])))
        {
            s_read_not_saved = true;   /* +SAVETRANSLINK:0 未保存链路 */
        }
        else
        {
            cfg_parse_translink(p_line);
        }
        return;
    }
    if (strstr(p_line, "ready") != NULL)
    {
        s_rx_ready = true;
    }
}

/* 从 UART 环形缓冲取行并解析（service 独享，与透传日志转发互斥） */
static void cfg_drain_lines(void)
{
    uint8_t buf[CFG_LINE_MAX];
    uint32_t const got = esp01s_uart_rx_read(buf, sizeof(buf));
    for (uint32_t i = 0U; i < got; i++)
    {
        char const ch = (char) buf[i];
        if ((ch == '\n') || (s_line_len >= (CFG_LINE_MAX - 1U)))
        {
            s_line[s_line_len] = '\0';
            while ((s_line_len > 0U) &&
                   (('\r' == s_line[s_line_len - 1U]) || ('\n' == s_line[s_line_len - 1U])))
            {
                s_line[--s_line_len] = '\0';
            }
            if (s_line_len > 0U)
            {
                cfg_process_line(s_line);
            }
            s_line_len = 0U;
        }
        else
        {
            s_line[s_line_len++] = ch;
        }
    }
}

/* ============================================================================
 * 结果结算（写回状态，OK/ERR 保持到下一次请求，GUI 可轮询到）
 * ==========================================================================*/
static void cfg_finish_ok(bool from_read)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    if (from_read)
    {
        if (s_read_valid)
        {
            memcpy(s_status.host, s_read_host, ESP01S_CFG_HOST_MAX + 1U);
            s_status.port      = s_read_port;
            s_status.has_cfg   = true;
            (void) snprintf(s_status.detail, sizeof(s_status.detail),
                            "当前透传主机: %s:%u", s_status.host, (unsigned) s_status.port);
        }
        else if (s_read_not_saved)
        {
            s_status.has_cfg = false;
            (void) snprintf(s_status.detail, sizeof(s_status.detail),
                            "模块未保存透传链路（当前为临时透传）");
        }
        else
        {
            s_status.has_cfg = false;
            (void) snprintf(s_status.detail, sizeof(s_status.detail),
                            "已返回，但未读到链路配置");
        }
        sys_log_add(SYS_LOG_OK, "ESP: 读取透传主机 OK (%s)", s_status.detail);
    }
    else
    {
        memcpy(s_status.host, s_cur_host, ESP01S_CFG_HOST_MAX + 1U);
        s_status.port    = s_cur_port;
        s_status.has_cfg = true;
        (void) snprintf(s_status.detail, sizeof(s_status.detail),
                        "已保存 %s:%u，模块重启后自动透传连接",
                        s_status.host, (unsigned) s_status.port);
        sys_log_add(SYS_LOG_OK, "ESP: 透传主机已保存 %s:%u",
                    s_status.host, (unsigned) s_status.port);
    }
    s_status.last_err = 0U;
    s_status.state    = ESP01S_CFG_OK;
    xSemaphoreGive(s_mutex);
}

static void cfg_finish_err(uint8_t err, const char * p_detail)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_status.last_err = err;
    (void) snprintf(s_status.detail, sizeof(s_status.detail), "%s", p_detail);
    s_status.state    = ESP01S_CFG_ERR;
    xSemaphoreGive(s_mutex);
    sys_log_add(SYS_LOG_ERR, "ESP: 透传配置失败(%u): %s", (unsigned) err, p_detail);
}

/* ============================================================================
 * 公开接口
 * ==========================================================================*/
void esp01s_cfg_init(void)
{
    if (s_mutex == NULL)
    {
        s_mutex = xSemaphoreCreateMutex();
    }
    memset(&s_status, 0, sizeof(s_status));
    s_status.state   = ESP01S_CFG_IDLE;
    s_status.host[0] = '\0';
    s_status.port    = 8080U;
    s_status.has_cfg = false;
    (void) snprintf(s_status.detail, sizeof(s_status.detail), "就绪");
    s_req    = CFG_REQ_NONE;
    s_step   = CFG_STEP_IDLE;
    s_line_len = 0U;
    s_rx_ok = s_rx_err = s_rx_ready = false;
    s_read_valid = s_read_not_saved = false;
    s_cur_is_set = false;
    s_cur_host[0] = '\0';
    s_cur_port = 0U;
}

bool esp01s_cfg_busy(void)
{
    bool busy;
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    busy = (s_status.state == ESP01S_CFG_BUSY) || (s_req != CFG_REQ_NONE);
    xSemaphoreGive(s_mutex);
    return busy;
}

bool esp01s_cfg_set(const char * p_host, uint16_t port)
{
    if ((NULL == p_host) || ('\0' == p_host[0]) || (0U == port))
    {
        return false;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool const busy = (s_status.state == ESP01S_CFG_BUSY) || (s_req != CFG_REQ_NONE);
    if (!busy)
    {
        (void) strncpy(s_req_host, p_host, ESP01S_CFG_HOST_MAX);
        s_req_host[ESP01S_CFG_HOST_MAX] = '\0';
        s_req_port = port;
        s_req = CFG_REQ_SET;
    }
    xSemaphoreGive(s_mutex);
    return !busy;
}

bool esp01s_cfg_read(void)
{
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool const busy = (s_status.state == ESP01S_CFG_BUSY) || (s_req != CFG_REQ_NONE);
    if (!busy)
    {
        s_req = CFG_REQ_READ;
    }
    xSemaphoreGive(s_mutex);
    return !busy;
}

void esp01s_cfg_abort(void)
{
    /* 仅取消"尚未开始"的请求；进行中的 AT 序列让它自然结束（约 5~20 秒），
     * 避免与状态机并发修改 s_step 造成序列交错。 */
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_req = CFG_REQ_NONE;
    xSemaphoreGive(s_mutex);
}

void esp01s_cfg_get_status(esp01s_cfg_status_t * p_out)
{
    if (NULL == p_out)
    {
        return;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *p_out = s_status;
    xSemaphoreGive(s_mutex);
}

/* ============================================================================
 * 状态机（Network 线程周期调用）
 * ==========================================================================*/
void esp01s_cfg_service(void)
{
    /* --- 空闲：检查新请求，启动序列 --- */
    if (s_step == CFG_STEP_IDLE)
    {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        esp01s_cfg_req_t const req = s_req;
        if (req != CFG_REQ_NONE)
        {
            if (req == CFG_REQ_SET)
            {
                s_cur_is_set = true;
                memcpy(s_cur_host, s_req_host, ESP01S_CFG_HOST_MAX + 1U);
                s_cur_port = s_req_port;
            }
            else
            {
                s_cur_is_set = false;
            }
            s_req = CFG_REQ_NONE;
            s_status.state = ESP01S_CFG_BUSY;   /* 先置忙再释放锁，堵住并发提交窗口 */
            s_status.last_err = 0U;
        }
        xSemaphoreGive(s_mutex);

        if (req == CFG_REQ_NONE)
        {
            return;
        }

        s_read_valid     = false;
        s_read_not_saved = false;
        s_rx_ok = s_rx_err = s_rx_ready = false;
        s_line_len = 0U;
        esp01s_uart_rx_flush();

        s_step       = CFG_STEP_EXIT_BEFORE;
        s_step_start = cfg_now_ticks();
        sys_log_add(SYS_LOG_INFO, "ESP: 开始%s透传主机配置（AT 序列）",
                    s_cur_is_set ? "保存" : "读取");
        return;
    }

    /* --- 分步执行 --- */
    switch (s_step)
    {
        case CFG_STEP_EXIT_BEFORE:
            if (cfg_elapsed(s_step_start, CFG_EXIT_SILENCE_MS))
            {
                if (!esp01s_uart_send((const uint8_t *) "+++", 3U))
                {
                    cfg_finish_err(3U, "串口忙/未打开，无法配置");
                    s_step = CFG_STEP_DONE;
                    return;
                }
                s_step       = CFG_STEP_EXIT_AFTER;
                s_step_start = cfg_now_ticks();
            }
            break;

        case CFG_STEP_EXIT_AFTER:
            if (cfg_elapsed(s_step_start, CFG_EXIT_SILENCE_MS))
            {
                esp01s_uart_rx_flush();   /* 丢弃 "+++" 应答 OK，避免误判 */
                s_step       = CFG_STEP_CMD_SEND;
                s_step_start = cfg_now_ticks();
            }
            break;

        case CFG_STEP_CMD_SEND:
        {
            char cmd[128];
            if (s_cur_is_set)
            {
                (void) snprintf(cmd, sizeof(cmd), "AT+SAVETRANSLINK=1,\"%s\",%u,\"TCP\"\r\n",
                                s_cur_host, (unsigned) s_cur_port);
            }
            else
            {
                (void) snprintf(cmd, sizeof(cmd), "AT+SAVETRANSLINK?\r\n");
            }
            s_rx_ok = s_rx_err = false;
            s_line_len = 0U;
            if (!esp01s_uart_send_str(cmd))
            {
                cfg_finish_err(3U, "串口忙/未打开，无法发送 AT 命令");
                s_step = CFG_STEP_DONE;
                return;
            }
            sys_log_add(SYS_LOG_INFO, "ESP: AT> %s",
                        s_cur_is_set ? "SAVETRANSLINK=1 (保存链路)" : "SAVETRANSLINK? (读取链路)");
            s_step       = CFG_STEP_CMD_WAIT;
            s_step_start = cfg_now_ticks();
            break;
        }

        case CFG_STEP_CMD_WAIT:
            cfg_drain_lines();
            if (s_rx_err)
            {
                cfg_finish_err(2U, "模块返回 ERROR（地址格式或固件不支持）");
                s_step = CFG_STEP_DONE;
                return;
            }
            if (s_rx_ok)
            {
                s_step       = CFG_STEP_RST_SEND;
                s_step_start = cfg_now_ticks();
                return;
            }
            if (cfg_elapsed(s_step_start, CFG_CMD_TIMEOUT_MS))
            {
                cfg_finish_err(1U, "模块无响应/超时（检查 ESP-01S 供电与接线）");
                s_step = CFG_STEP_DONE;
                return;
            }
            break;

        case CFG_STEP_RST_SEND:
            s_rx_ok = s_rx_ready = false;
            s_line_len = 0U;
            /* 重启失败不致命：链路已保存，下次上电自动生效 */
            (void) esp01s_uart_send_str("AT+RST\r\n");
            s_step       = CFG_STEP_RST_WAIT;
            s_step_start = cfg_now_ticks();
            break;

        case CFG_STEP_RST_WAIT:
            cfg_drain_lines();
            if (s_rx_ready || s_rx_ok)
            {
                cfg_finish_ok(!s_cur_is_set);
                s_step = CFG_STEP_DONE;
                return;
            }
            if (cfg_elapsed(s_step_start, CFG_RST_TIMEOUT_MS))
            {
                /* 超时按成功处理：SAVETRANSLINK 已收到 OK，模块正在重启 */
                cfg_finish_ok(!s_cur_is_set);
                s_step = CFG_STEP_DONE;
                return;
            }
            break;

        default:
            break;
    }

    if (s_step == CFG_STEP_DONE)
    {
        s_step = CFG_STEP_IDLE;   /* 保留 OK/ERR 状态供 GUI 轮询 */
    }
}
