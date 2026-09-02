#ifndef SYS_LOG_H
#define SYS_LOG_H

#include <stdbool.h>
#include <stdint.h>

/**********************************************************************************************************************
 * 系统日志模块（跨线程安全环形缓冲）
 *
 *  - 任意线程（LVGL / Camera / 驱动回调）均可调用 sys_log_add() 写入；
 *    内部用短临界区保护（条目复制 ~100B，微秒级，不影响共享 IIC0 事务）。
 *  - 日志条目保留"级别 + 开机时间戳 + 文本"，UI 层可读取最近 N 条展示。
 *  - 附带各级别累计计数，供 Logs 页指标卡（异常/成功/总数）统计。
 **********************************************************************************************************************/

typedef enum e_sys_log_level
{
    SYS_LOG_INFO = 0,   /* 常规信息 */
    SYS_LOG_OK,         /* 成功 */
    SYS_LOG_WARN,       /* 警告 */
    SYS_LOG_ERR,        /* 错误 */
    SYS_LOG_LEVELS
} sys_log_level_t;

#define SYS_LOG_TEXT_MAX    (80U)
#define SYS_LOG_ENTRIES     (32U)   /* 环形缓冲容量（保留最近 32 条） */

typedef struct
{
    uint32_t    tick;                /* 开机 tick（ms，xTaskGetTickCount） */
    sys_log_level_t level;
    char        text[SYS_LOG_TEXT_MAX];
} sys_log_entry_t;

/**********************************************************************************************************************
 * 写入一条日志（printf 风格格式化）
 **********************************************************************************************************************/
void sys_log_add(sys_log_level_t level, const char * p_fmt, ...);

/**********************************************************************************************************************
 * 读取环形缓冲（index 0 = 最新一条；返回 false 表示越界/无内容）
 **********************************************************************************************************************/
bool sys_log_peek(uint32_t index, sys_log_entry_t * p_out);

/* 环形内当前可读条数（≤ SYS_LOG_ENTRIES） */
uint32_t sys_log_count(void);

/* 开机以来各级别累计写入次数（供指标卡统计） */
uint32_t sys_log_count_level(sys_log_level_t level);

/* 开机以来总写入次数 */
uint32_t sys_log_total(void);

/* 清空环形缓冲与各级别计数（Logs 页"清空日志"按钮） */
void sys_log_clear(void);

#endif /* SYS_LOG_H */
