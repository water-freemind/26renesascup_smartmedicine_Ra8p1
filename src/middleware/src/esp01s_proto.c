/*
 * esp01s_proto.c
 *
 * ESP-01S ↔ 云端平台 数据协议层（权威版）实现。
 *
 * 上行：按协议构建 JSON 行（UTF-8、'\n' 结尾）经 esp01s_uart 透传发出；
 *       心跳由 esp01s_proto_service() 在 Network 线程定时发送。
 * 下行：esp01s_proto_process_line() 解析一行 JSON：
 *         CONNECTED       → 连接确认：ok:true 置 ONLINE，ok 非 true 置 OFFLINE；
 *         PING            → 回一条 HEARTBEAT；
 *         DISPENSE_ACTION → 解析 taskId + slots[]，逐货位执行机械出药
 *                           （复用 PickupTest 取放流程），每个货位完成后
 *                           回报 ACTION_FINISHED（SUCCESS/FAILED）；
 *         STORAGE_PLACE   → 解析 taskId + item{drugId/coord/layer/x}，
 *                           执行存药搬运（取药口取药 → 放到目标仓），
 *                           完成后回报 PLACE_FINISHED（SUCCESS/FAILED）。
 * 连接状态：三态（CONNECTING/ONLINE/OFFLINE）供 LVGL 显示；断线判定
 *           （发 HEARTBEAT 后约 40s 无任何云端下行 → 回 CONNECTING）。
 *
 * AT 透传主机配置（esp01s_cfg）期间暂停一切上行发送与下行解析。
 */

#include "esp01s_proto.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp01s_uart.h"
#include "esp01s_cfg.h"
#include "sys_log.h"
#include "pickup_test.h"
#include "pickup_params.h"
#include "FreeRTOS.h"
#include "task.h"

/* ============================================================================
 * 内部常量 / 类型
 * ==========================================================================*/
#define PROTO_CMD_MAX        (24U)   /* 下行 cmd 值缓冲 */
#define PROTO_TASK_ID_MAX    (48U)   /* taskId 缓冲（TASK-20260820-001 等） */
#define PROTO_COORD_MAX      (16U)   /* 货位坐标缓冲（A-03 等） */

typedef struct
{
    char    coord[PROTO_COORD_MAX + 1U];
    uint8_t tray_index;              /* 0 起（解析时 1 起减 1；备用，主用 coord 字母） */
    uint8_t qty;                     /* 1..255，缺省 1 */
} esp01s_proto_slot_t;

typedef struct
{
    char    task_id[PROTO_TASK_ID_MAX + 1U];
    esp01s_proto_slot_t slots[ESP01S_PROTO_MAX_SLOTS];
    uint8_t count;                   /* 有效货位数 */
    uint8_t index;                   /* 当前执行到第几个（0 起） */
    bool    active;                  /* 云端出药任务进行中 */
} esp01s_dispense_ctx_t;

/* 云端存药任务（STORAGE_PLACE）上下文 */
typedef struct
{
    char    task_id[ESP01S_PROTO_TASK_ID_MAX + 1U];
    char    coord[ESP01S_PROTO_COORD_MAX + 1U];   /* 目标货位（A-02 等） */
    char    drug_id[ESP01S_PROTO_DRUG_MAX + 1U];
    char    drug_name[ESP01S_PROTO_NAME_MAX + 1U];
    float   x_mm;                   /* 目标 X（mm，云端下发 item.x） */
    float   y_mm;                   /* 目标 Y（mm，layer → 层Y） */
    float   w_mm;                   /* 药品宽度（mm，item.w；≤0 回退 grip_pulses） */
    bool    active;                 /* 存药任务进行中 */
    /* 最近一次结果（供 LVGL 显示"已完成/失败"；tick 判龄） */
    uint8_t last_result;            /* 0=无 1=SUCCESS 2=FAILED */
    uint32_t last_tick;
} esp01s_place_ctx_t;

/* ============================================================================
 * 内部状态
 * ==========================================================================*/
