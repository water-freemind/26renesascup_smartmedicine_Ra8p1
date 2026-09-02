# 智能药柜 · 硬件对接手册（MCU + ESP-01S + LVGL）

> 本手册是**硬件/主控 MCU + ESP-01S（Wi-Fi 透传桥）+ LVGL 界面**与**云端智能药柜平台**对接的**唯一权威指南**。
> 它整合了此前的接线、协议、二维码解析、数据上传、连接判断与 LVGL 显示等全部内容。
>
> ✅ 当前状态：ESP-01S 已连上平台 TCP 网关 `:3002`，并已收到云端 `CONNECTED` 确认（网络与协议已打通）。

---

## 目录
1. [整体架构与硬件](#一整体架构与硬件)
2. [接线与波特率](#二接线与波特率)
3. [ESP-01S 接入云端（已配置透传）](#三esp-01s-接入云端已配置透传)
4. [云端如何确认连接 & 硬件判断"已连上"](#四云端如何确认连接--硬件判断已连上)
5. [LVGL 界面显示连接状态](#五lvgl-界面显示连接状态)
6. [二维码解析（MCU 拿到扫码文本）](#六二维码解析mcu-拿到扫码文本)
7. [分时段上传什么数据 & 缺数据占位](#七分时段上传什么数据--缺数据占位)
8. [协议上下行全集与字段规范](#八协议上下行全集与字段规范)
9. [端到端时序](#九端到端时序)
10. [MCU 伪代码（可直接移植）](#十mcu-伪代码可直接移植)
11. [排查清单](#十一排查清单)

---

## 一、整体架构与硬件

```
┌──────────┐   UART(115200)  ┌──────────┐  Wi-Fi/TCP  ┌──────────────┐
│   MCU     │  TX──RX ▲─────▶│  ESP-01S  │ ──────────▶│  云端平台      │
│ 主控+扫码+ │ ◀───── ▼ RX──TX│ Wi-Fi透传桥 │  TCP :3002  │ (Node 服务端) │
│  LVGL界面  │               └──────────┘             └──────────────┘
└──────────┘
```
- ESP-01S 是「串口透传桥 + Wi-Fi」：MCU 往串口写的每行 JSON 原样送达云端，云端下发的每行原样送回 MCU 串口。
- MCU 同时承担：**工业相机/扫码模块解析二维码、控制执行机构、驱动 LVGL 屏、收发串口 JSON**。

---

## 二、接线与波特率

| 连接 | 说明 |
| --- | --- |
| MCU TX → ESP-01S RXD | 交叉 |
| MCU RX ← ESP-01S TXD | 交叉 |
| 共地 GND | 必须共地 |
| 电源 | ESP-01S 需 3.3V（注意电流，建议独立供电，勿用 5V） |

> 波特率统一 **115200，8N1**（无校验 1 停止位），与 ESP-01S 透传配置一致。

---

## 三、ESP-01S 接入云端（已配置透传）

> ✅ **你的 ESP-01S 已配置好并处于透传模式**，MCU 固件**无需**做 Wi-Fi 联网或 AT 透传初始化。ESP-01S 开机自动连 Wi-Fi 并建起到云端 TCP `:3002` 的透传；掉线重连由 ESP-01S 侧负责。

- MCU 只需往串口写数据（每行一个 JSON，行尾 `\n`）。
- 连接目标 = ESP-01S 同网段内的平台地址（本场景 `192.168.137.1:3002`）。

> 仅当你未来自己接管 ESP-01S 时才需要：`AT+CIPMODE=1` → `AT+CIPSTART="TCP","192.168.137.1",3002` → `AT+CIPSEND`。

---

## 四、云端如何确认连接 & 硬件判断"已连上"

### 4.1 连接确认（云端 → 硬件）
设备成功连上平台后，平台**立即**下发一条「连接确认」：

```json
{ "cmd": "CONNECTED", "ok": true, "deviceId": "CAB-001", "serverTime": 1789000000000 }
```

### 4.2 硬件判断"已连上云端"（三态）
| 状态 | 判定/触发 |
| --- | --- |
| `OFFLINE` 未连接 | 未建连 / 收到 CONNECTED 之前 / 长时间无云端下行 |
| `CONNECTING` 连接中 | 正在建连，尚未收到 CONNECTED |
| `ONLINE` 已连上 | **收到 `{"cmd":"CONNECTED","ok":true}`** |

断线判定：发 HEARTBEAT 后约 35~45s 收不到任何云端数据，或 TCP 断开 → 回到 `CONNECTING` 并尝试重连。

---

## 五、LVGL 界面显示连接状态

放一个状态指示（圆点 + 文字），按三态切换：

```c
void lv_conn_set(conn_state st) {
    switch (st) {
        case OFFLINE:
            lv_label_set_text(lbl, "✗ 云端离线");
            lv_obj_set_style_bg_color(dot, lv_color_make(220,60,60), 0);   break;
        case CONNECTING:
            lv_label_set_text(lbl, "⏳ 连接中…");
            lv_obj_set_style_bg_color(dot, lv_color_make(240,160,40), 0);  break;
        case ONLINE:
            lv_label_set_text(lbl, "✓ 云端已连接");
            lv_obj_set_style_bg_color(dot, lv_color_make(60,200,90), 0);   break;
    }
}
```
- 串口收到 CONNECTED → `lv_conn_set(ONLINE)`；TCP/串口断开 → `CONNECTING`/`OFFLINE`。
- 可选：把 `serverTime` 换算成云端时间同步显示到屏上。

---

## 六、二维码解析（MCU 拿到扫码文本）

云端生成的取药二维码是**一个任务一个码**，内容是紧凑 JSON：

```json
{ "t": "PICK", "v": 1, "taskId": "PICK-PU-001", "items": [
  { "drugId": "DRG-1", "layer": 1, "x": 66,  "w": 72 },
  { "drugId": "DRG-2", "layer": 2, "x": 140, "w": 130 }
] }
```

### 解析要点
| 键 | 类型 | 含义 |
| --- | --- | --- |
| `t` | string | 固定 `"PICK"`（识别是取药码） |
| `v` | number | 版本 `1` |
| `taskId` | string | 取药任务号（整单标识） |
| `items[]` | array | 全部待取药品 |
| `items[].drugId` | string | **药品 id**（回传核销核心字段） |
| `items[].layer` | number | 所在层（1=A，2=B…） |
| `items[].x` | number | x 坐标 mm（药位中心） |
| `items[].w` | number | 药品宽度 mm |

> 只提取 `drugId` 即可核销；`layer/x/w` 供机械定位。资源紧张可用逐字段 strstr 替代完整 JSON 库。

（扫码取药/核销/超时回滚流程见第七、九节。）

---

## 七、分时段上传什么数据 & 缺数据占位

所有上行：**一行 JSON + `\n`**，公共字段 `deviceId` + `type`。

| 阶段/时机 | `type` | 报文要点 |
| --- | --- | --- |
| 连接建立后**立即**、之后**每 30s** | `HEARTBEAT` | 必须，否则平台不注册 |
| **扫到取药码、取到某药后** | `PICKUP_SCANNED` | 每个取到的药报一次 `drugId` |
| 机械出药完成（收过 DISPENSE_ACTION） | `ACTION_FINISHED` | `taskId`+`slotCoord`+`dispensedQty`+`status` |
| 传感器触发（可选） | `SENSOR_TRIGGERED` | `slotCoord` |
| 异常/故障 | `ALARM` | `level`+`message` |
| 收到 `CONNECTED` | 不额外上报 | 仅更新连接状态 |
| 收到 `PING` | （可选）回一条 HEARTBEAT | 应答 |

### 缺数据占位
- 无 `rssi` → **省略该字段**（不填即当作无信号）。
- 无某可选字段（`taskId`/`slotCoord`/`level`/`message`）→ **省略**。
- `status` 不确定 → 如实上报 `"FAILED"`。
- 没有有意义的业务数据 → **不主动上报**，不要塞 `null`/`0` 占位（避免误判）。

### 取药二维码的预扣 / 核销 / 超时回滚
- 网页生成二维码/取药任务时**预扣**货位数量（暂扣）。
- 你上报 `PICKUP_SCANNED {drugId}` → 平台匹配核销 → 预扣正式生效。
- 任务创建后 **30 分钟**未完成 → 平台自动回滚预扣、任务作废。

---

## 八、协议上下行全集与字段规范

### 8.1 上行（MCU → 平台）
```jsonc
{ "deviceId":"CAB-001", "type":"HEARTBEAT", "rssi":-60 }                                    // 心跳/注册
{ "deviceId":"CAB-001", "type":"PICKUP_SCANNED", "drugId":"DRG-1" }                          // 取药核销
{ "deviceId":"CAB-001", "type":"ACTION_FINISHED", "taskId":"TASK-1", "slotCoord":"A-03", "dispensedQty":1, "status":"SUCCESS" } // 出药完成
{ "deviceId":"CAB-001", "type":"PLACE_FINISHED", "taskId":"PLACE-001", "coord":"A-02", "status":"SUCCESS" } // 储药搬运完成
{ "deviceId":"CAB-001", "type":"RELAY_CTRL", "relay":1, "state":1 }                          // 继电器开关请求（relay 1~8，state 1开/0关）
{ "deviceId":"CAB-001", "type":"SENSOR_TRIGGERED", "slotCoord":"A-03" }                      // 传感器
{ "deviceId":"CAB-001", "type":"ALARM", "level":"WARN", "message":"卡料" }                  // 告警
```

### 8.2 下行（平台 → MCU）
```jsonc
{ "cmd":"CONNECTED", "ok":true, "deviceId":"CAB-001", "serverTime":1789000000000 }   // 连接确认
{ "cmd":"PING", "ts":1789000000000 }                                                  // 测试
{ "cmd":"DISPENSE_ACTION", "taskId":"TASK-1", "slots":[{ "coord":"A-03", "trayIndex":1, "qty":1 }], "ts":0 } // 机械出药任务
{ "cmd":"STORAGE_PLACE", "taskId":"PLACE-001", "item":{ "drugId":"DRG-1", "drugName":"阿莫西林胶囊", "from":"取药口", "coord":"A-02", "layer":1, "x":122, "w":72, "startX":102, "endX":174 }, "ts":0 } // 储药搬运任务
```

### 8.3 字段规范
上行必含 `deviceId` + `type`。可选字段见上文各事件。
> 字段名**必须驼峰**（`deviceId` 不要 `device_id`），合法 JSON、`\n` 结尾；解析失败的行被忽略。编码 UTF-8。

### 8.4 储药搬运闭环（STORAGE_PLACE / PLACE_FINISHED）
- 场景：药师网页扫码储药 → 云端分配位置生成搬运任务（QUEUED）→ 页面手动下发 `STORAGE_PLACE` → 设备到取药口取药 → 放到 `layer`/`x` 目标位 → 回报 `PLACE_FINISHED` → 云端置 DONE 并核销。
- 设备处理：`from` 固定为"取药口"（设备取暂存区坐标抓药）；`layer` 1=A → 层 Y 坐标；`x` = 药位中心 X（mm）；`w`/`startX`/`endX` 仅参考（夹爪按固定开度，不消费）。
- 回报：`taskId` 必须与下发完全一致（含 `PLACE-` 前缀）；`status` = `SUCCESS`/`FAILED`；失败可附 `message`。
- 与取药/出药独立：通过 `cmd`/`type` 区分，设备按 `cmd` 分派；机械臂忙时新任务回报 `FAILED`。
- 子流程补充文档：`docs/硬件指导_储药搬运STORAGE_PLACE.md`。

### 8.5 继电器控制（RELAY_CTRL）
- 硬件想控制某路继电器时，上报 `RELAY_CTRL{relay,state}`（relay 固定 1~8，state 1/true/"on"=开、0/false/"off"=关）；云端校验后广播给网页端，由浏览器 WebHID 执行 USB 继电器实际通断（继电器插在运行网页的电脑上）。
- 非法 relay（<1 或 >8）被云端忽略。
- 设备侧取放流程自动联动（`pickup_test`）：取药/存药开始关 2 号 → Z 轴伸出开 1 号 → 完成/失败开 2 号并关 1 号。
- 子流程文档：`docs/硬件指导_继电器控制RELAY_CTRL.md`。

---

## 九、端到端时序

```
MCU/LVGL             ESP-01S(透传)          云端平台
 建连 ─────────────────────────▶ :3002
                 ◀─── {CONNECTED,ok:true}  → LVGL 显示「✓ 已连接」
 发HEARTBEAT ──────────────────▶  注册 CAB-001 → 在线
 每30s HEARTBEAT ──────────────▶  保活
 扫到二维码 → 解析 items[] → 取药
 发 PICKUP_SCANNED{drugId} ───▶  核销 → 任务进度↑
 取完 → 任务「已完成」；30分钟未完成 → 自动回滚作废
```

---

## 十、MCU 伪代码（可直接移植）

```c
#define UART_BAUD 115200
#define DEVICE_ID "CAB-001"
#define MAX_LINE  320
char  rx_line[MAX_LINE]; uint16_t rx_len=0;
conn_state conn = CONNECTING; uint32_t last_hb=0;

void send_line(const char* j){ uart_write(j); uart_write("\n"); }   // 每行 \n 结尾

void send_heartbeat(void){
    char b[96]; snprintf(b,sizeof b, "{\"deviceId\":\"%s\",\"type\":\"HEARTBEAT\",\"rssi\":-60}", DEVICE_ID);
    send_line(b);
}
void send_pickup_scanned(const char* drug){
    char b[128]; snprintf(b,sizeof b, "{\"deviceId\":\"%s\",\"type\":\"PICKUP_SCANNED\",\"drugId\":\"%s\"}", DEVICE_ID, drug);
    send_line(b);
}

void on_qr_scanned(const char* qr_text){
    cJSON* root=cJSON_Parse(qr_text); if(!root)return;
    if(strcmp(cJSON_GetObjectItem(root,"t")->valuestring,"PICK")!=0){ cJSON_Delete(root); return; }
    cJSON* items=cJSON_GetObjectItem(root,"items");
    for(int i=0;i<cJSON_GetArraySize(items);i++){
        cJSON* it=cJSON_GetArrayItem(items,i);
        int layer=cJSON_GetObjectItem(it,"layer")->valueint;
        double x  =cJSON_GetObjectItem(it,"x")->valuedouble;
        move_to(layer, x);                      // 定位取药
        send_pickup_scanned(cJSON_GetObjectItem(it,"drugId")->valuestring);
        delay(200);
    }
    cJSON_Delete(root);
}

void handle_line(const char* line){
    cJSON* r=cJSON_Parse(line); if(!r)return;
    const char* cmd=cJSON_GetObjectItem(r,"cmd")?cJSON_GetObjectItem(r,"cmd")->valuestring:NULL;
    if(!cmd){ cJSON_Delete(r); return; }
    if(strcmp(cmd,"CONNECTED")==0 && cJSON_GetObjectItem(r,"ok")->valueint){ conn=ONLINE; lv_conn_set(ONLINE); }
    else if(strcmp(cmd,"PING")==0){ send_heartbeat(); }
    else if(strcmp(cmd,"DISPENSE_ACTION")==0){ /* 执行机械出药，完成后回 ACTION_FINISHED */ }
    cJSON_Delete(r);
}

void loop(void){
    if(uart_receive(&c)){
        if(c=='\n'){ rx_line[rx_len]=0; handle_line(rx_line); rx_len=0; }
        else if(rx_len<MAX_LINE-1) rx_line[rx_len++]=c;
    }
    if(millis()-last_hb>30000){ send_heartbeat(); last_hb=millis(); }
    // 若超时无响应 → conn=CONNECTING（重连由 ESP-01S 侧负责）
}
```

> 说明：`uart_*`/`cJSON` 为示意，可按你平台替换；重点是**每行 `\n` 结尾 + 收行按 `\n` 分帧 + 收到 CONNECTED 置 ONLINE**。

---

## 十一、排查清单

| 现象 | 处理 |
| --- | --- |
| LVGL 一直"连接中" | 确认 ESP 是否真连 `:3002`；是否收到 `CONNECTED` |
| 连上但设备不显示在线 | 是否发过首条 HEARTBEAT；`deviceId`/`type` 拼写与大小写 |
| 上报无反应 | 是否合法 JSON、`\n` 结尾、UTF-8、字段驼峰 |
| `PICKUP_SCANNED` 不核销 | `drugId` 与二维码原文一致；网页侧先创建对应取药任务 |
| 收到 CONNECTED 后又掉 | 超过 35~45s 无云端数据/心跳失败 → 回到连接中并重连 |
| 二维码解析失败 | 核对 JSON 键 `t/v/taskId/items[].drugId`；确认扫码文本完整 |
| 收不到平台下行 | 透传是否正常（ESP-01S 侧）；MCU 是否按 `\n` 分帧 |
| 接线/波特率异常 | TX RX 交叉、共地、3.3V；波特率 115200 |

---

*本手册替代《MCU_LVGL_对接指南.md》《ESP01S_通信协议.md》《MCU_串口对接ESP01S_指导文档.md》《取药二维码协议.md》四份文档。*
