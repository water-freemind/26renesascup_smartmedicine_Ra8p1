#include "sys_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

static sys_log_entry_t s_entries[SYS_LOG_ENTRIES];
static uint32_t        s_next_index;              /* 下一个写入槽 */
static uint32_t        s_level_counts[SYS_LOG_LEVELS];
static uint32_t        s_total;

void sys_log_add(sys_log_level_t level, const char * p_fmt, ...)
{
    if ((level >= SYS_LOG_LEVELS) || (NULL == p_fmt))
    {
        return;
    }

    sys_log_entry_t entry;
    entry.tick = (uint32_t) xTaskGetTickCount();
    entry.level = level;
    entry.text[0] = '\0';

    va_list args;
    va_start(args, p_fmt);
    (void) vsnprintf(entry.text, sizeof(entry.text), p_fmt, args);
    va_end(args);
    entry.text[sizeof(entry.text) - 1U] = '\0';

    taskENTER_CRITICAL();
    s_entries[s_next_index] = entry;
    s_next_index = (s_next_index + 1U) % SYS_LOG_ENTRIES;
    s_level_counts[level]++;
    s_total++;
    taskEXIT_CRITICAL();
}

bool sys_log_peek(uint32_t index, sys_log_entry_t * p_out)
{
    if (NULL == p_out)
    {
        return false;
    }

    taskENTER_CRITICAL();
    uint32_t const count = (s_total < SYS_LOG_ENTRIES) ? s_total : SYS_LOG_ENTRIES;
    if (index >= count)
    {
        taskEXIT_CRITICAL();
        return false;
    }
    /* index 0 = 最新：环形中最后一个有效槽 */
    uint32_t const slot = (s_next_index + SYS_LOG_ENTRIES - 1U - index) % SYS_LOG_ENTRIES;
    *p_out = s_entries[slot];
    taskEXIT_CRITICAL();
    return true;
}

uint32_t sys_log_count(void)
{
    uint32_t count;
    taskENTER_CRITICAL();
    count = (s_total < SYS_LOG_ENTRIES) ? s_total : SYS_LOG_ENTRIES;
    taskEXIT_CRITICAL();
    return count;
}

uint32_t sys_log_count_level(sys_log_level_t level)
{
    if (level >= SYS_LOG_LEVELS)
    {
        return 0U;
    }
    uint32_t count;
    taskENTER_CRITICAL();
    count = s_level_counts[level];
    taskEXIT_CRITICAL();
    return count;
}

uint32_t sys_log_total(void)
{
    uint32_t total;
    taskENTER_CRITICAL();
    total = s_total;
    taskEXIT_CRITICAL();
    return total;
}

void sys_log_clear(void)
{
    taskENTER_CRITICAL();
    memset(s_entries, 0, sizeof(s_entries));
    s_next_index = 0U;
    s_total = 0U;
    memset(s_level_counts, 0, sizeof(s_level_counts));
    taskEXIT_CRITICAL();
}