static esp01s_dispense_ctx_t s_dispense;
static esp01s_place_ctx_t    s_place;
static uint32_t              s_hb_last;        /* 上次心跳 tick */
static bool                  s_hb_sent_once;   /* 首条心跳是否已发（首条用 INITIAL 周期） */
static esp01s_conn_state_t   s_conn_state;     /* 云端连接状态（三态） */
static uint32_t              s_rx_last;        /* 最近一次收到云端数据（任意行）的 tick */
static bool                  s_rx_seen;        /* 是否收到过任意下行（用于断线判定基线） */

/* ============================================================================
 * 轻量 JSON 取值（针对协议固定字段格式："key":"value" / "key":123）
 * ==========================================================================*/
/* 在 p_from 起查找 "key" 键的位置（找不到返回 NULL） */
static const char * proto_key_pos(const char * p_from, const char * p_key)
{
    if ((NULL == p_from) || (NULL == p_key))
    {
        return NULL;
    }
    char pat[48];
    (void) snprintf(pat, sizeof(pat), "\"%s\"", p_key);
    return strstr(p_from, pat);
}

/* 从键位置解析字符串值 "key":"value"（失败返回 false） */
static bool proto_value_str(const char * p_key_pos, const char * p_key,
                            char * p_out, uint32_t out_size)
{
    if ((NULL == p_key_pos) || (NULL == p_out) || (out_size == 0U))
    {
        return false;
    }
    const char * p = p_key_pos + strlen(p_key) + 2U; /* 跳过 "\"key\"" */
    while ((*p == ' ') || (*p == '\t'))
    {
        p++;
    }
    if (*p != ':')
    {
        return false;
    }
    p++;
    while ((*p == ' ') || (*p == '\t'))
    {
        p++;
    }
    if (*p != '"')
    {
        return false;
    }
    p++;
    const char * p_end = strchr(p, '"');
    if (NULL == p_end)
    {
        return false;
    }
    uint32_t n = (uint32_t) (p_end - p);
    if (n >= out_size)
    {
        n = out_size - 1U;
    }
    memcpy(p_out, p, n);
    p_out[n] = '\0';
    return true;
}

/* 从键位置解析数值 "key":123（失败返回 false） */
static bool proto_value_num(const char * p_key_pos, const char * p_key, long * p_val)
{
    if ((NULL == p_key_pos) || (NULL == p_val))
    {
        return false;
    }
    const char * p = p_key_pos + strlen(p_key) + 2U;
    while ((*p == ' ') || (*p == '\t'))
    {
        p++;
    }
    if (*p != ':')
    {
        return false;
    }
    p++;
    while ((*p == ' ') || (*p == '\t'))
    {
        p++;
    }
    if (((*p < '0') || (*p > '9')) && (*p != '-'))
    {
        return false;
    }
    char * p_end = NULL;
    long const v = strtol(p, &p_end, 10);
    if (p_end == p)
    {
        return false;
    }
    *p_val = v;
    return true;
}

/* 整行取字符串值 */
static bool proto_json_str(const char * p_line, const char * p_key,
                           char * p_out, uint32_t out_size)
{
    const char * kp = proto_key_pos(p_line, p_key);
    if (NULL == kp)
    {
        return false;
    }
    return proto_value_str(kp, p_key, p_out, out_size);
}

/* ============================================================================
 * 上行发送（内部：拼行 + '\n' + 透传发出）
 * ==========================================================================*/
static void proto_send_line(const char * p_json)
{
    if (esp01s_cfg_busy())
    {
        /* AT 透传配置期间串口被 cfg 独占，跳过本轮发送（心跳错过几拍无妨） */
        return;
    }
    char line[ESP01S_PROTO_LINE_MAX];
    (void) snprintf(line, sizeof(line), "%s\n", p_json);
    if (esp01s_uart_send_str(line))
    {
        sys_log_add(SYS_LOG_OK, "ESP↑ %s", p_json);
    }
    else
    {
        sys_log_add(SYS_LOG_ERR, "ESP↑ 发送失败: %s", p_json);
    }
}

