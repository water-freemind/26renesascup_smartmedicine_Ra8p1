#ifndef ESP01S_PROTO_H
#define ESP01S_PROTO_H

#include <stdbool.h>
#include <stdint.h>

/**********************************************************************************************************************
 * ESP-01S ↔ 云端平台 数据协议层（权威版）
 *
 * 依据：docs/ESP01S_通信协议.md（权威版）——TCP-AT 网关透传，每行一条 UTF-8 JSON，
 * 以 '\n'（0x0A）结尾。上行公共字段 deviceId + type；下行指令字段 cmd。
 *
 * 上行（MCU/ESP-01S → 平台）：
 *   HEARTBEAT        连接建立后立即发一条（否则平台不注册），此后每 30s 保活
 *   PICKUP_SCANNED   取药二维码识别后，每个药品 drugId 分别上报一次（云端核销）
 *   ACTION_FINISHED  收到 DISPENSE_ACTION 并逐货位执行完毕后回报
 *   PLACE_FINISHED   收到 STORAGE_PLACE 并放置完成后回报
 *   RELAY_CTRL       继电器开关请求（relay 1~8 + state 1/0，云端广播给
 *                    网页 WebHID 控制 USB 继电器；《硬件指导_继电器控制RELAY_CTRL》）
 *   SENSOR_TRIGGERED 传感器触发（计数，可选）
 *   ALARM            告警（可选）
 *
 * 下行（平台 → MCU）：
 *   CONNECTED        连接确认：收到 {"cmd":"CONNECTED","ok":true} → 连接状态置 ONLINE
 *   PING             收到后可回一条 HEARTBEAT 作为应答（本实现回）
 *   DISPENSE_ACTION  机械出药任务：MCU 逐货位执行到底后回报 ACTION_FINISHED
 *   STORAGE_PLACE    储药搬运任务：MCU 到取药口取药 → 放到 layer/x 目标位
 *                    后回报 PLACE_FINISHED（《硬件指导_储药搬运STORAGE_PLACE》）
 *
 * 连接状态机（三态，供 LVGL 显示"已连接/连接中/离线"）：
 *   CONNECTING  初始/正在建连（尚未收到 CONNECTED）
 *   ONLINE      收到 {"cmd":"CONNECTED","ok":true}
 *   OFFLINE     收到 CONNECTED 但 ok!=true，或长时间无云端下行
 *   断线判定：发 HEARTBEAT 后约 40s 未收到任何云端数据 → 回 CONNECTING。
 *
 * 线程模型：
 *   - 发送 API 可在任意线程调用（esp01s_uart 内部 TX 互斥锁串行化）；
 *   - 下行解析（esp01s_proto_process_line）与心跳/出药推进
 *     （esp01s_proto_service）在 Network 线程周期调用；
 *   - AT 透传主机配置（esp01s_cfg）期间暂停上行发送与下行解析（串口被 cfg 独占）。
 **********************************************************************************************************************/

/* 设备唯一标识（平台注册用；多台设备需各不相同，如 CAB-001 / CAB-002 ...） */
#define ESP01S_PROTO_DEVICE_ID      "CAB-001"

/* 心跳周期（ms）：上电约 2s 后首条（模块联网/建链时间），此后每 30s 一条 */
#define ESP01S_PROTO_HB_INITIAL_MS  (2000U)
#define ESP01S_PROTO_HB_INTERVAL_MS (30000U)

/* 单条消息上限：协议要求 ≤1KB（含 '\n'），这里留发送缓冲余量 */
#define ESP01S_PROTO_LINE_MAX       (1100U)

/* DISPENSE_ACTION 单任务最多货位数 */
#define ESP01S_PROTO_MAX_SLOTS      (8U)

/* STORAGE_PLACE 单任务字段缓冲（与 esp01s_proto.c 内部一致） */
#define ESP01S_PROTO_TASK_ID_MAX    (48U)
#define ESP01S_PROTO_COORD_MAX      (16U)
#define ESP01S_PROTO_DRUG_MAX       (32U)
#define ESP01S_PROTO_NAME_MAX       (48U)

