#ifndef PICKUP_LOG_H
#define PICKUP_LOG_H

#include <stdint.h>
#include <stdbool.h>

/**********************************************************************************************************************
 * 取药记录模块（与系统日志分离的独立环形缓冲）
 *
 *  - 只记录"取药业务"事件（药品出库、取药单完成、重新扫描），不含系统事件
 *    （自检、字体、库存台账等仍走 sys_log）。
 *  - 跨线程安全：与 sys_log 相同，短临界区保护。
 *  - UI 层通过 pickup_log_peek() 读取最近 N 条展示。
 **********************************************************************************************************************/

#define PICKUP_LOG_MAX       (16U)                 /* 环形容量（保留最近 16 条） */
#define PICKUP_LOG_TEXT_MAX  (72U)

typedef struct
{
    uint32_t tick;                 /* 开机 tick（ms） */
    char     text[PICKUP_LOG_TEXT_MAX];
} pickup_log_entry_t;

/** 写入一条取药记录（printf 风格格式化） */
void pickup_log_add(const char * p_fmt, ...);

/** 读取环形缓冲（index 0 = 最新一条；返回 false 表示越界/无内容） */
bool pickup_log_peek(uint32_t index, pickup_log_entry_t * p_out);

/** 当前可读条数（≤ PICKUP_LOG_MAX） */
uint32_t pickup_log_count(void);

/** 清空取药记录 */
void pickup_log_clear(void);

#endif /* PICKUP_LOG_H */