void esp01s_proto_send_heartbeat(void)
{
    char j[192];
    (void) snprintf(j, sizeof(j),
                    "{\"deviceId\":\"%s\",\"type\":\"HEARTBEAT\",\"rssi\":-60}",
                    ESP01S_PROTO_DEVICE_ID);
    proto_send_line(j);
}

void esp01s_proto_send_pickup_scanned(const char * p_drug_id)
{
    if ((NULL == p_drug_id) || ('\0' == p_drug_id[0]))
    {
        return;
    }
    char j[256];
    (void) snprintf(j, sizeof(j),
                    "{\"deviceId\":\"%s\",\"type\":\"PICKUP_SCANNED\",\"drugId\":\"%s\"}",
                    ESP01S_PROTO_DEVICE_ID, p_drug_id);
    proto_send_line(j);
}

void esp01s_proto_send_action_finished(const char * p_task_id,
                                       const char * p_slot_coord,
                                       uint32_t dispensed_qty,
                                       const char * p_status)
{
    if ((NULL == p_task_id) || (NULL == p_slot_coord) || (NULL == p_status))
    {
        return;
    }
    char j[320];
    (void) snprintf(j, sizeof(j),
                    "{\"deviceId\":\"%s\",\"type\":\"ACTION_FINISHED\","
                    "\"taskId\":\"%s\",\"slotCoord\":\"%s\","
                    "\"dispensedQty\":%lu,\"status\":\"%s\"}",
                    ESP01S_PROTO_DEVICE_ID, p_task_id, p_slot_coord,
                    (unsigned long) dispensed_qty, p_status);
    proto_send_line(j);
}

void esp01s_proto_send_place_finished(const char * p_task_id,
                                      const char * p_coord,
                                      const char * p_status)
{
    if ((NULL == p_task_id) || (NULL == p_coord) || (NULL == p_status))
    {
        return;
    }
    char j[256];
    (void) snprintf(j, sizeof(j),
                    "{\"deviceId\":\"%s\",\"type\":\"PLACE_FINISHED\","
                    "\"taskId\":\"%s\",\"coord\":\"%s\",\"status\":\"%s\"}",
                    ESP01S_PROTO_DEVICE_ID, p_task_id, p_coord, p_status);
    proto_send_line(j);
}

void esp01s_proto_send_relay_ctrl(uint8_t relay, bool on)
{
    /* 继电器代号 1~8（USBRelay8 通道，云端校验范围） */
    if ((relay < 1U) || (relay > 8U))
    {
        return;
    }
    char j[160];
    (void) snprintf(j, sizeof(j),
                    "{\"deviceId\":\"%s\",\"type\":\"RELAY_CTRL\",\"relay\":%u,\"state\":%d}",
                    ESP01S_PROTO_DEVICE_ID, (unsigned) relay, on ? 1 : 0);
    proto_send_line(j);
}

void esp01s_proto_send_sensor_triggered(const char * p_slot_coord)
{
    if ((NULL == p_slot_coord) || ('\0' == p_slot_coord[0]))
    {
        return;
    }
    char j[192];
    (void) snprintf(j, sizeof(j),
                    "{\"deviceId\":\"%s\",\"type\":\"SENSOR_TRIGGERED\",\"slotCoord\":\"%s\"}",
                    ESP01S_PROTO_DEVICE_ID, p_slot_coord);
    proto_send_line(j);
}

void esp01s_proto_send_alarm(const char * p_level, const char * p_message)
{
    if ((NULL == p_level) || (NULL == p_message))
    {
        return;
    }
    char j[256];
    (void) snprintf(j, sizeof(j),
                    "{\"deviceId\":\"%s\",\"type\":\"ALARM\",\"level\":\"%s\",\"message\":\"%s\"}",
                    ESP01S_PROTO_DEVICE_ID, p_level, p_message);
    proto_send_line(j);
}

/* ============================================================================
 * DISPENSE_ACTION 解析
 * ==========================================================================*/
