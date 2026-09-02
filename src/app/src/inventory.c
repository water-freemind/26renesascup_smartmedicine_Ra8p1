#include "inventory.h"

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "drug_db.h"

#define INVENTORY_MAX_ITEMS     (8U)
#define INVENTORY_DEFAULT_CAP   (30U)
#define INVENTORY_INITIAL_QTY   (8U)

typedef struct
{
    char     code[24];
    uint32_t qty;
    uint32_t capacity;
    bool     used;
} inventory_item_t;

static inventory_item_t s_items[INVENTORY_MAX_ITEMS];
static bool             s_initialized;

static inventory_item_t * inventory_find(const char * p_code)
{
    for (uint32_t i = 0U; i < INVENTORY_MAX_ITEMS; i++)
    {
        if (s_items[i].used && (0 == strcmp(s_items[i].code, p_code)))
        {
            return &s_items[i];
        }
    }
    return NULL;
}

void inventory_init(void)
{
    if (s_initialized)
    {
        return;
    }

    memset(s_items, 0, sizeof(s_items));
    uint32_t used = 0U;

    /* 以 drug_db 演示表初始化：每个药品一个仓位 */
    for (uint32_t i = 0U; (i < drug_db_entry_count()) && (used < INVENTORY_MAX_ITEMS); i++)
    {
        const drug_db_entry_t * p_drug = drug_db_entry(i);
        if ((NULL == p_drug) || (NULL == inventory_find(p_drug->code)))
        {
            s_items[used].used = true;
            (void) snprintf(s_items[used].code, sizeof(s_items[used].code), "%s", p_drug->code);
            s_items[used].qty = INVENTORY_INITIAL_QTY;
            s_items[used].capacity = INVENTORY_DEFAULT_CAP;
            used++;
        }
    }
    s_initialized = true;
}

uint32_t inventory_get(const char * p_code)
{
    if (NULL == p_code)
    {
        return 0U;
    }
    inventory_item_t * p_item = inventory_find(p_code);
    return (NULL != p_item) ? p_item->qty : 0U;
}

uint32_t inventory_capacity(const char * p_code)
{
    if (NULL == p_code)
    {
        return 0U;
    }
    inventory_item_t * p_item = inventory_find(p_code);
    return (NULL != p_item) ? p_item->capacity : 0U;
}

uint32_t inventory_add(const char * p_code, uint32_t qty)
{
    if ((NULL == p_code) || (0U == qty))
    {
        return inventory_get(p_code);
    }

    inventory_item_t * p_item = inventory_find(p_code);
    if (NULL == p_item)
    {
        /* 新药品自动开一个仓位 */
        for (uint32_t i = 0U; i < INVENTORY_MAX_ITEMS; i++)
        {
            if (!s_items[i].used)
            {
                s_items[i].used = true;
                (void) snprintf(s_items[i].code, sizeof(s_items[i].code), "%s", p_code);
                s_items[i].qty = 0U;
                s_items[i].capacity = INVENTORY_DEFAULT_CAP;
                p_item = &s_items[i];
                break;
            }
        }
        if (NULL == p_item)
        {
            return 0U;
        }
    }

    taskENTER_CRITICAL();
    uint32_t const total = p_item->qty + qty;
    p_item->qty = (total > p_item->capacity) ? p_item->capacity : total;
    uint32_t const result = p_item->qty;
    taskEXIT_CRITICAL();
    return result;
}

uint32_t inventory_remove(const char * p_code, uint32_t qty)
{
    if ((NULL == p_code) || (0U == qty))
    {
        return inventory_get(p_code);
    }

    inventory_item_t * p_item = inventory_find(p_code);
    if (NULL == p_item)
    {
        return 0U;
    }

    taskENTER_CRITICAL();
    p_item->qty = (p_item->qty > qty) ? (p_item->qty - qty) : 0U;
    uint32_t const result = p_item->qty;
    taskEXIT_CRITICAL();
    return result;
}
