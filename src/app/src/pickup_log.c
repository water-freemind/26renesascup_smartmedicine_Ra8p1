#include "pickup_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

static pickup_log_entry_t s_entries[PICKUP_LOG_MAX];
static uint32_t           s_next_index;
static uint32_t           s_total;

void pickup_log_add(const char * p_fmt, ...)
{
    if (NULL == p_fmt)
    {
        return;
    }

    pickup_log_entry_t entry;
    entry.tick = (uint32_t) xTaskGetTickCount();
    entry.text[0] = '\0';

    va_list args;
    va_start(args, p_fmt);
    (void) vsnprintf(entry.text, sizeof(entry.text), p_fmt, args);
    va_end(args);
    entry.text[sizeof(entry.text) - 1U] = '\0';

    taskENTER_CRITICAL();
    s_entries[s_next_index] = entry;
    s_next_index = (s_next_index + 1U) % PICKUP_LOG_MAX;
    s_total++;
    taskEXIT_CRITICAL();
}

bool pickup_log_peek(uint32_t index, pickup_log_entry_t * p_out)
{
    if (NULL == p_out)
    {
        return false;
    }

    taskENTER_CRITICAL();
    uint32_t const count = (s_total < PICKUP_LOG_MAX) ? s_total : PICKUP_LOG_MAX;
    if (index >= count)
    {
        taskEXIT_CRITICAL();
        return false;
    }
    /* index 0 = 最新：环形中最后一个有效槽 */
    uint32_t const slot = (s_next_index + PICKUP_LOG_MAX - 1U - index) % PICKUP_LOG_MAX;
    *p_out = s_entries[slot];
    taskEXIT_CRITICAL();
    return true;
}

uint32_t pickup_log_count(void)
{
    uint32_t count;
    taskENTER_CRITICAL();
    count = (s_total < PICKUP_LOG_MAX) ? s_total : PICKUP_LOG_MAX;
    taskEXIT_CRITICAL();
    return count;
}

void pickup_log_clear(void)
{
    taskENTER_CRITICAL();
    memset(s_entries, 0, sizeof(s_entries));
    s_next_index = 0U;
    s_total = 0U;
    taskEXIT_CRITICAL();
}