static void proto_dispense_parse(const char * p_line)
{
    esp01s_dispense_ctx_t * ctx = &s_dispense;
    memset(ctx, 0, sizeof(*ctx));

    (void) proto_json_str(p_line, "taskId", ctx->task_id, sizeof(ctx->task_id));
    if ('\0' == ctx->task_id[0])
    {
        (void) snprintf(ctx->task_id, sizeof(ctx->task_id), "UNKNOWN");
    }

    /* slots[]：按平台序列化顺序逐对象扫描 coord → trayIndex → qty。
     * 每个对象字段在 JSON 中连续出现，顺序消费即可正确归属。 */
    const char * p = p_line;
    while (ctx->count < ESP01S_PROTO_MAX_SLOTS)
    {
        const char * ck = proto_key_pos(p, "coord");
        if (NULL == ck)
        {
            break;
        }
        esp01s_proto_slot_t * s = &ctx->slots[ctx->count];
        s->qty = 1U;
        (void) proto_value_str(ck, "coord", s->coord, sizeof(s->coord));
        if ('\0' == s->coord[0])
        {
            break;
        }
        const char * tk = proto_key_pos(ck, "trayIndex");
        long tray = 0L;
        if ((NULL != tk) && proto_value_num(tk, "trayIndex", &tray) && (tray > 0L))
        {
            s->tray_index = (tray > 255L) ? 255U : (uint8_t) (tray - 1L);
        }
        const char * qk = proto_key_pos(ck, "qty");
        long q = 0L;
        if ((NULL != qk) && proto_value_num(qk, "qty", &q) && (q > 0L))
        {
            s->qty = (q > 255L) ? 255U : (uint8_t) q;
        }
        p = ck + 7; /* 跳到本次 coord 键之后，继续找下一个货位 */
        ctx->count++;
    }
    sys_log_add(SYS_LOG_INFO, "ESP↓ DISPENSE_ACTION 任务%s 共%u个货位",
                ctx->task_id, (unsigned) ctx->count);
}

/* ============================================================================
 * 云端出药执行（复用 PickupTest 取放流程，Network 线程驱动）
 * ==========================================================================*/
/* "A-03" → 层号（A=0/B=1/C=2）+ 列号（1 起）→ X/Y 坐标（mm）。
 * 失败返回 false（坐标无效/越界）。 */
static bool proto_coord_target(const char * p_coord, float * p_x_mm, float * p_y_mm)
{
    if ((NULL == p_coord) || ('\0' == p_coord[0]) || (NULL == p_x_mm) || (NULL == p_y_mm))
    {
        return false;
    }
    char row = p_coord[0];
    if ((row >= 'a') && (row <= 'z'))
    {
        row = (char) (row - ('a' - 'A'));
    }
    if ((row < 'A') || (row > 'Z'))
    {
        return false;
    }
    char * p_end = NULL;
    long const col = strtol(p_coord + 1, &p_end, 10);
    if ((p_end == p_coord + 1) || (col < 1L))
    {
        return false;
    }

    const pickup_params_t * pp = PickupParams_Get();
    uint8_t const shelf = (uint8_t) (row - 'A');
    if (shelf >= pp->shelf_count)
    {
        return false;
    }
    if ((uint32_t) col > pp->slots_per_row)
    {
        return false;
    }
    *p_x_mm = PickupParams_SlotX((uint8_t) (col - 1L));
    *p_y_mm = PickupParams_ShelfY(shelf);
    return true;
}

/* 尝试启动当前货位的取放流程；成功启动返回 true，失败（已回报 FAILED）返回 false。 */
static bool proto_dispense_try_start_slot(void)
{
    esp01s_dispense_ctx_t * ctx = &s_dispense;
    esp01s_proto_slot_t * slot = &ctx->slots[ctx->index];

    if (PickupTest_IsRunning())
    {
        esp01s_proto_send_action_finished(ctx->task_id, slot->coord, 0U, "FAILED");
        esp01s_proto_send_alarm("ERROR", "出药失败: 机械臂忙");
        sys_log_add(SYS_LOG_ERR, "云端出药: %s %s 未启动（机械臂忙）", ctx->task_id, slot->coord);
        return false;
    }

    float x_mm = 0.0f, y_mm = 0.0f;
    if (!proto_coord_target(slot->coord, &x_mm, &y_mm))
    {
        esp01s_proto_send_action_finished(ctx->task_id, slot->coord, 0U, "FAILED");
        esp01s_proto_send_alarm("ERROR", "出药失败: 货位坐标无效");
        sys_log_add(SYS_LOG_ERR, "云端出药: %s %s 坐标无效", ctx->task_id, slot->coord);
        return false;
    }

    PickupTest_StartCloud(x_mm, y_mm, slot->qty);
    sys_log_add(SYS_LOG_INFO, "云端出药: 任务%s 货位%s 坐标(%d,%d) x%u",
                ctx->task_id, slot->coord, (int) x_mm, (int) y_mm, (unsigned) slot->qty);
    return true;
}

