#ifndef INVENTORY_H
#define INVENTORY_H

#include <stdint.h>

/**********************************************************************************************************************
 * 库存台账（RAM 演示数据源）
 *
 * 上电时按 drug_db 演示表初始化每个药品的库存数量；存药确认 +N、
 * 取药动作 -N，供 Store 容量 / Pickup 清单数量状态 实时显示。
 * 后续接入真实数据库（EEPROM/网页端下发）时替换底层即可，接口不变。
 **********************************************************************************************************************/

/* 初始化库存（按 drug_db 条目，每个药品一个仓位计数） */
void inventory_init(void);

/* 查询药品库存数量（按 code 查；未知名返回 0） */
uint32_t inventory_get(const char * p_code);

/* 查询仓位容量（默认 30 盒） */
uint32_t inventory_capacity(const char * p_code);

/* 存药 +qty；返回操作后数量 */
uint32_t inventory_add(const char * p_code, uint32_t qty);

/* 取药 -qty；不足时按 0 截断；返回操作后数量 */
uint32_t inventory_remove(const char * p_code, uint32_t qty);

#endif /* INVENTORY_H */
