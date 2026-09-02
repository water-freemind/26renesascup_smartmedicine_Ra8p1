# 硬件指导 · 继电器控制（RELAY_CTRL）

> 场景：**硬件（ESP-01S / 按键 / 主控）想控制某个继电器**，做法是**把"要哪个继电器、开还是关"上报给云端**；云端把它下发给"正在跑网页的浏览器"，浏览器用 **WebHID** 直接控制插在本机的 **USB 免驱继电器（8 路）**。
>
> 本文件告诉硬件**该发送什么请求、怎么发**。继电器代号固定 **1~8**（= USBRelay8 的 8 个通道）。

---

## 一、整体链路

```
 硬件(ESP-01S)         云端(Node)            浏览器(页面)
   │ 上报 RELAY_CTRL ──▶ 校验/记录 ──广播 relay_control ──▶ WebHID 打开/关闭相应继电器通道
   │   {relay:1,state:1}                                  （继电器插在本机 USB，需先授权）
```

- 继电器的**实际通断由浏览器 WebHID 执行**（继电器插在跑网页这台电脑的 USB 上）。
- 云端只负责**接收硬件上报、校验（1~8 / 开关态）、并推送给前端执行**。
- 网页上也能**手动点继电器面板**直接开关（不经云端）。

---

## 二、硬件上报格式（走已有硬件上行通道）

与其它硬件事件一致，走 **TCP:3002（透传）或 WS /ws/hardware**，每行一个 JSON，`\n` 结尾：

```json
{ "deviceId": "CAB-001", "type": "RELAY_CTRL", "relay": 1, "state": 1 }
```

| 字段 | 类型 | 必填 | 说明 |
| --- | --- | --- | --- |
| `deviceId` | string | 是 | 设备唯一标识 |
| `type` | string | 是 | 固定 `RELAY_CTRL` |
| `relay` | number | 是 | **继电器代号：只能是 1、2、3、4、5、6、7、8**（对应 USBRelay8 第 1..8 路） |
| `state` | number/str | 是 | 开关态：`1`/`true`/`"on"`（开）或 `0`/`false`/`"off"`（关） |
| `ts` | number | 否 | 时间戳（可选） |

### 示例
- 打开 1 路：# 开
  ```json
  { "deviceId":"CAB-001","type":"RELAY_CTRL","relay":1,"state":1 }
  ```
- 关闭 1 路：
  ```json
  { "deviceId":"CAB-001","type":"RELAY_CTRL","relay":1,"state":0 }
  ```
- 打开 8 路：
  ```json
  { "deviceId":"CAB-001","type":"RELAY_CTRL","relay":8,"state":"on" }
  ```

> ⚠️ `relay` 必须是 1~8；`state` 必须能解析为开/关。**非法代号（<1 或 >8）会被云端忽略**（日志记一笔），其余正常。

---

## 三、云端行为

收到 `RELAY_CTRL` 后云端：
1. 校验 `relay ∈ [1..8]`、`state` 可解析为开/关；不合法则记 "relay.invalid" 日志并丢弃。
2. 合法则向所有网页端广播 `relay_control { relay, state, deviceId }`。
3. 网页端（浏览器）收到后，若已用 WebHID 授权并接入继电器，则**执行对应通道开/关**。

> 云端**不直接**控制继电器（WebHID 只能在浏览器用）。若浏览器未连接/未授权继电器，指令会被网页记录但不会真的动继电器硬件，前端会显示"未连接"。

---

## 四、网页端要求（接入继电器才能执行）

- 继电器插在**运行网页的这台电脑**的 USB 上。
- 浏览器需 **Chrome/Edge**（支持 WebHID）且经 **HTTPS 或 localhost** 打开本页。
- **首次**需人工在网页点一下“连接继电器设备”并在系统弹窗授权；之后自动扫描接入，不用再授权。
- 页面“设备运维 → USB 继电器控制（8 路）”面板可手动开关，也可看到硬件上报的自动执行。

---

## 五、与取药/储药/告警等事件的关系

`RELAY_CTRL` 是与 HEARTBEAT / PICKUP_SCANNED / PLACE_FINISHED / ALARM 并列的**独立事件**，按 `type` 区分、互不影响。硬件一条线路上可发任意事件。

---

## 六、硬件（MCU/ESP-01S）伪代码

```c
// 开关某路继电器：relay∈[1,8], on=true/false
void relay_set(int relay, int on){
    char b[128];
    snprintf(b, sizeof b,
        "{\"deviceId\":\"%s\",\"type\":\"RELAY_CTRL\",\"relay\":%d,\"state\":%d}",
        DEVICE_ID, relay, on ? 1 : 0);
    send_line(b);   // 每行 \n 结尾，走透传/TCP:3002 或 /ws/hardware
}

// 用法
relay_set(1, 1);   // 打开继电器 #1
relay_set(2, 0);   // 关闭继电器 #2
```

---

## 七、排查

| 现象 | 处理 |
| --- | --- |
| 上报后继电器不动作 | 看网页继电器面板是否"已连接"；未连接则需先在页面授权并接入 USB 继电器（WebHID 需浏览器执行） |
| 只有某几个通道能动 | relay 是否在 1..8；继电器是否为 8 路（USBRelay8） |
| 云端日志 relay.invalid | 你的 `relay` 或 `state` 不符合格式；对照上面字段 |
| 网页面板不可用 | 浏览器需 Chromium、HTTPS/localhost；继电器插本机且已授权 |

---

## 八、固件实现对照（RA8P1 设备侧，2026-08-21 提交）

| 项 | 实现 |
| --- | --- |
| 上报接口 | `esp01s_proto_send_relay_ctrl(relay, on)`（`esp01s_proto.c/.h`），relay 1~8 校验，`state` 1/0 |
| 取药/存药开始 | `pickup_test` PH_ENABLE → 关闭 2 号继电器（`RELAY_CTRL relay:2 state:0`） |
| Z 轴伸出 | PH_Z_DOWN / PH_Z_DOWN2 → 打开 1 号继电器（`RELAY_CTRL relay:1 state:1`） |
| 取药/存药完成 | DONE 转换 → 打开 2 号（`state:1`）+ 关闭 1 号（`state:0`） |
| 失败/异常终止 | `fail()` 同样恢复"开 2 号、关 1 号"（防吸盘常吸） |
| 幂等 | 期望状态标志（s_relay1_on/s_relay2_on），仅状态变化时上报，避免重复广播 |