/* 推进到下一个货位（失败货位自动跳过，直至任务结束） */
static void proto_dispense_advance(void)
{
    esp01s_dispense_ctx_t * ctx = &s_dispense;
    while (1)
    {
        ctx->index++;
        if (ctx->index >= ctx->count)
        {
            ctx->active = false;
            sys_log_add(SYS_LOG_OK, "云端出药任务 %s 全部货位执行完毕", ctx->task_id);
            return;
        }
        if (proto_dispense_try_start_slot())
        {
            return;
        }
        /* 本格失败已回报，继续下一格 */
    }
}

static void proto_dispense_begin(void)
{
    esp01s_dispense_ctx_t * ctx = &s_dispense;
    if (ctx->count == 0U)
    {
        sys_log_add(SYS_LOG_WARN, "ESP↓ DISPENSE_ACTION 无有效货位，忽略");
        return;
    }
    if (ctx->active)
    {
        sys_log_add(SYS_LOG_WARN, "ESP↓ 出药任务进行中，忽略新任务 %s", ctx->task_id);
        return;
    }
    ctx->active = true;
    ctx->index = 0U;
    if (!proto_dispense_try_start_slot())
    {
        proto_dispense_advance();   /* 首格失败 → 继续后续格或结束 */
    }
}

/* Network 线程周期调用：推进云端出药任务 */
static void proto_dispense_service(void)
{
    esp01s_dispense_ctx_t * ctx = &s_dispense;
    if (!ctx->active)
    {
        return;
    }

    pickup_test_state_t st;
    PickupTest_GetStatus(&st, NULL);
    if (st == PICKUP_TEST_RUNNING)
    {
        return; /* 等待当前货位取放完成 */
    }
    if (st == PICKUP_TEST_IDLE)
    {
        return; /* 未在运行（理论不出现：StartCloud 后立即 RUNNING） */
    }

    esp01s_proto_slot_t * slot = &ctx->slots[ctx->index];
    if (st == PICKUP_TEST_DONE_OK)
    {
        esp01s_proto_send_action_finished(ctx->task_id, slot->coord,
                                          (uint32_t) slot->qty, "SUCCESS");
        sys_log_add(SYS_LOG_OK, "云端出药: %s %s 完成 %u 盒",
                    ctx->task_id, slot->coord, (unsigned) slot->qty);
    }
    else /* DONE_FAIL */
    {
        esp01s_proto_send_action_finished(ctx->task_id, slot->coord, 0U, "FAILED");
        esp01s_proto_send_alarm("ERROR", "出药失败: 机械动作异常");
        sys_log_add(SYS_LOG_ERR, "云端出药: %s %s 执行失败", ctx->task_id, slot->coord);
    }
    /* 本层消费 DONE 态（GUI 取药轮询在 IsCloudMode 时已让位，不再代 Reset） */
    PickupTest_Reset();
    proto_dispense_advance();
}

