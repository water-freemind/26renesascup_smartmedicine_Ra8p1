# ESP-01S ↔ 云端平台 硬件对接协议（权威版）

> ⚠️ **已被取代**：云端侧发布了《硬件对接手册（MCU + ESP-01S + LVGL）》（`docs/硬件对接手册_MCU_LVGL.md`），
> 作为**唯一权威指南**，替代本文及《MCU_LVGL_对接指南.md》《MCU_串口对接ESP01S_指导文档.md》《取药二维码协议.md》。
> 本文保留作历史参考，新开发一律以 `docs/硬件对接手册_MCU_LVGL.md` 为准（协议行格式/字段/时序无冲突，
> 新增下行 `CONNECTED` 连接确认与三态连接状态机）。

> 状态：**权威版（v2.0，2026-08-24）**。本文是硬件（ESP-01S / 药柜主控 MCU）与云端智能药柜平台真正对接的完整数据契约。
> 它覆盖并取代此前的 `ESP01S_通信协议.md` 版本（v1.0/v1.1）；接线细节见 `RA8P1_硬件总览.md` §7（对应原 `MCU_串口对接ESP01S_指导文档.md` 内容）。
> 固件侧实现状态见文末附录。

> ✅ 当前状态：ESP-01S 已连上平台 TCP 网关 `:3002`（网络层已通）。本文告诉你**接下来该上传什么、格式是什么**，让设备在平台注册上线并驱动业务（储药/取药/出药/告警）。

---

## 一、接入方式与连接

| 通道 | 地址 | 说明 |
| --- | --- | --- |
| **WebSocket** | `ws://<平台IP>:3001/ws/hardware?deviceId=PMC-001` | 自定义固件 |
| **TCP-AT 网关**（本场景） | `TCP,<平台IP>,3002`，透传后逐行发 JSON | AT 固件免刷机 |

两类通道**上行报文完全一致**：UTF-8 JSON，**每行一条，`\n`（0x0A）结尾**。

**AT 固件接入（已连上时跳过联网，直接进透传）**
```
AT+CIPMODE=1
AT+CIPSTART="TCP","192.168.137.1",3002     # 目标 = ESP-01S 同网段的平台地址
AT+CIPSEND                                  # 进入透传 → 之后逐行发 JSON
{"deviceId":"CAB-001","type":"HEARTBEAT","rssi":-60}
```

> 连接建立后**必须先发一条 HEARTBEAT**，平台才会注册设备并显示"在线"；之后建议每 30s 一条心跳保活。

---

## 二、上行数据（MCU/ESP-01S → 平台）——你需要上传的内容

所有上行都含公共字段：`deviceId`（你的设备唯一标识，如 `CAB-001`）+ `type`（事件类型）。

### 2.1 HEARTBEAT 心跳 / 注册【必发】
```json
{ "deviceId": "CAB-001", "type": "HEARTBEAT", "rssi": -60 }
```
- 连接后立即发一次（否则平台不注册）；此后**每 30s** 一次。
- `rssi`：信号强度（dBm），可选填。

### 2.2 PICKUP_SCANNED 取药扫码核销【取药用】
MCU 扫到网页生成的取药二维码后，把二维码里的药品 `drugId` 回传：
```json
{ "deviceId": "CAB-001", "type": "PICKUP_SCANNED", "drugId": "DRG-1" }
```
- 平台在**当前待取/进行中取药任务**里匹配该 `drugId` → 标记"已取"→ 进度推进；全部取齐任务完成。
- 一个二维码含多种药（`items[]`），**每个取到的药分别上报一次** `drugId`。
- 未匹配：平台记"未匹配"日志，不影响整体。

### 2.3 ACTION_FINISHED 出药完成【机械出药时】
平台下发过 `DISPENSE_ACTION` 后，执行机构取出药回报完成：
```json
{ "deviceId": "CAB-001", "type": "ACTION_FINISHED", "taskId": "TASK-...", "slotCoord": "A-03", "dispensedQty": 1, "status": "SUCCESS" }
```

### 2.4 SENSOR_TRIGGERED 传感器触发【可选，计数】
```json
{ "deviceId": "CAB-001", "type": "SENSOR_TRIGGERED", "slotCoord": "A-03" }
```

### 2.5 ALARM 告警【可选】
```json
{ "deviceId": "CAB-001", "type": "ALARM", "level": "WARN", "message": "出药口红外计数超时" }
```

---

## 三、下行数据（平台 → MCU/ESP-01S）

平台下发的每行会出现在 MCU 串口 RX（透传模式下原样送达）。

### 3.1 PING 测试指令
```json
{ "cmd": "PING", "ts": 1789000000000 }
```
- 收到后可回一条 HEARTBEAT 作为应答（可选）。

### 3.2 DISPENSE_ACTION 机械出药任务【有机械出药时】
```json
{
  "cmd": "DISPENSE_ACTION",
  "taskId": "TASK-20260820-001",
  "slots": [ { "coord": "A-03", "trayIndex": 1, "qty": 1 } ],
  "ts": 1789000000000
}
```
- MCU 逐货位执行到底后，回报 `ACTION_FINISHED`（见 2.3）。

---

## 四、字段规范（与平台 protocol.js 严格一致）

上行必含：`deviceId` + `type`。可选/事件相关字段：

