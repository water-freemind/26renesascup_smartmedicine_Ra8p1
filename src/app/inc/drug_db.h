#ifndef DRUG_DB_H
#define DRUG_DB_H

#include <stdint.h>

/**********************************************************************************************************************
 * 药品信息库（演示数据源）
 *
 * 当前无真实药品数据库（待网页端/服务器下发），先内置少量演示条目：
 *  - Medicine 页：剂量/批次/有效期/仓位 由扫码 payload 查表填充；
 *  - Pickup 页：取药单 payload 若命中药品编码，填充清单行。
 * 查表规则：payload 与条目 code 做子串匹配（双向），最优先精确相等。
 **********************************************************************************************************************/

typedef struct
{
    const char * code;      /* 二维码中的药品编码（如 "AMOX-001"） */
    const char * name;      /* 药品名 */
    const char * dose;      /* 剂量 */
    const char * batch;     /* 批次 */
    const char * expiry;    /* 有效期 */
    const char * position;  /* 默认仓位（机械臂坐标） */
} drug_db_entry_t;

/* 按 payload 查表；命中返回条目指针，未命中返回 NULL */
const drug_db_entry_t * drug_db_lookup(const char * p_payload);

/* 演示条目表（供 UI 遍历/填充清单） */
const drug_db_entry_t * drug_db_entry(uint32_t index);
uint32_t drug_db_entry_count(void);

#endif /* DRUG_DB_H */