/* ============================================================================
 * STORAGE_PLACE 解析与存药搬运执行
 * 协议（《硬件指导_储药搬运STORAGE_PLACE.md》）：下行
 *   {"cmd":"STORAGE_PLACE","taskId":"PLACE-001",
 *    "item":{"drugId":"DRG-1","drugName":"阿莫西林胶囊","from":"取药口",
 *            "coord":"A-02","layer":1,"x":122,"w":72}, "ts":...}
 * 设备：到取药口（暂存区）抓药 → 放到第 layer 层 x mm 处 → 回报
 * PLACE_FINISHED（SUCCESS/FAILED）。from 为"取药口"固定取暂存区坐标；
 * layer 1=A（映射层Y）、x = 药位中心 X（mm）；w/startX/endX 仅参考不消费
 * （夹爪按固定 grip_pulses，与取药一致）。
 * ==========================================================================*/
static void proto_place_parse(const char * p_line)
{
    esp01s_place_ctx_t * ctx = &s_place;
    memset(ctx, 0, sizeof(*ctx));

    (void) proto_json_str(p_line, "taskId", ctx->task_id, sizeof(ctx->task_id));
    if ('\0' == ctx->task_id[0])
    {
        (void) snprintf(ctx->task_id, sizeof(ctx->task_id), "UNKNOWN");
    }
    /* item.*：键名唯一（drugId/coord/layer/x），全行 strstr 即得 */
    (void) proto_json_str(p_line, "drugId", ctx->drug_id, sizeof(ctx->drug_id));
    (void) proto_json_str(p_line, "drugName", ctx->drug_name, sizeof(ctx->drug_name));
    (void) proto_json_str(p_line, "coord", ctx->coord, sizeof(ctx->coord));

    long layer = 0L;
    const char * lk = proto_key_pos(p_line, "layer");
    if ((NULL != lk) && proto_value_num(lk, "layer", &layer) && (layer > 0L))
    {
        /* layer 1=A → shelf 0 起；越界按失败处理（不静默回退第 0 层） */
        const pickup_params_t * pp = PickupParams_Get();
        uint8_t const shelf = (uint8_t) (layer - 1L);
        if (shelf < pp->shelf_count)
        {
            ctx->y_mm = PickupParams_ShelfY(shelf);
        }
        else
        {
            ctx->y_mm = -1.0f;   /* 标记无效层 */
        }
    }
    else
    {
        ctx->y_mm = -1.0f;
    }

    long x_mm = 0L;
    const char * xk = proto_key_pos(p_line, "x");
    if ((NULL != xk) && proto_value_num(xk, "x", &x_mm))
    {
        ctx->x_mm = (float) x_mm;
    }
    else
    {
        ctx->x_mm = -1.0f;   /* 标记无效 x */
    }

    /* 药品宽度 item.w（mm）：夹爪按宽度自动闭合（留 1mm 夹紧）；
     * 缺省 0 = 回退参数 grip_pulses */
    long w_mm = 0L;
    const char * wk = proto_key_pos(p_line, "w");
    if ((NULL != wk) && proto_value_num(wk, "w", &w_mm) && (w_mm > 0L))
    {
        ctx->w_mm = (float) w_mm;
    }
    sys_log_add(SYS_LOG_INFO, "ESP↓ STORAGE_PLACE 任务%s 药%s(%s) → %s 层%ld x%ldmm w%ldmm",
                ctx->task_id, ctx->drug_name, ctx->drug_id, ctx->coord,
                layer, x_mm, w_mm);
}

/* 启动存药流程；失败（坐标无效/机械臂忙）已回报 FAILED，返回 false。 */
static bool proto_place_try_start(void)
{
    esp01s_place_ctx_t * ctx = &s_place;

    if (PickupTest_IsRunning() || s_dispense.active)
    {
        esp01s_proto_send_place_finished(ctx->task_id, ctx->coord, "FAILED");
        esp01s_proto_send_alarm("ERROR", "存药失败: 机械臂忙");
        sys_log_add(SYS_LOG_ERR, "云端存药: %s %s 未启动（机械臂忙）", ctx->task_id, ctx->coord);
        return false;
    }
    if ((ctx->x_mm < 0.0f) || (ctx->y_mm < 0.0f) || ('\0' == ctx->coord[0]))
    {
        esp01s_proto_send_place_finished(ctx->task_id, ctx->coord, "FAILED");
        esp01s_proto_send_alarm("ERROR", "存药失败: 目标坐标无效");
        sys_log_add(SYS_LOG_ERR, "云端存药: %s %s 坐标无效", ctx->task_id, ctx->coord);
        return false;
    }

    PickupTest_StartPlace(ctx->x_mm, ctx->y_mm, ctx->w_mm);
    sys_log_add(SYS_LOG_INFO, "云端存药: 任务%s → %s 坐标(%d,%d) w%ldmm",
                ctx->task_id, ctx->coord, (int) ctx->x_mm, (int) ctx->y_mm,
                (long) ctx->w_mm);
    return true;
}

