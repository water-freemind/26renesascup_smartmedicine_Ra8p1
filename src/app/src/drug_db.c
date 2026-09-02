#include "drug_db.h"

#include <stdint.h>
#include <string.h>

/* 演示药品表：与 GUI 页面示例数据一致，后续由真实数据库/网页端下发替换。
 * position 格式 = "X<脉冲>Y<脉冲>"（用户确认：仓位带坐标位置、单位是脉冲数），
 * 换算基准 X/Y 轴 3600/84 脉冲/mm，对应参数页默认药柜（格宽50mm，层Y 9.33/149.33/291.7mm）：
 *   AMOX-001 → 第1层第1格 (X0, Y400脉冲)  → "X0Y400"
 *   VITC-002 → 第2层第2格 (X50mm=2143, Y6400脉冲) → "X2143Y6400"
 *   IBU-003  → 第3层第3格 (X100mm=4286, Y12500脉冲) → "X4286Y12500"
 *   AMOX     → 第1层第2格 (X50mm=2143, Y400脉冲) → "X2143Y400" */
static const drug_db_entry_t s_drug_db[] =
{
    { "AMOX-001", "阿莫西林胶囊", "0.25 g", "RA8P1-A01", "2028-08", "X0Y400" },
    { "VITC-002", "维生素C片",    "100 mg", "RA8P1-B02", "2027-06", "X2143Y6400" },
    { "IBU-003",  "布洛芬缓释胶囊", "0.3 g", "RA8P1-C03", "2029-01", "X4286Y12500" },
    { "AMOX",     "阿莫西林胶囊", "0.25 g", "RA8P1-A01", "2028-08", "X2143Y400" },
};

const drug_db_entry_t * drug_db_lookup(const char * p_payload)
{
    if (NULL == p_payload)
    {
        return NULL;
    }

    /* 精确相等优先 */
    for (uint32_t i = 0U; i < drug_db_entry_count(); i++)
    {
        if (0 == strcmp(p_payload, s_drug_db[i].code))
        {
            return &s_drug_db[i];
        }
    }

    /* 子串匹配：payload 含 code，或 code 含 payload（如 URL 中夹带编码） */
    for (uint32_t i = 0U; i < drug_db_entry_count(); i++)
    {
        if ((NULL != strstr(p_payload, s_drug_db[i].code)) ||
            (NULL != strstr(s_drug_db[i].code, p_payload)))
        {
            return &s_drug_db[i];
        }
    }

    return NULL;
}

const drug_db_entry_t * drug_db_entry(uint32_t index)
{
    if (index >= drug_db_entry_count())
    {
        return NULL;
    }
    return &s_drug_db[index];
}

uint32_t drug_db_entry_count(void)
{
    return (uint32_t) (sizeof(s_drug_db) / sizeof(s_drug_db[0]));
}