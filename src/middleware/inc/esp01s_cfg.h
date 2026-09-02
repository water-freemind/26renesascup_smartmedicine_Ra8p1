/*
 * esp01s_cfg.h
 *
 * ESP-01S 透传主机配置（AT 命令引擎，由 Network 线程独占执行）。
 *
 * 背景：ESP-01S 常态处于 TCP 透传模式。要修改"透传主机地址"必须回到
 * AT 命令模式：
 *   1. 发送 "+++"（前后各静默 >=1s）退出透传模式；
 *   2. AT+SAVETRANSLINK=1,"<host>",<port>,"TCP"  保存透传链路（掉电不丢）；
 *      或 AT+SAVETRANSLINK?  读取当前保存的链路；
 *   3. AT+RST 重启模块，按保存的链路自动重连并进入透传模式。
 *
 * 整个序列在 Network 线程内以状态机分步执行（不阻塞 GUI），GUI/业务侧只
 * 发起请求（esp01s_cfg_set / esp01s_cfg_read）并轮询状态。请求提交后模块
 * 进入忙状态，串口透传数据流会暂停约 5~20 秒（配置期间）。
 */
#ifndef ESP01S_CFG_H_
#define ESP01S_CFG_H_

#include <stdbool.h>
#include <stdint.h>

typedef enum e_esp01s_cfg_state
{
    ESP01S_CFG_IDLE = 0,   /* 空闲（无请求、无进行中操作） */
    ESP01S_CFG_BUSY,       /* 正在执行 AT 序列 */
    ESP01S_CFG_OK,         /* 最近一次操作成功 */
    ESP01S_CFG_ERR,        /* 最近一次操作失败 */
} esp01s_cfg_state_t;

#define ESP01S_CFG_HOST_MAX   (63U)

typedef struct
{
    esp01s_cfg_state_t state;
    uint8_t  last_err;            /* 0=无；1=模块无响应/超时；2=模块返回 ERROR；3=参数非法 */
    bool     has_cfg;             /* host/port 是否为有效配置 */
    char     host[ESP01S_CFG_HOST_MAX + 1U];
    uint16_t port;
    char     detail[120];         /* 面向 UI 的结果文案（UTF-8） */
} esp01s_cfg_status_t;

/** 初始化（创建互斥量、默认状态；Network 线程进入主循环前调用一次）。 */
void esp01s_cfg_init(void);

/** 是否忙（有请求未处理或 AT 序列进行中）。Network 线程用其暂停透传日志转发。 */
bool esp01s_cfg_busy(void);

/** 请求配置透传主机（GUI 调用，非阻塞）。返回 false = 忙或参数非法。 */
bool esp01s_cfg_set(const char * p_host, uint16_t port);

/** 请求读取模块当前保存的透传主机（GUI 调用，非阻塞）。返回 false = 忙。 */
bool esp01s_cfg_read(void);

/** 中止当前 AT 序列（回到空闲；GUI 返回/关闭页面时调用，可选）。 */
void esp01s_cfg_abort(void);

/** Network 线程周期调用（间隔 <= 50ms）推进 AT 状态机。 */
void esp01s_cfg_service(void);

/** 查询状态（GUI 线程调用；内部互斥拷贝）。 */
void esp01s_cfg_get_status(esp01s_cfg_status_t * p_out);

#endif /* ESP01S_CFG_H_ */