/* Network 线程周期调用：推进云端存药任务（完成/失败后回报并复位状态机，
 * 由本层消费 DONE 态，GUI 侧经 get_place_info 显示结果，避免双消费竞态） */
static void proto_place_service(void)
{
    esp01s_place_ctx_t * ctx = &s_place;
    if (!ctx->active)
    {
        return;
    }

    pickup_test_state_t st;
    PickupTest_GetStatus(&st, NULL);
    if (st == PICKUP_TEST_RUNNING)
    {
        return; /* 等待存药动作完成 */
    }

    char const * p_status;
    uint8_t result;
    if (st == PICKUP_TEST_DONE_OK)
    {
        p_status = "SUCCESS";
        result = 1U;
        sys_log_add(SYS_LOG_OK, "云端存药: %s %s 完成", ctx->task_id, ctx->coord);
    }
    else /* DONE_FAIL / IDLE（理论不出现） */
    {
        p_status = "FAILED";
        result = 2U;
        esp01s_proto_send_alarm("ERROR", "存药失败: 机械动作异常");
        sys_log_add(SYS_LOG_ERR, "云端存药: %s %s 执行失败", ctx->task_id, ctx->coord);
    }

    esp01s_proto_send_place_finished(ctx->task_id, ctx->coord, p_status);
    ctx->active = false;
    ctx->last_result = result;
    ctx->last_tick = (uint32_t) xTaskGetTickCount();
    PickupTest_Reset();   /* 消费 DONE 态（防 GUI 轮询重复处理） */
}

static void proto_place_begin(void)
{
    esp01s_place_ctx_t * ctx = &s_place;
    if (ctx->active)
    {
        sys_log_add(SYS_LOG_WARN, "ESP↓ 存药任务进行中，忽略新任务 %s", ctx->task_id);
        return;
    }
    ctx->active = true;
    (void) proto_place_try_start();
}

/* ============================================================================
 * 下行解析入口
 * ==========================================================================*/
void esp01s_proto_process_line(const char * p_line)
{
    if ((NULL == p_line) || ('\0' == p_line[0]))
    {
        return;
    }

    /* 任何到达的透传行都视为"云端有数据"，刷新断线判定基线 */
    s_rx_last = (uint32_t) xTaskGetTickCount();
    s_rx_seen = true;

    char cmd[PROTO_CMD_MAX];
    if (!proto_json_str(p_line, "cmd", cmd, sizeof(cmd)))
    {
        return; /* 非指令行（透传日志等），已由 Network 线程记录 */
    }
    sys_log_add(SYS_LOG_OK, "ESP↓ 指令: %s", cmd);

    if (0 == strcmp(cmd, "CONNECTED"))
    {
        /* 连接确认：{"cmd":"CONNECTED","ok":true,...} → ONLINE；ok 非 true → OFFLINE */
        bool const ok = (NULL != strstr(p_line, "\"ok\":true"));
        esp01s_conn_state_t const prev = s_conn_state;
        s_conn_state = ok ? ESP01S_CONN_ONLINE : ESP01S_CONN_OFFLINE;
        sys_log_add(ok ? SYS_LOG_OK : SYS_LOG_WARN,
                    "ESP↓ CONNECTED ok=%s（%s → %s）",
                    ok ? "true" : "false",
                    (prev == ESP01S_CONN_ONLINE) ? "在线" :
                    (prev == ESP01S_CONN_OFFLINE) ? "离线" : "连接中",
                    (s_conn_state == ESP01S_CONN_ONLINE) ? "在线" :
                    (s_conn_state == ESP01S_CONN_OFFLINE) ? "离线" : "连接中");
        return;
    }
    if (0 == strcmp(cmd, "PING"))
    {
        /* 协议：收到 PING 可回一条 HEARTBEAT 作为应答（可选，本实现回） */
        esp01s_proto_send_heartbeat();
        return;
    }
    if (0 == strcmp(cmd, "DISPENSE_ACTION"))
    {
        proto_dispense_parse(p_line);
        proto_dispense_begin();
        return;
    }
    if (0 == strcmp(cmd, "STORAGE_PLACE"))
    {
        proto_place_parse(p_line);
        proto_place_begin();
        return;
    }
    sys_log_add(SYS_LOG_WARN, "ESP↓ 未知指令: %s", cmd);
}

