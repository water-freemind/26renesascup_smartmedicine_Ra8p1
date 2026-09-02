# 电机调试页 + ZDT CAN 协议探索 — 工作记录

> 工程：RA8P1 智能药品工作站（Cortex-M85 / FSP 6.3.0 / FreeRTOS / LVGL 9.3）
> 主题：GUI 电机调试页（四轴点动/设零/归零/解除/急停）+ 应用层软限位 + 驱动读命令探索
> 最后更新：2026-08-20

---

## 1. 现状结论（重要）

- ✅ **电机调试页可用**：四轴（X/Y/Z/抓手 0x0400）独立 `+`/`-` 点动、设零、归零、底部步长（100/1000/10000）、解除堵转、急停、到位状态显示。
- ✅ **应用层软限位已配置**（正式取药流程 `gantry_robot` 生效，调试页不受限）：
  - X 轴：0 ~ **17400** 脉冲（≈406.0 mm）
  - Y 轴：0 ~ **12050** 脉冲（≈281.2 mm）
  - Z 轴：0 ~ **6600** 脉冲（≈151.8 mm）
  - 抓手：0 ~ **-28000** 脉冲（负方向；pulses_per_mm=1.0，应用层参数即脉冲数）
- ❌ **驱动无读命令**：实测 0x30/0x33/0x36/0x3A × 多种格式/校验共 24 个变体**全部无应答**。脉冲值只能**本地累计 + 到位应答确认**（堵转/失败时状态行提示，需点"解除"）。

---

## 2. 驱动协议实测结论（用户抓包为准，官方文档仅参考）

| 命令 | 报文（CAN 扩展帧 ID=轴 ID） | 应答 |
|------|------------------------------|------|
| 使能 | `F3 AB [01/00] 00 6B` | 无 |
| 设零 | `93 6B` | 无 |
| 回零 | `9A 02 [sync] 6B` | `9A 02 6B`（回零完成） |
| 位置移动 | `FD dir spdH spdL acc pos3..pos0 01 sync 6B`（12 字节拆 2 包，第二包 ID+1） | `FD 9F 6B`（到位应答） |
| 急停 | `FE 98 00 6B` | 无 |
| 同步 | `FF 66 6B` / `FF 02 6B`（广播） | 无 |
| 解除堵转 | `0E 52 6B` | 无 |
| 读脉冲 0x33 等 | `33 6B`（及 0x30/0x36/0x3A 变体） | **无应答（固件不支持）** |

- **到位应答确认**：`FD 9F 6B`（位置到位）/ `9A 02 6B`（回零完成），ID 与发送轴一致（0x0100/0x0200/0x0300/0x0400）。
- 上位机（Y42_X_CAN_Tool）有 `command_calXOR` / `command_calCRC8`（通讯校验方式选择）——**读命令校验可能非固定 0x6B**，盲试未命中。
- 应用层点动要点：`ZDT_MovePosition` 的 payload[9]=0x01 是**绝对位置**，连续点动必须发"累计位置+步长"的新目标，否则第二次不动；绝对目标重复发送无害（故 JOG 连发 3 次抵抗偶发丢命令）。

---

## 3. 新增/修改文件

| 文件 | 说明 |
|------|------|
| `src/app/inc/gantry_debug.h` / `src/app/src/gantry_debug.c` | **新增**：四轴调试模块（ENABLE/SET_ZERO/GOZERO/JOG/UNPROTECT/STOP 命令队列），Motor 线程独占执行；到位应答识别（s_arrived）；协议探测环形缓存（s_rx_ring，J-Link 可读）；`gantry_robot` 忙时命令暂缓 |
| `src/Motor_thread_entry.c` | 接入 `GantryDebug_Init/Service`；`canfd0_callback` 转发所有 RX 帧给 `GantryDebug_OnCanRxFromISR` |
| `src/app/inc/gantry_robot.h` / `src/app/src/gantry_robot.c` | 加 **CATCH 轴**（GANTRY_AXIS_CATCH，MASK 位 3，`GANTRY_AXIS_MASK_ALL` 保持 XYZ 不变）；软限位按实机行程换算（X 406.0 / Y 281.2 / Z 151.8 mm / CATCH -28000~0）；`validate_position`/`enqueue_command` 接受 CATCH 掩码 |
| `gui/RA8P1/custom/custom.c` | 电机调试页（手写 LVGL，不进 guider）：四轴行（脉冲值 + `-`/`+`/设零/归零）、步长切换、解除、急停、到位状态行；Device 页"电机调试"入口按钮 |
| `tools/pcan_zdt.py` | PCAN-USB 调试工具（enable/disable/move/stop/listen/status，波特率可调），用于电脑直连电机调试 |
| `.tmp/read_ring.jlink` 等 | J-Link 读内存诊断脚本 |

---

## 4. PCAN-USB 链路调试记录（电脑直连电机）

- PCAN-USB（VID_0C72）设备正常、PCANBasic.dll（System32/SysWOW64）可用、PcanView 占用通道需先关闭。
- 发送使能/移动帧**被总线 ACK**（无 BUSHEAVY/BUSOFF）→ 物理链路通、500k 波特率匹配；但驱动对使能无应答、对 0x33 等读命令无应答。
- **上位机（张大头 Y42_X_CAN_Tool）不认 PCAN-USB**（只认自家 CAN 卡）→ 无法用上位机+PCAN 抓包。需张大头配套 CAN 卡才能抓读命令报文。
- 上位机为打包 Qt 32 位程序（无符号表、metaobject 索引引用、含 calXOR/calCRC8），静态提取命令码不可行。

---

## 5. 调试页功能清单（最终）

- 每轴行：轴名 + 本地累计脉冲值（十进制，实时刷新）+ `-`（负向点动）+ `+`（正向点动）+ `设零`（当前为零点）+ `归零`（回零运动）
- 底部：步长 100/1000/10000 切换、`解除`（堵转保护解除 0E 52）、`急停`（全部停止）
- 状态行：四轴"已到位/未到位"（收到 `FD 9F 6B`/`9A 02 6B` 即已到位）
- 入口：Admin → 系统状态（Device 页）→ 底部"电机调试"按钮

---

## 6. 待办 / 后续

1. [ ] 如需真实脉冲回读：拿到张大头配套 CAN 卡 → 上位机连电机 → 抓"读取系统状态"报文（命令码+校验字节）→ 更新固件。
2. [ ] 抓手正式流程接入：`Gantry_MoveAxisTo(GANTRY_AXIS_CATCH, 负脉冲数)`，软限位已配 0~-28000。
3. [ ] 测范围时：撞限位/堵转 → 点"解除"再继续；到位状态行确认每次点动成功。
4. [ ] 应用层软限位为 mm 换算近似（误差<1 脉冲），若需精确可按脉冲级重新设计。