| 字段 | 类型 | 用于事件 | 说明 |
| --- | --- | --- | --- |
| `deviceId` | string | 全部 | 设备唯一标识，**必须存在**，否则报文丢弃 |
| `type` | string | 全部 | `HEARTBEAT`/`PICKUP_SCANNED`/`ACTION_FINISHED`/`SENSOR_TRIGGERED`/`ALARM` |
| `rssi` | number | HEARTBEAT | 信号强度 dBm |
| `drugId` | string | PICKUP_SCANNED | 二维码中的药品 id |
| `taskId` | string | ACTION_FINISHED | 平台下发的任务号 |
| `slotCoord` | string | ACTION_FINISHED/SENSOR | 货位坐标 |
| `dispensedQty` | number | ACTION_FINISHED | 实际出药数量 |
| `status` | string | ACTION_FINISHED | `SUCCESS`/`PARTIAL`/`FAILED` |
| `level` | string | ALARM | `WARN`/`ERROR` |
| `message` | string | ALARM | 告警描述 |
| `ts` | number | 可选 | 时间戳 ms |

> ❗ 字段名必须驼峰正确（`deviceId` 不要 `device_id`），必须是合法 JSON、`\n` 结尾；解析失败的行会被忽略。

---

## 五、端到端时序（开始真正驱动业务）

```
MCU/ESP-01S(透传)                云端平台
   │ 1.HEARTBEAT ─────────────────▶ 注册设备 CAB-001 → 在线
   │ 2.每30s HEARTBEAT ──────────▶ 保活
   │ 3.[取药] PICKUP_SCANNED{drugId:DRG-1} ─▶ 匹配取药任务→核销
   │ 4.[出药] 收到 DISPENSE_ACTION ◀──────── 平台下发任务
   │ 5.ACTION_FINISHED ──────────▶ 核销出药→复核审计
   │ 6.ALARM ────────────────────▶ 告警记录/广播
```

---

## 六、MCU 发送示例（C 伪代码）

```c
#define DID "CAB-001"
void send_line(const char* j){ uart_print(j); uart_print("\n"); }  // 每行 \n 结尾

// 连接成功后立即：
char hb[96];
snprintf(hb, sizeof hb, "{\"deviceId\":\"%s\",\"type\":\"HEARTBEAT\",\"rssi\":-60}", DID);
send_line(hb);            // 注册上线

// 扫到二维码后，每个 drugId 上报一次：
char pk[128];
snprintf(pk, sizeof pk, "{\"deviceId\":\"%s\",\"type\":\"PICKUP_SCANNED\",\"drugId\":\"%s\"}", DID, drug_id);
send_line(pk);

// 主循环每 30s：
send_line(hb);

// 收到下行行（\n 分帧）：
//   {"cmd":"PING"}            → 可选回一次 HEARTBEAT
//   {"cmd":"DISPENSE_ACTION"} → 有机械出药时执行并回 ACTION_FINISHED
```

---

## 七、排查

| 现象 | 处理 |
| --- | --- |
| 连上了平台却不显示在线 | 检查是否发过 HEARTBEAT；`deviceId`/`type` 拼写与大小写 |
| 上报无反应 | 字节是否为 UTF-8、每行是否 `\n` 结尾、是否为合法 JSON |
| PICKUP_SCANNED 不核销 | `drugId` 与二维码原文一致；网页侧先创建对应取药任务 |
| 收不到下行 | 确认处于透传模式（`CIPSEND`）；MCU 是否按 `\n` 分帧 |

---

> 接线/波特率/透传桥 等物理层细节见 `MCU_串口对接ESP01S_指导文档.md`（本仓库对应 `RA8P1_硬件总览.md` §7 与 `src/middleware/esp01s_uart.*`）。

---

## 附录：固件侧实现状态（RA8P1，2026-08-24）

协议层 `src/middleware/esp01s_proto.*` 已按本文落地：

| 项 | 实现 |
| --- | --- |
| HEARTBEAT | 上电约 2s 后首条，此后每 30s（Network 线程 `esp01s_proto_service`）；AT 配置期间暂停 |
| PICKUP_SCANNED | 取药页识别取药单二维码成功后，每个药品 id 逐条上报（`gui_app.c` 调 `esp01s_proto_send_pickup_scanned`） |
| ACTION_FINISHED | 收到 `DISPENSE_ACTION` 后逐货位执行（复用 `PickupTest_StartCloud` 取放流程），每货位完成后回报 `SUCCESS`/`FAILED` |
| PING | 收到后回一条 HEARTBEAT |
| DISPENSE_ACTION | `cmd`+`taskId`+`slots[]`（coord/trayIndex/qty）解析；`coord` 如 `"A-03"` → 层 A=0/B=1/C=2、列 1 起 → `PickupParams_SlotX/ShelfY` 坐标；每任务最多 8 货位 |
| SENSOR_TRIGGERED / ALARM | 发送 API 已提供；ALARM 在出药失败时自动上报 |
| deviceId | `ESP01S_PROTO_DEVICE_ID`（`esp01s_proto.h`，默认 `"CAB-001"`，多台设备需各自修改） |
| 多线程发送 | `esp01s_uart` 新增 TX 互斥锁，Network 线程（心跳）与 LVGL 线程（扫码上报）并发安全 |