/* 查询当前云端存药任务快照（LVGL 线程周期调用）。 */
void esp01s_proto_get_place_info(esp01s_place_info_t * p_info)
{
    if (NULL == p_info)
    {
        return;
    }
    const esp01s_place_ctx_t * ctx = &s_place;
    p_info->active = ctx->active;
    (void) snprintf(p_info->task_id, sizeof(p_info->task_id), "%s", ctx->task_id);
    (void) snprintf(p_info->coord, sizeof(p_info->coord), "%s", ctx->coord);
    (void) snprintf(p_info->drug_id, sizeof(p_info->drug_id), "%s", ctx->drug_id);
    (void) snprintf(p_info->drug_name, sizeof(p_info->drug_name), "%s", ctx->drug_name);
    p_info->x_mm = ctx->x_mm;
    p_info->y_mm = ctx->y_mm;
    p_info->last_result = ctx->last_result;
    p_info->last_tick = ctx->last_tick;
}

/* 查询当前云端连接状态（LVGL 线程周期调用）。 */
esp01s_conn_state_t esp01s_proto_get_conn_state(void)
{
    return s_conn_state;
}

/* ============================================================================
 * 初始化与心跳调度
 * ==========================================================================*/
void esp01s_proto_init(void)
{
    memset(&s_dispense, 0, sizeof(s_dispense));
    memset(&s_place, 0, sizeof(s_place));
    s_hb_last = (uint32_t) xTaskGetTickCount();
    s_hb_sent_once = false;
    s_conn_state = ESP01S_CONN_CONNECTING;  /* 初始：正在建连，尚未收到 CONNECTED */
    s_rx_last = s_hb_last;
    s_rx_seen = false;
}

void esp01s_proto_service(void)
{
    /* 心跳：上电约 2s 后首条（模块联网/建链），此后每 30s 一条。
     * 若 TCP 尚未建立，首条会被 ESP 丢弃，后续 30s 周期自然补上（平台注册）。 */
    uint32_t const now = (uint32_t) xTaskGetTickCount();
    uint32_t const interval = s_hb_sent_once ? ESP01S_PROTO_HB_INTERVAL_MS
                                             : ESP01S_PROTO_HB_INITIAL_MS;
    if ((uint32_t) (now - s_hb_last) >= pdMS_TO_TICKS(interval))
    {
        s_hb_last = now;
        s_hb_sent_once = true;
        esp01s_proto_send_heartbeat();
    }

    /* 断线判定（手册 §4.2）：发 HEARTBEAT 后约 40s 未收到任何云端数据
     * → 回到 CONNECTING（重连由 ESP-01S 侧负责）。已 OFFLINE 不再降级。 */
    if ((s_conn_state != ESP01S_CONN_OFFLINE) &&
        (s_rx_seen) &&
        ((uint32_t) (now - s_rx_last) >= pdMS_TO_TICKS(ESP01S_PROTO_RX_TIMEOUT_MS)))
    {
        s_conn_state = ESP01S_CONN_CONNECTING;
        sys_log_add(SYS_LOG_WARN, "ESP: 云端数据超时(>%ums)，连接状态 → 连接中",
                    (unsigned) ESP01S_PROTO_RX_TIMEOUT_MS);
    }

    proto_dispense_service();
    proto_place_service();
}