/* 断线判定：发 HEARTBEAT 后超过该时长未收到任何云端数据 → 状态回 CONNECTING
 * （手册 §4.2：约 35~45s，取中间值 40s）。 */
#define ESP01S_PROTO_RX_TIMEOUT_MS  (40000U)

/* 云端连接状态（三态，与 LVGL 显示一一对应） */
typedef enum e_esp01s_conn_state
{
    ESP01S_CONN_CONNECTING = 0,    /* 正在建连，尚未收到 CONNECTED */
    ESP01S_CONN_ONLINE,            /* 已连上：收到 {"cmd":"CONNECTED","ok":true} */
    ESP01S_CONN_OFFLINE,           /* 未连接/长时间无云端数据 */
} esp01s_conn_state_t;

/* 云端存药任务（STORAGE_PLACE）状态快照（LVGL 线程轮询显示用） */
typedef struct
{
    bool     active;               /* 存药任务执行中 */
    char     task_id[ESP01S_PROTO_TASK_ID_MAX + 1U];
    char     coord[ESP01S_PROTO_COORD_MAX + 1U];   /* 目标货位（A-02 等） */
    char     drug_id[ESP01S_PROTO_DRUG_MAX + 1U];
    char     drug_name[ESP01S_PROTO_NAME_MAX + 1U];
    float    x_mm;                 /* 目标 X（mm） */
    float    y_mm;                 /* 目标 Y（mm，layer → 层Y） */
    uint8_t  last_result;          /* 最近一次结果：0=无 1=SUCCESS 2=FAILED */
    uint32_t last_tick;            /* 最近结果时刻（FreeRTOS tick） */
} esp01s_place_info_t;

/**********************************************************************************************************************
 * 初始化与服务（Network 线程）
 **********************************************************************************************************************/

/* 初始化（Network 线程进入主循环前调用一次）：重置心跳计时与云端出药上下文。 */
void esp01s_proto_init(void);

/* Network 线程周期调用（20ms）：心跳定时发送 + 云端出药任务推进。 */
void esp01s_proto_service(void);

/* 处理一行下行 JSON（Network 线程按 '\n' 分帧后调用；行尾 \r\n 已去除）。
 * 解析失败/未知指令的行被忽略（仅记录）。 */
void esp01s_proto_process_line(const char * p_line);

/* 查询当前云端连接状态（三态；LVGL 线程周期调用用于显示）。 */
esp01s_conn_state_t esp01s_proto_get_conn_state(void);

/* 查询当前云端存药任务（STORAGE_PLACE）快照（LVGL 线程周期调用用于显示）。
 * p_info 为空则忽略。 */
void esp01s_proto_get_place_info(esp01s_place_info_t * p_info);

/**********************************************************************************************************************
 * 上行发送（任意线程可调；AT 配置期间自动跳过）
 **********************************************************************************************************************/

/* HEARTBEAT：注册/保活。 */
void esp01s_proto_send_heartbeat(void);

/* PICKUP_SCANNED：取药二维码识别后，每个 drugId 上报一次。 */
void esp01s_proto_send_pickup_scanned(const char * p_drug_id);

/* ACTION_FINISHED：出药完成回报。p_status: "SUCCESS"/"PARTIAL"/"FAILED"。 */
void esp01s_proto_send_action_finished(const char * p_task_id,
                                       const char * p_slot_coord,
                                       uint32_t dispensed_qty,
                                       const char * p_status);

/* PLACE_FINISHED：储药搬运完成回报。p_status: "SUCCESS"/"FAILED"。 */
void esp01s_proto_send_place_finished(const char * p_task_id,
                                      const char * p_coord,
                                      const char * p_status);

/* RELAY_CTRL：继电器开关请求（relay 1~8；on=true 开 / false 关）。
 * 云端校验后广播给网页端 WebHID 执行（USB 继电器）。 */
void esp01s_proto_send_relay_ctrl(uint8_t relay, bool on);

/* SENSOR_TRIGGERED：传感器触发（计数）。 */
void esp01s_proto_send_sensor_triggered(const char * p_slot_coord);

/* ALARM：告警。p_level: "WARN"/"ERROR"。 */
void esp01s_proto_send_alarm(const char * p_level, const char * p_message);

#endif /* ESP01S_PROTO_H */
