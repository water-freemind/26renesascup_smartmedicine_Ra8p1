# RA8P1 智能医疗设备工程总览

> 本文是软件工程的权威总览。顶部记录当前 Git 基线和阶段状态，后续章节保留按日期排列的历史过程，便于追溯问题。硬件原理图、接口、电源和实物接线请查阅 `docs/RA8P1_硬件总览.md`；云端通信以 `docs/硬件对接手册_MCU_LVGL.md` 为准。

## 当前归档基线（2026-09-02，比赛版本）

| 项目 | 当前结论 |
|---|---|
| Git 分支 | `main` |
| 远程同步 | `main` 与 `origin/main` 均指向 `70fa36f` |
| 最新提交 | `70fa36f`：比赛版本 |
| 提交总数 | 70（包含完整研发追溯） |
| 工作区 | 归档时干净，无未提交改动 |
| GUI 入口 | `gui/RA8P1/RA8P1.guiguider`，十个基础页面 |
| 软件主线 | 摄像头/二维码、MIPI+LVGL、CST816S 触摸、ZDT XYZ 龙门、ESP-01S、存取药与继电器 |

### 关键 Git 版本线

| 提交 | 日期 | 里程碑 |
|---|---|---|
| `e539fd2` | 2026-08-18 | RA8P1 工程骨架、FreeRTOS、驱动移植 |
| `e290109` | 2026-08-18 | ST7701S MIPI 屏幕、GUI Guider 医疗界面 |
| `b4da65d` | 2026-08-18 | quirc 解码线程栈和二维码链路修复 |
| `19f2277` | 2026-08-19 | VGA 摄像头、取药单解析、二维码识别实机验证 |
| `bbd4367` | 2026-08-20 | EAN-13、机械臂调试、ESP-01S 协议和文档归档 |
| `8caafb0` | 2026-08-21 | 云端取药坐标与逐药核销 |
| `ac0e913` | 2026-08-21 | STORAGE_PLACE 储药搬运闭环 |
| `4cab40f` | 2026-08-21 | RELAY_CTRL 继电器云端联动 |
| `70fa36f` | 2026-09-02 | 比赛版本基线 |

### 阅读和维护规则

1. 判断当前工程状态时，以本节、最新 Git 提交和源码为准；下面以“最新状态”开头的旧章节是历史记录，不代表所有事项仍待完成。
2. 硬件信息只维护在 `RA8P1_硬件总览.md`，软件进度、测试结论和遗留问题维护在本文。
3. 云端字段和时序只以 `硬件对接手册_MCU_LVGL.md` 为权威；`ESP01S_通信协议草案.md` 仅供历史追溯。
4. README 只做入口说明，不写逐次调试日志；详细过程留在本文和 `.planning/` 工作记录中。

## 比赛后整理待办

- [ ] 依据比赛后的实机结果补充“已验证/已实现待验证/未实现”三态结论。
- [ ] 重新核对当前固件 ELF、烧录产物和实机版本号，补充最终验收记录。
- [ ] 复核摄像头、触摸、屏幕、ESP-01S、CAN 和 XYZ 机械臂的联合测试结果。
- [ ] 若后续继续开发，在本节新增日期条目，不覆盖历史记录。

---

# 历史研发记录
# 最新状态：格子宽度按药盒宽度自动规划（盒宽+10mm，下限夹爪最小间距35mm）+ 机械参数核实（2026-08-21 凌晨）

## 2026-08-21 凌晨 格子宽度自动规划（用户：按药盒宽度来，格子最小间隔 ≥3.5cm）

- **需求**：用户确认——① 格子宽度按**药盒宽度**规划（每种药不一样，参数化）；② 格子最小间隔 = **3.5cm(35mm) 或更大**（修正，原 3cm 偏小）；
  ③ 夹爪开口实测：**归零(0脉冲)=125mm、收到底(-28000脉冲)=52mm**，**用户不会放夹爪最小都夹不住的东西**；
  ④ 步进电机 **1.8° + XYZ 16 细分 = 3200 脉冲/圈**。
- **方案（v3）**：新增 `box_width_mm`（药盒宽度，默认 40mm，参数页可调）；`slot_width_mm = 0` = 自动 =
  **max(药盒宽 + 10mm 抓取余量, 35mm)**，且不超 `X量程406/格数`；参数页输入 `0` 恢复自动，非 0 用固定值（35~406/格数）。
- **实现**（`pickup_params.c/.h`）：新增 `box_width_mm` 字段 + `PickupParams_SlotWidthEffective()`（0=自动公式）；
  `PickupParams_SlotX()` 用生效格宽；`clamp_ranges` 钳 box_width 10~200、固定格宽 ≥35；**参数版本 v2→v3**（结构体变更，旧参数回退默认）。
- **GUI**（`custom.c` 参数页）：新增"药盒宽度"行（10~200mm）；格子宽度行显示 `自动(50)`（盒宽40+10）或固定值。
- **机械参数核实**：步进电机 1.8°/200步 + 16 细分 = 3200 脉冲/圈；与实测标定自洽（X/Y 标定 3600/84≈42.86 脉冲/mm → 74.7mm/圈，Z 标定 1000/23≈43.48 脉冲/mm → 73.6mm/圈），标定保持实测值不变。
- **层高不变**（{5, 150, 296.3}），**最高层 = 12700 脉冲 = 296.3mm**（用户确认）。
- **已烧录 flash_out38 + J-Link 板端验证**：s_params version=3、box_width=40.0、slot_width=0（自动）、shelf_y={5.0,150.0,296.3}、boot_state=05（归零正常）。
- **待办**：实机复测第1层第1格/第3层取药，确认夹爪到每格位置与药盒对正。

# 最新状态：取药格子宽度自动规划（X 量程 406/格数）+ 参数 v2（2026-08-20 深夜）

## 2026-08-20 深夜 格子宽度自动规划（用户：层高不变，格子按最大脉冲数自动规划）

- **需求**：用户提出"每个格子还有很多空余位置，能不能根据最大脉冲数决定格子宽度，自动规划还是固定"。
- **方案（自动规划）**：`slot_width_mm = 0` = 自动 = **X 轴最大量程 406mm ÷ 每层格数**（当前 3 格 → **135.3mm/格**），
  格子铺满整个 X 行程（0~406mm），每格左边缘 X = 0 / 135.3 / 270.7，无空余；参数页输入 `0` 恢复自动，输入非 0 用固定值（≤ 406/格数）。
- **实现**（`pickup_params.c/.h`）：新增 `PickupParams_SlotWidthEffective()`；`PickupParams_SlotX()` 改用生效格宽；
  默认值 `slot_width_mm=0`；`clamp_ranges` 允许 0=自动；**参数版本 v1→v2**（旧 OSPI 固定值 140 自动失效回退默认，防残留）。
- **GUI**（`custom.c` 参数页）：格子宽度行显示 `自动(135)` 或固定值；输入 `0` 恢复自动。
- **层高不变**（{5, 150, 296.3}），**最高层 = 12700 脉冲 = 296.3mm**（用户确认）。
- **已烧录 flash_out36 + J-Link 板端验证**：s_params version=2、shelf_y={5.0,150.0,296.3}、slot_width=0.0（自动）、boot_state=05（归零正常）。
- **待办**：实机复测第1层第1格/第3层取药，确认夹爪到每格位置与药盒对正。

# 最新状态：ESP-01S 通信协议切换为云端平台权威版（2026-08-24）

## 2026-08-24 云端数据协议落地（docs/ESP01S_通信协议.md 权威版 v2.0）

- **背景**：用户提供云端平台协议标准（原 `ESP01S_通信协议.md` v1.0/v1.1 被其取代）——TCP-AT 网关 `:3002` 透传，每行一条 UTF-8 JSON（`\n` 结尾），上行公共字段 `deviceId`+`type`，下行指令字段 `cmd`。
- **文档**：`docs/ESP01S_通信协议.md` 已整体替换为权威版（上行 HEARTBEAT/PICKUP_SCANNED/ACTION_FINISHED/SENSOR_TRIGGERED/ALARM；下行 PING/DISPENSE_ACTION；字段规范与平台 protocol.js 一致）；草案 `ESP01S_通信协议草案.md` 指针同步。
- **协议层**（新增 `src/middleware/esp01s_proto.c/.h`）：
  - 心跳：上电约 2s 首条、此后每 30s（Network 线程 `esp01s_proto_service` 调度），AT 配置期间暂停；
  - 下行解析：`PING` → 回 HEARTBEAT；`DISPENSE_ACTION` → 解析 taskId + slots[]（coord/trayIndex/qty，最多 8 货位）；
  - 上行构建：HEARTBEAT / PICKUP_SCANNED / ACTION_FINISHED / SENSOR_TRIGGERED / ALARM（`esp01s_proto_send_*`，任意线程可调）。
- **云端出药执行**：`DISPENSE_ACTION` 按货位 `coord`（"A-03" → 层 A=0/B=1/C=2、列 1 起 → `PickupParams_SlotX/ShelfY` 坐标）逐货位启动取放流程（`pickup_test` 新增 `PickupTest_StartCloud(x,y,qty)` 显式目标模式，同格 qty 盒逐盒取放），每货位完成后回报 `ACTION_FINISHED`（SUCCESS/FAILED），失败自动上报 ALARM。
- **取药核销**：取药页识别取药单二维码成功后，每个药品 id 逐条上报 `PICKUP_SCANNED`（`gui_app.c`）。
- **多线程发送安全**：`esp01s_uart` 新增 TX 互斥锁，Network 线程（心跳）与 LVGL 线程（扫码上报）并发发送串行化。
- **deviceId**：`ESP01S_PROTO_DEVICE_ID`（`esp01s_proto.h`，默认 `"CAB-001"`）。
- **未烧录**（用户暂未接板）：待实机验证心跳上线、DISPENSE_ACTION 出药回报。

# 最新状态：储药搬运闭环（STORAGE_PLACE / PLACE_FINISHED）（2026-08-21）

## 2026-08-21 云端存药闭环实现（《硬件指导_储药搬运STORAGE_PLACE.md》）

- **背景**：云端新增储药搬运闭环——药师网页扫码储药 → 云端分配位置生成搬运任务（PLACE-xxx, QUEUED）→ 页面手动下发 `STORAGE_PLACE` → 设备到取药口取药 → 放到 `coord`/`layer`/`x` 目标位 → 回报 `PLACE_FINISHED` → 云端置 DONE 核销。
- **协议层**（`src/middleware/esp01s_proto.c/.h`）：
  - 下行新增 `STORAGE_PLACE` 分支：解析 `taskId` + `item{drugId,drugName,coord,layer,x}`；`from` 固定"取药口"不消费（取暂存区坐标）；`layer` 1=A → `PickupParams_ShelfY(layer-1)`，越界回报 FAILED；`x` = 药位中心 X（mm）；`w/startX/endX` 仅参考不消费（夹爪固定开度）。
  - 上行新增 `PLACE_FINISHED{deviceId,type,taskId,coord,status}`（SUCCESS/FAILED，失败附 ALARM）；`taskId` 原样回传。
  - 任务推进 `proto_place_service()`（Network 线程，与 DISPENSE 并列）：启动 `PickupTest_StartPlace(x,y)` → 等 DONE → 回报 → 复位状态机（消费 DONE 态，防 GUI 双消费竞态）；机械臂忙（取药/出药运行中）→ 新任务直接 FAILED；存药进行中忽略新 STORAGE_PLACE。
  - 状态查询 `esp01s_proto_get_place_info()`：active/taskId/coord/drugId/drugName/x/y + 最近结果（result/tick，10s 判龄），供 LVGL 显示。
- **状态机**（`src/app/pickup_test.c/.h`）：新增存药模式 `PickupTest_StartPlace(x,y)`——使能 → 回零 → XY→取药口(暂存区) → Z伸出 → 夹爪闭合抓 1 盒 → Z收回 → XY→目标仓(layer/x) → Z伸出放下 → 夹爪张开 → Z收回 → 回零点备机；`PickupTest_IsPlaceMode()` 供 GUI 区分模式。
- **GUI**（`src/app/gui_app.c` Store 存药页）：
  - 任务执行中：药品名/任务号/目标仓显示云端下发内容，机械臂「执行中」、仓位「占用中」、提示「机械臂正在执行存药…」，「确认存药」按钮禁用（文字"存药执行中"）；
  - 完成 10s 内：「已完成/已入库」（绿）或「失败」（红），提示相应文案，按钮恢复；
  - 无任务：保持原本地扫码 → 手动确认存药（库存台账）逻辑；手动确认在云端任务执行中拦截。
  - 取药轮询 `gui_app_poll_pickup_robot` 在存药模式直接让位（防误扣库存/误发 PICKUP_SCANNED）。
- **文档**：权威手册 `docs/硬件对接手册_MCU_LVGL.md` 新增 §8.4（上下行示例 + 闭环说明）；云端子流程文档存档 `docs/硬件指导_储药搬运STORAGE_PLACE.md`（附设备侧实现对照表）。
- **未烧录**（本轮已完成编译）：待实机验证网页下发 → 机械臂存药 → 网页 DONE 闭环。

# 最新状态：取药逻辑测试全流程实机跑通（量程定稿 + 参数系统 + 取药调试页 + 继电器）（2026-08-20）

## 2026-08-20 取药逻辑测试（用户验收：先收 Z 再动 XY、XY 一起动、零点=第一个药柜）

- **需求**：检查应用层取药逻辑——① **必须先收好 Z 轴才能移动 XY 轴**（硬约束）；② **XY 可以一起动**（并行）；③ 写取药测试程序，**当前零点 = 第一个药柜**；④ 测试抓取逻辑（完整取药流程）。
- **量程定稿（用户实机测得，本轮全链路统一）**：X 0~17400 脉冲→406mm；**Y 0~12700 脉冲→296.3mm**（`3600/84` pulse/mm）；Z 0~6600 脉冲→151.8mm（`1000/23`）；抓手 0~**-28000** 脉冲（负方向往里收）。任何移动不得超过最大脉冲数——**超量程会堵转**（Z 堵转两次实测教训，堵转后移动命令全不执行，需 `0E 52` 解除）。
- **实测关键结论（J-Link 探针，决定实现方式）**：
  - 各轴"回零完成"应答**只有 Z 可靠**（arrived=[0,0,1,0]）→ 回零用固定时序（Z 单独 → 2s → X+Y+抓手 → 1.5s，与用户原 `ZDT_Gozero_ALL()` 一致）；
  - "移动到位"应答只有 X 会回 FD 9F 6B 且随机丢帧 → 移动按 `距离脉冲×4000/速度` **估算固定等待**（下限 1.5s 上限 8s），应答仅提前结束；
  - **同步模式（ZDT_SyncTrigger）下电机不动** → 一律 sync=false 分开发命令；
  - **CAN 命令帧偶发丢失** → 移动命令**连发 3 次（间隔 5ms）** + 轴间 10ms（与调试页 JOG、用户原 `Move_XY_To_mm` 的 X/Y 间 vTaskDelay(10) 同一思路）；
  - 线程优先级：Motor=3 最高（排除优先级问题）。
- **取药流程状态机**（`src/app/pickup_test.c`，非阻塞）：使能电机（含**每轮解堵转**）→ 回零 → XY→药柜(测试格/层) → Z伸出151.8 → 夹爪闭合(-14000) → **Z收回0（先收Z再动XY）** → XY→暂存区(0,280) → Z伸出放下 → 夹爪张开 → Z收回0 → 完成；任何一步失败记录 err 停。
- **参数系统**（`src/app/pickup_params.c/.h`）：药柜布局/动作/测试目标/上电归零全部可设，默认=药柜设置（3层 {5,150,296.3}、格宽 140、cabinet_x0=0、grip=14000、store=(0,280)）；OSPI 主+备双份 CRC16 持久化；**量程三层防护**——GUI 参数校验（P_SHELF_Y≤296.3、P_Z_REACH≤151.8、P_GRIP≤28000 等）+ OSPI 加载 `clamp_ranges` 钳制回写 + 取药调试页点击前检查（超量程提示不启动）。
- **取药调试页**（`gui/RA8P1/custom/custom.c`，手写 LVGL）：Device 页入口 (452,416)（与电机调试 20,416、自检 236,416 不重叠）；按参数生成药柜格子矩阵，**镜像布局（第1层最下、第1格最右）**；**点哪个格子就取哪个**；全体归零按钮（Gantry_Home 固定时序）、急停、状态行实时显示阶段/错误。
- **继电器驱动**（`src/driver/relay_drv.c/.h`）：P904=真空泵、P807=电磁阀1、PA07=电磁阀2；GPIO 输出初始低、高电平开启（`ra_gen/pin_data.c` + configuration.xml 已同步）；Motor 线程 `RelayDrv_Init`。
- **上电自动归零**：Motor 线程按用户原 `ZDT_Gozero_ALL()` 时序内联 `auto_home_run()`（Z→2s→X+Y+CATCH→1.5s，固定时序），~7s 内 boot_state=05、无错误。
- **修复清单**（详见 `.planning/取药逻辑测试_工作记录.md`）：旧 getMedicine 失效（依赖 extern 无定义、Z 伸出状态动 XY、200000 超量程）；到位事件未接通（NotifyAxisDone 无调用者→15s 超时 FAULT）；CATCH 轴被掩码丢弃（永远 NOT_HOMED）；移动等应答→err=7（改固定时序）；FAULT 时 s_waiting 先查 idle 卡死不报错（改先查 FAULT）；夹爪方向负向恢复；Z 收回=0；Y 量程 12700 定稿；暂存区 (50,299)→(0,280)；取药区 X 统一 0。
- **已烧录**：flash_out35（Total 17.8s 成功）；**待用户上电复测**：取药调试页点第1层第1格（右下角）与第3层（最上排 Y=296.3）全流程。
- **待办**：flash_out35 实机验收 → git 提交（上电归零+参数系统+取药测试+取药调试页+继电器整体未提交）；正式取药业务（Pickup 页"开始取药"）按同一模式接入；继电器实机动作待测。

# 最新状态：EAN-13 药品商品条码识别（存药页）+ ESP-01S 串口栈（2026-08-19）

## 2026-08-19 EAN-13 药品商品条码识别（存药流程）

- **需求**：Store（存药）页扫码识别药盒上的**商品条形码**（非溯源码），成功后屏幕显示 13 位序列号。
- **条码形态**：中国药品商品条码 = **EAN-13**（13 位，前缀 690-699），GB 12904；小包装偶尔 EAN-8（暂未实现，按需扩展）。
- **解码器**（`src/middleware/barcode_1d.c/.h`，自写，无第三方库）：
  - 输入复用 QR 链路 `s_gray` 快照（640x480），**水平多行扫描**（每 4px 一行，垂直 3 行平均降噪）；
  - 完整 EAN-13 结构解析：起始/中间/结束 guard（101/01010/101）+ 左侧 L/G 码 + 右侧 R 码 + **首位奇偶模式** + **校验位验证**（错码/误报自动丢弃）；
  - 每字符 4 run 自归一化（总宽=7 模块）→ 7 位模式查表，抗模糊/缩放；
  - **镜像支持**：OV7725 COM3=0x50 配置 HFLIP（画面水平镜像），EAN-13 无镜像对称（L/G/R 三套码），解码器先正向扫描，失败则**整行像素反转**（=画面水平翻转恢复正向）重试；
  - 阈值：行均值动态二值化；对比度 <32 跳过（无条码帧零开销）。
- **host 单元验证**（`tools/test_ean13_host.py` + `tools/ean13_host_main.c` + `tools/gen_ean13_test.py`）：正向/镜像/倾斜 5°/倾斜 12°/模糊/±10 噪声 **12/12 通过**。
- **接入解码线程**（`qr_decoder.c`）：每帧先试 EAN-13（毫秒级），命中即发布（`type=EAN13`），未命中再跑 quirc（QR）；`qr_decoder_result_t` 增加 `type` 字段（QR/EAN13），去抖同时匹配类型+内容。对取药单 QR 扫码零干扰。
- **Store 页显示**：`gui_app_refresh_store_page` —— 识别到条码时药品名显示 13 位序列号（drug_db 未收录时原样显示），meta 区显示"序列号 xxxxx"（配合批次/有效期）。
- **测试码**：`.tmp/barcode_test/`（6901234567892 / 6971234567895 正向+镜像+倾斜+模糊+噪声，gitignore）。

# 最新状态：ESP-01S 串口栈落地（SCI8 方案 B 定稿 + Network 线程 + 透传驱动）（2026-08-19）

## 2026-08-19 ESP-01S 串口栈 + 网络线程（透传收发打通）

- **背景**：用户实机接线 **方案 B（SCI8）**：ESP TX→P500（RXD8）、ESP RX←P501（TXD8）、ESP EN→P514、ESP RST→P515。ESP-01S 已预配置 TCP 透传（AT+CIPMODE=1 + AT+CIPSEND 已进透传），固件只需裸字节收发。
- **RASC 配置**（`configuration.xml`，headless `D:\RASC\eclipse\rascc.exe` 生成）：
  - 新增 `g_uart8`（`r_sci_b_uart`，`module.driver.uart_on_sci_b_uart.2051863391`）：SCI8 通道，**115200 8N1**，`esp01s_uart_callback`，RXI/TXI/TEI/ERI 中断优先级 `priority12`（与其它外设一致，满足 FreeRTOS `configMAX_SYSCALL_INTERRUPT_PRIORITY` 调用要求）；
  - 新增 `config.driver.sci_b_uart`：FIFO/DTC/流控全部关闭（中断收发 + 软件环形缓冲）；
  - 新增 `Network_thread`（`rtos.awsfreertos.thread.911503482`）：**优先级 1**（= LVGL，满足协议 §12"不高于 LVGL"），**栈 8192B**，`_symbol=Network_thread`；
  - **SCICLK 使能**：`board.clock.sciclk.source` disabled→`hoco`（HOCO 48MHz，div4→12MHz）——r_sci_b_uart 约束要求 SCICLK>0；
  - 引脚：活动 pincfg `RA8P1_CPKHMI.pincfg` 新增 `p500.sci8.rxd8`、`p501.sci8.txd8`（TX 驱动速度 medium）+ 反向映射；**P514/P515 由输入改为输出高**（`gpio_mode_out.high`，EN 高使能 / RST 常态高）；
  - **移除 SCI9**（二选一，方案 B 定稿）：P208/P209 的 `sci9` 复用从活动 pincfg 删除，恢复 JTAG TDI/TDO 默认；非活动 pincfg 的 sci9 保持自洽不动。
- **生成产物**：`ra/fsp/src/r_sci_b_uart/`（驱动源 + `r_uart_api.h` 实例头，RASC 从 pack 复制）、`ra_cfg/fsp_cfg/r_sci_b_uart_cfg.h`、`ra_gen/Network_thread.c/.h`（`g_uart8` 实例 + 线程创建）、`ra_gen/main.c` 增加 `Network_thread_create()`。
- **固件驱动**（`src/middleware/esp01s_uart.c/.h`）：
  - `esp01s_uart_init()`：`g_uart8.p_api->open`（FSP 实例 API，无 `R_UART_Open` 宏）；
  - 发送：`esp01s_uart_send/send_str`，`write` 后阻塞等 `UART_EVENT_TX_COMPLETE`（二值信号量，100ms 超时）；
  - 接收：RXI 中断回调逐字节入 **1024B 环形缓冲**（协议要求 ≥1KB 容纳整行 JSON），`esp01s_uart_rx_available/read/flush`；
  - 控制：`esp01s_uart_enable(on)`（P514）、`esp01s_uart_reset()`（P515 低脉冲 50ms）；
  - 诊断：`s_dbg_esp_tx_ok/err`、`s_dbg_esp_rx_bytes/dropped`（J-Link 可读）。
- **网络任务**（`src/Network_thread_entry.c`，优先级 1）：使能 ESP → 开 UART → 轮询 RX 按行（≤1KB）转 `sys_log`（透传下行）；发送入口供后续协议消息/心跳接入。
- **注意**：P208/P209 现为 JTAG TDI/TDO（SWD 调试不受影响）；`gui_set_esp01s_online()` 已存在于 `gui/RA8P1/custom/custom.c`（自检页"自检-网络"读取），业务接入时由 Network 线程置位。

# 最新状态：摄像头升 VGA 640x480 + 完整取药单 JSON 识别 + 取药单解析/滚动清单 + 通信协议定稿（2026-08-19）

## 2026-08-19 摄像头 VGA 640x480 升级（完整取药单 JSON 码可识别）

- **背景**：QVGA 320x240 下 25x25 短码可识别，但 45x45 完整取药单 JSON 码模块太小（<4px）扫不了；2x/3x 软件放大不增加真实信息。升 VGA 让模块像素翻倍（源像素 4 倍）。
- **传感器配置**（`camera_app.c` g_ov7725_config，基于 **openmv ov7725.c 权威驱动**）：
  - **关键**：`COM7=0x00`（VGA|YUV）——OV7725 `COM7_RES_QVGA=0x40`（bit6），此前 QVGA 用的 0x40 就是 QVGA 位，**不改成 0x00 传感器根本不切 VGA**（两次试错根因）；
  - 窗口/输出：HSTART=0x22 / HSIZE=0xA4 / VSTART=0x07 / VSIZE=0xF0 / HOUTSIZE=0xA0 / VOUTSIZE=0xF0；
  - **完整 DSP 链**（VGA 原生配置）：DSP_CTRL2=0x2F、EDGE1-3(0x90-0x93)、MTX1-6(0x94-0x99)、GAM1-15(0x7E-0x8C)、SDE=0x06、LC_*(0x46-0x4C) 等——**注意 0x70-0x7D 是 AWB_CTRL 区不是 EDGE/MTX**，地址必须按 openmv ov7725_regs.h 核对；
  - DSPAUTO=0xF3 + SCAL0/1/2=0x00/0x40/0x40（关自动缩放，VGA 直出）；
  - CLKRC=0xC0（NO_PRESCALE）。
- **CEU 640x480**：`ra_gen/Camera_thread.c` x/y_capture_pixels=640/480 + image_area_size=640*480*2；`configuration.xml` x_pixels/y_pixels 同步（RASC 重新生成不丢）。
- **内存布局**：CEU 双缓冲 614KB×2 + s_gray/snapshot/smooth 921KB 全下沉 `.sdram_noinit`（SRAM 91% 满载）；quirc 图像缓冲 307KB 从 **SDRAM 静态池**（`quirc.c`，0x68576000 起 640KB，bump 分配，free 为 no-op——单实例生命周期无需回收）。
- **解码链路**：VGA 源 640x480 **直接解码**（去掉 QVGA 时代的 2x 软件放大）；保留镜像翻转重试（OV7725 HFLIP）+ 1 帧即发布 + 3x3 平滑重试（高密度码 ECC 失败时）。
- **实测**：完整取药单 JSON（45x45，77B）**实机扫描成功**（OK=6 帧）；板端自检 MED-001/RXORDER 均通过。
- **排错记录**：VGA 初版无帧（FRAME_END=0、VD 中断 15 万次）→ 排查 CEU 寄存器（HWDTH=1280 正确）、事件计数（evt_other 大量）→ 二分回滚 QVGA 确认是传感器配置 → openmv 完整表 + COM7=0x00 后成功。**教训：OV7725 分辨率切换必须改 COM7 分辨率位，仅改窗口/输出尺寸无效。**

## 2026-08-19 取药单 JSON 解析 + 单号显示 + 药品清单滚动

- **乱码修复**：取药单 payload 是 77B JSON，窄栏直接显示换行成一团（"乱码"）。新增轻量 JSON 解析（`gui_app.c` `pickup_json_parse`，不引第三方库）：提取 `oid`（单号）+ `i[]`（药品 id/数量，最多 16 项）。
- **单号栏**：显示解析出的取药单号（如 `RX-20260818-001`）；非 JSON 内容（短单号）原样显示。
- **药品清单动态滚动**：原静态 3 行改为**动态行 + 可滚动容器**（medicine_list_card 内 table_header 之下，580x104 容器，`LV_DIR_VER` 滚动）——按取药单 `i[]` 实际药品填充（名称/数量/仓位/状态），**超过 3 种可上下滑动**；合计显示"单内种类 / 总盒数"。
- **格式约定**（对齐 `docs/ESP01S_通信协议.md` §10）：`{"oid":"RX-...","i":[{"id":"MED-001","n":2},...]}`；固件按 `drug_db_lookup(code)` 匹配名称/仓位，未命中显示 code 本身。

## 2026-08-19 通信协议定稿联动

- `docs/ESP01S_通信协议.md` 已更新 **v1.1**：固件侧二维码识别（VGA 640x480 + 取药单 JSON 解析）已落地，`scan` 上报链路（§7.3）与取药单二维码格式（§10）具备实机数据；§12 固件实现清单同步标记已完成项。

# 最新状态：二维码识别链路真正打通（三处根因 + 板端自检验证）（2026-08-19）

## 2026-08-19 二维码识别：死代码修复 + 栈溢出修复 + 板端自检

**背景**：此前"扫码无反应/decode_count 停 0"被误判为 quirc 卡死。本轮逐层排查后确认**流式解码从未真正运行过**，且一旦运行会栈溢出整机 HardFault——两个独立根因叠加：

### 根因 A：feed_frame 尺寸判空 bug（流式解码是死代码）
- `qr_decoder_feed_frame()` 原逻辑 `if (w != g_width) return;`，而 `g_width/g_height` 初始为 0、**全工程没有任何地方初始化成摄像头尺寸**（只有 `qr_decoder_init` 内部赋值，但线程里调用的是 `init(g_width, g_height)` = `init(0,0)` 直接返回 false）。
- 后果：帧永远进不了 `s_gray`，quirc 永不分配，`s_dbg_qr_decode_count` 永远 0——"解码卡死"实为"从未尝试"。
- **修复**：`feed_frame` 首次喂帧时采纳摄像头尺寸（`g_width=w, g_height=h`），解码线程懒初始化随之生效。

### 根因 B：解码线程栈溢出（8KB 栈 vs 12.9KB 栈帧 → HardFault）
- `qr_decoder_decode_gray()` 的局部 `struct quirc_code`（cell_bitmap 3,916B）+ `struct quirc_data`（payload 8,896B）≈ **12.9KB 栈帧**（反汇编 `sub.w sp, sp, #12864` 实锤）。
- 原 `QR_DECODE_TASK_STACK=2048 words=8KB` → SP 下探 → **UNALIGNED HardFault → 整机死**（CFSR=0x00100000、IPSR=3 实测）。此前文档"quirc 卡死"的描述实为**栈溢出 fault**；"移出 Camera 线程"只缓解了表象，且当时 decode 因根因 A 根本没跑。
- **修复**：`QR_DECODE_TASK_STACK 2048→6144 words（24KB）`，余量 ~10KB；任务栈来自 FreeRTOS heap（256KB），不影响紧张的 BSS。
- 定位手段：`qr_decoder.c` 加阶段探针 `s_dbg_qr_selftest_stage`（0空闲/1init/2渲染/3解码/4发布）+ PSP 异常帧解析 faulting PC=0x0205AD2E → addr2line 命中 `decode_gray` 栈帧调整指令。

### 板端自检（无需摄像头/光学，验证 quirc 在 M85 上可用 + 耗时）
- `tools/gen_qr_selftest.py`（依赖 `pip install qrcode`）：嵌入两个已知二维码模块矩阵（`src/middleware/src/qr_selftest.c`，29x29 MED-001 + 45x45 取药单 JSON，~2.9KB flash）→ 渲染进灰度缓冲 → 同一解码线程解码。
- 触发：Device 页"运行自检"按钮（异步，结果进系统日志）；或 J-Link 复位后直写 `s_dbg_qr_selftest_trigger`（**`.noinit` @ 0x22000850**，复位不清零、冷缓存直写可见——同 `s_ttf_burn_mode` 惯例；普通 .bss 变量会被启动清零写缓存、J-Link 直写被掩蔽）。
- **实测（J-Link 探针）**：MED-001 268ms、RXORDER JSON（77B）278ms 均**精确解码、无 fault、可重复**；GUI 日志同步"二维码自检通过(0/1)"。
- **PC 端预验证**（`.tmp/qr_host_test/`，不入库）：host gcc 编译真实 quirc 源 + Python 生成 6 个 320x240 场景（清晰/模糊），5/6 精确解码（唯一失败为 7 字节小码+重模糊，物理限制非库问题）。

### 探针/工具修复
- `tools/probe_qr_selftest.py`：J-Link Commander 复位→`w4` 写触发→go→halt→读结果（**pylink 的 reset+go 会把系统卡在调度器启动**，必须走 Commander）；`.map` 锚定 sys_log 实例地址（`sys_log.c` 与 `pickup_log.c` 有同名 static `s_total/s_next_index/s_entries`，nm 无法区分）。
- `tools/read_syslog.py`：同样修正地址（此前读的是 pickup_log 的空实例）；级别映射修正（INFO=0/OK=1/WARN=2/ERR=3）。

### 当前状态与待办
- 内部 Flash 现 **778,240 B**（+4KB：自检矩阵+渲染代码）；解码链路（喂帧→Y 提取→独立线程 quirc→去抖发布→GUI 显示）已全部打通并经自检验证。
- 实机待验证：**Scan/Pickup 页手机二维码对准摄像头**（焦距/距离/光照由物理决定；解码 270ms/帧，持稳 ~1s 即可命中 2 帧去抖）。若实机识别率低或太慢：可降解码分辨率（如 160x120，约 4 倍提速）、调 `QR_DECODE_INTERVAL_TICKS` / `QR_CONFIRM_FRAMES`。
- 遗留不变：登录"记住"持久化到 OSPI、机械臂/ESP-01S 接入、一维条码库。

# 最新状态：OSPI 图标区 + Boot 封面页 + 登录软键盘 + 内存/存储占用 + Git 历史整理（2026-08-18）

## 2026-08-18 图标资源移入外部 Flash（OSPI 图标区）

- **动机**：用户希望继续添加更多 icon 甚至图片；内部 flash 1MB 已被代码/精简字库占满，图标 `.c` 数组实际占 ~160KB（.c 源文本 934KB 是文本，非二进制），封面照片（640x332 ARGB8888 ≈ 850KB）更不可能放内部 flash。
- **分区规划**（W25Q256 32MB，偏移 0 起）：
  - `0x00000000 ~ 0x0094B000`（9,744,384B ≈ 9.29MB）：`黑体_simhei.ttf`（GB2312 全覆盖，tiny_ttf 运行时渲染）；
  - `0x00950000 ~`（剩余 ~21.7MB 可用）：**图标区**，XIP 映射基址 `0x80950000`；
- **实现**：
  - `src/middleware/src/ospi_icons.c`（**自动生成，勿手改**）：19 个 `lv_image_dsc_t`（17 个 icon + `icon_nuedc_70x42_ARGB8888` + `boot_photo` 640x332 封面 849,920B），`.data = 0x80950000 + 偏移`，图标数据 **XIP 零拷贝直读**；
  - `CMakeLists.txt`：`gui/RA8P1/generated/assets/images/*.c` 排除编译（图标数组不再进内部 flash）；
  - `tools/gen_ospi_icons.py`：自动扫描 `gui/RA8P1/resources/image/` 全部 `icon_*.c`（含未列入 `gg_image.h` 的）+ PIL 生成 `boot_photo`（源图 `cover_640x332.png`，640x332 LANCZOS 缩放 → ARGB8888 `[B,G,R,A]` 字节序，与 LVGL little-endian 一致）→ `build/ospi_icons.bin` + `ospi_icons.c`；
  - `tools/ospi_burn_icons.py`：J-Link + RTT down 烧录，数据帧命令 **`0x10`（图标区）**；`ospi_ttf_loader.c` 增加 `TTF_LOADER_CMD_ICON_DATA=0x10` / `ICON_FLASH_BASE_OFFSET=0x00950000U`，DATA 分支按命令选基址（0x01→字体区 / 0x10→图标区），64KB 块擦除按**绝对地址**；
  - **烧录稳定性（本次踩坑）**：Boot 页渲染 830KB 大图持续读 OSPI 占总线 → J-Link 读内存超时、烧录停滞。修复：`boot_build_images/home_build_images` 检测 `ospi_ttf_loader_burn_mode()`（`.noinit` 标志 `0x2200084c`，复位不清零，=1 时跳过建图），烧录结束（DONE 帧清 0）后下次正常启动再建；另 J-Link 偶发读失败 → `_read32_retry` 10 次重试 + reset 后 `sleep(1.0)`；
  - 验证：J-Link 读 `0x80950000` 区与 `build/ospi_icons.bin` 逐字节一致（0/512 mismatch）；固件烧录 Verify 通过（图标 1,026,888B，34s）；`burn_mode` 确认=0（tiny_ttf 正常渲染）。
- **后续加图流程**：图片/封面放 `gui/RA8P1/resources/image/`（封面固定名 `cover_640x332.png`）→ `py tools/gen_ospi_icons.py` → `py tools/ospi_burn_icons.py <elf路径> build/ospi_icons.bin`。固件 dsc 指向 OSPI 不变时**图标更新无需重烧固件**。

## 2026-08-18 Boot 启动页：封面照片 + 白色粗体文字（含居中修复）

- 用户把白底封面替换为 `封面_compressed.png`（1920x1068 P 模式压缩 443KB）→ 预处理缩放为 `cover_640x332.png`（与 brand_band 之下 148..480 白色区域一致）。
- `boot_build_images`（custom.c）：Boot 屏幕**最底层**（`lv_obj_move_to_index(0)`）铺 640x332 封面；**Boot 页不放 nuedc**（用户最新要求，工程文件同步删除 boot_nuedc）。
- 下半部分 title/subtitle/status 三行文字：**白色 + 深色（0x10233f）粗描边**（stroke width 3/2/2，模拟加粗，封面背景上醒目）；工程文件 `text_color` 同步 `#ffffff`。
- **居中修复（本次）**：`configure_ui_label_layout` 为防换行把 title 固定成 280x42 框（`configure_single_line_label_box`），但文字左对齐 → `LV_ALIGN_TOP_MID` 只居中"框"、**文字整体左偏 ~35px**。修复：`boot_style_texts` 对三行文字补 `LV_TEXT_ALIGN_CENTER`；工程文件/生成代码 `gg_Boot.c` 同步 `LV_TEXT_ALIGN_CENTER`。
- 工程文件同步：`RA8P1.guiguider` Boot 屏加 `boot_photo` 控件（640x332 @(0,148)），并**修正为 Boot 屏幕直接子元素、children[0] 最底层**（此前误嵌套进 `boot_logo` 容器）。

## 2026-08-18 Home 页 renesas 左侧 nuedc 图标

- 需求："进入之后 renesas 左边再放一个 icon_nuedc"（加载页不要）。
- `home_build_images`（custom.c，Home 懒创建后由 500ms 周期刷新挂载）：以 `ui->Home.header_renesas_brand` 为锚点，`x = renesas.x - 8 - 70`、垂直居中，**跟随 renesas 移动**；`icon_nuedc_70x42_ARGB8888` 数据在 OSPI 图标区；烧录模式跳过。
- 工程文件：Home header 加 `home_nuedc` 控件（70x42，src `icon_nuedc.png`，此文件已由 PIL 覆盖为 70x42 版，原 816x495 备份在 `.tmp/icon_nuedc_orig_816x495.png`）。
- 用户后来自行把 Home renesas 换成自定义图（`304b0ee6072557fb6224d9cfdc23aa05.png`，160x60 居中），本轮**保持不动**。

## 2026-08-18 登录页软键盘 + 记住用户名/密码

- 需求：登录页要"能输入"（软键盘），默认预填正确账号密码方便调试但允许修改，另加"记住用户名/记住密码"复选框。
- 实现（`gui/RA8P1/custom/custom.c`，固件与模拟器通用）：
  - `LOGIN_STAFF_ID "renesas"` / `LOGIN_PASSWORD "1234"`（工号大小写不敏感比较 `login_streq_ci`）；
  - 运行时把 GUI Guider 的只读 value label 换成 `lv_textarea` + `lv_keyboard`（`lv_layer_top` 上全局唯一）：工号 → `LV_KEYBOARD_MODE_TEXT_UPPER` 大写键盘、密码 → `LV_KEYBOARD_MODE_NUMBER` 数字键盘；离开登录页自动收键盘（network_badge_timer_cb 检测）；
  - 复选框"记住用户名/记住密码"（默认勾选）→ 打开页面即预填，取消勾选则留空；提交校验通过 → 先收键盘 → 跳转 Admin；错误留在登录页红色提示"工号或密码错误"。
- **API 注意**：LVGL 9 无 `LV_KEYBOARD_MODE_TEXT_DIGIT`，数字模式枚举为 `LV_KEYBOARD_MODE_NUMBER`；文字描边 setter 为 `lv_obj_set_style_text_outline_stroke_width/color`（无 `text_outline_width`）。

## 2026-08-18 内存与存储占用统计（当前固件 2026-08-18 构建）

| 资源 | 总量 | 已用 | 占用率 | 剩余 |
|------|------|------|--------|------|
| 内部 Flash（代码+只读数据） | 1,048,576 B（1MB） | 772,348 B（754.2KB） | **73.7%** | ~276KB |
| 内部 SRAM（0x22000000） | 1,916,928 B（1.83MB） | 1,746,212 B（1.67MB） | **91.1%** | ~167KB |
| 外部 SDRAM（0x68000000） | 128MB | 3,573,264 B（3.4MB） | 2.7% | 富余 |
| 外部 OSPI Flash（W25Q256） | 33,554,432 B（32MB） | 10,791,752 B（10.3MB） | **32.2%** | ~21.7MB |

- 内部 Flash 明细（`arm-none-eabi-size -A`）：`__flash_vectors$$` 160B + `__flash_readonly$$`（.text+.rodata：代码 + GUI Guider 精简字库 + 常量）772,164B + 数组/异常表 24B；option settings 区（0x02C9F040 等）在 1MB 区域之外，不计入。
- 内部 SRAM 明细：`.data`（`__ram_from_flash$$`）1,040B + `.bss`（`__ram_zero$$`，含 **LVGL 堆 512KB**（LV_MEM_SIZE=0x80000）、**FreeRTOS 堆 256KB**（configTOTAL_HEAP_SIZE=0x40000）、tiny_ttf 字形缓存等）1,729,748B + `.noinit`（`__ram_noinit$$`，含烧录模式标志 0x2200084c）1,072B + 线程栈（`__ram_thread_stack$$`）14,336B。
- 外部 SDRAM：LVGL 双渲染缓冲 + OV7725 预览缓冲 + OSPI 烧录接收缓冲（内部 RAM 已近满载，大缓冲全部下沉 `.sdram_noinit`）。
- 外部 OSPI：字体区 `黑体_simhei.ttf` 9,744,384B（0x0~0x94B000）+ 图标区 1,026,888B（0x950000~0xA4AB48，17 icon + nuedc + boot_photo 封面）。
- **结论**：内部 SRAM 是最紧张资源（91.1%），后续大缓冲优先 SDRAM；OSPI 图标/图片空间余量充足（~21.7MB），可持续加图。

## 2026-08-18 Git 历史整理（63 提交 → 9 提交，内容不变）

- 用户要求"重新整理 git log，里面的内容不变"（历史版本太多、提交错乱复杂）。
- 方案：`git commit-tree` 重建历史（按功能主线合并为 9 个提交）；整理前后 HEAD tree hash **完全一致（6ea6220b…）**——工作区内容逐字节不变。
- 已 force push 到 `origin`（git@github.com:water-freemind/26renesascup_smartmedicine_Ra8p1.git）。
- 整理前完整历史备份：`.tmp/git_backup_20260818_212500.bundle`。

## 2026-08-18 其余本轮改动

- 取药页标题"取药单已识别"→"取药单待扫描"（GUI Guider 工程文件同步）。
- `tools/restore_guider_fonts.ps1`：GUI Guider 重新生成会膨胀字体（12 个 SourceHanSerifSC 字库，22 号字形 bitmap 13.6KB→44.5KB，内部 flash 超支 48,308B）→ 一键 `git checkout HEAD -- gui/RA8P1/generated/assets/fonts/` 恢复精简字库。
- `gui/RA8P1/preview.png`：GUI Guider 预览快照随界面更新。

## 遗留（本轮）

- 实机待验证：Boot 封面 + 白字粗体居中、Home nuedc 图标、登录键盘/记住选项、各页图标 OSPI 区显示、二维码识别（Scan 扫药品码 / Pickup 扫取药单）。
- 登录"记住"勾选状态持久化到 OSPI（当前仅会话内生效）；机械臂动作接入、库存持久化、ESP-01S UART 栈、一维条码库等早期待办不变。

# 最新状态：tiny_ttf 全面替换编译期字库（界面任意中文渲染，用户实机确认无乱码）（2026-08-18）

## 2026-08-18 tiny_ttf 全面应用（实机验证通过）

- **背景**：编译期字库（`lv_font_SourceHanSerifSC_*.c`）实际仅覆盖 164 个中文（GUI Guider 生成字库含"乱码字符集"，大量运行态文本——日志/自检/状态词——缺字 → 方块）。
- **方案（用户建议）**：既然 tiny_ttf 已就绪（外部 Flash 4MB 仿宋 TTF 含完整 GB2312 6763 汉字），**界面全面切换到 tiny_ttf**，字库即"全的"，不再受内部 flash 1MB 限制。
- **实现**（`gui_app.c`）：
  - `g_tiny_fonts[6]`：15/17/18/22/24/28px 六个字号 tiny 字体（`lv_tiny_ttf_create_data_ex`，cache=64）；
  - `gui_app_apply_tiny_to_screens()`：遍历 Boot/Home/Pickup/Scan/Medicine/Login/Admin/Store/Logs/Device 十个懒创建 screen，出现后递归替换 label 字体（按原字体行高匹配最接近字号）；
  - 日志页 label 字体也改用 tiny（`g_tiny_font` 18px 别名）。
- **加粗**：仿宋笔画偏细，全部 tiny 文字加 1px 描边（`LV_STYLE_TEXT_OUTLINE_STROKE_WIDTH`，注意 LVGL 9.3 setter 为 `lv_obj_set_style_text_outline_stroke_width`，无 `text_outline_width`）。若仍不够粗可换粗体 TTF 重烧（见遗留）。
- **验证**：J-Link 读 `g_tiny_fonts` 6 指针全非 NULL；用户实机确认界面/日志**无乱码、全部正常**；测试 demo（"动态字体: 亍丌兀廿卅" 生僻字验证）已按用户要求移除。
- **性能**：stbtt 软件渲染，字形缓存 64 项/字体；界面首次渲染稍慢，日常操作正常。

## 遗留

- **加粗**：描边 1px 已生效；若用户要求更粗，需准备粗体 TTF（黑体/粗宋，GB2312 覆盖）烧入 OSPI 后替换字体源。
- **字库文件**：`gui/RA8P1/generated/assets/fonts/lv_font_SourceHanSerifSC_*.c` 内容异常（GUI Guider 重新生成覆盖过补字）；`sync_generated_chinese.js` 仍保留，若回归编译字库可重跑。
- 早期待办不变：机械臂动作接入、库存持久化、ESP-01S UART 栈、一维条码库。

# 最新状态：TTF 完整烧入 OSPI（全量验证通过）+ tiny_ttf 字体创建成功（2026-08-18）

## 2026-08-18 TTF 烧录通道最终打通（实机全量验证）

- **关键修复 1：CHUNK 与 64KB 块边界对齐**。`CHUNK=2000B` 不整除 65536（块擦边界），每块起始产生"帧间隙"（块边界到下一帧边界之间无数据覆盖，块擦后被读为 0xFF）。实测块 1（0x10000-0x101CF）、块 2（0x20000-0x203DF）等起始区域全 FF。**改为 `CHUNK=1024`**（1024×64=65536 整除；帧 16+1024=1040B < RTT down 2048B 缓冲）。
- **关键修复 2：loader 增加 CMD 0x00 重置命令**（`s_ttf_written/s_ttf_chip_erased/s_last_block` 清零，不清 flash）。固件未重启时上次进度残留会导致 PC 等待逻辑失效、帧堆积（实测 s_ttf_written 累计到 2× 文件大小）。
- **烧录结果**：3,996,872 字节，147s，`s_ttf_written` 精确等于文件大小；J-Link 读 14 个关键偏移（含 0x40/0x10000/0x20000/各块边界/末尾）**与本机文件逐字节一致**。
- **tiny_ttf 运行时字体创建成功**（`g_tiny_font=0x22045CE8` 非 NULL）——之前创建失败的直接原因是 TTF 数据损坏（0x40 后全 FF），数据修好后 stbtt_InitFont 正常。
- **界面验证待做**：Device（系统状态）页懒创建，进入后自动挂载「动态字体: 犇骉焱燚龘」演示 label（18px 仿宋，GB2312 生僻字，编译期字库未覆盖）。**请进 Device 页查看底部蓝色文字**。

## 2026-08-18 OSPI 写路径关键修复：R_OSPI_B_Write 映射写 WEL 粒度

- **现象**：TTF 烧入 OSPI 后，J-Link 读 `0x80000000` 前 64B 正确，**`0x40` 起全为 0xFF**（映射读/写都正确但数据是"空的"）。
- **根因**：`R_OSPI_B_Write` 的 CPU 映射写路径（`*p_dest64 = *p_src64`）把数据写进 D-Cache，之后必须 `SCB_CleanDCache` 强制回写才触发 OSPI 页编程事务。**OSPI 映射写以 64B（组合功能）为事务粒度，每个事务消耗一次 WEL（写使能）**；`R_OSPI_B_Write` 只在开头发一次 06h，一次写入超过 64B 时 clean 回写会拆成多个事务，只有第一个事务（WEL 置位）成功，其余数据丢失。
- **修复**（`ospi_storage.c`）：`ospi_storage_write()` 按 **64B 段**写入，每段独立 `06h + 等 WIP`，再 `R_OSPI_B_Write + clean + 等 WIP`。
- **自检升级**：selftest 由 64B 扩到 **256B**（direct 写读 + 映射写读双路径），可发现此类"部分写入"问题。
- **教训**：自检数据长度必须覆盖真实写路径的粒度（≥64B），否则 64B 假通过。

## 2026-08-18 TTF 烧录通道打通（实机验证）

- **PC 脚本**（Python + pylink-square，J-Link 直写内存操作 RTT down 缓冲，绕开 DLL RTT 功能限制）：
  - `tools/rtt_down_test.py <elf>`：RTT down 端到端测试（写一帧 → 固件消费 → `s_ttf_written` 增长）。
  - `tools/ospi_burn_ttf.py <elf> <ttf>`：整片/块擦除 + 分块（2000B/块）烧录 TTF，轮询 `s_ttf_written` 进度；`--erase` 可选先 chip erase（~100-200s）。
  - `tools/read_syslog.py <elf>`：J-Link 读固件 sys_log 环形缓冲（诊断用）。
- **固件烧录通道**：`ospi_ttf_loader.c` 支持 0x01 数据 / 0x02 完成 / **0x03 整片擦除**；数据帧按 **64KB 块擦除**（0xD8，~1.5s/块，4MB ≈ 96s，远快于 1024 次扇区擦）。
- **实测**：3,996,872 字节（仿宋_GB2312.ttf）77s 烧入；J-Link 读 `0x80000000` 与 `0x80000100` 与本机文件逐字节一致（**0x40 处确认坏块 → 见上 WEL 修复**）。
- **pylink 注意**：Renesas 定制 J-Link DLL 的 `JLINKARM_DEVICE_GetIndex` 返回大索引 → `pylink.connect()` 报 Invalid index，改用手动 `exec_command("Device = ...")` + `JLINKARM_Connect()`；RTT 控制块地址用符号 `_SEGGER_RTT` 直传。
- **D-Cache 直读陷阱**：J-Link 经 SWD 直读 RAM 绕过 CPU D-Cache，固件更新诊断变量后必须 `SCB_CleanDCache`（`s_ttf_written/s_ttf_chip_erased/s_ttf_busy/g_tiny_font` 均已 clean）。

## tiny_ttf 实时渲染（代码就绪，待 J-Link 恢复后实机验证）

- `gui_app.c`：`gui_app_tiny_font_service()`（gui_app_poll 内）——OSPI ready 后 `lv_tiny_ttf_create_data_ex(mmap, 3996872, 18, KERNING_NONE, 64)` 创建运行时字体（`g_tiny_font`），Device 页底部挂演示 label「动态字体: 犇骉焱燚龘」（GB2312 生僻字，编译期字库未覆盖）。
- **cache_size 取 64**：默认 256 项节点池一次性分配 ~15KB，曾导致创建失败（后续确认主因是 TTF 数据损坏，但 64 更省堆，保留）。
- **待验证**：J-Link 恢复后 → 烧录新固件 → selftest 256B 通过 → 重烧 TTF → `g_tiny_font` 非 NULL + Device 页渲染生僻字。
- **遗留**：J-Link USB 驱动层未就绪（VID_1366 枚举 Unknown），需换 USB 口/线重试；`backup_pre_ospi_20260818_002331/` 确认稳定后可删。

# 最新状态：串行 Flash 打通（W25Q256 验证 OK）+ tiny_ttf 就绪（2026-08-18）

## 2026-08-18 OSPI 串行 Flash 打通（实机验证）

- **硬件**：RA8P1 OSPI0 / CS0（P107=CE, P808=SCK, P100/P803/P103/P101=SIO0-3），板载 32MB 串行 NOR。
- **实机 JEDEC ID 读取成功**：`EF 40 19` = **Winbond W25Q256JVEIQ**（32MB），非 AT25SF256。
- **RASC 配置**（`configuration.xml`）：
  - 新增 `module.driver.ospi_on_ospi_b.0`（实例 `g_ospi0`，unit 0 / CS0，W25Q256 标准命令集：读 0x03、页编程 0x02、扇区擦 0x20/4KB、块擦 0xD8/64KB、chip 0xC7、页 256B、4 字节地址）；
  - `raComponentSelection` 加 `r_ospi_b` 组件；挂到 Camera 线程 context；
  - 时钟：`board.clock.octaspiclk.source=pll2r`、div=2（OCTACLK 必须启用，且 PLL2 源超 120MHz 上限 → 用 PLL2R）；
  - RASC 生成：`ra_cfg/fsp_cfg/r_ospi_b_cfg.h`、`ra_gen/Camera_thread.c` 内 `g_ospi0` 实例。
- **驱动源码**：FSP 裁剪包缺 `r_ospi_b`，从 Renesas FSP GitHub v6.3.0 拉取 `r_ospi_b.c/h` + `r_spi_flash_api.h` 放入 `ra/fsp/`。
- **应用封装**：`src/middleware/ospi_storage.c/h`（init/读 JEDEC/扇区擦/页编程/内存映射读，映射基址 0x80000000）；
  - Camera 线程启动时初始化并写日志（JEDEC 三字节）。
- **RTT down 通道**：`rtt_preview.c` 增加 down buffer（PC→MCU 2KB 环形），供 TTF 烧录接收。
- **TTF 烧录协议**：`src/middleware/ospi_ttf_loader.c/h`，RTT 帧格式 `TTF cmd seq offset len data`，轮询写入 OSPI；PC 端脚本未写。
- **RASC 生成后手改项恢复**：`tools/post_rasc_patch.ps1` 重新注入 `LV_USE_FONT_COMPRESSED=1`（RASC 会清掉手改块）；`INCLUDE_uxTaskGetStackHighWaterMark` 已在 configuration.xml 持久化为 enabled。
- **RAM 溢出修复**：`s_screen_preview`(345KB) + `s_rtt_preview`(150KB) 移到 `.sdram_noinit`（SDRAM）；TTF 接收帧缓冲也放 SDRAM。RAM 释放 ~495KB。
- 固件：925KB/1MB flash；构建通过。

## tiny_ttf 实时渲染（驱动就绪，应用待接）

- `LV_USE_TINY_TTF=1` 已启用（lv_conf.h + post_rasc_patch 持久化）；`lv_tiny_ttf.c` 已编译（被 gc 丢弃，等调用）。
- **下一步（恢复后继续）**：
  1. 用 PC 脚本经 RTT 把 `gui/RA8P1/resources/font/仿宋_GB2312.ttf`（4MB TrueType）烧入 OSPI Flash 偏移 0；
  2. `lv_tiny_ttf_create_data(ospi_storage_mmap_base(), ttf_size, px)` 创建运行时字体，替换界面字体；
  3. 验证中文任意文本渲染；更新 README/docs。
- **遗留**：J-Link 当前 USB 连接失败（待重试烧录）；`backup_pre_ospi_20260818_002331/` 为 RASC 改动前备份（确认稳定后可删）。

# 最新状态：登录验证落地 + 自检流程归档 + 字库全面审计（2026-08-18）

## 2026-08-18 药师登录验证（默认预填正确账号密码）

- **需求**：管理台入口要有"登录跟验证"，且默认预填正确的用户名/密码方便调试。
- **实现**（`gui/RA8P1/custom/custom.c`，固件与模拟器通用，不碰 GUI Guider 生成的事件文件）：
  - 默认预填 **工号 PH-001 / 密码 123456**（密码直接可见，方便调试）；
  - 提交时与常量 `LOGIN_STAFF_ID / LOGIN_PASSWORD` 比对：
    - 匹配 → `s_authenticated = true`，跳转 Admin 管理台；
    - 不匹配 → 留在登录页，副标题红色提示"工号或密码错误"；
  - 实现方式：`install_auth_hooks` 里先清空 submit 按钮上生成事件的无条件跳转处理器（`lv_obj_remove_event` 按索引逐个移除），再挂"校验→跳转"回调；登出按钮照旧清 `s_authenticated`。
- 字库：登录提示"工号或密码错误"汉字并入 **16px**（`runtimeBySize[16]`）。

## 2026-08-18 运行自检流程（Device 页"运行自检"按钮）

自检入口：Admin 管理台 → 设备管理 → 运行自检。每次点击依次检查 4 项，全部写入系统日志（Logs 页可见），并更新 Device 页"系统状态"摘要：

| 项 | 检查内容 | 判定 | 计入失败数 |
|----|----------|------|-----------|
| 1. 触摸 | `g_cst816s_probe_ok != 0`（CST816S 探测成功） | 失败→"异常" | 是 |
| 2. 摄像头 | `camera_app_capture_active()` + 当前 FPS | 空闲态只记录"待机"（不算异常） | 否 |
| 3. 网络 | `gui_get_esp01s_online()`（ESP-01S 当前在线状态） | 未接入驱动时"离线"，仅记录 | 否 |
| 4. LVGL | `s_lvgl_ready`（界面启动完成标志） | 失败→"异常" | 是 |

- 摘要显示：全过 → 绿字"自检通过"；否则 → 红字"自检 N 项异常"。
- 日志格式：`自检-触摸: 正常/异常 (read_ok=N)`、`自检-摄像头: 采集中/待机 (x.x FPS)`、`自检-网络: 在线/离线`、`自检-LVGL: 正常/异常`。
- 实现位置：`src/app/src/gui_app.c` 的 `gui_app_run_self_test()`（返回失败数）+ `self_test_button_click_hook`。
- **本次修复**：摘要/日志用到的"自/检/通/过/项/异/常"并入 **24px**（摘要字号）、"上次"并入 **17px**（Device 摄像头行"就绪 / 上次 X.X FPS"，用户反馈此前显示方块方块）、"解/创/建"并入 **15px**（"二维码解码线程已创建"日志）。

## 2026-08-18 字库全面审计（确保所有运行态文本无方块）

- 脚本化逐 label 审计：对 `gui/RA8P1/generated/screens/gg_*.c` 的每个 label（文本+字号）和 src 中全部运行态字符串（gui_app.c/custom.c/sys_log/drug_db）与 11 个生成字库比对，确认缺失字形后一次性补入 `sync_generated_chinese.js` 的 `runtimeBySize`：
  - **15px**：+解/创/建（二维码解码线程已创建）；
  - **16px**：+待（Store"待扫码"徽章）、工/或/密/码/错/误（登录错误提示）；
  - **17px**：+上/次（Device 摄像头行"上次 X.X FPS"）、未（Medicine"未验证"）；
  - **22px**：+扫/描/取/药/单/已/识/别（Pickup 标题"取药单扫描/取药单已识别"）；
  - **24px**：+自/检/通/过/项/异/常（Device 自检摘要）。
- 重新生成后逐项复核通过（15/16/17/18/20/22/23/24px 全覆盖），静态设计文本与运行态文本均无缺口。
- 后续加字流程不变：新文本进 `src/**` → `node gui\tools\sync_generated_chinese.js` → 重编译烧录。

# 最新状态：Logs 滚动列表 + 常用药名词库 + 缺口字符修复（2026-08-18）

## 2026-08-18 三项修复

- **Logs 页可滚动**：固定 4 行改为动态创建 12 行（时间/内容/级别），`log_table_card` 启用垂直滚动 + 自动滚动条；生成的 4 个静态行隐藏。
- **常用药名词库**：`medicineChars`（689 字符，头孢/阿奇霉素/对乙酰氨基酚/奥美拉唑/二甲双胍/胰岛素/氯雷他定等常见药）并入显示扫码结果的 17/18/23px；28px 保持精简（仅 drugDbChars，大字形省 flash）。FLASH 超支 260KB→0，固件 888832 B。
- **缺口字符修复**：U+00B7 中点"·"（Boot"系统就绪 · 触摸正常"）、"条"（Logs 指标卡"X 条"）、"种/盒"（Pickup 合计"X种 / X盒"）并入所有/对应字号。
- **Pickup 未扫码不再显示初始清单**：未扫码时 3 行清单与合计显示"等待扫码/--"，扫码识别后才填充 drug_db 演示数据。
- 验证：编译烧录通过（888832 B）；运行态健康（触摸 read_ok 增长/read_fail=0/probe_ok=1、LVGL=1、日志 5 条）。

# 最新状态：字库 RLE 压缩（flash 省 190KB）+ 未来动态字体方案归档（2026-08-18）

## 2026-08-18 字库压缩与"未来加字"策略

- **背景**：用户提出"只能烧录已有字库，未来要用新字怎么办"。编译期字库是固件常态（加字=重跑脚本+重编译+重烧录），但 flash 余量决定能加多少。
- **RLE 压缩落地**：`lv_font_conv` 默认 RLE 压缩（此前因 `--stride 16` 修复被迫用 `--no-compress`）。确认 **LVGL 9.3 压缩字库走 decompress() 路径、`fdsc->stride=0`，完全独立于 stride 修复**（stride 计算仅用于非压缩字库）→ 启用压缩无冲突。
  - `ra_cfg/fsp_cfg/lvgl/lvgl/lv_conf.h`：`LV_USE_FONT_COMPRESSED=1`（configuration.xml 无此属性，直接改生成头，注释说明）。
  - `sync_generated_chinese.js`：去掉 `--no-compress`（启用压缩）。
  - **实测**：固件 964608 B → **792576 B（774KB，省 190KB）**，flash 占用 1MB 的 76%，为未来新字预留充足余量；压缩字库实机运行正常（LVGL=1、触摸/日志健康），decompress 路径工作。
- **"未来加字"流程**（低成本，已自动化）：新文本进 `src/**` → `node gui\tools\sync_generated_chinese.js`（自动收集 sys_log 格式串 + 按字号分配固定词）→ 重编译 → 烧录。当前 774KB/1MB，还可容纳约 200KB 新增字形（约 1000+ 常用字）。
- **待未来解决：运行时动态字体**（完整方案见下，非本次实施）：

### 未来方案：运行时加载任意字符（暂缓，写入本档备忘）
- 需求场景：显示**服务器/用户下发任意文本**（网页端药品库、自定义名称等）时，编译期字库会缺字。
- 可选路线：
  1. **tiny_ttf 运行时渲染**（LVGL 9.3 自带 `src/libs/tiny_ttf/`）：把 TTF 放外部存储（SD/SPI Flash），`lv_tiny_ttf_create_file()` 运行时加载，任意字符即时渲染。代价：需要存储介质+文件系统；MCU 软件渲染中文慢（测试 16px 单字渲染耗时）；内存占用（TTF 解析+字形缓存）；与当前 Bold 加粗视觉需重新对齐。
  2. **GB2312 全量字库**（3755 字 × 11 字号）：约 1.5MB+，超出 1MB flash，需外部存储或压缩后仍超——不可行。
  3. **LZ4/自定义分片字库**：按需从外部存储加载字形块，复杂度最高。
- 建议优先级：若网页端后续要下发任意文本 → 上 tiny_ttf（存 SD）；否则维持编译期字库 + 脚本自动收集即可。

# 最新状态：运行时中文字符补齐（修复方块）+ FLASH 空间治理（2026-08-18）

## 2026-08-18 方块问题根因与修复

- 现象：界面很多文字显示为方块。
- **根因**：`sync_generated_chinese.js` 只收集 GUI Guider 设计文件的字符；而我近几轮加的**运行时文本**（"运行/余/已取/查看详情/采集中/请先登录药师账号"、sys_log 日志内容、drug_db 药名等）只存在于 C 代码，字库缺字形 → LVGL 渲染为方块。
- **修复**（`sync_generated_chinese.js` 增强）：
  - 自动从 src 提取 `sys_log_add()` 格式串的 CJK 字符 → 并入 15px（日志行字号）；
  - 按字号分配运行时状态词（15-28px 各加其实际用到的固定文本）；
  - drug_db 药名汉字并入 17/18/23/28px。
- **FLASH 空间治理**：全量灌入所有字号会溢出 1MB flash（实测超 1.28MB）：
  - 只对"显示任意内容"的字号（15px 日志）灌全量字符，其余字号只加固定词；
  - 删除 36px/26px 字库（36px 仅渲染"+"、26px 仅登录标题，统一改 24px，生成文件+设计文件同步）；
  - 最终固件 964608 B（<1MB），比历史基线多约 29KB（补齐的运行时字形）。
- **验证**：编译烧录通过（964608 B，Program/Verify OK）；运行态健康（触摸 read_ok 增长/read_fail=0/probe_ok=1、LVGL=1、日志 5 条）。
- 待办：实机确认方块消失（重点：主页"运行 HH:MM:SS"、存药页"余 N 盒"、取药页"库存 N 盒"、Logs 页日志内容、登录页标题）。

# 最新状态：qr_decoder 头文件清理（移除旧 API 残留）（2026-08-18）

## 2026-08-18 代码清理

- `qr_decoder.h`：删除已废弃的 `qr_decoder_process_frame` 声明与旧线程模型注释，统一为 feed_frame + 独立解码线程 + get_result 三线程模型说明。
- 编译烧录通过；运行态健康（触摸 read_ok 增长/probe_ok=1、LVGL=1、日志 5 条含解码线程创建）。

# 最新状态：quirc 解码移出 Camera 线程（独立低优先级线程）——触摸/画面卡死根治（2026-08-18）

## 2026-08-18 回归根因：quirc 解码在 Camera 线程同步执行，卡死饿死 LVGL

- 现象（复现）：进取药/识别页后摄像头无画面 + 触摸完全失灵。
- **实测诊断**（J-Link）：CEU 帧计数停 1、`s_dbg_qr_decode_count=0`（解码从未完成一次）、**触摸 read_ok 也冻结不增长**——Camera 与 LVGL 线程同时被卡。
- **根因**：Camera 线程优先级 **2**（高于 LVGL 的 **1**）。`qr_decoder_process_frame()` 在 Camera 线程内同步执行 `quirc_end()`，首次解码卡死（无返回）。高优先级 Camera 线程占死 CPU → LVGL 线程被饿死 → 触摸读取（LVGL 线程内 IIC）停止、画面不刷新；Camera 线程自身也卡住无法 CaptureStart 下一帧 → CEU 停帧。
- **修复**：解码彻底移出 Camera 线程——
  - `qr_decoder_feed_frame()`：Camera 线程只提取 Y 灰度到共享缓冲（快速，无 quirc）；
  - 新增独立解码线程 `qr_decoder_task_entry()`（优先级 **0**，低于 LVGL/Camera）：轮询新帧 → 200ms 限频 quirc 解码 → 去抖发布；8KB 栈；
  - `qr_decoder_start_task()` 在 Camera 线程启动时创建（日志确认"二维码解码线程已创建"）。
  - 即使 quirc 卡死，也只会卡住优先级 0 的线程自身，触摸与采集完全不受影响。
- **验证**：编译烧录通过；运行态日志 5 条（含解码线程创建）、触摸 read_ok 增长/read_fail=0/probe_ok=1、LVGL=1。待实机：进 Pickup/Scan 页确认预览+触摸正常；扫码后读 `s_dbg_qr_decode_ms` 确认解码耗时（若 >200ms 需进一步降低限频或优化 quirc 输入尺寸）。

# 最新状态：存药/取药操作反馈（按钮状态变化）（2026-08-18）

## 2026-08-18 操作反馈显示逻辑

- **Store"确认存药"按钮反馈**：点击后按钮文字"确认存药"→"已确认"（绿，入库成功）/ "确认失败"（红，未识别）；随库存 +1 日志。
- **Pickup"开始取药"按钮反馈**：点击后"开始取药"→"取药完成"（绿，库存扣减+状态行"已取"）；"重新扫描"重置时恢复"开始取药"。
- 均用"变化才设置"helper 更新，不引入额外重绘；不改生成事件文件。
- **验证**：编译烧录通过；运行态健康（触摸 read_ok 增长/probe_ok=1、LVGL=1、日志正常）。待实机：存药确认后按钮变"已确认"、取药后按钮变"取药完成"。

# 最新状态：Logs 表头语义同步 + Home 运行时长（2026-08-18）

## 2026-08-18 表头与内容一致化

- **Logs 页表头语义同步**：旧"取药单号/药品/结果/用时"（取药记录语义）改为"时间/内容/级别/级别/（空）"，与系统日志显示逻辑一致（生成文件+设计文件同步）。
- **Home 欢迎卡运行时长**：welcome_card_hint 由静态标语"安全 · 高效 · 可追溯"改为"运行 HH:MM:SS"（lv_tick_get，模拟器/固件通用，秒值变化才更新）。
- **验证**：编译烧录通过；运行态健康（触摸 read_ok 增长/read_fail=0/probe_ok=1、LVGL=1、日志正常）。

# 最新状态：Pickup 重新扫描按钮 + Store 操作药师联动（2026-08-18）

## 2026-08-18 取药单重扫闭环 + 药师会话显示

- **Pickup"重新扫描"按钮接真实动作**（`pickup_rescan_click_hook`，事件钩子不改生成文件）：
  - 重置取药单 UI：单号"等待扫码…"、徽章"待扫描"、标题"取药单扫描"；
  - 重置 `s_pickup_dispensed`（允许重新执行取药）；
  - 新增 `qr_decoder_reset()`（清空去抖/发布状态，允许重新解码同一二维码）。
- **Store 操作药师真实化**：假数据"PH-001"改为登录会话状态——"已登录/未登录"（新增 `gui_get_authenticated()` 查询接口）；生成文件+设计文件同步。
- **验证**：编译烧录通过；运行态健康（触摸 read_ok=288 增长/read_fail=0/probe_ok=1、LVGL=1、日志正常）。待实机：取药页扫单→重新扫描→可再次扫同一单；存药页操作药师随登录状态变化。
- 待办：机械臂/ESP-01S 硬件接入；库存持久化。

# 最新状态：Store 入库余量真实化 + Scan 徽章颜色变化检测 + 模拟器构建验证（2026-08-18）

## 2026-08-18 余量显示与 UI 刷新微优化

- **Store 页"入库数量"真实化**：由假数据"20盒"改为"余 N 盒"（容量-当前库存，`inventory` 台账驱动）；生成文件与设计文件占位改 "--"。
- **Scan 徽章颜色变化检测**：`lv_obj_set_style_text_color` 每 500ms 无条件调用改为 `lv_color_eq` 比较后仅在变化时设置（与文本 helper 同理，避免样式重算）。
- **模拟器构建验证**：近几轮改动（custom.c 变化检测、生成文件占位）后重新 configure+构建 GUI Guider 模拟器成功（LVGL 9.4），模拟器与固件共享 generated 目录不受影响。
- **实测**：触摸 read_ok=494 增长/read_fail=0/probe_ok=1、LVGL=1、Camera 线程栈高水位 894 words（3.5KB 空闲，未溢出）、日志 4 条。
- 待办：实机扫码后读 `s_dbg_qr_decode_ms` 确认解码耗时；机械臂/ESP-01S 硬件接入。

# 最新状态：解码耗时与 Camera 线程栈诊断（2026-08-18）

## 2026-08-18 预防性诊断：quirc 解码耗时 + 线程栈余量

- 背景：Camera 线程栈仅 4096B，quirc 解码（otsu 直方图 1KB 等局部数组）在 Camera 线程同步执行——若栈不足或耗时过大，是"预览不显示/触摸异常"的深层诱因（节流修复后仍需实证）。
- **新增诊断变量**（J-Link 可读）：
  - `s_dbg_qr_decode_ms` / `s_dbg_qr_decode_count`（qr_decoder.c）：最近一次 quirc_end 解码耗时（ms）与累计次数；
  - `s_dbg_camera_stack_high_water`（Camera_thread_entry.c）：Camera 线程栈高水位（每 2s 采样，记录最小值）；需 `INCLUDE_uxTaskGetStackHighWaterMark=1`（FreeRTOSConfig.h，configuration.xml 对应项下次 RASC 生成时同步）。
- **实测（主页空闲态）**：Camera 线程栈高水位 886 words（≈3.5KB 空闲 / 4096B 栈），未溢出；解码诊断 count=0（未进识别页，正确）；触摸 read_ok 增长/read_fail=1（开机初期一次）/probe_ok=1；LVGL=1。
- 待实机：进识别/取药页扫码后读 `s_dbg_qr_decode_ms`——若单次 >100ms 且预览掉帧明显，把 `QR_DECODE_INTERVAL_TICKS` 调大或将解码移出 Camera 线程；若栈高水位接近 0，增大 Camera 线程栈。

# 最新状态：custom.c 定时刷新同样接入"变化才设置"（2026-08-18）

## 2026-08-18 custom.c 500ms 定时刷新加固

- 背景：custom.c 的 network_badge_timer_cb（500ms）对网络徽章/机械臂坐标/设备行/认证徽章/Admin 欢迎语无条件 `lv_label_set_text`——与 gui_app.c 同样存在"相同文本反复 malloc+invalidate"的碎片化风险。
- **加固**：新增 `gui_custom_label_set_if_changed()`（lv_label_get_text 比较，仅变化时设置），替换所有 500ms 定时刷新点；机械臂坐标改用 snprintf 到局部缓冲后比较。lv_image_set_src 对 VARIABLE 类型相同 src 仅赋值+invalidate，代价小，保持原样。
- **验证**：编译烧录通过；运行态健康（触摸 read_ok 增长/read_fail=0/probe_ok=1、LVGL=1、日志正常）。
- 至此 gui_app.c 与 custom.c 的所有周期刷新均完成"节流 + 变化检测"双保险。

# 最新状态：状态刷新"变化才设置"加固（防 LVGL 池碎片化）（2026-08-18）

## 2026-08-18 文本变化检测：周期刷新不再重复设置相同 label

- 背景：LVGL 9.3 `lv_label_set_text()` 对相同文本**不做短路**（每次都 malloc+strcpy+invalidate）。上轮 500ms 节流解决了渲染过载，但相同文本仍每 500ms 分配/释放一次——长时间运行会碎片化 LVGL 池。
- **加固**：新增 `gui_app_label_set_if_changed()` / `gui_app_label_set_fmt_if_changed()`（先 `lv_label_get_text` 比较，仅变化时设置），全部周期性刷新点（FPS/Scan 按钮/Medicine/Pickup 清单/Store/Logs 页/Boot 状态）接入；事件驱动的一次性设置（扫码结果、自检、存药确认）不受影响。
- **验证**：编译烧录通过；运行态健康（触摸 read_ok=256 增长/read_fail=0/probe_ok=1、LVGL=1、日志正常）。
- 待办：实机复测 Pickup/Scan 页预览与触摸（上轮节流修复 + 本轮加固同批验证）。

# 最新状态：修复取药/识别页摄像头不显示+触摸失灵（状态刷新过载）（2026-08-18）

## 2026-08-18 回归根因：gui_app_poll 内状态刷新无节流

- 现象：打开取药（Pickup）/识别药（Scan）页后摄像头预览不显示，且触摸完全失灵。
- **根因**：`gui_app_poll` 每 5ms（LVGL tick）运行一次，而我加的 `refresh_scan_badge/scan_button/medicine_page/pickup_list/store_page/boot_status` **无条件调用 `lv_label_set_text` + `set_style_text_color`**（Pickup 页一次 12 个 label）。页面激活时每 5ms 触发十几个 label 重绘 → LVGL 渲染线程被占满 → ① 触摸读取（同线程 IIC）被饿死 → 无法触摸；② 摄像头预览 invalidate 被排挤 → 预览不显示。
- **修复**：统一 500ms 节流闸门 `gui_app_status_throttle(uint32_t *p_last_ms)`（每函数独立计数，避免共享闸门互相拦截），所有页面状态刷新降为 2Hz；FPS 刷新原本已有独立节流，不受影响。
- **验证**：编译烧录通过；运行态健康（触摸 read_ok=281 持续增长、read_fail=0、probe_ok=1、LVGL=1、日志正常）。待实机：进 Pickup/Scan 页确认预览显示 + 触摸可用。
- 教训：LVGL 线程内任何周期性 UI 刷新必须节流（≥500ms），且 label 文本相同时不要反复 set_text。

# 最新状态：库存台账模块 + 存药/取药显示闭环 + Admin 欢迎语（2026-08-18）

## 2026-08-18 库存台账 inventory 接入界面显示

- **新增 `src/app/inc/inventory.h` + `src/app/src/inventory.c`**：RAM 库存台账（上电按 drug_db 初始化，每药品一仓位，容量默认 30）；API：`inventory_init/get/capacity/add/remove`；存药确认 +1、取药动作 -N。
- **Store 页容量真实化**：`warehouse_card_capacity` 显示"实存 / 容量 盒"（如 "8 / 30 盒"→存药后 9/30）；"确认存药"钩子调用 `inventory_add` 并记录带库存的日志。
- **Pickup 清单数量真实化**：3 行清单数量列改为"库存 N 盒"（来自台账，替代生成示例的假数量"1盒/2盒"）。
- **Admin 欢迎语联动认证**：`refresh_admin_welcome()` —— 已登录 → "当前药师：已登录"；未登录 → "请先登录药师账号"（生成文件+设计文件同步）。
- **验证**：编译烧录通过；运行态实测 `s_items[0].code="AMOX-001"`（台账按 drug_db 初始化成功）、日志 4 条（含"库存台账初始化完成"）、触摸/LVGL 健康。待实机：Store 确认存药后容量 +1、Pickup 清单显示"库存8盒"。
- 待办：机械臂动作接入（取药 -N 触发点）；库存持久化（EEPROM）；真实数据库/网页端下发。

# 最新状态：Boot 启动页真实化 + Logs 清空按钮（2026-08-18）

## 2026-08-18 Boot 状态摘要 + 日志清空

- **Boot 启动页状态真实化**（`gui_app_refresh_boot_status()`）：状态文本从静态"正在加载界面..."改为真实摘要"系统就绪 · 触摸正常（/异常）· 摄像头采集（可选）"；进度条满格。
- **Logs 页"日志详情"按钮改"清空日志"**：新增 `sys_log_clear()`（清空环形缓冲+级别计数），点击后清空并留一条"日志已清空"记录；按钮文本生成文件+设计文件同步；事件钩子不改生成事件文件。
- **验证**：编译烧录通过；运行态健康（触摸 read_ok 增长/read_fail=0/probe_ok=1、LVGL=1、日志含触摸重连记录正常）。
- 待办：机械臂状态/动作接入；库存容量真实数据；ESP-01S 驱动上线。

# 最新状态：Store 存药页真实化（识别→存药流程闭环）（2026-08-18）

## 2026-08-18 Store 存药页接入 drug_db + 最近扫码

- **Store 页药品信息真实化**（`gui_app_refresh_store_page()`，页面激活时刷新）：
  - 药品名：drug_db 查表（未命中显示 payload 原文，CLIP 限宽）；
  - 批次·有效期："批次 X · 有效期 Y"（查表）；
  - 目标药仓：drug_db position（如 AMOX → A03）；
  - 核验徽章：有结果 → "药品已核验"（绿），无 → "待扫码"（灰）；
  - 生成文件与设计文件占位改 "--/等待扫码…"。
- **Store 按钮接真实动作**（事件钩子，不改生成事件文件）：
  - "确认存药" → 写系统日志（成功/未识别警告；机械臂动作待接入）；
  - "重新识别" → 跳转 Scan 页重新扫码。
- **验证**：编译烧录通过；运行态健康（触摸 read_ok 增长/read_fail=0/probe_ok=1、LVGL=1、日志正常）。待实机：识别药后进存药页 → 药品信息/仓位自动填充；确认存药 → 日志页新增记录。
- 待办：确认存药后机械臂入库动作（需机械臂状态回读/控制）；库存容量真实数据（warehouse_card_capacity 仍为演示）。

# 最新状态：Pickup 清单数据源化 + Device 运行自检（2026-08-18）

## 2026-08-18 Pickup 药品清单真实化 + Device 自检按钮

- **Pickup 页药品清单数据源化**：3 行清单的名称/仓位不再硬编码，进入页面时由 `drug_db` 查表填充（`gui_app_refresh_pickup_list()`）；数量/状态仍为生成示例（无订单数据源）。
- **Device 页"运行自检"按钮接真实逻辑**（`self_test_button_click_hook`，懒创建页面出现后挂载一次，不改生成事件文件）：
  - 检查 4 项：触摸（probe_ok + read_ok 计数）、摄像头（采集激活状态 + 实测 FPS）、ESP-01S 网络（`gui_get_esp01s_online()` 新接口）、LVGL 渲染线程；
  - 每项结果写系统日志（级别分级：正常=OK/异常=ERR/待机信息=INFO）；
  - 按钮点击后更新 Device 页"系统状态"标题：全过 → "自检通过"（绿），有异常 → "自检 N 项异常"（红）。
- **验证**：编译烧录通过；运行态健康（触摸 read_ok 增长/read_fail=0/probe_ok=1、LVGL=1、日志正常、自检钩子待 Device 页触发）。待实机：进设备状态页点"运行自检"，观察标题变色 + 日志页新增自检记录。
- 待办：ESP-01S 驱动上线后自检网络项变"在线"；机械臂状态回读后自检可加电机项；Pickup 数量/状态真实化（需订单数据）。

# 最新状态：药品信息库 + Pickup 取药页扫码接入（2026-08-18）

## 2026-08-18 drug_db 药品信息库 + 取药单解码

- **新增 `src/app/inc/drug_db.h` + `src/app/src/drug_db.c`**：演示药品表（4 条，与 GUI 示例一致：阿莫西林/维生素C片/布洛芬），`drug_db_lookup(payload)` 精确相等优先、子串匹配兜底（支持 URL 夹带编码）。
- **Medicine 页剩余字段填充**：剂量/批次/有效期由 `drug_db_lookup` 查表填充（未命中 "--"），名称/编码/徽章沿用最近扫码 payload。
- **Pickup 取药页接入解码**：`qr_decoder_set_enabled` 从"仅 Scan"改为"Scan 或 Pickup 激活即启用"；解码结果 → `order_card_order_number`（取药单号）、`order_badge`（"扫描成功"）、`order_title`（"取药单已识别"）实时更新。
- **修复联动缺陷**：`s_scan_has_result` 不再随页面切换清空（原逻辑导致 Scan→Medicine 跳转后详情页显示"等待扫码…"），保留最近一次识别结果。
- **验证**：编译烧录通过；运行态健康（触摸 read_ok 增长/read_fail=0/probe_ok=1、LVGL=1、日志正常）。待实机：Pickup 页扫取药单 → 单号/徽章更新；Medicine 页扫 AMOX-001 类编码 → 剂量/批次/有效期填充。
- 待办：药品库真实数据源（网页端下发/EEPROM）；Pickup 药品清单 3 行真实化（按取药单明细填充，需订单数据）；机械臂执行取药流程。

# 最新状态：Medicine 药品识别结果页真实化（扫码→详情闭环）（2026-08-18）

## 2026-08-18 扫码结果 → Medicine 详情页闭环

- 背景：Medicine（药品识别结果）页此前是 GUI Guider 硬编码假数据（"阿莫西林胶囊/DEMO-2026-001/0.25g/RA8P1-A01/2028-08"），且无任何入口。
- **入口打通**：Scan 页"开始扫描"按钮在扫码成功后动态变为**"查看详情"**（`gui_app.c` 事件钩子，不修改生成事件文件），点击跳转 Medicine 页（`gg_load_screen_animation`）。
- **Medicine 页真实化**（`gui_app_refresh_medicine_page()`，页面激活时刷新）：
  - 药品名称 = 最近一次扫码 payload（CLIP 限宽 420，防长 URL 撑破）；
  - 编码 = "编码：<payload>"；无结果时显示"等待扫码…/编码：--"；
  - 验证徽章：已识别 → "已验证"（绿），未识别 → "未验证"（灰）；
  - 剂量/批次/有效期无真实数据源（药品库未接），生成文件与设计文件占位改 "--"。
- 返回/重新扫码沿用 GUI Guider 生成事件（Medicine→Home、重新扫码→Scan）。
- **验证**：编译烧录通过；运行态健康（触摸 read_ok 持续增长、LVGL=1、日志缓冲正常）。待实机：识别页扫码 → 按钮变"查看详情" → 点击进入详情页显示 payload。
- 待办：药品数据库接入后填充剂量/批次/有效期；ESP-01S 驱动上线后 Device 网络行自动变"在线"；Store 页机械臂状态/容量真实化（需机械臂状态回读）。

# 最新状态：系统日志模块 + UI 状态动态化（Logs 页真实数据、Scan 徽章状态机）（2026-08-18）

## 2026-08-18 界面显示逻辑完善（替换生成界面的静态假数据）

- **新增系统日志模块** `src/app/inc/sys_log.h` + `src/app/src/sys_log.c`：跨线程安全环形缓冲（32 条 × 80B，短临界区保护，不影响共享 IIC0）；API：`sys_log_add(level, fmt, ...)`、`sys_log_peek()`（0=最新）、`sys_log_count()`、`sys_log_count_level()`、`sys_log_total()`；级别 INFO/OK/WARN/ERR + 累计计数。
- **日志埋点**：Camera 线程启动（INFO）、界面启动完成（OK）、触摸屏初始化成功（OK）/失败（WARN）/重连成功（OK）、摄像头初始化成功（OK）/采集启动（INFO）/采集停止（INFO）、扫码成功（OK，含内容）。
- **Logs 页真实化**（`gui_app.c` 500ms 节流，仅页面激活刷新）：
  - 4 行日志行动态填充最新 4 条系统日志：时间（开机 HH:MM:SS）/内容（截断 176px）/级别（信息·成功·警告·错误，各自着色：灰蓝/绿/橙/红）；无日志时隐藏空行；
  - 指标卡改语义：日志总数 / 成功日志数 / 异常日志数（WARN+ERR）；标题"最近取药记录"→"系统运行日志"（生成文件 + .guiguider 设计文件同步）。
- **Scan 页状态徽章状态机**：就绪（绿）→ 采集中（蓝，`camera_app_capture_active()`）→ 已识别（绿，扫码结果发布后）。
- **Device 页**：网络行（ESP-01S 在线/离线，与 Home/Admin 徽章同源，custom.c 刷新）、电机行保持"待机"（ZDT 无状态回读）、摄像头行/机械臂坐标行已有实时数据。
- **验证**：编译烧录通过；运行态实测日志缓冲已写入 3 条启动日志（INFO=1/OK=2），级别计数与内容完全吻合；触摸/LVGL 健康。
- 待办：Medicine 页跳转入口与药品数据源；Store 页机械臂状态/容量真实化（需机械臂状态回读）；ESP-01S 上线后网络行自动变"在线"。

# 最新状态：管理页卡死根因修复（LVGL 内存池耗尽 + quirc assert 死循环）（2026-08-18）

## 2026-08-18 两个"卡死"根因与修复

- 现象：① 进入管理界面（Admin/Device 等页面开多后）一段时间卡死；② 扫描药单/识别药之后再进管理页也卡死。
- **根因 A：LVGL 独立内存池仅 64KB（`LV_MEM_SIZE=0x10000`），页面懒创建后永不销毁。**
  - GUI Guider 10+ 页面全部懒创建且从不删除；进入 Admin 时 `setup_Admin` 一次性创建最多控件（卡片/行/标签），此时 Boot/Home/Login 等页面仍存活，池压力达峰值。
  - 加上每 500ms 的 FPS 标签/网络徽章文本分配释放，池碎片化 → 大块分配返回 NULL → LVGL `LV_ASSERT_MALLOC` 触发但 `LV_ASSERT_HANDLER` 被配置为空（只 LOG 不 halt）→ 调用方继续用 NULL → **HardFault → Default_Handler while(1) 整机卡死**。
  - 修复：`LV_MEM_SIZE 0x10000 → 0x40000`（64KB → 256KB），`configuration.xml` 与 `ra_cfg/fsp_cfg/lvgl/lvgl/lv_conf.h` 同步；SRAM 充足（链接后 BSS 布局验证通过）。
- **根因 B：quirc 内部 assert 在 Debug 构建激活 → 扫码时整机卡死。**
  - `third_party/quirc/lib/identify.c` 的 flood-fill 有 3 处 `QUIRC_ASSERT`（= C `assert`）。Debug 构建未定义 NDEBUG，assert 激活；一旦某帧图像（模糊/遮挡/极端视角）触发内部不变量，FSP 精简 `__assert_func` 执行 **`__BKPT(0)` + `while(1)`** —— BKPT 无调试器时触发 HardFault → `Default_Handler` 死循环，**整个系统卡死**（此后进任何页面都卡）。
  - 修复：CMake 对 quirc 4 个源文件加 `COMPILE_DEFINITIONS NDEBUG`（上游 release 构建同样不带 assert；quirc 的 assert 只是调试辅助，失败路径有 QUIRC_ERROR_* 返回值兜底）。
- **验证**：编译通过、烧录（~935KB）成功；运行态健康（`s_lvgl_ready=1`、触摸 read_ok 增长/read_fail=0、`s_quirc_ready=0` 懒初始化未触发）；LVGL 池 256KB 链接生效（`work_mem_int` 数组 BSS 就位）。
- 待办：实机验证——多开页面（Boot→Home→Login→Admin→Store/Logs/Device）不再卡死；扫码模糊/遮挡时不再死机；同时注意 quirc 解码仍限频 5Hz，若预览掉帧可调大 `QR_DECODE_INTERVAL_TICKS`。

# 最新状态：quirc 二维码解码正式接入识别页 + ESP-01S 协议草案（2026-08-18）

## 2026-08-18 quirc 二维码解码移植（Scan 识别页）

- **库**：`third_party/quirc/quirc-master/`（MIT，纯 C，无浮点依赖以外外部依赖）已随 CMake 编译（quirc.c/identify.c/decode.c/version_db.c）。
- **内存适配**：板端 C 库 heap 禁用（`BSP_CFG_HEAP_BYTES=0`），quirc 的 `malloc/calloc/free` 在 `quirc.c` 顶部宏重定向到 **FreeRTOS heap_4**（`configTOTAL_HEAP_SIZE=256KB`）。320x240 灰度图像+flood-fill 工作区约 90KB，实测分配成功。
- **模块**：`src/middleware/qr_decoder.{h,c}`：
  - 同步底层 API `qr_decoder_decode_gray()`（单帧灰度直解）；
  - 流式 API `qr_decoder_process_frame()`：Camera 线程喂 UYVY422 帧 → 提取 Y 灰度 → **限频 5Hz**（200ms 间隔，避免拖垮预览）→ **连续 2 帧相同 payload 才发布**（去抖）；`qr_decoder_set_enabled()` 仅 Scan 页启用（Pickup 页不占 CPU）；结果经短临界区发布，LVGL 线程 `qr_decoder_get_result()` 读取。
- **接入**：`Camera_thread_entry.c` 每帧处理时调用 `qr_decoder_process_frame()`（与预览共用同一帧）；`gui_app.c` 轮询结果更新 Scan 页 `control_panel_result` 标签（固定宽 112 + WRAP，防长 URL 撑破 136px 侧栏）。
- **验证**：编译通过、烧录（935936 B）成功；进入识别页后 `s_quirc_ready=1`（quirc 懒初始化成功）、解码开关随页面启停；触摸/LVGL 运行健康。待实机：手机二维码（或药品码）对准摄像头验收识别与结果显示。
- 注意：quirc 解码在 Camera 线程同步执行（每次 ~几十 ms），限频 5Hz 下占用可接受；若实机发现预览掉帧明显，可把 `QR_DECODE_INTERVAL_TICKS` 调大。

## 2026-08-18 ESP-01S ↔ 网页端通信协议定稿 v1.0

- 见 `docs/ESP01S_通信协议.md`（定稿）：物理层 SCI9（P208/P209，115200 8N1）或 SCI8（P500/P501）**二选一**（当前 `RA8P1_CPKHMI.pincfg` 已复用 SCI9，**UART 栈实例待 RASC 添加**）、传输层推荐 TCP 长连接+JSON 行（备选 HTTP 轮询）、应用层消息模板（hello/heartbeat/scan/pickup/stock/log/alarm/arm/resp 上报，hello/ack/sync/restock/dispense/drugdb/config/timesync/reboot/arm 下发），含错误码/告警码/取药单二维码内容格式与固件+网页端实现清单。
- 旧草案 `docs/ESP01S_通信协议草案.md` 已标注被定稿取代。
- 结论：**协议已可交付开发**（网页端可并行开发），消息字段与 UI/库存/药品库数据模型对齐，与 ESP-01S AT 细节解耦。

# 最新状态：摄像头真实 FPS 实时显示（Scan 识别页 + Device 管理页）（2026-08-18）

## 2026-08-18 实测帧率接入 UI（替换写死 "15 FPS"）

- 背景：Scan 页 `control_panel_fps`（"15 FPS"）与 Device 页 `device_camera_row_value`（"就绪 / 15 FPS"）是 GUI Guider 里的写死文本，与真实帧率（约 9.9 FPS @ XCLK 12.5MHz）不符。
- **FPS 统计**（`src/app/src/camera_app.c`）：以 CEU FRAME_END 中断计数 `s_dbg_ceu_frame_cnt`（ISR 内递增，最真实的采集帧率源）为基准，在 `camera_app_service_capture()` 每次调用（Camera 线程 ~1ms 周期）时推进 1 秒滑动窗口，输出 **×10 定点**（99 = 9.9 FPS）：
  - `camera_app_get_fps_x10()`：采集激活期间的实时值，停止后为 0；
  - `camera_app_get_last_fps_x10()`：最近一次有效实测值，采集停止后仍保留（供管理后台显示"上次"）。
  - 时间差用 `portTICK_PERIOD_MS` 换算，不依赖具体 tick 频率；tick/帧计数均用无符号差，回绕安全。
- **UI 刷新**（`src/app/src/gui_app.c`，`gui_app_poll()` 内每 500ms 节流一次）：
  - Scan 页：采集激活 → `"9.9 FPS"` 实时；未激活 → `"待机"`；
  - Device 页：采集激活 → `"就绪 / 9.9 FPS"`；未激活但有历史 → `"就绪 / 上次 9.9 FPS"`；无历史 → `"就绪 / 待机"`。
  - 刷新逻辑放在 gui_app.c（固件侧，模拟器不编译该文件），不影响 GUI Guider 模拟器构建。
- **写死文本同步修正**：`gui/RA8P1/generated/screens/gg_Scan.c`、`gg_Device.c` 与 `gui/RA8P1/RA8P1.guiguider` 设计文件中的 "15 FPS" 均改为 "待机"（首帧不再闪现错误数字；GUI Guider 重新生成代码也不会带回 15 FPS）。
- **编译/烧录/运行验证（2026-08-18）**：固件编译通过并烧录（919552 B，Program/Verify OK）；运行态：相机按需未激活（`s_dbg_ceu_frame_cnt=0`、`s_camera_fps_x10=0`，符合预期）、触摸健康（`read_ok=1104` 持续增长、`read_fail=0`、`probe_ok=1`）、`s_lvgl_ready=1`。
- 待办：实机进入 Scan 页确认 FPS 标签显示约 9.9 且 1 秒刷新一次；Device 页确认"上次 X.X FPS"。

# 最新状态：文字显示根因修复（LVGL 9.3 stride bug）+ 全字体加粗加大（2026-08-18）

## 2026-08-18 根因：LVGL 9.3.0 lv_font_fmt_txt.c stride 计算 bug（"始"下方出现"存"的总根因）

- 现象（长期未决）："开始扫描"按钮"始"字下方出现"存"；标题/大字偶发"乱码"、模拟器正常但实机异常。
- **根因**：板端编译的 FSP LVGL **9.3.0** `lv_font_fmt_txt.c` 中 glyph 行 stride 计算有上游 bug：
  `dsc_out->stride = LV_ROUND_UP(dsc_out->box_w, fdsc->stride)` —— 把 **box_w（像素）** 当 **字节** 用。
  对 bpp=4 字库，正确公式是 `LV_ROUND_UP(ceil(box_w*4/8), stride)`（上游 [PR #8887](https://github.com/lvgl/lvgl/pull/8887) 在 9.4.0 修复）。
  后果：凡是 box_w > 16px 的字形（16px 字库 10 个、18px 字库 14 个、22px 字库 43 个、34px 字库 49 个……），解码时每行跳过 32 字节、实际数据每行 16 字节 → 隔行错位并**把下一个字形的数据画进当前格**。"始"(0x59CB) 的下一字形恰好是"存"(0x5B58)，所以"始"格内出现"存"。
- 模拟器正常的原因：GUI Guider 自带 platform/lvgl 是 **9.4.0**（已修复该 bug）；板端 ra/lvgl 是 9.3.0（未修复）。
- **修复**：`ra/lvgl/lvgl/src/font/lv_font_fmt_txt.c` 移植上游 9.4 公式：
  ```c
  uint32_t bit_count = dsc_out->box_w * fdsc->bpp;
  uint32_t width_in_bytes = (bit_count + 7) >> 3;
  dsc_out->stride = LV_ROUND_UP(width_in_bytes, fdsc->stride);
  ```
  已用脚本模拟验证：修复前 17px 字形按 32B/行读（隔行+串下一字形），修复后按 16B/行读（正确）。
- 注意：ra/ 为供应商目录，该改动是**必需的上游 bug 移植**（RASC 重新生成不会覆盖 ra/lvgl，但 FSP 升级后需复查）。

## 2026-08-18 全字体加粗加大（SourceHanSerifSC-Bold + 字号 +2px）

- 字库源从 GUI Guider 常规 `SourceHanSerifSC.otf` 换成 **粗体** `SourceHanSerifSC-Bold.otf`（官方 Adobe 下载，25.5MB，保存在 `gui/RA8P1/resources/font/`）。
- 全部字号 **+2px**（13→15、14→16、15→17、16→18、18→20、19→21、20→22、21→23、22→24、24→26、26→28、28→30、34→36），旧字号字库文件（13/14/19/34）已删除，gg_font.h 同步清理。
- `gui/tools/sync_generated_chinese.js` 升级为自动完成：设计 JSON text_size +2、生成屏幕字体引用 +2、Bold 源生成新字库、清理过期字库。
- UI 适配：`custom.c` 为 Admin 三个卡片副标题（store/logs/device）增加双行 WRAP 配置（156px 宽装不下 16px 的 10 个汉字，改两行）；其余固定框经逐项核算（行高 vs 框高）全部放得下，无需改尺寸。
- **编译/烧录/运行验证（2026-08-18）**：固件编译通过并烧录（952320 B，Program/Verify OK）；运行态健康：`probe_ok=1`、`g_cst816s_read_ok` 294→755 持续增长、`read_fail=0`、`s_dbg_camera_init_attempts=0`（摄像头按需未启动）、`s_lvgl_ready=1`。GUI Guider 模拟器已重新配置+重建（原构建缓存引用已删除的旧字库 13/14，需重新 configure）。
- **脚本幂等性**：`sync_generated_chinese.js` 只做"Bold 源重生成字库+字符集收集+清理过期字库"，**不再 bump 字号**（一次性 +2 已落在设计文件和屏幕里）；重复运行零变化（已验证 fonts/design 均无 diff）。
- 工具脚本已归档到 `tools/`：`dump_font_glyph.js`（字形位图 ASCII 渲染）、`sim_stride.js`（9.3/9.4 双解码对比）、`check_font_coverage.js`（字库覆盖检查）、`verify_stride_fix.js`（stride 修复越界验证）、`analyze_sizes.js`（设计字号分析）。
- 待办：实机验收加粗加大界面（"始"字异常应随 stride 修复消失）；真实 FPS 接入设备状态页；quirc QR 解码正式循环；一维条码库（ZXing-C++/ZBar）评估。

# 最新状态：按需采集全链路实测闭环（2026-08-17）

## 2026-08-17 按需采集自动测试（RA8P1_CAMERA_AUTOTEST）实测结果

- 无人值守自动测试（`RA8P1_CAMERA_AUTOTEST=ON`，模拟 t+5s 进入 Pickup/Scan、t+15s 离开，默认 OFF）：
  - 请求后：`s_dbg_camera_init_attempts=1`、fail_step=0（初始化恰好一次、成功）；
  - 采集期间：`s_dbg_ceu_frame_cnt=97`（10 秒 ≈9.7FPS）；
  - 采集期间触摸：`g_cst816s_read_ok` 650→872 持续增长（分时复用无冲突）；
  - 离开后：帧计数冻结（0x61 不再变）、IIC0 SDAI=1/BBSY=0（模块下电、总线释放）、触摸继续健康。
- 正式固件已恢复（AUTOTEST=OFF），主页空闲态：attempts=0、模块下电、触摸轮询健康、LVGL 正常。
- **目标达成**：屏幕与摄像头均跑通；摄像头仅在 LVGL Pickup/Scan 页面激活时开启（gui_app_poll 请求→service_capture 初始化采集），离开即停止并下电；空闲零占用（无 IIC0 流量、模块零功耗）。

# 最新状态：摄像头采集期触摸根因修复（预览临界区），待实机验收（2026-08-17）

## 2026-08-17 采集期触摸失灵根因：camera_preview_put_frame 临界区

- 现象：进入 Pickup/Scan 页、摄像头采集+预览显示后触摸失灵。实时诊断：IIC0 SDAI=0（SDA 低）、BBSY=1、read_fail 暴涨、read_ok 冻结；摄像头采集与 LVGL 刷新正常。
- **根因**：`camera_preview_put_frame()` 用 `taskENTER_CRITICAL()` 包裹 480×360×2B≈345KB memcpy——Cortex-M85 上关全部中断 1~2ms，**打断进行中的 IIC0 事务**（外设保持 SDA 低），共享总线触摸读取连锁失败。自动测试窗口短未触发，真实场景（预览持续重绘）必现。
- **修复**：① 移除 put_frame 临界区（预览允许撕裂帧，`s_new_frame` 标志保序）；② 触摸读取连续失败时自动执行 `camera_i2c_recover()`（Abort+Close+Open+9 脉冲 GPIO 恢复）兜底 MCU 侧 IIC 卡死。
- 待办：实机验收摄像头采集期触摸；"开始扫描"按钮"始"字下方出现"存"字（16 号字库同时含两字，疑似字形映射错位或控件重叠，待查）；真实 FPS 接入设备状态页。

# 最新状态：CST816S 闩锁根因修复（总线静默保护），待扫描页实机验收（2026-08-17）

## 2026-08-17 触摸闩锁根因与总线静默保护

- **现象**：进入 Pickup/Scan 页后触摸失灵。实测：摄像头采集正常（~10FPS）、LVGL 正常刷新，但触摸读取全失败（read_ok 冻结、read_fail 暴涨、I2C Abort 激增）；IIC0 SDAI=0（SDA 被拉低）、BBSY=1。
- **归因实验**：扫描页状态下 J-Link 强制 PWDN=1，SDA **仍低** → 拉低者不是 OV7725，而是 **CST816S 被闩锁**（主页时模块下电从不触发；进扫描页→OV7725 上电瞬间 SIO_D 毛刺被触摸芯片误判为 I2C START → 状态机闩锁，只有断电可恢复）。
- **修复**：`camera_power_on()`/`camera_power_off()` 的 PWDN/RST 切换期间，把共享 P409/P410 临时切为 GPIO 强输出低（总线静默，钳制任何 START 毛刺），切换完成并等模块稳定（50ms）后再恢复 IIC 复用。
- **验证（自动测试 30 秒采集窗口实时采样）**：采集期间 IIC 总线全程空闲（SDAI=1/BBSY=0）、触摸 read_ok 279→449→563 持续增长、read_fail≈3 几乎为零；离开瞬间小量失败（~90 次）已通过 power_off 同样加保护消除。
- 待办：用户实测进扫描页——触摸可点返回、预览画面显示（"蓝色矩形"=camera_preview 容器背景，图像显示问题另查：疑似 LVGL image cache 或 lv_image 重绘链路，需在实机确认）。

# 最新状态：摄像头空闲下电根治 SDA 占线，触摸恢复健康（2026-08-17）

## 2026-08-17 空闲下电修复（SDA 拉低根治）

- **Bug3：模块"上电未初始化"态拉低共享 SDA**。按需化后模块保持 PWDN=0/RST=1+有 XCLK 但未做 SCCB 初始化，运行一段时间后把共享 IIC0 SDA 拉低（实测 SDAI=0、BBSY=1），触摸读取从健康（read_ok 增长）转为全失败（read_fail 暴涨 4144、I2C 回调停止）。
- **归因实验**：J-Link 直接写 POSR 使 PWDN=1 → SDAI 瞬间 0→1、总线释放 → 拉低者确认为 OV7725。
- **修复**：① Camera 线程 GUI 模式启动时只执行 `camera_power_off()`（PWDN=1/RST=0，SIO_D 高阻、模块零空闲功耗、不碰 IIC0/不开 XCLK——避免 camera_app_init 二次 Open 报 IN_USE）；② `camera_app_service_capture()` 停止采集时追加 `camera_power_off()` 并 `s_camera_initialized=false`（下次进页完整重新初始化）。
- **实测（主页 15 秒窗口）**：IIC0 SDAI=1/BBSY=0 全程空闲；触摸 read_ok 337→679（+342/15s）持续健康；read_fail 不再暴涨；attempts=0（摄像头零初始化）；LVGL flush 正常。
- 待办：用户实测点击进入 Pickup/Scan 页 → 摄像头上电初始化（attempts=1）、灰度预览、触摸共存；离开页面后模块下电、总线空闲。

# 最新状态：摄像头按需采集落地（主页零占用），待扫描页实机验收（2026-08-17）

## 2026-08-17 按需采集修复（两处真实 bug）

- **Bug1：GUI 模式启动即请求采集**。`Camera_thread_entry.c` 的 `CAMERA_CAPTURE_ON_DEMAND` 分支原先执行 `camera_app_request_capture(true)`，导致开机即初始化摄像头并占用共享 IIC0（实测 attempts=1），与"按需"矛盾。已改为 `request_capture(false)`，采集请求完全由 `gui_app_poll()` 在进入 Pickup/Scan 页时发出、离开时撤销。
- **Bug2：摄像头不初始化后 SDA 被拉死**。按需化后无人执行 OV7725 上电时序，P709/P710 保持复位默认输出低，OV7725 停在 RST=0 状态把共享 IIC0 SDA 拉低（实测 SDAI=0、BBSY=1），触摸探测 27 次全失败且无 I2C 回调。修复：GUI 模式启动时执行"安全上电" `camera_xclk_init()+camera_power_on()`（XCLK 输出 + PWDN=0/RST=1，**无任何 SCCB 事务、无 CEU**），既保持按需又不占总线。
- 实测（主页）：`s_dbg_camera_init_attempts=0`、`frame_cnt=0`、`s_lvgl_ready=1`、`g_cst816s_probe_ok=1`、IIC0 互斥/超时 0、触摸读回调 24/22 次正常。
- **空闲 30 秒持续观察**：`s_dbg_camera_init_attempts` 与 `s_dbg_ceu_frame_cnt` 全程保持 0——主页空闲时摄像头零初始化、零采集（"点击才开启"的反向证据）。
- 待办：用户实测点击进入 Pickup/Scan 页 → 摄像头初始化（attempts=1）、屏幕预览出现、触摸与摄像头分时复用无冲突；离开页面采集停止。

# 最新状态：摄像头 10FPS 定案，恢复完整 GUI 固件（2026-08-17）

## 2026-08-17 定案与 GUI 恢复

- **XCLK 正式定为 12.5MHz**（模块唯一可靠工作点，9.9FPS）；14.7~25MHz 全部实测不启动，VTS/COM5 寄存器调优无效（详见硬件总览 A8 段）。帧率 0.8 FPS/MHz 线性，当前模块物理上限约 10FPS，对二维码扫描足够。
- **IIC0 屏幕触摸/摄像头冲突 = 分时复用**：`camera_drv.c` 的 `s_i2c_bus_mutex` 互斥串行化全部事务（取锁→切地址 CST816S=0x38/OV7725=0x21→读写→恢复 0x21→放锁），配合超时 Abort 与 9 脉冲 GPIO 恢复；GUI 固件回归验证该路径。
- **二维码/条形码**：quirc 仅支持 QR（2D）；一维条码（EAN/UPC/Code128）需新库，候选 ZXing-C++（Apache-2.0）或 ZBar，后续单独立项，不阻塞 QR 主线。
- 已切换完整 GUI 固件（RTT-only=OFF、GUI=ON、12.5MHz），恢复屏幕+触摸+摄像头按需采集流程。
- GUI 固件实机诊断：`s_lvgl_ready=1`、`g_st7701s_init_error=0`、`g_cst816s_probe_ok=1`、LVGL flush 35 次 0 错误、IIC0 互斥/超时 0 —— 屏幕/触摸/摄像头共线固件整体健康；待用户实测触摸四角与 Pickup/Scan 页按需预览。

# 最新状态：摄像头链路已恢复！CEU 采帧 + RTT 预览运行中（2026-08-17）

## 2026-08-17 里程碑：摄像头恢复采帧（BTB 重新插拔 + 12.5MHz XCLK）

- 用户重新插拔自绘板↔核心板 BTB 座子后，PCLK/HREF 两路接触恢复；烧录 12.5MHz 正式基线后 **CEU 帧计数持续增长（147→177 帧，约 7~8 FPS）**，RTT 预览缓冲出现真实图像数据（`81808382 8381817F ...`），PCLK 跳变 18523 次/HREF 25 次/VSYNC 3 次，`tools/rtt_camera_viewer.ps1` 预览窗口运行中。
- 根因链条回顾：① 模块时钟链只在约 12~13MHz 工作（25MHz 不启动）→ XCLK 正式基线改为 12.5MHz（configuration.xml period=12500kHz + ra_gen period_counts=0x14）；② PCLK/HREF 座子到 MCU 两路接触不良（模块端有信号、MCU 端恒高）→ BTB 重新插拔解决。
- 工具地址已同步：`_SEGGER_RTT=0x220002C0`、`s_rtt_preview=0x22041D80`（rtt_camera_viewer.ps1 默认值已更新）。
- 待办：① 用户确认 RTT 预览画面正常；② 恢复屏幕/触摸共用 IIC0 回归（RTT-only 关闭、GUI 开启、CST816S+OV7725 共线互斥）；③ GUI Pickup/Scan 扫描页预览；④ quirc 正式解码循环。

# 最新状态：摄像头模块确认存活，断点收敛到 PCLK/HREF 两路线（2026-08-17，优先级最高）

## 2026-08-17 摄像头突破：12.5MHz XCLK 下模块全速输出

- 慢速 XCLK 体检（`RA8P1_CAMERA_SLOW_XCLK=ON`，先 25MHz 完成初始化再降频）实机确认：**模块引脚端 PCLK≈1.4V 波动、VSYNC≈3.2V、HREF≈1.57V，模块在 13MHz 下全速输出视频时序**；而 25MHz 下模块端 HREF/VSYNC 仅 0.21/0.42V（不启动）。该模块时钟链只在约 12~13MHz 工作。
- **正式基线 XCLK 已改为 12.5MHz**：`configuration.xml` period 24000→12500 kHz，ra_gen `period_counts` 0xA→0x14（RASC 重新生成待补）。
- 逐路核对结论：SCCB（P409/P410）、XCLK（P109）、VSYNC（P708）、D0~D7、PWDN/RESET **全部连通**；**唯独 PCLK/P414 与 HREF/P415 两路“模块端有信号、MCU 侧收不到”**（J-Link 45 次端口采样恒高、0 跳变）。模块插自绘板座子、引脚按模块标准定义无错。
- 下一步（用户硬件操作）：重新插拔模块与自绘板↔核心板 BTB → 断电蜂鸣定位 PCLK/HREF 断点（座子↔BTB 或 BTB↔MCU）→ 飞线/补焊；之后烧录 12.5MHz 正式基线验证 `s_dbg_ceu_frame_cnt` 递增 → 恢复 RTT 预览 → 屏幕/触摸共用 IIC0 回归。

# 最新状态：接盘整理后摄像头根因收敛为模块端并行输出缺失（2026-08-17，优先级最高）

## 2026-08-17 接盘整理：恢复官方基线与 XCLK 实机确认

- 工作区曾残留多个 docs 已声明回退的 A/B 实验（COM7=0x46、CLKRC=0x80、CEU dsel/hdsel/vdsel=1），与官方基线及 `configuration.xml` 漂移。本轮已统一恢复为官方基线：**COM7=0x40（QVGA|YUV422）、CLKRC=0x00、CEU 边沿 rising（dsel/hdsel/vdsel=0，与 `configuration.xml` 一致）**；`ra_gen/Camera_thread.c` 的手工 A/B 已还原为 XML 一致的生成值（本机 RASC 无 RA8P1 device pack，无法 headless 重新生成前的临时处理）。
- 保留 codex 的有用诊断增量：同步脚跳变采样（`camera_diag_sample_sync_pins` + `s_dbg_pin_{xclk,pclk,href,vsync}_{toggles,high,low}`）、GPT10 `R_GPT_OutputEnable`、彩条诊断宏（默认关闭）、CMake 宏按模式单一定义整理、J-Link 诊断脚本地址同步。
- `git diff HEAD --check` 与 `tools/rasc_config_audit.ps1 -Profile tools/rasc_config_profile.json` 审计均通过；Debug（RTT-only）全量编译链接成功并烧录，Program/Verify 通过（`Writing target memory failed` 仍为 J-Link 尾部历史告警）。

### 本轮实机运行态证据（J-Link 读取）

| 项目 | 读数 | 结论 |
| --- | --- | --- |
| OV7725 PID / VER | 0x77 / 0x21 | SCCB 通路正常 |
| COM7 / CLKRC | 0x40 / 0x00 | 官方基线已生效 |
| COM2 | 0x00 | **无软睡眠，视频输出未被寄存器门控** |
| COM3 / COM10 / COM12 | 0x50 / 0x02 / 0x03 | 与基线一致 |
| DSPAUTO / DSP_CTRL1 / DSP_CTRL3 | 0xFF / 0xFF / 0x00 | 彩条关，DSP 通路寄存器正确 |
| GPT10 GTCR / GTCNT / GTPR / GTIOR | 1 / 计数中 / 9 / 1 | 计数器运行 |
| **P109 XCLK 跳变** | 紧循环 0x4127 次；高 47 / 低 53；PORT1 PIDR bit9 快照在变化 | **XCLK 确实在 P109 翻转（约 25 MHz）** |
| PCLK(P414) / HREF(P415) / VSYNC(P708) 跳变 | 全部 0 | 无并行视频时序 |
| P414/P415/P708 直接 PIDR vs 计数器 | 直接读低/低/低、计数器恒高 | **三线悬空无驱动特征**（PIDR 与 PCNTR2 采样矛盾） |
| CEU CAPSR / CAMCR / CMCYR / CSTSR | 1 / 0x10(JPG=1,DSEL=0) / 0 / 0 | CEU 启动干净，边沿 A/B 已还原 |
| CEU frame_cnt / 事件 / 双缓冲 | 0 / 全 0 / 全 0 | 未收到帧 |
| IIC 写入 31 次、读失败 0、恢复计数 0 | 全部通过 | 共享 IIC0 无异常 |

### 结论与下一步

- 软件侧已穷尽且全部排除：传感器可寻址、寄存器可写可读、无软睡眠、XCLK 在 MCU 引脚确认翻转、CEU 启动无错、格式/边沿/VSYNC 极性/彩条/复位时序 A/B 均无效；保存的历史成功 SREC（61ec657）在同一块板上复测同样无同步输出。
- 根因收敛为 **OV7725 模块端并行视频输出（PCLK/HREF/VSYNC 及 D0-D7）没有到达 RA8P1 的 P414/P415/P708 输入脚**：可能是模块端无输出、排线/转接板引脚映射与官方 P8 定义不一致、或模块供电/复位状态问题。
- 下一步必须做端到端波形测量：用示波器/逻辑分析仪同时测 **模块连接器侧**与 **RA8P1 侧**的 `P109(XCLK)、P414(PCLK)、P415(HREF)、P708(VSYNC)`；并核对实际购买模块的 20pin/排线定义与 P8 官方映射（PCLK=P414、HREF=P415、VSYNC=P708）是否一致。在取得波形证据前，不再随机叠加 OV7725/CEU 寄存器修改。
- 手头只有万用表时的排查顺序、期望电平与结果反馈格式见[硬件总览](RA8P1_硬件总览.md)顶部「摄像头万用表排查清单」；用户按清单测量后把数值发回，再据实继续定位。
- 摄像头帧计数恢复后再按 docs 顺序回归：RTT 预览 → 屏幕/触摸共用 IIC0 → GUI 扫描页预览 → quirc 解码循环。

# 最新状态：CST816S 触摸已实测，正在恢复正式 LVGL 输入链路（2026-08-16，优先级最高）

## 2026-08-16 最新实机反馈：触摸坐标方向已修正，扫描页摄像头故障增加熔断保护

- 用户实测确认 CST816S 已能正常产生触摸数据，原先的通信故障已经解除；剩余问题是触摸命中位置与画面呈整体 180° 对称。
- LVGL 回调已由 `logical_x=raw_y`、`logical_y=479-raw_x` 修正为 `logical_x=639-raw_y`、`logical_y=raw_x`。这是对 480×640 原始触摸坐标到 640×480 横屏画布的整体验证后反向修正，不改变 IIC 地址、寄存器协议或显示驱动。
- 进入扫描页会请求摄像头按需初始化。为避免摄像头初始化失败后每秒重复访问共享 IIC0，现增加初始化前/后的 SDA、SCL、BBSY 空闲检查；失败后在当前扫描页熔断，离开扫描/取药页后才清除熔断状态并允许下一次尝试。
- 新增 J-Link 诊断量：`s_dbg_camera_init_blocked`、`s_dbg_camera_init_fail_step`、`s_dbg_camera_init_attempts`。失败步骤含义：`1=IIC打开失败`、`2=初始化前总线非空闲`、`3=摄像头上电/复位后总线非空闲`、`4=OV7725寄存器初始化失败`。
- 本轮 `mingw32-make -C build\\Debug -j4` 已通过，`git diff --check` 已通过。烧录后应先验证触摸四角，再读取上述摄像头诊断量；不要在诊断量异常时反复点击扫描页。
- 进一步确认：进入扫描页后触摸立即失效，实测 SDA 被永久拉低、SCL仍高，说明摄像头初始化失败后外部器件或未完成的 IIC 事务占住了 SDA；不能用 GPIO 强推 SDA 高电平。
- 已增加失败清理链：摄像头初始化失败或上电后总线异常时，执行 `PWDN=1`、`RST=0`，随后对 IIC0 执行 Abort、Close、Open，并在恢复后再次检查 `SDAI/SCLI/BBSY`；当前页面不再重复初始化。该版本已编译并尝试烧录，J-Link 仍有历史性的 `Writing target memory failed` 提示，需以整板断电后的实测电平确认最终效果。
- 针对“断电/带电复位后 SDA 仍永久为低”的实测，已加入标准 GPIO 总线恢复：关闭 IIC0 后临时把 P409/P410 切为 GPIO 开漏模拟线，输出最多 9 个 SCL 脉冲，随后发送 STOP，再恢复 IIC 外设复用并重新打开 IIC0。恢复过程从不强推 SDA 高电平，只依赖外部上拉释放总线；新增 `s_dbg_i2c_recovery_calls/success/pulses` 诊断计数。

## 2026-08-16 阶段归档：暂停屏幕，优先修复摄像头 RTT 预览

### 已完成

- CST816S 已能通信，触摸坐标映射已修正为 `X=639-raw_y`、`Y=raw_x`，实机主页按钮命中正确。
- RASC 目标、FSP、MIPI 1 Lane、SDRAM、IIC0 P409/P410 配置审计通过。
- 已实现共享 IIC0 的 FreeRTOS 互斥、地址切换、事务超时、Abort/Close/Open 和 GPIO 9 脉冲恢复。
- 摄像头已改为按扫描页请求初始化，并增加初始化前后总线空闲检查、失败熔断、摄像头下电/复位清理。
- Debug ARM 工程可全量编译链接；最新保护版已生成 S-record 并尝试烧录。

### 尚未解决

- 实机进入扫描页后 SDA 仍被拉到 0V，SCL保持高电平；普通断电重启、MCU reset、IIC 外设重建和当前 9 脉冲恢复均未释放该状态。
- 因此当前不能继续把屏幕触摸和摄像头放在同一条 IIC0 上联调；屏幕触摸暂时冻结，下一阶段先单独恢复 OV7725 的 XCLK、PWDN/RST、SCCB 初始化和 CEU/RTT 数据链路。
- J-Link Commander 每次下载末尾仍报告 `Writing target memory failed`，虽然 Program/Verify 阶段完成；后续摄像头验证必须同时读取最新 map 地址和运行计数，不能只依据该提示判断。

### 下一阶段任务

1. 让摄像头初始化完全独立：失败时不触碰 CST816S 可用的 IIC0 状态，并记录 OV7725 PID/VER、每个初始化阶段和 FSP 错误。
2. 单独验证 OV7725 的 PWDN/RST/XCLK 与 SCCB 地址 `0x21`，确认初始化表第一笔写入是否成功。
3. 逐层验证 CEU 开启、帧结束中断、DMA 缓冲区和 RTT RGB565 发送；先恢复稳定预览，再恢复屏幕共线。

## 当前硬件事实：触摸控制器

- 屏幕资料中的触摸验证固件使用 CST816S 驱动；当前以 CST816S 为最终软件协议，不再以此前的 CST826 假设为准。
- 实测触摸控制器 7 位 I2C 地址为 `0x38`，不是默认尝试的 `0x15`。
- CST816S 寄存器：芯片 ID `0xA7`、固件版本 `0xA9`、触摸数据从 `0x01` 开始读取；当前独立轮询已验证 `probe_ok=1` 且 `detected_address=0x38`。
- 触摸与 OV7725 共用 RA8P1 IIC0：P409/SDA、P410/SCL；OV7725 地址为 `0x21`，软件通过互斥串行访问。
- TP_INT/TP_RST 当前未接；独立轮询不依赖 TP_INT，但没有 TP_RST 时无法由 MCU 对触摸控制器做硬件复位。

## 2026-08-16 CST816S 实测与 LVGL 接入

- 独立轮询已读取到有效按压：`pressed=1`、`fingers=1`、`X=229`、`Y=354`。
- 当前触摸地址为 `0x38`，探测成功；说明 CST816S 的供电、IIC0、地址和 `0x01` 数据寄存器协议已经打通。
- 已移除 CMake 中的 `TOUCH_STANDALONE_TEST=1` 隔离定义，恢复 `LVGL_thread_entry.c` 创建 `lv_indev` 并注册 CST816S 轮询回调；Camera 线程不再停留在临时触摸测试循环。
- LVGL 逻辑画布为 `640×480`，CST816S 原始坐标为 `480×640`；实机确认触摸通信正常但命中点整体呈 180° 对称，现已修正为 `logical_x=639-raw_y`、`logical_y=raw_x`。
- 本阶段尚未重新烧录；下一步为 Debug 编译、烧录、验证按压状态、四角坐标和页面按钮命中，再继续摄像头/屏幕共用 IIC0 的运行回归。

## 软件运行流程（正式 LVGL 路径）

1. `ra_gen/main.c` 进入 BSP/FreeRTOS 启动流程，RASC 生成的 `Camera_thread`、`LVGL_thread`、`Motor_thread` 按配置创建；优先级保持 Motor=3、Camera=2、LVGL=1。
2. `LVGL_thread_entry()` 先调用 `lv_init()`，再由 `RM_LVGL_PORT_Open()` 打开 GLCDC、MIPI DSI 和 LVGL Port。屏端使用原生 `480×640` 扫描缓冲，LVGL 使用 `640×480` 逻辑画布，完成帧由自定义 flush 回调旋转后提交给 GLCDC。
3. MIPI `POST_OPEN` 回调发送 ST7701S 初始化命令；初始化成功后 `gui_app_init()` 创建 GUI Guider 页面，`gui_app_poll()` 和 `lv_timer_handler()` 在 LVGL 线程中持续运行。
4. LVGL 初始化完成后，调用 `cst816s_touch_init()` 探测 `0x38`，创建一个绑定到 `g_lvgl_port_ctrl.p_lv_display` 的 `lv_indev`。LVGL 每次读取输入时调用 `cst816s_lvgl_read_cb()`，驱动通过共享 IIC0 读取 CST816S 的 `0x01` 数据包。
5. CST816S 原始坐标为 `480×640`，回调映射为横屏逻辑坐标：`logical_x=639-raw_y`、`logical_y=raw_x`，再交给 LVGL 的按钮命中、页面切换和拖动逻辑。TP_INT/TP_RST 未接时使用轮询，不依赖中断。
6. `camera_drv.c` 为 OV7725 `0x21` 与 CST816S `0x38` 共用 IIC0 提供同一把 FreeRTOS 总线互斥锁；一次事务包含“获取锁→切换从机地址→完整读写→恢复 OV7725 地址→释放锁”，因此 LVGL 输入回调不能与摄像头 SCCB 并发访问。
7. `Camera_thread_entry()` 正式模式只维护 USB/RTT 诊断和摄像头应用状态；由于 `CAMERA_CAPTURE_ON_DEMAND=1` 且 RTT 默认关闭，离开 Pickup/Scan 页面时不采集，进入需要预览的页面后由 GUI 应用显式请求采集。
8. `Motor_thread` 独占 ZDT/CAN 机械臂执行队列，LVGL 只发布页面请求和显示状态，不直接发送电机报文；这样触摸、显示刷新或摄像头异常不会阻塞电机安全线程。

### 本阶段清理结果

- 已移除 CMake 的 `TOUCH_STANDALONE_TEST=1` 定义和 Camera 线程中的独立触摸死循环。
- 已关闭默认 RTT 摄像头采集；`tools/rtt_camera_viewer.ps1` 和 `tools/jlink_touch_status_only.jlink` 仍保留为显式诊断工具，不参与正式固件运行。
- 已删除工作区根目录下的 `tmp/` 临时截图和临时 J-Link 文件；构建目录保留，因为它是当前 Debug 编译输出，不是源码测试程序。

## 2026-08-16 RTT 摄像头预览开关归档

- 已将 `src/Camera_thread_entry.c` 中的 `CAMERA_RTT_PREVIEW_ENABLE` 默认值从 `0` 改为 `1`，后续重新编译并烧录后，摄像头线程会持续生成 RTT 预览帧。
- 已将 `tools/rtt_camera_viewer.ps1` 的默认帧地址更新为本次 Debug map 中的 `0x22052140`；此前的 `0x220E1920` 已过期，会导致读取到错误区域。该地址随链接布局变化，若后续源码增删导致 map 改变，应以最新 map 的 `.bss.s_rtt_preview` 起始地址为准。
- 本轮仅完成代码和工具准备，尚未重新烧录到实机；当前窗口黑屏不能作为摄像头硬件故障结论。下一步应重新编译、烧录，再启动 RTT 预览确认真实画面。
- 已使用 `cmake --build build\\Debug --parallel 4` 编译通过，并重新确认 map 中 `s_rtt_preview` 起始地址为 `0x22052140`。
- 对比历史正常摄像头版本后发现，GUI 构建定义了 `CAMERA_CAPTURE_ON_DEMAND=1`，但 RTT 开启时没有请求采集，导致 `s_dbg_ceu_frame_cnt=0`、RTT 缓冲区全零。已在 RTT 开启分支补上 `camera_app_request_capture(true)`；GUI 的普通按需采集策略不变。
- RTT 窗口约 2 FPS 是 J-Link 逐块读取 SRAM 的显示刷新率，不代表 OV7725 的实际采集帧率。

> 本轮结论：触摸控制器最终按 CST816S 处理，实测地址为 `0x38`；独立轮询已获得有效响应，下一步才进入 LVGL 触摸输入和坐标映射验收。LVGL 双行标题继续采用运行时固定标题框+裁剪策略修复。

- **已确认根因**：核心板 U2=`W9812G2KB`（`1M × 4 Bank × 32-bit`）实际使用 8 位列地址；旧 RASC `addr_shift.9` 造成 1024-byte / 512-pixel 地址别名，使动态 RGB565 图像产生条纹、重影和花屏。
- **已实施修复**：`configuration.xml` 改为 `config.bsp.fsp.sdram.addr_shift.8`，RASC 重新生成且 `BSP_CFG_SDRAM_MULTIPLEX_ADDR_SHIFT=0`；XML 审计、完整 ARM 构建、J-Link 编程与校验全部通过。
- **板端证据**：LVGL 源缓冲与旋转后 480×640 扫描缓冲的红/绿/蓝/白采样均连续正确；MIPI `LINKSR=0x1100`、GLCDC 换帧错误为 0，真实 GUI 刷新计数持续增长。
- **当前重点**：纯色帧已在实机正常显示；CST816S 已独立读出按压坐标，正在验收正式 LVGL 输入事件以及它与 OV7725 共用 IIC0 的运行链路。

- **实物纯色验证**：用户已确认屏幕开始红、绿、蓝、白四色轮播，说明面板、ST7701S 初始化、MIPI 1-Lane 视频输出与 SDRAM 帧缓冲已真正显示。
- **当前板端固件**：`RA8P1_SCREEN_COLOR_TEST=OFF`，`RA8P1_GUI_ENABLE=ON`；已切换为 GUI Guider 中文完整界面，而不是纯色程序。
- **构建与烧录**：完整 GUI、生成页面、中文字体、图标、quirc 和驱动均已 ARM 编译链接成功，并已使用 J-Link 擦除、编程、校验、复位运行。
- **运行复核**：ST7701S 初始化步骤 `0x26`（38 条命令）完成且错误为 0；LVGL Port 打开成功；MIPI `LINKSR=0x1100`、RGB888、1 Lane，未见视频 FIFO 欠载/溢出。
- **显示验收状态**：启动页/主页已经进入实机显示阶段；若发现局部文字分成上下两行，优先检查标签长文本模式和控件宽度，不再修改 MIPI Lane、时钟或面板初始化。
- **方向修正**：曾尝试 `LV_DISPLAY_ROTATION_270`，实物仍呈竖屏，已回到 `LV_DISPLAY_ROTATION_90`。此类改动只影响 LVGL 逻辑坐标到帧缓冲的映射，不影响 DSI 物理时序；烧录后必须执行触摸四角和按钮命中测试。
- **旋转诊断**：运行时确认逻辑分辨率为 `640×480`、`LV_DISPLAY_ROTATION_90` 和矩阵旋转均已启用。LVGL 的物理分辨率查询会随当前旋转坐标系变换，不能仅凭其显示值判断原生帧缓冲错误；GLCDC/MIPI 原生扫描仍是 `480×640`。已显式声明原生物理尺寸；若实物仍显示为竖屏，必须根据屏幕照片区分内容旋转故障与面板机械安装方向。
- **照片确认后的最终方向**：实物照片表明 GUI 已全屏横向绘制，但相对屏幕倒置 180°。最终改用 `LV_DISPLAY_ROTATION_270`；该选项保持 640×480 横屏，只反转横屏朝向。
- **摄像头策略**：GUI 已保留取药与识别药页面的预览挂载点；仅进入相关页面后请求摄像头采集，离开后停止，以降低 MCU 压力。

# 显示链路最新基线（2026-08-16，优先级最高）

- 屏幕背面丝印和供应商 README 均确认 W280BF036I 为 **DSI 1 Lane**；RASC 使用 Clock + DL0，DL1 不参与协议。
- 供应商参数：480×640、RGB888、Burst、连续时钟，HFP/HS/HBP=`30/10/30`、VFP/VS/VBP=`20/4/20`。
- 早期 PLL=550 MHz 导致单 Lane 线速约275 Mb/s，触发视频 FIFO 欠载/溢出。现配置 PHY PLL=1100 MHz，单 Lane 线速约550 Mb/s，RASC 重新生成的 video delay=271。
- 最新纯色固件的38条 ST7701S DCS命令（含软件复位 `0x01`）全部成功；`RM_LVGL_PORT_Open()` 成功，`LINKSR=0x1100`，`VMPPSETR=0x003E0000`（RGB888），视频状态 `0x0D`，无 FIFO 欠载/溢出。
- 当前 `RA8P1_SCREEN_COLOR_TEST=OFF`，默认运行 LVGL/GUI Guider；四色纯色程序仅作为显示链路回归测试保留。
- `skills/rasc-configure-ra/` 已随仓库加入；无界面生成用 `D:\RASC\eclipse\rascc.exe`，不要使用 GUI 启动器 `rasc.exe`。

# RA8P1 智能医疗工程总览

> 最后整理：2026-08-16
>
> 工程根目录：`D:\Renesas_project\26renesascup_smartmedicine_Ra8p1`
>
> 文档职责：工程结构、软件进度、验证结论、未完成任务和恢复顺序
>
> 硬件事实来源：[RA8P1 硬件总览](RA8P1_硬件总览.md)

本文不再保存大段原理图、逐针排针、电源树或扩展板审查内容。硬件型号、资料指纹、精确引脚、电气约束、打板整改和上电顺序统一维护在硬件总览；本文只记录软件如何使用这些硬件，以及当前做到什么程度。

## 1. 项目目标与当前结论

目标设备是一套基于 Renesas RA8P1 的智慧药品工作站，核心业务流程为：

```text
扫描取药单
  → 解析多种药品及数量
  → 查询药仓坐标
  → 控制 ZDT 电机/机械臂逐项取药
  → 送至取药口
  → 更新库存并保存日志
```

系统另包含药品二维码识别、药师登录、存药管理、取药日志、设备状态、ESP-01S 网络状态和 MIPI 触摸屏界面。

截至 2026-08-16，摄像头/CAN 基线、Windows GUI 模拟器以及完整 GUI 板端固件均已通过 ARM 全量编译和链接。MIPI 1 Lane 与外部 SDRAM 链路已通过实机四色纯色显示；当前主要软件验收项是中文标签布局、CST816S 触摸轮询和 IIC0 共享访问，二维码正式采集循环与 USBHS 图像通道仍待后续完成。

### 2026-08-16 屏幕实机联调结论

- 原厂树莓派资料明确：W280BF036I 为 480×640、DSI 1 Lane；转接板上物理引出的三组差分对是 Clock、Lane0、Lane1，不能据此把 RASC 改成 2 Lane。
- 已将 `configuration.xml`、审计配置、ST7701S 参数校验和当前生成源统一恢复为 1 Lane；RASC 只读审计通过，Debug 固件编译并烧录成功。
- 加载初始化命令时，`RM_LVGL_PORT_Open()` 返回 `FSP_ERR_IN_USE (8)`，`SQ0RUN/SQ1RUN` 保持忙，视频运行位未起来；去掉初始化命令的隔离固件可正常返回且 `s_lvgl_ready=1`。因此当前阻塞在低功耗 DSI 命令/面板链路，不是 LVGL 页面或字体问题。
- “只有背光”只能证明背光供电/控制有效，不能证明 ST7701S 已退出复位或 DSI 差分链路正确。必须优先核对 FPC pin1 方向、Clock/Lane0 极性与连续性、3.3V/GND 和转接板 P_RST 状态；Lane1 软件保持不使用。
- 当前没有声称纯色或 LVGL 已通过；待硬件链路确认后重新烧录，再依次进行纯色、LVGL、中文界面和摄像头预览。

## 2. 当前可交付基线

| 项目 | 当前状态 | 结论 |
| --- | --- | --- |
| 默认固件 | `RA8P1_GUI_ENABLE=ON` | 完整GUI、屏幕、摄像头、quirc和机械臂接口已共同链接成功 |
| OV7725/CEU | 已完成并实机预览 | 320×240灰度采集与RTT预览可用，机内采集曾达到约20 FPS |
| PC预览 | RTT通道可用 | J-Link读取SRAM，窗口约2 FPS；不代表机内采集只有2 FPS |
| USBHS预览 | 未完成 | COM端口可枚举，PCDC Bulk IN图像发送回调仍不稳定 |
| quirc | 已移植并链接 | 中间件适配已完成，尚未进入Camera正式解码循环 |
| CAN/ZDT/XYZ龙门 | 底层已实测、应用接口已实现 | 500 kbit/s链路、ZDT协议和到位应答保持已验证版本；XYZ队列、回零、移动、安全路径、限位、超时和急停已完成 |
| ST7701S/MIPI | 配置审计和视频启动可用，面板命令链路未通过 | 480×640、1 Lane、RGB888、DCS包型和错误记录已实现；实机低功耗初始化仍卡在 `FSP_ERR_IN_USE` |
| LVGL/GUI | 模拟器与板端编译均完成 | 十页中文GUI、Pickup/Scan按需摄像头预览和页面绑定已链接；Code MRAM占用约891.5 KiB，未超过1 MiB |
| ESP-01S | 仅规划 | GUI状态接口已预留，UART/AT/网络线程尚未实现 |
| microSD | 硬件与引脚已有 | SDHI驱动、FatFS和LVGL文件系统适配尚未实现 |
| 自绘扩展板 | 审查已归档 | 尚未重新打板，投板前整改见硬件总览 |

当前不可回退约束：

- 不修改已经实测正常的 ZDT 报文、协议封装和到位应答逻辑。
- 不直接修改 `ra_gen/`、`ra/` 或 RASC 自动生成文件。
- 当前带CAN飞线的样板上，P514/P515保持输入/高阻，不能改成输出。
- GUI页面结构以 `.guiguider` 为权威来源，不维护第二套手写页面。
- 不把桌面模拟器正常显示视为板端摄像头、二维码、电机或屏幕已经接入。

## 3. 开发环境与常用命令

| 项目 | 版本/配置 |
| --- | --- |
| MCU | Renesas RA8P1，`R7KA8P1KFLCAC` |
| FSP / RASC | 6.3.0 |
| RTOS | FreeRTOS 11.1.0 |
| 板端 LVGL | RASC/FSP LVGL 9.3.0 |
| GUI Guider | 2.0.0，桌面模拟器LVGL 9.4.0 |
| 编译器 | Arm GNU Toolchain 13.3.Rel1 |
| 构建 | CMake 3.31.8 |
| 调试 | J-Link Commander V7.96n，SWD 4 MHz |

```powershell
# 编译当前Debug基线
& 'C:\Users\Zhanglongsheng\.renesas\platform\cmake\3.31.8\cmake-3.31.8-windows-x86_64\bin\cmake.exe' --build build\Debug -- -j2

# 烧录、复位并运行
& 'C:\Users\Zhanglongsheng\.renesas\platform\DebugComp\Dialog\ARM\Segger\JLink.exe' -CommanderScript flash.jlink

# 启动RTT摄像头预览
powershell -NoProfile -ExecutionPolicy Bypass -File tools\rtt_camera_viewer.ps1

# 只读审计RASC配置
powershell -NoProfile -ExecutionPolicy Bypass -File tools\rasc_config_audit.ps1
```

J-Link日志末尾偶尔出现 `Writing target memory failed.`；若前面Verify成功且复位后板端正常运行，应以实际烧录和运行结果为准。

团队VS Code配置位于根目录 `Renesas.code-profile`。同事导入后需要按本机安装位置调整Renesas CMake、clangd与RASC路径。

## 4. 工程结构与所有权

```text
src/
├─ app/                 应用层：摄像头、GUI、电机业务协调
│  ├─ inc/
│  └─ src/
├─ middleware/          中间件：二维码、USB CDC、RTT预览
│  ├─ inc/
│  └─ src/
├─ driver/              驱动层：OV7725、ST7701S、ZDT
│  ├─ inc/
│  └─ src/
├─ Camera_thread_entry.c
├─ LVGL_thread_entry.c
├─ Motor_thread_entry.c
└─ hal_entry.c

gui/RA8P1/              GUI Guider设计源、生成代码、资源与模拟器
third_party/quirc/      quirc原始第三方源码
ra/                     Renesas FSP/LVGL/FreeRTOS供应商代码
ra_gen/                 RASC生成代码
cmake/                  构建与RASC生成辅助
tools/                  RTT预览、RASC审计等开发工具
docs/                   工程总览与硬件总览
.planning/              Codex复杂任务恢复记录
```

依赖方向固定为：

```text
线程入口
  → app应用层
  → middleware中间件 → third_party/quirc
  → driver驱动层
  → RASC/FSP
```

维护规则：

| 类型 | 路径 | 规则 |
| --- | --- | --- |
| 自有应用代码 | `src/` | 可直接修改并编译验证 |
| GUI设计 | `gui/RA8P1/RA8P1.guiguider` | 用GUI Guider 2.0编辑 |
| GUI回调 | `gui/RA8P1/custom/` | 可维护，但不创建第二套页面 |
| GUI生成代码 | `gui/RA8P1/generated/` | 由GUI Guider生成，避免手工业务修改 |
| 第三方库 | `third_party/` | 保留许可证，通过中间件封装 |
| FSP/RASC生成 | `ra/`、`ra_gen/` | 不手改，修改 `configuration.xml` 后Generate |
| 构建输出 | `build/`、GUI模拟器build | 不作为源码提交 |

## 5. FreeRTOS线程与中断优先级

FreeRTOS数值越大优先级越高。当前 `configuration.xml` 的线程配置符合用户要求：Motor最高、LVGL最低。

| 线程 | 优先级 | 栈 | 主要职责 |
| --- | ---: | ---: | --- |
| Motor | 3 | 2048 B | CAN/ZDT命令、到位应答和高优先级运动控制 |
| Camera | 2 | 4096 B | CEU采集、帧转换、预览与后续二维码调度 |
| LVGL | 1 | 8192 B | MIPI/LVGL初始化、页面刷新与UI消息消费 |

主要中断配置：CANFD Lite为IPL10，CEU/MIPI/GLCDC/USB/I2C多为IPL12，D/AVE2D为IPL2。FreeRTOS允许调用系统API的最高中断优先级配置为IPL1；后续修改回调中的RTOS API前必须再次核对该约束。

推荐的跨线程边界：

- LVGL只显示状态和发布请求，不直接操作CEU或发送CAN帧。
- Camera线程拥有CEU的Open/CaptureStart/Close和灰度帧处理。
- Motor线程拥有ZDT动作序列、应答、超时和急停状态。
- 网络、日志和库存后续通过队列或线程安全快照向LVGL提供数据。

## 6. 摄像头与PC预览

### 6.1 已验证数据格式

OV7725当前输出为 `Y0 U0 Y1 V0` 的YCbCr422，CEU按8-bit数据总线采集。灰度只取每两个字节中的Y分量；彩色转换使用：

```text
Y = byte0 / byte2
U = byte1
V = byte3
```

当前画面采用320×240，灰度预览优先用于二维码。模糊时先调镜头焦距、目标距离和照明；软件锐化不能恢复失焦细节。

### 6.2 RTT预览

- `tools/rtt_camera_viewer.ps1` 通过J-Link直接读取 `s_rtt_preview`，不依赖COM端口。
- 当前PC窗口约2 FPS主要受J-Link读取速度限制，不是CEU真实帧率。
- 预览器标题使用英文，避免Windows终端或窗口编码乱码。
- 该通道是当前推荐的摄像头调试方式。

### 6.3 USBHS状态

- Windows可以枚举CDC/COM端口，控制请求与端口打开状态可到达。
- PCDC Bulk IN发送仍有超时/完成回调不稳定问题，尚未通过图像连续传输验收。
- 后续优先评估Vendor Bulk + WinUSB；若需要标准摄像头兼容性再考虑UVC。
- 当前不得把RTT预览可用表述为USBHS已修复。

## 7. 二维码解码

第三方库位于 `third_party/quirc/quirc-master/`，顶层CMake显式编译 `quirc.c`、`identify.c`、`decode.c` 和 `version_db.c`（带 `NDEBUG`，防 Debug assert 死循环）。应用层通过 `src/middleware/inc/qr_decoder.h` 和 `src/middleware/src/qr_decoder.c` 使用，不直接依赖quirc内部结构。

已完成（2026-08-19 全部实机/板端验证）：

- quirc源码离线导入、许可证保留、ARM交叉编译和最终链接；灰度图初始化、识别、结果提取和释放的中间件封装。
- **正式解码循环已打通**（Camera线程只提取 Y 灰度 → 独立低优先级解码线程 → 200ms 限频 → 去抖发布 → LVGL 读取），含板端自检（嵌入已知二维码，Device 自检按钮/J-Link 触发，MED-001 268ms、取药单 JSON 278ms 实测精确解码）。
- 取药单二维码数据格式已定义（`docs/ESP01S_通信协议.md` §10：单行紧凑 JSON ≤120B，`{"oid":...,"i":[{"id":...,"n":...}]}`），板端以原始 payload 展示，后续解析查药品库。
- 历史误判澄清：此前的"quirc 卡死"实为**解码线程栈溢出 HardFault**（`decode_gray` 局部结构体 12.9KB vs 8KB 栈），且 `feed_frame` 尺寸判空 bug 使解码从未真正运行——均已修复（详见顶部最新状态章节）。

未完成：

- 实机 Scan/Pickup 页手机二维码对摄像头的识别率/焦距/光照标定（物理环节，需用户上机）；持续解码 CPU 占比与不同光照识别率测量。
- 药品ID到坐标的数据库接口（依赖机械臂接入）。

接入原则：只在Pickup/Scan页面需要时启动采集；以受控频率解码；成功后冻结最后一帧、停止CEU/解码，并通过消息把结果交给LVGL线程。

## 8. CAN与ZDT电机

当前CAN软件基线：经典CAN、500 kbit/s、75%采样点、扩展帧。详细收发器、终端和引脚见硬件总览。

已经验证：

- CANgaroo捕获扩展ID `0x18FF5001`、DLC=8、数据 `A5 5A 00 01 02 03 04 6B` 的周期测试帧。
- 该结果确认Motor线程、CANFD Lite初始化、TJA1042发送链路和USB-CAN接收链路可用。
- 用户此前已完成ZDT协议和到位应答实机测试，结论正常。

当前 `CAN_TEST_TX_ENABLE=0`，CANgaroo周期测试帧默认关闭，避免正式机械臂运行时污染ZDT总线；需要复测物理链路时可临时通过编译宏开启。该测试帧不是电机命令。

### 8.1 XYZ龙门机械臂应用接口

`src/app/inc/gantry_robot.h` 与 `src/app/src/gantry_robot.c` 在已验证的ZDT驱动之上提供三轴龙门应用层，`src/driver/src/ZDT_drv.c` 的指令编码、CAN报文与既有到位应答协议未修改。Motor线程初始化该模块并每5 ms调用 `Gantry_Service()`，其他线程只投递命令，不直接发送ZDT报文。

主要接口：

```c
Gantry_EnableAxes(GANTRY_AXIS_MASK_ALL, true);       // 使能XYZ
Gantry_Home(GANTRY_AXIS_MASK_ALL);                   // Z先回零，再回X/Y
Gantry_MoveTo(&target);                              // XYZ绝对坐标同步移动
Gantry_MoveBy(&delta);                               // XYZ相对移动
Gantry_MoveAxisTo(GANTRY_AXIS_Z, z_mm);              // 单轴移动
Gantry_MoveSafeTo(&target, safe_z_mm);               // Z退回→XY横移→Z下降
Gantry_EmergencyStop();                              // 请求急停
Gantry_GetStatus(&status);                           // 状态、当前/目标坐标
```

坐标单位为毫米，X/Y默认脉冲当量沿用旧代码的 `3600/84 pulse/mm`，Z沿用 `1000/23 pulse/mm`；默认三轴软限位暂设0～420 mm，方向均为+1，必须在实机标定后通过 `Gantry_Configure()` 修正真实行程、方向、速度和加速度。默认要求完成回零后才能移动，位置只在收到到位通知后提交。

到位应答继续由用户已经实测的解析逻辑负责；解析成功/失败后调用 `Gantry_NotifyAxisDone()`，若解析位置位于CAN中断则调用 `Gantry_NotifyAxisDoneFromISR()`。该FromISR桥只写每轴轻量标志，不调用FreeRTOS ISR API，适配当前CAN IPL12；Motor线程随后消费标志。若未接入通知，运动会在默认30秒、回零会在60秒超时并发送现有广播停止指令。

旧 `ZDT_app.c` 中的 `getMedicine()` / `storeMedicine()` 暂为兼容代码，存在忙等和直接驱动问题，新业务不得继续从GUI/Camera线程调用；后续取药任务应组合 `Gantry_MoveSafeTo()`、夹爪接口和到位事件形成非阻塞流程。

## 9. MIPI屏幕与LVGL板端移植

目标面板为 WLK2802MIPI-15P V2（ST7701S），面板原生时序为480×640，业务界面将屏幕横置后按640×480使用。2026-08-12已逐项对照厂商初始化文本和随附树莓派驱动，并完成以下软件闭环：

- `src/driver/src/st7701s_panel.c/.h` 的DCS初始化、命令等待、Sleep Out与Display On时序。
- `src/LVGL_thread_entry.c` 的MIPI DSI POST_OPEN初始化入口。
- GLCDC、MIPI DSI/PHY和LVGL Port生成栈；LVGL D/AVE2D绘制后端已关闭，使用软件矩阵旋转避免FSP 6.3后端与LVGL浮点矩阵类型冲突。
- Pickup/Scan页面已动态挂接摄像头图像对象，并按页面进入/离开请求或停止采集。
- RASC的1条Data Lane、RGB888输出、480×640有效区、600 MHz PHY和连续时钟与厂商参考驱动的接口类型一致。
- ST7701S初始化表除额外的Command2退出命令外与厂商文本一致；该退出命令也存在于随附Linux参考驱动，保留合理。

已完成的软件整改：

- 保持PLL2R=240 MHz，将LCDCLK设为/2、GLCDC设为/5，得到24 MHz像素时钟；GLCDC配置为H/V总周期550/684、有效区480/640、Sync 10/4、组合Back 30/20，理论约63.8 FPS。FSP生成的DSI独立字段为H active/sync/back/front=`480/10/20/30`、V=`640/4/16/20`，实物首测需观察边缘是否完整。
- 增加DCS `0x01`软件复位；无参数、单参数和多参数命令分别使用DCS Short Write 0 Param、Short Write 1 Param和Long Write。
- MIPI回调记录最后事件/状态和FATAL错误；LVGL线程检查面板初始化结果，失败时不继续启动GUI。
- 使用 `LV_DISPLAY_ROTATION_90` 和软件矩阵旋转，在原生480×640帧缓冲上提供640×480 GUI画布。
- 默认启用 `RA8P1_GUI_ENABLE=ON`，修复旧 `Copy` 页面绑定；启用完整GUI的ARM全量编译、链接和S-record生成成功。
- 固件Flash只读内容约891.5 KiB（向量160 B、只读区890424 B及少量初始化节），未超过1 MiB Code MRAM；SDRAM no-init帧缓冲约1.17 MiB，片内零初始化区约1.42 MiB。

仅剩硬件验收项：

- 屏幕背光、纯色显示和 LVGL 基本链路已完成实物验收；触摸轮询、中文标签布局和摄像头屏显仍需板端回归。
- 尚未完成 CST816S 实际 ID/坐标确认，以及正式二维码业务流程的屏显验收。

首次点屏顺序固定为：确认FPC方向、电源、共地和三组排线连续性 → 烧录当前Debug S-record → 上电判断控制板是否自动开背光 → 五色纯色/启动页 → 中文LVGL控件 → Pickup/Scan摄像头预览 → 二维码与业务流程。只有自动背光或软件复位验证失败时才补 `LCD_BL`/`LCD_RST` GPIO；触摸另行验收。具体电气和FPC信息见硬件总览。

## 10. GUI Guider医疗界面

### 10.1 页面结构

当前设计源为 `gui/RA8P1/RA8P1.guiguider`，包含十页：

```text
Boot
  → Home
      ├─ Pickup    取药单扫描、多药品坐标与取药流程
      ├─ Scan      单个药品二维码识别
      │    └─ Medicine  药品信息
      └─ Login     药师认证
           └─ Admin
                ├─ Store   存药管理
                ├─ Logs    取药日志
                └─ Device  系统状态
```

主页只公开取药、识别药和药师管理。存药、日志与系统状态需进入药师管理台。当前登录仅为模拟器导航，尚不具备真实认证安全性。

### 10.2 已完成视觉与交互

- 640×480简体中文界面，使用按字符裁剪的 Source Han Serif SC。
- 十页导航、按钮事件、药师权限入口和模拟数据布局。
- 启动页、医疗服务图标、RENESAS品牌；主页/管理台加入全国大学生电子设计竞赛标识。
- ESP状态默认显示“系统离线”，可通过 `gui_set_esp01s_online()` 更新。
- 设备状态页第二项由CAN链路摘要改为机械臂实时X/Y/Z坐标，可在LVGL线程调用 `gui_set_arm_coordinates()` 更新。
- 存药与取药日志已拆分为独立页面。
- 取药页具有摄像头预览占位、多药品清单、坐标和状态区域。
- GUI JSON、控件ID唯一性、页面跳转、Windows高DPI和关键页面截图均已验证。
- 新增“机械臂坐标”后已重新运行 `gui/tools/sync_generated_chinese.js`，13个字号裁剪字库完成同步；Windows Simulator重新完整编译通过，解决设备页缺字乱码。

当前资源为14张PNG和18个按控件尺寸生成的LVGL图像数组。用户自行修改的Boot配色已保留；后续不要在GUI Guider仍打开旧缓存时运行整页重建脚本。

### 10.3 板端接入与容量

板端使用RASC LVGL 9.3，GUI Guider生成代码按9.4处理，兼容层位于 `src/app/inc/gui_guider_lvgl_compat.h`。

完整GUI曾在 `RA8P1_GUI_ENABLE=ON` 时超过1 MiB内部Code MRAM **143,512字节**。该测量发生在前一版资源上；当前中文和图片资源变更后必须重新测量，不能直接沿用数值作为最终容量。默认已经恢复 `RA8P1_GUI_ENABLE=OFF`。

GUI Guider生成的字体、图标和页面常量只要保持 `const`，链接器就会把它们直接放入1 MiB Code MRAM，不需要额外编写Flash驱动。当前器件 `R7KA8P1KFLCAC` 的生成配置为 `BSP_DATA_FLASH_SIZE_BYTES=0`，没有普通Data Flash，也不支持 `r_flash_hp/r_flash_lp`；RA8P1运行时擦写内部非易失存储应使用FSP `r_mram`。但Code MRAM写入/擦除期间不能从Code MRAM取指，相关例程必须在RAM执行并处理向量表/中断，同时还要通过链接脚本预留不与固件重叠的分区。因此本项目不使用内部MRAM保存药品图片、流水日志或频繁变化的库存，只把它作为程序和固定资源存储；若未来确需保存少量低频校准参数，再单独设计双副本、版本号、CRC和掉电恢复的MRAM参数区。

板载串行Flash候选为约32 MiB的 `W25Q256JVEIQ` 或 `AT25SF2561C-MWUB`，OSPI0引脚复用已经存在，但RASC中尚未加入 `r_ospi_b` 实例。确认实际焊接型号后，应由Codex通过受控RASC差异加入OSPI驱动，先完成JEDEC ID、擦除、页写、读回和掉电恢复测试，再接LVGL资源与药品图片。microSD继续用于可拆卸图片库、日志导出和数据库更新。

此外，`src/app/src/gui_app.c` 仍含旧 `guider_ui.Copy` 预览控件适配，而当前页面已经改为Pickup/Scan。启用板端GUI前必须改绑新控件，否则桌面模拟器通过也不能说明固件接口正确。

板端接入顺序：

1. 重新测量当前十页资源的Flash/RAM占用。
2. 优化未用字号、图片格式和重复资源；不足时规划外部Flash。
3. 将旧Copy预览适配改为Pickup/Scan生命周期和图像控件。
4. 建立GUI与Camera/Motor/库存/日志/网络之间的消息结构。
5. 使用 `RA8P1_GUI_ENABLE=ON` 完整链接通过后再烧录。

## 11. ESP-01S、microSD与后续数据层

ESP-01S目前只完成硬件规划和GUI状态接口。尚未增加SCI8 UART、AT驱动、网络状态机或独立线程。后续建议：

```text
driver: ESP UART/复位/AT收发
middleware: AT事务、超时、重试、联网状态机
app: 药品数据同步、在线状态、业务请求
LVGL: 只消费连接状态和业务结果
```

microSD硬件与SDHI引脚已有，但没有驱动实例、FatFS或LVGL文件系统。药品图片、日志和可更新数据库适合放在microSD；固定图标和少量字体可根据容量留在Flash。两项外设的精确引脚和电气要求均见硬件总览。

## 12. RASC配置与自动化

`configuration.xml` 是外设、线程、栈、引脚、时钟和中断的声明式源；`ra_gen/`、`ra_cfg/`和相关CMake文件是生成结果。

仓库内已有：

- `tools/rasc_config_audit.ps1`：只读检查目标器件、FSP、活动pincfg、CAN/USB、MIPI Lane、显示尺寸、线程优先级和模块引用。
- `tools/rasc_config_profile.json`：项目特有硬件不变量。
- 本机个人Skill `C:\Users\Zhanglongsheng\.agents\skills\rasc-configure-ra`：通用XML审计、修改、headless Generate和差异验证流程。

当前已知配置结论：

- Motor=3、Camera=2、LVGL=1，优先级关系正确。
- CAN、USBHS和摄像头关键引脚与当前软件基线一致。
- MIPI DSI Lane=1与WLK2802厂商参考一致；`tools/rasc_config_profile.json` 和 `tools/rasc_config_audit.ps1` 已统一为1 Lane并复跑通过。
- 显示有效区480×640、RGB565双帧缓冲、RGB888链路格式、LCDCLK /2和GLCDC /5均正确；LVGL以软件矩阵旋转提供640×480画布。
- LVGL D/AVE2D后端及其DRW子栈已关闭，项目审计会阻止二者不一致；CMake排除未配置的DRW/DAVE2D实现源码。

自动修改流程固定为：只读审计 → 精确修改已知属性 → XML结构验证 → RASC headless Generate → 生成差异检查 → ARM完整编译 → 有硬件时烧录验收。

## 13. 已验证结果清单

| 日期 | 范围 | 结果 |
| --- | --- | --- |
| 2026-08-08 | 工程分层与quirc | CMake重新配置、ARM全量编译与最终链接通过 |
| 2026-08-08 | OV7725/CEU | 灰度画面可用，RTT预览正常，数据格式确认 |
| 2026-08-08 | CAN台架 | CANgaroo捕获 `0x18FF5001` 测试帧 |
| 已有实测 | ZDT | 电机协议与到位应答正常，保持现状 |
| 2026-08-11 | GUI设计 | 十页 `.guiguider` 工程、中文字体和导航完成 |
| 2026-08-11 | GUI模拟器 | CMake/Ninja/MinGW/SDL完整构建，关键页面截图通过 |
| 2026-08-11 | GUI品牌与状态 | 系统离线、日志详情、电赛/RENESAS标识显示正常，Boot配色未覆盖 |
| 2026-08-16 | 屏幕/RASC复核 | 根据原厂 W280BF036I 资料纠正为1 Lane；RASC审计通过，Debug编译/烧录通过；实机初始化序列仍卡忙，未宣布点屏成功 |
| 2026-08-12 | 内外部存储审计 | 确认目标器件为1 MiB Code MRAM、无Data Flash；固定GUI资源无需驱动，运行时写入应使用r_mram但不适合大资源，板载约32 MiB OSPI Flash待确认料号后接入 |
| 2026-08-12 | ARM全量编译 | 完整GUI、ST7701S、MIPI/GLCDC、摄像头、quirc和机械臂接口编译链接成功并生成S-record；Flash只读内容约891.5 KiB |
| 2026-08-10 | 硬件资料 | 官方板卡与自绘扩展板信息完成归档和风险审查 |
| 2026-08-12 | GUI设备状态 | CAN摘要项改为机械臂实时X/Y/Z坐标，并预留LVGL线程更新接口；未修改ZDT协议 |
| 2026-08-12 | XYZ龙门应用层 | 新增队列化三轴回零/移动/安全路径/限位/超时/急停接口，Motor线程接入；ZDT底层零差异，ARM单目标编译通过 |
| 2026-08-12 | GUI中文字体 | 为“机械臂坐标”等新增文字重建13个字号裁剪字库，Simulator完整编译通过，设备页缺字乱码修复 |

## 14. 未完成任务与优先级

### P0：硬件到位后的首要验收

1. 按硬件总览检查FPC方向、电源、共地和三组排线，烧录当前GUI Debug固件并确认控制板是否自动开启背光。
2. 依次验收纯色/启动页、中文LVGL、触摸、Pickup/Scan摄像头预览；记录 `g_st7701s_init_error`、最后MIPI事件和状态。
3. 若完全无背光或初始化不稳定，再确认并配置LCD复位/背光GPIO；最简三排线方案成功时不增加飞线。

### P1：硬件到位后

1. 按硬件总览先检查 FPC 方向、电源、共地、Clock/Lane0 极性和 P_RST 状态；必要时补复位/背光线，再进行 ST7701S 纯色和 LVGL 点屏，触摸独立验收。
2. 实测Pickup/Scan现有按需采集逻辑是否能做到进入页面采集、离开停止，并校准刷新率。
3. 把quirc接入正式灰度解码循环，定义取药单格式和药品坐标查询。
4. 将取药/存药业务接入现有 `gantry_robot` 命令队列，把已验证的ZDT到位应答映射到 `Gantry_NotifyAxisDone()`，并完成三轴脉冲当量、方向、真实软限位和安全Z高度的实机标定。

### P2：功能完善

1. 实现microSD/FatFS、药品图片、库存与日志持久化。
2. 实现ESP-01S SCI8、AT状态机和系统在线状态。
3. 决定USBHS使用WinUSB Vendor Bulk还是UVC，并完成持续图像传输。
4. 增加真实药师认证、权限、审计与会话超时。

### P3：下一版PCB

完成硬件总览中的A/B级整改和签核，再重新打板；之后实施ESP-01S、PDM麦克风和四路PWM扩展。

## 15. 恢复工作与文档维护

复杂任务过程保存在 `.planning/<任务名>/`：

- `task_plan.md`：目标、阶段、决策和错误记录。
- `findings.md`：调查结论。
- `progress.md`：操作、测试和恢复点。

`.omo/` 是OpenCode留下的历史记录，保留只读参考。Codex后续继续使用 `.planning/`，重要结论最终写入本工程总览或硬件总览。

恢复顺序：

1. 阅读本文件的“当前可交付基线”和“未完成任务”。
2. 涉及电源、引脚、连接器或扩展板时阅读[硬件总览](RA8P1_硬件总览.md)。
3. 阅读对应 `.planning/<任务名>/` 的三份文件。
4. 检查 `git status`、最近提交和当前构建配置。
5. 修改后记录编译/烧录/实测结果，再更新两份权威文档。

文档职责固定为：

| 文件 | 内容 |
| --- | --- |
| `README.md` | 仓库入口与极简摘要 |
| `docs/RA8P1_工程总览.md` | 软件架构、完整进度、验证和下一步 |
| `docs/RA8P1_硬件总览.md` | 官方板、自绘板、引脚、电气、打板与上电事实 |

新增硬件事实先更新硬件总览；新增软件实现或测试结果先更新工程总览；影响整体状态时再同步README。

## 2026-08-16 纯色测试软件验收结果

### 方向修正追加记录

- 实物已能完整显示横屏 GUI，但整页倒置；LVGL 90°/270°运行诊断值确实改变而视觉不变，说明不能继续只修改 LVGL 角度枚举。
- 已根据 ST7701S 规格书 MADCTL(36h) 增加 `{0x36,0xC0}`，并将 LVGL rotation 设回 0，采用面板硬件 180°扫描翻转，避免双重旋转。
- 最新固件已编译、烧录、校验并运行：初始化 39 条、初始化错误 0、LVGL Port 错误 0、MIPI 1 Lane 视频运行无 FIFO 错误；待实物确认方向后再决定是否实现 DIRECT 帧缓冲显式旋转。
- 最新实物照片仍为竖屏并有重影；已撤销不适合横竖交换的 `MADCTL=0xC0`，恢复原厂 38 条初始化序列和 LVGL rotation=90。下一阶段改为修正 DIRECT 帧缓冲实际旋转与双缓冲刷新一致性。

- 已修正 `st7701s_panel.c` 的 DSI 包类型：ST7701S 厂商寄存器页使用 Generic Short/Long Write，`0x01/0x11/0x29/0x35` 使用 DCS 包。
- 已修正初始化序列等待竞态：每条低功耗命令现在等待 MIPI Sequence 0 完成回调，再等待 SQ0/SQ1 空闲，避免最后一条命令尚未真正完成就启动视频。
- 已增加 `RA8P1_SCREEN_COLOR_TEST` CMake 选项。打开后跳过 GUI Guider，LVGL 每秒循环显示红、绿、蓝、白四种纯色。
- 最新纯色测试固件已经编译并烧录：ST7701S 初始化步骤 `0x26`（38 条）完成，最后命令错误为 0，`RM_LVGL_PORT_Open()` 返回 0，`s_lvgl_ready=1`，DSI 为 1 Lane。
- 若实物仍只有背光，软件侧已进入面板物理链路排查阶段，优先检查 P2 FPC pin1、Clock/Lane0 极性与连续性、3.3V/GND 和 P_RST。
- 关闭纯色测试并恢复 GUI：重新配置时使用 `-DRA8P1_SCREEN_COLOR_TEST=OFF`，然后再编译。

### SDRAM 帧缓冲修正

本轮继续检查纯色测试时发现，GLCDC/LVGL 帧缓冲位于外部 SDRAM 的 `0x68000000`，但原 `configuration.xml` 将 `config.bsp.fsp.sdram.enabled` 设为 Disabled。这样会出现 MIPI 链路和背光正常、但显示没有有效像素的现象。现已将 RASC 源配置改为 Enabled，并把该约束加入 `tools/rasc_config_audit.ps1`；必须重新由 RASC 生成 `ra_cfg/fsp_cfg/bsp/bsp_mcu_family_cfg.h` 后再烧录验证。

重新生成、编译、烧录后，J-Link 读取 `0x68000000` 得到 `F800F800...`，说明纯色测试已实际写入 SDRAM 帧缓冲；软件侧的“帧缓冲全零”问题已解决。当前只剩观察实物屏幕颜色，以及必要时检查 FPC、P_RST 和屏端控制板链路。
# 屏幕联调最新状态（2026-08-16）

> 历史更正：本段先前误写为 2 Lane，现已废止。有效基线以本文开头“显示链路最新基线”为准：`num_lanes=1`、Clock + DL0、PHY PLL=1100 MHz。

- 实际扩展板原理图引出了 Clock、DSI0 与 DSI1 三组物理差分对；原厂 W280BF036I 资料明确为 DSI 1 Lane，因此工程配置保持 1 Lane。
- 已修复低功耗命令包类型、Sequence 完成等待与 SDRAM 未初始化问题；当前正式初始化路径下 `RM_LVGL_PORT_Open()` 成功，`s_lvgl_ready=1`。
- 最新 J-Link 读取显示两个帧缓冲已经包含 `0x001F`（蓝）和 `0x07E0`（绿）的 RGB565 纯色数据，说明红/绿/蓝/白循环和双缓冲软件链路已运行；屏幕可视验收仍待观察实物。
- P_RST 当前测得 3.3V，只能说明其处于释放状态；DCS `0x01` 软件复位已在初始化序列中执行，但不能替代可拉低的硬件复位 GPIO。
- RASC standalone 生成器尚未识别已安装的 RA8P1 device family pack；正式提交前必须用带 pack 注册的 RASC/e² studio 重新生成 `ra_gen/` 并检查差异。
- 本轮显示适配：LVGL 使用独立 SDRAM 双缓冲渲染 640×480 逻辑画布，完成一帧后由 `src/LVGL_thread_entry.c` 明确旋转到原生 480×640 扫描缓冲，再交给 GLCDC。已完成编译、烧录和运行诊断；RASC、MIPI lane 与 ST7701S 初始化表保持不变。
## 2026-08-16 归档基线：屏幕纯色通过，LVGL 动态显示仍有问题

### 当前结论

- W280BF036I / ST7701S 物理屏幕为 480×640，项目逻辑界面为 640×480，MIPI DSI 使用 1 Lane（Clock + DL0）。
- RASC 配置、SDRAM 初始化、ST7701S 初始化、MIPI 链路和 GLCDC 静态输出已经验证正常。
- 实机四色轮播测试已通过，说明背光、面板初始化、MIPI 发送、GLCDC 输出、外部 SDRAM 帧缓存和 RGB565 静态扫描链路均可工作。
- 当前仍存在的问题是 GUI/LVGL 动态刷新时的重影和花屏；这不能再归因于 MIPI Lane、RASC 或背光配置。

### 当前软件基线

- `RA8P1_SCREEN_COLOR_TEST=OFF`
- `RA8P1_GUI_ENABLE=ON`
- LVGL 使用独立 SDRAM 渲染缓冲区，逻辑画布为 640×480 RGB565。
- 输出前将逻辑帧旋转并转换到面板原生 480×640 扫描缓冲区。
- 当前尝试使用 FULL 全帧渲染、独立输出缓冲区、三缓冲轮换以及 VPOS 同步，避免 LVGL 逻辑缓冲区与 GLCDC 扫描缓冲区混用。
- 最新 FULL 版本已完成编译、烧录和寄存器诊断；仍需以实机画面确认重影/花屏是否消失。

### 已知问题与下一步

1. 如果 FULL 版本仍重影或花屏，下一步使用“静态测试图经过同一旋转提交回调”的隔离测试，区分 LVGL 绘制问题与 GLCDC BufferChange/缓存一致性问题。
2. 再检查 RGB565 字节序、D-Cache 清理范围、SDRAM 缓冲区是否需要非缓存区，以及三缓冲提交时序。
3. 在动态显示链路稳定前，不继续修改 MIPI Lane、ST7701S 初始化表和 RASC 基础配置。
4. 屏幕稳定后再恢复触摸测试、摄像头预览和二维码业务联调。

### 本次归档说明

本节作为 2026-08-16 的工作基线。历史实验记录仍保留，但以本节的“当前软件基线”和“当前结论”为准。
## 2026-08-16 LVGL 动态换帧修正（待实机验证）

- 自定义旋转输出回调现在独立管理 LVGL 刷新完成状态：提交 GLCDC 后显式调用 `lv_display_flush_ready()`。
- 已关闭 RM_LVGL_PORT 原有的 `flush_wait_cb`，避免旧路径与自定义 VPOS 等待发生状态竞争。
- 已显式设置 `LV_COLOR_FORMAT_RGB565`，与 GUI 渲染缓冲区和面板扫描缓冲区保持一致。
- 新增 LVGL 刷新计数、最近一次 `BufferChange` 错误码和最近使用的扫描缓冲区索引，便于 J-Link 诊断动态刷新是否连续工作。
- 本次修正尚未完成实机验证；纯色测试仍作为已通过的静态基线保留。
## 2026-08-16 最新实机诊断

- 换帧时序修正版已经烧录运行。
- `g_lvgl_flush_count=0x5B`，确认 LVGL 正在持续刷新；`g_lvgl_last_flush_error=0`，确认 GLCDC `BufferChange` 未报错。
- MIPI 1 Lane 和视频链路状态保持正常。
- 当前待解决问题进一步收敛为旋转后 RGB565 数据、D-Cache/SDRAM 一致性或面板扫描显示表现；下一步使用固定静态图案走同一输出回调进行隔离。
## 2026-08-16 屏幕动态刷新隔离测试

实机最新现象仍为横向色带、密集条纹和重影。静态纯色输出已经通过，MIPI 1 Lane、SDRAM、ST7701S 初始化和 GLCDC 静态扫描链路已有证据正常；当前故障范围收敛到动态 RGB565 缓冲区、旋转映射或缓存一致性。

已完成一次绕过 LVGL 控件绘制的直写源缓冲区测试，使用现有旋转刷新回调和 GLCDC 提交接口。J-Link 读数为 `flush_error=0`、扫描缓冲区索引正常变化、刷新计数递增。测试配置随后已恢复正常 GUI 配置。最终结论等待用户观察该直写图案的实机画面后确定。
## 2026-08-16 LVGL 显示根因定位更新

原生 GLCDC 帧缓冲隔离测试已经成功：直接填充 `fb_background[0/1]` 后，J-Link 可读到 `0x68000000` 的有效 RGB565 数据，屏幕输出链路具备显示非白色内容的能力。当前重影/白屏问题已收敛到 LVGL 源缓冲区转换和提交路径，不再归因于 MIPI 线数、RASC 显示栈或屏幕硬件。

同时确认 LVGL 缓冲符号地址不能按声明顺序猜测，必须以 ELF 符号为准；当前 `s_scanout_buffer_2` 位于 `0x6812C000`，`s_lvgl_render_buffer` 位于 `0x681C2000`。RASC XML 已将 RA8P1/FSP 6.3.0 的 D-Cache 选项改为 enabled，待匹配 RASC 重新生成后再验收生成文件。
## 2026-08-16 最新进度：中文换行与触摸 IIC0

- 实物界面中部分本应单行的中文副标题出现上下两行。根因不是字库乱码，而是 `custom.c` 原先每 500 ms 对当前页面所有标签强制设置 `LV_SIZE_CONTENT`，覆盖了 GUI Guider 对固定宽度/固定高度标签的布局约束；同时部分文本宽度接近控件边界，触发了 LVGL 默认换行。
- 已撤销全局递归布局修正，改为一次性、按控件分类设置：需要说明文字的标签使用明确的 `LV_LABEL_LONG_WRAP` 和固定两行高度；本应单行的副标题/提示使用 `LV_LABEL_LONG_CLIP`，不再被定时器反复改高。该修复保留在 `gui/RA8P1/custom/custom.c`，不改 GUI Guider 生成文件。
- 触摸芯片已从屏幕资料确认：带触摸的 ESP32-P4 验证固件包含 `CST816S` 驱动（`esp_lcd_touch_new_i2c_cst816s`、`touch_cst816s_read_id` 等符号）。当前按 CST816S 规划，常用 7-bit I2C 地址为 `0x15`；最终仍需上电后读取芯片 ID 作为实物确认。
- 触摸检查现状：当前 `configuration.xml` 仅生成摄像头使用的 `g_i2c_master0/IIC0`，P409/P410 已被 OV7725 SCCB 占用；CST816S 使用同一 SDA/SCL 总线，地址 `0x15` 与 OV7725 `0x21` 不冲突。本轮增加了初始化互斥、总线互斥、完成信号清理、超时 Abort 和地址恢复；不增加第二个 RASC IIC 实例。TP_INT/TP_RST 未连接，因此暂不依赖中断和硬件复位。
- 触摸软件实现：`src/driver/src/cst816s_touch.c` 负责 CST816S 探测、ID/固件版本读取和坐标读取；`src/driver/src/camera_drv.c` 增加按从机地址访问及 FreeRTOS 互斥，摄像头访问固定恢复到 `0x21`；`src/LVGL_thread_entry.c` 创建 LVGL pointer input device，并将 CST816S 原生 480x640 坐标旋转映射到 640x480 画布。
- 触摸测试顺序固定为：烧录 → 观察 `g_cst816s_probe_ok`、`g_cst816s_probe_fail`、`g_cst816s_chip_id` → 手指触摸并观察 `g_cst816s_touch_x/y/pressed` → 测试四角和按钮命中。若 ID 读取成功但坐标方向反了，只调整坐标映射，不改 I2C 地址。
- 本轮 RASC 审计通过，Debug ARM 固件全量编译通过；`tools/jlink_screen_status.jlink` 已加入 CST816S 和 IIC0 诊断变量读取。当前构建中的关键地址以最新 map 为准：`chip_id=0x2216BCCC`、`fw_version=0x2216BCC8`、`probe_ok=0x2216BCDC`、`probe_fail=0x2216BCD8`、`read_ok=0x2216BCD4`、`read_fail=0x2216BCD0`、`x=0x2216BCC4`、`y=0x2216BCC0`、`pressed=0x2216BCBC`、`fingers=0x2216BCB8`、`last_reg=0x2216BCB4`、`error_reg=0x2216BCB0`。
- 本轮重新编译成功；新增 IIC0 诊断计数包括 `s_dbg_i2c_mutex_timeout`、`s_dbg_i2c_init_mutex_timeout`、`s_dbg_i2c_timeout`、`s_dbg_i2c_abort_calls` 和 `s_dbg_i2c_addr_fail`，用于判断摄像头与触摸是否发生抢占、超时或地址切换失败。
- 文档归档规则：工程进度统一写入本文件，硬件接线与芯片信息统一写入 `docs/RA8P1_硬件总览.md`；不再新增独立屏幕调试 Markdown。

### 当前实机触摸读数

- 已通过本机 J-Link 连接 RA8P1：`VTref=3.300V`，目标识别正常。
- 触摸诊断读数为 `probe_ok=0`、`probe_fail=114`、`error_reg=0xA7`、`last_reg=0xA7`、`read_ok=0`；IIC0 互斥、超时和地址切换错误均为 0。
- 结论：当前是 CST816S 在 `0x15` 地址没有 ACK，不是 LVGL 坐标映射问题。由于烧录命令仍返回 `Writing target memory failed`，本次不把烧录后的运行画面当作最终验收依据；后续默认先编译、再用 `flash.jlink` 烧录并检查返回码。

## 工程工具索引与调用方式

工程内已有工具统一保存在根目录 `flash.jlink` 和 `tools/`，后续优先复用这些文件，不重复创建临时脚本：

| 工具 | 用途 | 调用方式 |
| --- | --- | --- |
| `flash.jlink` | 完整 Debug 固件烧录、复位、运行 | 默认使用 `build/Debug/26renesascup_smartmedicine_Ra8p1.srec`，SWD 速度 1 MHz；每次固件修改后优先执行并检查 J-Link 返回码 |
| `tools/jlink_flash_screen_color.jlink` | 烧录纯色测试 S-record | J-Link Commander 执行；用于屏幕链路回归 |
| `tools/jlink_reset_run.jlink` | 只复位并运行当前固件 | J-Link Commander 执行 |
| `tools/jlink_screen_status.jlink` | 读取 MIPI、帧缓冲、CST816S 和 IIC0 诊断变量 | 烧录后执行；地址必须以最新 `.map` 为准 |
| `tools/jlink_screen_color_status.jlink` | 读取屏幕纯色/MIPI/LVGL 状态 | 纯色或显示异常时执行 |
| `tools/jlink_camera_format_diag.jlink` | 读取摄像头格式和 CEU 状态 | 摄像头花屏、条纹或格式异常时执行 |
| `tools/jlink_rtt_diag.jlink` | 读取 RTT 控制块和摄像头调试数据 | RTT 预览异常时执行 |
| `tools/jlink_usb_diag.jlink`、`tools/usbhs_diag.jlink` | 读取 USBHS/PCDC 状态 | USBHS 预览或 COM 口异常时执行 |
| `tools/jlink_help.jlink` | 查询当前 J-Link Commander 命令 | J-Link 工具不熟悉时执行 |
| `tools/rasc_config_audit.ps1` | 只读审计 `configuration.xml` 的目标、线程、Lane、SDRAM 等 | `powershell -ExecutionPolicy Bypass -File tools/rasc_config_audit.ps1` |
| `tools/rtt_camera_viewer.ps1` | 通过 J-Link DLL 查看 RTT 摄像头帧 | PowerShell 执行，默认使用 Renesas 安装的 J-Link DLL |
| `tools/usbhs_camera_viewer.ps1` | 通过 COM 口显示 USBHS 摄像头帧 | `powershell -File tools/usbhs_camera_viewer.ps1 -Port COM15` |
| `tools/find_camera_port.py` | 扫描摄像头 USB 虚拟串口 | `python tools/find_camera_port.py` |
| `tools/camera_viewer.py` | Python/OpenCV USB 摄像头预览 | `python tools/camera_viewer.py --port COM15` |

当前触摸验收应使用 `flash.jlink` 烧录后，再执行 `tools/jlink_screen_status.jlink`，重点读取 `g_cst816s_probe_ok/probe_fail`、`g_cst816s_chip_id`、`g_cst816s_read_ok/read_fail`、`g_cst816s_touch_x/y/pressed` 以及 IIC0 互斥和超时计数。若 `probe_ok=0`，先不要调整 LVGL 坐标映射，应先确认 CST816S 的 I2C ACK、供电、上拉和 TP_RST 状态。

## 2026-08-16 小屏可读性调整

- 针对实物屏幕尺寸较小的问题，`gui/RA8P1/custom/custom.c` 增加统一的可读性样式：启动页标题、主页标题、页面标题、药品名称、状态值和主要按钮均使用更大的现有中文字库。
- 当前中文字库没有独立 Bold 字重；通过 1 px 同色文字描边实现视觉加粗，不新增大体积粗体字库，避免增加 MRAM 压力。
- Home、Admin、Login、Pickup、Scan、Medicine 等页面均在延迟创建后应用样式；布局不再依赖页面初始化时机。副标题仍按单行/双行分类处理，防止字号增大后发生重叠。
- 本轮仅修改自定义运行时样式，不修改 GUI Guider 生成文件和 RASC 配置；Debug ARM 固件已重新编译通过。实机烧录后需重点检查启动页、主页卡片、药品名称、按钮和设备状态页是否完整显示。
- 触摸新增 `g_cst816s_last_reg`、`g_cst816s_error_reg` 和 `g_cst816s_touch_fingers` 诊断值，用于区分探测阶段、具体失败寄存器和实际触点数；不要在没有读取这些值前盲目修改坐标旋转。

## 2026-08-16 GUI Guider 字体回归

- 实机照片中出现中文标题重排/疑似乱码。检查 GUI Guider 生成页面后确认中文字库和设计文件本身正常，问题来自 `custom.c` 运行时替换字体、字号和描边，导致板端布局与 Guider 模拟器不一致。
- 已停用运行时字体覆盖，恢复 GUI Guider 生成的字体、字号和行高作为唯一显示基线；保留必要的单行/双行长文本策略。ARM 固件和 GUI 模拟器均重新编译通过。
- 已重新配置并构建 `gui/RA8P1/build/simulator`，修复旧构建缓存引用已删除 `custom/medical_ui.c` 的问题；GUI Guider 2.0 已打开 `gui/RA8P1/RA8P1.guiguider`，模拟器可启动。
- 触摸实机诊断仍为 `CST816S 0x15` 无 ACK（`probe_ok=0`、`error_reg=0xA7`），所以触摸问题仍停留在 I2C/供电/排线层，尚未进入 LVGL 事件层。

### J-Link 自动烧录状态

- 已找到本机 J-Link Commander：`C:\Users\Zhanglongsheng\.renesas\platform\DebugComp\Dialog\ARM\Segger\JLink.exe`。
- 当前 `flash.jlink` 已切换为 Debug S-record、1 MHz SWD，并作为后续每次修改后的默认烧录入口。
- 本次实际连接确认 `VTref=3.300V`、目标识别为 `R7KA8P1KF_CPU0`；但 J-Link 在 `loadfile` 结束时仍返回 `Writing target memory failed`，因此本次不能判定新固件已可靠烧录运行。后续必须以 J-Link 返回码为准，烧录失败时不读取触摸运行结果。

## 2026-08-16 标题单行与触摸轮询修正

- 修复范围扩大到所有 GUI Guider 页面：存药、取药日志、设备状态等延迟创建页面的页面标题和返回按钮也统一使用内容宽度、单行裁剪和零行距；放大字体后不会继承旧固定宽度而重新排成上下两行。
- 触摸输入设备现在显式绑定到 LVGL 实际显示对象，不再依赖“当前默认显示对象”，避免页面/显示初始化顺序导致 CST816S 读到了数据但 LVGL 不处理。
- CST816S 坐标包由原先 6 次单字节读取改为一次从 `0x01` 开始的连续 6 字节读取，减少摄像头与触摸共用 IIC0 时的事务数量，并保证手势、触点数和坐标来自同一个采样包。
- RASC 仍保持单个 IIC0（P409/P410），不增加第二个 I2C 实例；CST816S 使用 `0x15`，OV7725 使用 `0x21`，两者继续由共享总线互斥保护。
- Debug ARM 固件重新编译通过，RASC 只读审计通过。实机验证必须先烧录，再读取 `tools/jlink_screen_status.jlink`：若 `probe_ok=0`，先查 `error_reg/last_reg`、`0x15` ACK、3.3V、SDA/SCL 上拉和触摸排线；若 `fingers`/`pressed` 能变化但按钮不响应，才继续调整坐标映射。

## 2026-08-16 最新实测：重复 START 与标题布局

- 已详细复核 `屏幕`资料目录：原理图中的触摸接口为 `SDA0/SCL0/INT/T_RST`，触摸控制器的型号由同目录 ESP32-P4 触摸验证固件中的 `CST816S`、`esp_lcd_touch_new_i2c_cst816s`、`touch_cst816s_read_id` 等符号交叉确认；显示资料和树莓派参考驱动均确认 480x640、MIPI 1 Lane。
- 已修正 IIC0 事务模型：CST816S 的寄存器读取现在使用 FSP `R_IIC_MASTER_Write(..., true)` 保持总线，再调用 Read，形成重复 START；OV7725 仍使用原 STOP 分隔读法。两者仍由同一 IIC0 互斥保护，未新增 RASC IIC 实例。
- 本次重新编译、烧录并读取 J-Link 诊断：`g_cst816s_probe_ok=0`、`probe_fail=0x1BC`、`chip_id=0`、`fw_version=0`、`last_reg/error_reg=0xA7`；IIC 初始化互斥、地址切换和超时计数为 0，底层回调有 TX/RX/ABORT 事件。说明软件已执行到 CST816S 的 `0xA7` 探测阶段，但设备仍未返回有效 ACK；当前不把问题归因于 LVGL 触摸映射，也不修改用户已确认正确的硬件连接。
- J-Link 烧录日志仍出现 `Writing target memory failed`，但同一次下载已经完成 `Program` 和 `Verify`，复位后诊断变量持续运行并递增；因此该提示暂按 J-Link Commander 末尾返回异常处理，不能据此断言固件没有更新。
- 双行标题修正已收敛到 `gui/RA8P1/custom/custom.c`：不再运行时替换 GUI Guider 字体；对页面标题、卡片标题和返回按钮统一设置 `LV_LABEL_LONG_CLIP`，标题框使用明确宽高，禁止首帧按父容器重新换行。设计上需要两行的说明文字仍保留 `LV_LABEL_LONG_WRAP`，避免把正常副标题误当成乱码。
- 下一步验收顺序：先观察烧录后屏幕标题是否仍出现第二行；若标题正常，再继续做 CST816S 地址/ACK 诊断和坐标轮询。只有 `probe_ok>0` 后，才进入 LVGL 点击命中和坐标旋转测试。
## 2026-08-16 CST826 触摸与 IIC0 根因诊断（最新）

- 用户已确认屏幕触摸芯片型号为 **CST826**。CST826 常用 7 位 I2C 地址为 `0x15`；其芯片识别寄存器为 `0x11`，触点数量寄存器为 `0x02`，坐标数据从 `0x03` 开始。此前工程按 CST816S 读取 `0xA7/0xA9` 和 `0x01` 数据包，协议型号不匹配，已改为 CST826 协议。
- 代码仍保留 `src/driver/inc/cst816s_touch.h` 与兼容函数名，原因是避免改动 GUI/LVGL 输入层；文件内容和寄存器协议已经按 CST826 实现。后续稳定后可再做文件名重命名，不应在当前硬件联调阶段增加无关改动。
- 实机最新 J-Link 诊断：NVIC `ISER0=0x00FFFF20`，IIC0 IRQ 8~11 已使能；`IPR2=0xC0C0C0C0`，优先级为12，满足 FreeRTOS 调用要求。因此当前不是 RASC 未生成 IIC 中断。
- IIC0 硬件寄存器 `0x4025E000` 读数为 `ICCR1=0x9E`、`ICCR2=0x80`：`SCLI=1`、`SDAI=0`、`BBSY=1`。结论是 **SCL 已为高，但 SDA 被外部器件持续拉低，IIC 总线处于物理忙状态**；FSP 返回 `FSP_ERR_IN_USE=8`、没有正常 TX/RX 回调与 CST826 ID 是该状态的直接结果。
- 软件已增加 IIC 互斥、超时 Abort、Close/Open 重建和一次重试，并让 CST826 读事务可使用 STOP 分隔方式；这些措施能处理 MCU 内部状态残留，但不能强行释放一个被外部触摸芯片拉低的 SDA。
- 由于 `TP_RST` 当前未连接，RA8P1 无法独立复位 CST826 来释放 SDA。下一步硬件验收顺序固定为：断电重启触摸板/整板 → 万用表确认 SDA 空闲约3.3V、SCL约3.3V → 重新烧录 → 读取 `g_cst816s_chip_id/probe_ok`。若再次出现 SDA低电平，应补接 CST826 的 `TP_RST` 到可控 GPIO；`TP_INT` 可以继续不接并使用轮询，但 `TP_RST` 建议接出用于总线故障恢复。
- 该结论优先级高于 LVGL 按键、坐标旋转和中文布局问题；在 `BBSY=0` 且 `SDAI=1` 前，不继续修改触摸坐标映射，也不把触摸无响应归因于界面代码。
## 2026-08-16 触摸修复阶段切换（摄像头暂停）

- 用户已重新接入屏幕排线，本阶段暂停 RTT 摄像头预览修复，先完成 CST826 触摸验收。
- 已复核 `configuration.xml`：目标 `R7KA8P1KFLCAC`、FSP `6.3.0`、活动引脚配置 `RA8P1_CPKHMI.pincfg`，IIC0 仍为 `P409/SDA`、`P410/SCL`，RASC 结构审计通过。
- 发现并修复触摸读取的实质性错误：`src/driver/src/cst816s_touch.c` 原来调用 `camera_i2c_read_block_stop_at()`，实际发送 STOP；CST826 的寄存器读取需要“写寄存器地址后重复 START 再读数据”，现已改为 `camera_i2c_read_block_at()`。触摸初始化探测和连续坐标读取均走重复 START 路径。
- 本次修复未修改已经验证过的 OV7725/ZDT/CAN 协议，也未新增 RASC 外设；摄像头与触摸继续由 IIC0 互斥保护，地址分别为 `0x21` 和 `0x15`。
- RASC 审计通过；使用现有 `mingw32-make -C build\\Debug -j4` 全量链接成功，已生成新的 Debug S-record。下一步应烧录后优先读取 CST826 的 `probe_ok/probe_fail/chip_id/read_ok/read_fail`，再进行屏幕四角和按钮触摸测试。
- 本次烧录后的实机诊断：IIC0 总线已空闲（`ICCR1=0x9F`、`ICCR2=0x00`，不再是 `BBSY=1/SDAI=0`）；IIC 回调正常计数，未出现互斥、超时、地址切换或 Abort 错误。但 CST826 仍未 ACK，`probe_ok=0`、`probe_fail=0x5B`、`chip_id=0`、`last_reg/error_reg=0x11`。因此重复 START 修复已排除总线卡死问题，当前故障收敛为 CST826 未响应（供电/排线/上拉/TP_RST 状态或实际地址仍需核对），尚未进入 LVGL 坐标映射层。
- 2026-08-16 独立触摸轮询测试：为排除 LVGL 和摄像头线程影响，临时启用 `TOUCH_STANDALONE_TEST=1`；摄像头线程只初始化/轮询 CST826，LVGL 线程不创建触摸输入设备，摄像头 CEU/USB 也不启动。修正 CMake 传参后，Debug 固件全量编译成功并已烧录。
- 独立实机读数：`probe_ok=0`、`probe_fail=1`、`chip_id=0`、`fw_version=0`、`read_ok=0`、`read_fail=0`、`last_reg/error_reg=0x11`；IIC0 回调有 TX/RX/Abort，互斥、超时、地址切换错误均为 0，IIC0 寄存器显示总线空闲。结论：当前触摸无响应已与 LVGL 事件层、摄像头并发和总线卡死区分开，CST826 在当前候选地址/寄存器事务下未返回有效 ID；下一步保留独立模式，重点核对触摸控制板实际供电、SDA/SCL 上拉、实际地址和 TP_RST 状态。
- 协议回退实测：依据屏幕资料中的 CST816S 驱动符号，将独立测试改为 `0xA7/0xA9` ID/FW 寄存器和 `0x01` 起始的 6 字节触摸包。实机结果为 `probe_ok=1`、`detected_address=0x38`、`ack_address=0x38`，连续读取计数正常增长，说明此前 CST826 的 `0x11`/默认 `0x15` 判断不适用于当前触摸控制板。当前已将 `0x38` 设为 CST816S 首选地址，仍保持 LVGL 和摄像头隔离。
# 2026-08-16 软件根因梳理：共享 IIC0 与摄像头按需初始化

- 正式 LVGL 固件的运行时诊断显示：触摸读取在第一笔 IIC 事务前返回 `FSP_ERR_IN_USE (8)`，无 TX/RX 回调；这不是 CST816S 地址、LVGL 坐标映射或按钮命中问题。
- 已确认的软件流程 bug：`Camera_thread_entry()` 之前无条件执行 `camera_app_init()`，即使当前不在 Pickup/Scan 页面、摄像头未请求采集，也会启动 XCLK、给 OV7725 上电并访问共享 IIC0。摄像头未接、初始化失败或总线异常时，会影响 CST816S 轮询。
- 已修复为真正的按需初始化：只有 `gui_app_poll()` 请求 Pickup/Scan 预览后，`camera_app_service_capture()` 才初始化摄像头并启动 CEU；普通主页、药师管理、日志和系统状态页面不会初始化或采集摄像头。RTT 诊断模式仍可显式请求采集。
- 已将 `camera_i2c_init()` 从无返回值改为 `bool`。IIC 互斥量、完成信号、内存资源或 `R_IIC_MASTER_Open()` 失败时，摄像头/触摸会立即退出并保留诊断值，不再继续调用未就绪的 FSP 控制块。
- 已删除 `camera_drv.c` 中重复的 IIC 读计数器定义；共享总线仍由同一把 FreeRTOS 互斥锁保护，未修改已经验证过的 OV7725、CST816S 协议和 ZDT/CAN 报文。
- 本轮验证：`mingw32-make -C build\\Debug -j4` 全量编译、链接和 S-record 生成成功；`tools/rasc_config_audit.ps1 -Profile tools/rasc_config_profile.json` 审计通过。尚未把本轮固件烧录到实机，烧录后应优先读取 `tools/jlink_touch_status_only.jlink`，确认 `probe_ok`、`read_ok`、`last_err` 和 `FSP_ERR_IN_USE` 是否停止增长。

## 2026-08-16 最新烧录与运行诊断

- 已使用工程内最新 `build\\Debug\\26renesascup_smartmedicine_Ra8p1.srec` 执行 J-Link 烧录。J-Link 完成大部分下载后报告 `Writing target memory failed`，因此烧录工具返回码不能作为成功依据；但目标复位运行后诊断区已出现本轮固件的触摸轮询计数。
- 运行参数：`probe_ok=0`、`probe_fail=0x4D`、`chip_id=0`、`fw_version=0`、`last_reg/error_reg=0xA7`、`read_ok=0`、`read_fail=read_calls=0x138`。
- IIC 软件参数：`last_err=0x08`（`FSP_ERR_IN_USE`）、`addr_fail=0`、`mutex_timeout=0`、`init_mutex_timeout=0`、`timeout=0`、TX/RX/Abort 回调计数均为0；说明不是互斥锁、IRQ回调或地址切换错误。
- IIC0 硬件寄存器读取：`0x4025E000` 起始字节为 `ICCR1=0x9E`、`ICCR2=0x80`，即 `SDAI=0`、`SCLI=1`、`BBSY=1`。结论：SCL 已释放，但 SDA 仍被外部器件持续拉低；按需初始化修复已经生效，剩余故障是外部触摸控制器/屏幕控制板未释放 SDA，不能再归因于摄像头线程抢占或 LVGL。
- 当前硬件没有接 `TP_RST`，MCU 无法主动复位持续拉低 SDA 的触摸控制器。下一次硬件操作应先断电重启屏幕控制板/整板并测量 SDA、SCL 空闲电平；若 SDA 再次为低，必须接出 TP_RST 或暂时断开触摸 I2C 线后再验证。

## 2026-08-16 摄像头优先阶段：RTT 隔离固件归档

- 本阶段明确暂停屏幕和触摸联调，先验证 OV7725 的独立链路：`OV7725 SCCB -> CEU -> SDRAM 双缓冲 -> RTT/J-Link`。摄像头与 CST816S 虽然物理上共用 IIC0（P409/P410），但 RTT-only 构建通过 `CAMERA_RTT_ONLY=1` 跳过 LVGL 线程中的 CST816S 初始化、探测和轮询，不再由摄像头测试固件访问触摸控制器。
- `CMakeLists.txt` 新增 `RA8P1_CAMERA_RTT_ONLY`，当前 Debug 默认开启，并为摄像头线程定义 `CAMERA_RTT_PREVIEW_ENABLE=1` 与 `CAMERA_RTT_ONLY=1`。该模式会主动请求摄像头采集，但不会创建 CST816S LVGL 输入设备；恢复正常屏幕固件时使用 `-DRA8P1_CAMERA_RTT_ONLY=OFF`。
- 本轮 ARM 全量编译、链接和 S-record 生成成功。最新 map 已确认 RTT 帧缓冲地址为 `0x22052140`，尺寸为 `320×240 RGB565`；`tools/rtt_camera_viewer.ps1` 的默认帧地址与该值一致。
- 已修正 `tools/jlink_touch_status_only.jlink` 的诊断地址，避免将新版本 IIC 计数器误读为 PWDN/RST 或摄像头状态。新增可读取：OV7725 最后写入寄存器 `0x22117040`、IIC 恢复脉冲/成功/调用次数 `0x2216BCA4/0x2216BCA8/0x2216BCAC`、摄像头初始化状态 `0x220CC004...0x220CC018`。
- 当前尚未宣称实机 RTT 已恢复：必须烧录本轮 RTT-only S-record 后读取诊断值，再启动 `tools/rtt_camera_viewer.ps1`。重点判断顺序为：IIC0 `SDAI/SCLI/BBSY` 空闲 → `s_dbg_camera_init_attempts` 增长 → `s_dbg_ov_pid`/`s_dbg_ov_ver` 有效 → `s_dbg_ov_last_write_reg` 到达配置表末尾 → `s_dbg_ceu_frame_cnt` 增长 → RTT 窗口出现图像。
- 若 RTT-only 模式下 SDA 仍然为 0 V，则可排除 CST816S 软件初始化和 LVGL 触摸回调，故障只剩 OV7725/其模块供电、SCCB 上拉、PWDN/RST 电平、P409/P410 物理接线或摄像头器件持续占线；此时不再修改触摸代码。
## 2026-08-17 RTT摄像头基线回归排查

- 用户确认提交 `61ec65705808df6ecf8aa16a115115820f94580b` 曾经可以正常显示 OV7725 画面。本轮以该提交为软件基线，不再把当前现象先归因于硬件。
- 已确认当前固件中 OV7725 初始化成功：PID=`0x77`、VER=`0x21`、关键寄存器读回正常；`R_GPT_Open/Start`、`R_CEU_Open`、`R_CEU_CaptureStart` 均返回成功。
- 已确认当前异常点是 CEU 帧结束计数保持 0，且 CEU 错误计数也为 0；因此问题位于 CEU 输入时序/中断链路，尚未恢复 RTT 画面。
- 已恢复并验证的历史软件路径：摄像头线程直接初始化并启动采集、PWDN/RST 使用历史 GPIO 原子寄存器控制、RTT 模式跳过触摸 I2C 和 MIPI/GLCDC 初始化。
- 为严格复现历史基线，RTT-only 构建暂时关闭 D-Cache；该变化未使帧计数恢复，说明 D-Cache 不是当前唯一根因。
- 当前仍需继续对照 `61ec` 的 CEU 输入链路、启动时钟以及中断绑定。禁止在没有新的软件证据前修改 OV7725 操作指令和硬件接线结论。
- 本轮构建已成功生成 Debug ELF/S-record 并完成 J-Link 下载流程，但 J-Link 仍报告 `Writing target memory failed`；后续必须结合运行时诊断确认固件是否真正运行。

## 2026-08-17 摄像头 CEU 输入时序继续排查

- 以 `61ec65705808df6ecf8aa16a115115820f94580b` 为软件基线复核后，确认当前工程没有第二个线程同时打开 CEU；`camera_usb_thread_entry.c` 虽参与编译，但 RTT-only 入口没有调用它。
- 已修正 CMake 中 RTT-only 与屏幕采集宏重复定义的问题，当前摄像头诊断固件明确使用 `CAMERA_RTT_ONLY=1`、`CAMERA_RTT_PREVIEW_ENABLE=1`、`CAMERA_PREVIEW_SCREEN_ENABLE=0`，不会启动 MIPI/LVGL/触摸 I2C 流程。
- 已增加 CEU VSYNC 极性软件覆盖：OV7725 读回 `COM10=0x02`（VSYNC_NEG）后，将 CEU `CAMCR.VDPOL` 置为低有效；实测寄存器为 `CAMCR=0x12`，说明补丁已实际运行。
- 本轮实机结果：OV7725 `PID=0x77`、`VER=0x21`，IIC 初始化及寄存器访问成功；`CAPSR=1`、`CEIER=0x00110201`、`CMCYR=0`，但 `CSTSR=0`、CEU帧计数仍为0、DMA缓冲仍全0、CEU错误计数仍为0。
- 连续读取 `P708`（VSYNC）输入约30 ms，得到相同低电平；因此当前软件已完成“传感器可访问→CEU已启动→等待同步输入”的完整链路，异常集中在 P708 没有出现帧同步跳变。当前不再修改已经验证过的 OV7725 初始化表。
- `tools/jlink_camera_format_diag.jlink` 已同步更新为当前 map 地址，并增加 `CAMCR/CMCYR/CSTSR/P708` 读取。下一步应优先用示波器/逻辑分析仪确认 P109 XCLK、P708 VSYNC、P415 HREF、P414 PCLK；若必须继续纯软件 A/B，应以旧提交完整固件重建后逐寄存器对比，而不是继续叠加随机寄存器改动。

## 2026-08-17 GUI Guider 字体恢复

- 已将 GUI Guider 设计文件、生成屏幕代码、`gg_font.h` 和 `custom.c` 的活动字体恢复为原来的 `SourceHanSerifSC.otf` / `lv_font_SourceHanSerifSC_*`。
- 已恢复原有 Source Han 生成字体文件，并移除本轮新增的 ZhouZiFangTi 生成 C 文件；新增字体资源文件保留但不再被工程引用。
- 已同步更新两个 GUI 辅助脚本的默认字体名称，后续重新生成时仍会使用原字体。
- 本轮仅修改字体相关文件，未重新编译、未烧录；摄像头、屏幕、CMake 和 RTT 诊断改动均保留。

## 2026-08-17 摄像头 RTT 预览暂停归档

### 本轮目标

以已知成功提交 `61ec65705808df6ecf8aa16a115115820f94580b` 为基线，恢复 OV7725 的独立采集链路：XCLK → OV7725 SCCB/IIC0 → CEU → SRAM 双缓冲 → RTT/J-Link 预览；RTT-only 模式不启动 MIPI、LVGL 和触摸线程，避免共享 IIC0 互相影响。

### 已完成并验证

- 已对比 `61ec657`、`9f4e99a`、`af4c9df` 以及后续共享 IIC 改动，确认成功基线的关键条件为：GPT XCLK、OV7725 QVGA/YUV422、CEU data-synchronous、8-bit、双缓冲、D-Cache 关闭、CEU IRQ0。
- 已清理 CMake 中 RTT-only 与屏幕预览宏的重复追加。当前摄像头源文件实际编译宏为：`CAMERA_RTT_ONLY=1`、`CAMERA_RTT_PREVIEW_ENABLE=1`、`CAMERA_PREVIEW_SCREEN_ENABLE=0`，并使用 `BSP_CFG_DCACHE_ENABLED=0`。
- 已移除一个不属于已知成功提交的临时 `CAMCR.VDPOL` 强制覆盖，避免继续叠加未经验证的寄存器修改。
- 已重新生成并完整编译 Debug ELF/SREC，链接成功；仅有未使用诊断函数和第三方 quirc 的普通警告，无编译错误。
- 已通过 J-Link DLL 将新 SREC 烧录到 `R7KA8P1KF`，并成功启动 RTT 预览窗口。窗口能够连接 J-Link，但窗口标题中的 FPS 只是读取缓冲区次数，不能代替 CEU 实际帧计数。
- 新固件运行诊断：OV7725 `PID=0x77`、`VER=0x21`，`COM7=0x40`、`COM10=0x02`；GPT XCLK 的 Open/Start 错误均为 0；IIC 写入计数为 31、读失败为 0；`R_CEU_Open` 和 `R_CEU_CaptureStart` 返回成功；`CAPSR=1`、`CEIER=0x00110201`。
- 已连续读取 3 秒确认：`s_dbg_ceu_frame_cnt` 仍为 0，`s_dbg_ceu_evt_vd`、其他 CEU 事件计数也为 0；`CSTSR=0`，CEU DMA 缓冲区没有出现有效帧。因此当前不能宣称摄像头已经恢复。

### 当前结论

当前故障已经收敛到“传感器可通过 IIC 配置，但 CEU 没有收到有效帧同步/采集事件”，不是 OV7725 地址、SCCB 初始化失败、RTT 窗口或 CMake 宏重复导致。运行时 `CAMCR=0x10`，`CMCYR=0`，P708 VSYNC 采样值在复读期间没有出现跳变；但仅凭 J-Link 的 PFS 快照不能证明真实引脚波形。

此前文档中“已应用 VDPOL=1、CAMCR=0x12”的描述属于中间实验记录，当前代码已撤回该临时覆盖，实际新固件读回的是 `CAMCR=0x10`。恢复任务时必须先明确 OV7725 COM10 的 VSYNC 有效电平与 CEU `VDPOL` 的对应关系，再做单变量 A/B 烧录，不应继续随机叠加寄存器修改。

### 暂停时保留的工作树状态

- 保留 `CMakeLists.txt` 的 RTT-only 宏整理、摄像头诊断和 IIC 互斥改动；未修改 OV7725 已验证寄存器表。
- 保留新生成的 `build/Debug/26renesascup_smartmedicine_Ra8p1.elf` 与 `.srec`，但后续恢复任务前仍需以实际烧录后的寄存器和帧计数复核。
- RTT 预览窗口已关闭，避免继续占用 J-Link；本轮未修改屏幕、触摸和 LVGL 显示逻辑。

### 恢复任务后的最短验证路径

1. 先用示波器或逻辑分析仪确认 P109 XCLK、P708 VSYNC、P415 HREF、P414 PCLK，尤其确认 P708 是否有帧频跳变。
2. 软件只改变一个变量：先分别验证 `CAMCR.VDPOL=0/1`，读取 `CAMCR`、`CAPSR`、`CSTSR`、`CETCR` 和 `s_dbg_ceu_frame_cnt`。
3. 若 VSYNC 无波形，回到 XCLK/PWDN/RST/传感器输出使能链路；若 VSYNC 有波形但无 CEU 事件，再检查 CEU 输入边沿、HREF 极性和 RASC 生成寄存器。
4. 只有 `s_dbg_ceu_frame_cnt` 稳定递增后，才重新启动 RTT 预览并恢复屏幕/触摸集成。

## 2026-08-17 摄像头暂停前最终诊断

- 已确认上电复位电平：`s_dbg_power_levels=0x02`，即 `PWDN=0`、`RST=1`；因此当前不是摄像头被掉电或持续复位。
- 已确认 `XCLK` 持续翻转，OV7725 `PID=0x77`、`VER=0x21`、`COM7=0x40`、`COM10=0x02`，IIC 读写无错误。
- CEU 已启动：`CAPSR=1`、`CAMCR=0x12`、`CEU` 打开/采集启动无错误；但 `PCLK/HREF/VSYNC` 采样计数仍为 0，`s_dbg_ceu_frame_cnt=0`，DMA 缓冲区全 0。
- 已进行单变量 A/B：普通 YUV 配置的 `COM3` 从 `0x50` 改为标准输出 `0x00`，烧录后仍无 PCLK/HREF/VSYNC 和帧计数变化。该变化保留用于后续回退对比。
- 当前根因已收敛为“OV7725 并行视频输出端没有到达 RA8P1 输入脚”，尚不能归因于 RTT 窗口、LVGL、SRAM 或 IIC0 互斥。恢复任务前应优先用示波器/逻辑分析仪确认 `P109(XCLK)`、`P414(PCLK)`、`P415(HREF)`、`P708(VSYNC)` 的实际波形；不要继续无依据叠加寄存器修改。
- 本次已重新编译并烧录 Debug SREC；J-Link 末尾仍出现 `Writing target memory failed`，但 `Program/Verify` 已完成，运行时诊断变量可读，暂记录为工具尾部告警。
- 后续已恢复到 `61ec657` 的关键传感器配置：`COM3=0x50`、不额外写 `COM2`、不在 CEU 启动后强制覆盖 `CAMCR.VDPOL`；重新烧录后仍为 `CAPSR=1`、`PID=0x77`、`s_dbg_ceu_frame_cnt=0`，说明这些临时 A/B 修改不是根因。
- 又完成一次模块资料厂商表 A/B：`COM7=0x46`、厂商完整寄存器表、`COM2=0x03`，烧录后仍无 PCLK/HREF/VSYNC 和帧计数；测试后默认值已恢复为 `CAMERA_AB_VENDOR_TABLE=0`。软件寄存器表差异基本排除。
- LVGL 侧已修正网络状态运行时文字，统一使用 UTF-8 的“系统在线/系统离线”；单行标题继续使用 `LV_LABEL_LONG_CLIP` 和固定标题框，说明文字才允许 `LV_LABEL_LONG_WRAP`。本次只修改 `gui/RA8P1/custom/custom.c`，未改 GUI Guider 生成画布。

## 2026-08-17 摄像头历史固件交叉验证（最新）

- 已将工程内保存的完整 `61ec65705808df6ecf8aa16a115115820f94580b` S-record 实际烧录到同一块 RA8P1，并等待 8 秒后读取硬件状态；结果与当前固件一致：`CAPSR=1`、`CAMCR=0x10`、`CSTSR=0`，没有 CEU 事件和帧结束计数。
- 旧固件运行时直接读取端口输入寄存器：P414/PCLK=`1`、P415/HREF=`1`、P708/VSYNC=`1`，等待 3 秒后仍保持不变；因此不是当前源码新增的 RTT、LVGL、共享 IIC 或链接布局造成的差异。
- 旧固件和当前固件的 CEU 缓冲区地址相同（`g_ceu_buffer_0=0x220E13C0`、`g_ceu_buffer_1=0x220BBBC0`），且两者均为 XCLK 有活动、并行时序输入无跳变。
- 交叉验证后已重新烧录当前正式固件，当前板上不是历史测试固件。正式默认参数保持：`COM3=0x50`、`COM7=0x40`、`COM10=0x02`、`CAMERA_AB_CLOCK_BYPASS=0`、`CAMERA_AB_VENDOR_TABLE=0`。
- 诊断脚本中原先的“连续 PFS”读取命令并非有效的 RA8P1 PFS 地址排列，已使用独立地址脚本核对：P414=`0x40400938`、P415=`0x4040093C`、P708=`0x40400A20`、P400=`0x40400900`、P703=`0x40400A0C`。后续读取必须使用这些地址，不能依据失败的整段读取结果下结论。
- 当前软件证据已足够说明：OV7725 的 SCCB 控制链路和 RA8P1 CEU 启动链路都能运行，但视频输出脚在 MCU 侧没有产生有效跳变。下一步最有效的实测是用示波器/逻辑分析仪同时确认模块端与 MCU 端的 `XCLK、PCLK、HREF、VSYNC`；在取得波形前不再继续叠加寄存器表修改。

## 2026-08-17 摄像头暂停后的清理与最终复核

- 已删除未被使用的 `ceu_recover_from_error()` 诊断恢复函数及其计数变量，避免把未经验证的自动恢复逻辑留在正式固件中；OV7725 正式寄存器表保持历史成功基线，不再保留 A/B 实验宏。
- 清理后 Debug 固件已重新编译成功，并重新通过 J-Link 下载、复位运行。J-Link 末尾仍报告 `Writing target memory failed`，但同时报告目标内容已匹配；运行态寄存器和诊断变量可正常读取，因此该提示暂按工具尾部告警处理，后续仍以实际运行状态为准。
- 清理后实测结果仍为：PID=`0x77`、VER=`0x21`、COM7=`0x40`、COM10=`0x02`、CAPSR=`1`、CAMCR=`0x10`、CSTSR=`0`；IIC 写入 31 次、读取失败 0 次；CEU 帧计数、VD/HD/field 事件计数及两个 DMA 缓冲区仍为 0。
- 已修正诊断脚本中的 PFS 读取方式，使用独立有效地址：P414=`0x40400938`、P415=`0x4040093C`、P708=`0x40400A20`、P400=`0x40400900`、P703=`0x40400A0C`。实测 P414/P415/P708 端口采样值分别固定为高电平，未观察到 PCLK/HREF/VSYNC 跳变；P109 XCLK 计数仍在变化。
- 已将保存的 `61ec65705808df6ecf8aa16a115115820f94580b` 完整 S-record 实际下载到同一块板，并得到相同的 PCLK/HREF/VSYNC 固定高电平、CEU 无事件、帧计数为 0 结果。由此可排除“当前源码新增 RTT/LVGL/IIC 互斥逻辑导致摄像头失效”这一解释。
- 当前摄像头根因已收敛为“OV7725 模块视频输出到 RA8P1 CEU 输入脚之间没有有效同步波形”，但仅凭 J-Link 端口快照不能区分模块端无输出、排线/引脚映射问题或 MCU 端采样点无波形。恢复任务时必须优先用示波器/逻辑分析仪同时测量模块端和 MCU 端的 XCLK、PCLK、HREF、VSYNC；在取得波形前不再随机修改 OV7725 寄存器。

## 2026-08-17 极简上下文后的摄像头 A/B 结论

- 已根据当前 ELF map 修正 `tools/jlink_camera_format_diag.jlink`：删除不存在的旧 `s_dbg_ceu_camcr` 地址，更新摄像头初始化诊断变量名称；当前诊断地址与 `nm` 符号一致。
- `COM7=0x40` 的历史灰度/YUV 路径实测仍无帧；临时开启内部彩条后 `DSP_CTRL3=0x20` 可读回，证明彩条写入和 IIC 事务确实执行，但 PCLK/HREF/VSYNC 仍无有效翻转，`s_dbg_ceu_frame_cnt` 仍为 0。
- 当前诊断：PID=`0x77`、VER=`0x21`、PWDN/RST=`0x02`、XCLK 有翻转、IIC 写入 31 次且失败 0 次、CEU Open/CaptureStart 成功；因此已排除 OV7725 地址访问失败、RTT 窗口、LVGL、SRAM 缓冲区和 CEU API 返回错误。
- 临时彩条宏已恢复为关闭状态（`CAMERA_COLOR_BAR_DIAG=0`）；当前不保留彩条测试固件。正式摄像头修复的下一步不是继续堆寄存器，而是用示波器/逻辑分析仪同时测量模块端和 RA8P1 端的 `XCLK/PCLK/HREF/VSYNC`，确认信号是否在排线或模块输出端已经消失。

- 依据 OV7725 数据手册，单独测试了 `COM2=0x03`（并行输出 4×驱动）；寄存器写入读回正确，但同步脚和帧计数仍无变化，已回退该 A/B，不保留在正式配置中。

## 2026-08-17 摄像头帧率与复位时序复核

- 需要区分“历史实际采集帧率”和“当前 RTT 窗口刷新率”：历史调试记录曾出现 VSYNC 约 30 Hz、CEU 帧计数达到 3，说明当时确实收到过有效视频帧；RTT 窗口标题中的 `2 FPS` 只是 J-Link 读取/显示缓冲区的刷新频率，不是摄像头实际帧率。
- 当前正式基线实测 `s_dbg_ceu_frame_cnt=0`，因此当前实际采集帧率是 **0 FPS**，不能用 RTT 窗口标题判断已经恢复。
- 临时打开 OV7725 内部彩条后，`DSP_CTRL3=0x20` 能正确读回，证明彩条配置写入成功；但 PCLK/HREF/VSYNC 仍无跳变、CEU 帧计数仍为 0。彩条测试宏已关闭并回退。
- 临时将 XCLK 有效后的 RST 低电平保持时间由 1 ms 延长至 10 ms，重新编译、烧录并读取诊断，结果仍无同步波形；该时序已回退为历史基线的 1 ms。
- 当前软件状态恢复为正式基线：`COM3=0x50`、`COM7=0x40`、`COM10=0x02`、`CLKRC=0x00`、彩条关闭、复位低电平 1 ms。下一步必须在模块端和 RA8P1 端同时测量 `XCLK/PCLK/HREF/VSYNC`，否则继续修改寄存器无法形成可靠证据。

## 2026-08-17 摄像头正式基线复核

- 已回退 `COM3=0x56` 临时 A/B，正式源码恢复为历史基线 `COM3=0x50`；重新编译、下载并校验通过。
- `COM3=0x56` 曾能正确读回，但没有产生 `PCLK/HREF/VSYNC` 波形，因此不是当前无帧问题的有效修复。
- 当前可靠证据仍是：OV7725 I2C 读写正常、PID/VER 正确、XCLK 有翻转、CEU 启动无错误，但同步信号与 CEU 帧计数均为 0。
- 诊断脚本已同步到当前 ELF 符号地址，后续不得使用旧的 `COM7` 地址。
- 下一步必须测量摄像头模块端与 RA8P1 端的 `XCLK/PCLK/HREF/VSYNC`，据波形定位模块输出、排线/映射或 MCU 输入端问题；在此之前不再随机修改传感器寄存器。

- 追加验证：将历史彩色路径 `COM7=0x46` 单独烧录测试，寄存器读回正确，但同步脚仍无跳变、帧计数仍为 0；已恢复正式值 `COM7=0x40`，未把该试验参数留在工程中。
- 追加验证：尝试 `PWDN=1/RST=0 → 释放 PWDN → 释放 RST` 的暖复位时序，仍无 `PCLK/HREF/VSYNC` 跳变；该 A/B 已回退，正式固件恢复历史 GPIO 时序并重新编译烧录。
- 当前板上固件与工作区正式源码一致；J-Link 下载过程仍显示历史性的 `Writing target memory failed`，但 Program/Verify 已完成，不能把该提示当作摄像头帧恢复证据。
## 2026-08-17 极简上下文后的摄像头正式复核

- 当前 Debug 构建缓存确认：`RA8P1_CAMERA_RTT_ONLY=ON`、`CAMERA_RTT_ONLY=1`、`CAMERA_RTT_PREVIEW_ENABLE=1`、`CAMERA_PREVIEW_SCREEN_ENABLE=0`，并关闭 D-Cache；本次验证没有启用 MIPI、LVGL 或触摸线程。
- 本次重新执行 `mingw32-make -C build/Debug -j4`，ARM ELF/SREC 构建成功；随后使用 `flash.jlink` 烧录并复位运行。J-Link 末尾仍显示历史性的 `Writing target memory failed`，但程序内容已可运行并能被 J-Link 读取，后续仍以运行态变量为准。
- 运行态证据：`PID=0x77`、`VER=0x21`、`COM7=0x40`、`COM10=0x02`；XCLK 计数持续变化；`R_CEU_Open` 和 `R_CEU_CaptureStart` 均返回成功，`CAPSR=1`。
- 未解决证据：PCLK(P414)、HREF(P415)、VSYNC(P708) 没有跳变，`CSTSR=0`，CEU 帧结束/错误事件均为 0，两个 CEU DMA 缓冲区仍无有效帧。因此当前 RTT 黑屏的根因仍收敛为“OV7725 并行视频时序没有到达 RA8P1 CEU 输入端”，不能归因于 RTT 窗口、SCCB 地址、LVGL、SDRAM 或共享 I2C 互斥。
- 后续恢复摄像头时只做端到端波形验证：同时测量模块端和 RA8P1 端的 XCLK、PCLK、HREF、VSYNC，确认模块输出、排线连续性、引脚映射和 MCU 采样端；在获得波形证据前不再随机叠加 OV7725 寄存器 A/B 修改。
- 屏幕/LVGL 与触摸问题暂不在本次 RTT 固件中验证；正式共线测试必须先确认摄像头独立帧计数递增，再恢复 CST816S 与 OV7725 共用 IIC0 的运行回归。

## 2026-08-17 恢复任务继续后的最新 A/B 结果

- 已重新编译、烧录并读取当前板端运行状态；`COM7=0x46`（历史 RGB565 输出）已实际生效，但 PCLK/HREF/VSYNC 仍无跳变，CEU 帧计数仍为 0。
- 已临时开启 OV7725 内部彩条 `COM3=0x51 + DSP_CTRL3=0x20` 做端到端验证；寄存器读回为 `DSP_CTRL3=0x20`，但同步脚和 CEU 事件仍为 0，随后已关闭彩条并恢复正式配置。
- 本轮 A/B 已撤销，正式源码恢复为 `COM7=0x40`、`COM3=0x50`、`CAMERA_COLOR_BAR_DIAG=0`，不保留实验模式。
- 当前最强证据仍是：IIC/PID/寄存器写入、XCLK、CEU Open/CaptureStart 均正常，但视频同步没有到达 RA8P1 输入端；下一步必须测量模块端与 MCU 端的 `XCLK/PCLK/HREF/VSYNC`，不能继续无依据叠加寄存器修改。
## 2026-08-17 极简上下文后的准确运行态诊断

- 已用当前 ELF 的 `arm-none-eabi-nm` 重新核对 J-Link 诊断地址，确认脚本没有把计数器读错；此前只是把 VSYNC/HREF/PCLK/XCLK 四组计数的顺序读反了。
- 当前实测：初始化失败步骤为 0，IIC 写入 31 次、读失败 0 次，CEU Open/CaptureStart 返回 0；OV7725 PID/VER 为 `0x77/0x21`，PWDN/RST 状态为 `0x02`，XCLK 持续翻转。
- 四组视频输入实际计数为：P708 VSYNC 翻转 0、P415 HREF 翻转 0、P414 PCLK 翻转 0，均采样为高电平；因此当前不是 RTT 窗口、LVGL、SRAM、IIC 访问失败或 CEU API 返回错误。
- 已撤销本轮无效的 `CAMCR.VDPOL` 强制覆盖，并将 `configuration.xml` 的 CEU VSYNC 极性恢复为历史生成值；不把未证明的极性实验留在正式固件中。
- 下一步必须在 OV7725 模块端和 RA8P1 端同时测量 `XCLK/PCLK/HREF/VSYNC`，定位模块输出、排线连续性、引脚映射或 MCU 输入采样端；在获得波形证据前暂停继续随机修改寄存器表。

## 2026-08-17 上下文压缩后的工作记录

- 已完成 GUI Guider 模拟器增量构建，`gui/RA8P1/platform/simulator/build/bin/simulator.exe` 的 Ninja 构建通过。
- 已检查 LVGL 刷新链路：逻辑画布为 `640x480`，面板扫描区为 `480x640`，使用单次 CPU 旋转后提交到 GLCDC；当前没有修改这条已能显示纯色和界面的路径。
- 已在 `gui/RA8P1/custom/custom.c` 强化单行/双行标签策略：单行标签固定为 `LV_LABEL_LONG_CLIP`、左对齐、零行距；说明文字才使用 `LV_LABEL_LONG_WRAP`。这避免 GUI Guider 生成的固定宽度标签在实机首帧重新排版成异常双行。
- 已修正运行时网络徽标文本为 UTF-8 的“系统在线/系统离线”，并在更新文本后重新应用单行布局；没有修改 GUI Guider 画布文件和用户的封面配色。
- 摄像头仍未恢复：当前最新实测为 OV7725 SCCB 正常、XCLK 有输出，但 PCLK/HREF/VSYNC 固定高电平、CEU 帧计数为 0。下一次恢复摄像头必须继续基于波形和历史成功版本比对，不把 GUI 修改误判为摄像头根因。
- 当前未执行清理测试文件、提交或远程同步；后续完成实机字体/双行验证和摄像头修复后，再集中清理并归档。

## 2026-08-17 GPT10/XCLK 历史成功路径与当前复核

- 历史成功修复的关键是 RASC 中 `GPT10` 的 XCLK 配置：单位选择 `unit_frequency_khz`，请求频率 `24000 kHz`，占空比 `50%`，启用 GTIOCA 输出到 `P109`。FSP 将周期舍入为 `GTPR=0x09`，实际输出约 `25 MHz`；随后 VSYNC 曾出现约 `30 Hz` 脉冲，CEU 开始采集。
- 当前 ELF 与板端实测已确认这条配置仍然生效：GPT10 `GTCR=0x00000001`（运行中）、`GTPR=0x00000009`、`GTCNT` 持续变化、`GTIOR=0x00000001`。因此当前“没有帧”不能再归因于 GPT10 未启动或 XCLK 周期配置错误。
- 当前 OV7725 读回：`PID=0x77`、`VER=0x21`、`CLKRC=0x00`、`COM3=0x50`、`COM4=0x41`、`COM7=0x40`、`COM10=0x02`、`COM12=0x03`；SCCB 写入/读取失败数为 0。
- 当前仍未解决：PCLK、HREF、VSYNC 在 RA8P1 端没有跳变，CEU 帧计数为 0。下一步应测量 OV7725 模块端和 RA8P1 端的 `XCLK/PCLK/HREF/VSYNC`，定位模块输出、排线/引脚映射或 MCU 输入端问题，不再重复修改已经确认正确的 GPT10 配置。
- `tools/jlink_camera_format_diag.jlink` 已补充 GPT10 运行寄存器及当前 ELF 布局下的 OV7725 关键寄存器读取；以后重新链接导致地址变化时，必须先用 `arm-none-eabi-nm` 更新诊断脚本。
- 诊断地址复核：此前脚本仍使用旧布局，把 `0x2215B404/0x2215B408` 误标为 CEU 错误码；当前 ELF 中正确地址为 `s_dbg_ceu_cap_err=0x2215B40C`、`s_dbg_ceu_open_err=0x2215B410`、`s_dbg_ceu_camcr=0x2215B408`。按正确地址读取后，CEU Open/CaptureStart 错误均为 0；当前仍只有 `PCLK/HREF/VSYNC` 无跳变、帧计数为 0。
- 已完成一次严格的历史格式回归：临时将 OV7725 最后的 `COM7` 改为 `0x46`（历史 `9f4e99a` 基线的 QVGA/RGB565），重新编译、烧录并读回确认；`COM7=0x46` 仍没有 PCLK/HREF/VSYNC 或 CEU 帧计数。该变量已撤销，正式源码恢复 `COM7=0x40`（QVGA/YUV422）。
- 又完成一次严格的时钟寄存器回归：临时将 `CLKRC` 改为 CPK 记录中的 `0x80`，实测读回正确，但 PCLK/HREF/VSYNC 和帧计数仍为 0；该变量已撤销，正式源码恢复 `CLKRC=0x00`，并已重新编译烧录。

## 2026-08-17 当前摄像头运行态复核（COM7=0x46）

- 本轮依据历史有效帧记录，重新采用 `COM7=0x46`（QVGA、RGB565）进行单变量验证；已重新编译、烧录并用 J-Link 读取运行态寄存器，确认芯片实际读回 `COM7=0x46`。
- 当前板端读回：OV7725 `PID=0x77`、`VER=0x21`、`COM10=0x02`、`COM3=0x50`、`COM4=0x41`、`CLKRC=0x00`；SCCB 写入 31 次、读取失败 0 次；PWDN/RST=`0x02`；GPT10=`GTCR=1/GTPR=9/GTIOR=1`，XCLK 确认在运行。
- CEU `Open/CaptureStart` 返回成功，`CAPSR=1`；但是 PCLK(P414)、HREF(P415)、VSYNC(P708) 仍无翻转，CEU 事件计数和 `s_dbg_ceu_frame_cnt` 均为 0，DMA 缓冲区没有有效图像。
- 结论：本轮已经排除“COM7 色彩/输出格式”和“XCLK 没启动”这两个软件猜测；当前实际采集帧率仍为 **0 FPS**。RTT 窗口标题显示的 `2 FPS` 仍只是 J-Link 主机轮询刷新率，不能当作摄像头帧率。
- 诊断脚本本轮同步修正了当前 ELF 的运行态地址：电源状态、I2C 计数和四组同步脚计数均按当前 `nm` 符号读取。当前同步脚采样为 VSYNC/HREF 高、PCLK 低且无跳变，证据可信度高于旧脚本结果。
- 下一步采用单变量 CEU 输入边沿 A/B（只改变 DSEL/HDSEL/VDSEL），然后立即复测同步脚和帧计数；如果仍为 0，必须在 OV7725 模块端与 RA8P1 端同时测量 `XCLK/PCLK/HREF/VSYNC`，定位模块输出、排线映射或 MCU 输入端问题。屏幕触摸/I2C0 共线暂不重新接入摄像头测试链路。

## 2026-08-17 CEU 输入边沿单变量 A/B 结果

- 临时将 CEU `DSEL/HDSEL/VDSEL` 置 1，未修改 OV7725 寄存器、XCLK、I2C 或屏幕配置；板端确认 `CAMCR=0x0D000010`，说明该 A/B 确实生效。
- 结果仍为 `PCLK=0`、`HREF=0`、`VSYNC=0` 翻转计数，CEU 事件计数为 0，`s_dbg_ceu_frame_cnt=0`；因此 CEU 输入边沿选择不是当前无帧根因。
- A/B 已撤销，正式源码恢复 RASC 生成的 CEU 边沿配置。当前应停止继续修改 CEU 位，优先对摄像头模块端和 RA8P1 端做端到端波形测量：确认模块是否输出 PCLK/HREF/VSYNC，以及这些信号是否分别到达 P414/P415/P708。

## 2026-08-17 历史帧率证据与当前板端状态的区分

- `.omo/progress/2026-08-07_ceu-capture-debug.md` 记录的历史运行确实曾测到约 30 Hz VSYNC、PCLK 方波和短 HREF，并观察到 CEU 事件；`.omo/progress/2026-08-07_ceu-link-established.md` 还记录过 `s_dbg_ceu_frame_cnt=3` 和真实场景数据。这些是有效的历史成功证据，不是 RTT 窗口标题推测。
- 但是，保存的 `61ec65705808df6ecf8aa16a115115820f94580b` 完整 S-record 后续在当前同一块板复测时，PCLK/HREF/VSYNC 已固定、帧计数为 0。说明“曾经成功”和“当前仍有同样波形”不能混为一谈，板端外部状态或摄像头输出状态在两个时间点之间已经发生变化。
- 当前 RASC 审计已通过：GPT10/P109、CEU P414/P415/P708、D0-D7 复用均正确；当前 Debug 也已重新编译并烧录。现阶段最强证据仍是运行态同步波形缺失，不能继续把历史约 30 FPS 当作当前帧率。

## 2026-08-17 摄像头恢复任务：格式与 IIC 路径 A/B 结论

- 根据 `61ec65705808df6ecf8aa16a115115820f94580b` 的实际源码，最后一笔 OV7725 格式寄存器是 `COM7=0x40`（QVGA/YUV422），与当前 CEU 的 YCbCr422 输入配置一致；当前正式源码已恢复为该值，不再使用 `0x46` RGB565。
- 临时将当前 RTT-only 固件的每笔 `slaveAddressSet()` 跳过，使 IIC0 访问路径完全贴近历史成功版本；重新烧录后 PID/寄存器读写、GPT10 和 CEU API 仍正常，但 PCLK/HREF/VSYNC 与帧计数仍为 0。该差异已排除，正常 GUI 路径仍保留摄像头/触摸地址切换与总线互斥。
- 临时启用 OV7725 内部彩条（`COM3=0x51`、`DSP_CTRL3=0x20`）做传感器内部输出验证；`DSP_CTRL3=0x20` 写入成功，但 CEU 仍无同步事件，随后已关闭彩条并恢复正式配置。
- 本轮最终工作区状态：`COM7=0x40`、`COM3=0x50`、`COM10=0x02`、`CLKRC=0x00`、彩条关闭；Debug 编译和烧录流程完成。当前实际采集帧率仍为 **0 FPS**，历史约 30 Hz VSYNC/约 20 FPS 采集记录仅作为成功基线。
- 结论进一步收敛为：当前不是 OV7725 SCCB、COM7 格式、GPT10 启动、IIC 地址切换、CEU API 或 RTT 窗口问题；仍需同时测量摄像头模块端和 RA8P1 端 `XCLK/PCLK/HREF/VSYNC`，确认模块输出及排线到 P109/P414/P415/P708 的连续性后才能继续软件定位。

## 2026-08-17 低有效 VSYNC 单变量复核

- 根据 OV7725 `COM10=0x02` 的低有效 VSYNC 设置，将 RASC 源配置 `module.driver.ceu.vsync_polarity` 临时改为 `low`；由于本机 RASC 独立生成器缺少 RA8P1 device family pack，无法自动生成，故仅为 A/B 验证临时同步修改了 `ra_gen/Camera_thread.c` 的生成结果。
- 板端确认 `CAMCR=0x00000012`，说明低有效 VSYNC 配置确实进入 CEU；OV7725 PID/VER=`0x77/0x21`、SCCB 读写正常、GPT10 XCLK 仍运行、CEU Open/CaptureStart 无错误。
- 结果仍为 VSYNC/HREF/PCLK 翻转计数全部为 0，`s_dbg_ceu_frame_cnt=0`，等待约 3 秒后仍无 CEU 事件。因此 VSYNC 极性不是当前无帧根因，不能把 `2 FPS` 窗口标题当作实际采集帧率。
- 当前正式配置应以 `configuration.xml` 为准；`ra_gen/Camera_thread.c` 的低有效修改仅为验证产物，后续须在本机安装匹配的 RA8P1 device family pack 后重新用 RASC 生成并复核，避免手工生成文件与 XML 漂移。
- 当前实际采集帧率：**0 FPS**。历史成功版本曾有约 30 Hz VSYNC、帧计数递增和约 1–2 FPS RTT 预览显示；两者属于不同运行状态。

## 2026-08-17 历史固件隔离与传感器复位 A/B

- 为避免把当前工程差异误判为根因，已直接烧录工作区保存的 `61ec65705808df6ecf8aa16a115115820f94580b` 历史 S-record，并使用该版本对应的诊断脚本读取运行态；该历史固件在当前板上同样得到 `PCLK/HREF/VSYNC` 无跳变、CEU 帧计数为 0。
- 这项隔离测试证明：当前无帧状态不能仅归因于本轮 GUI、LVGL、IIC 互斥、RASC XML 或当前摄像头应用层改动；历史成功记录与当前板端状态之间存在外部状态差异。
- 当前正式摄像头复位时序已增强为：`PWDN=1、RST=0` 保持 10 ms → `RST=1` 保持 20 ms → `PWDN=0` 唤醒 → 再等待 50 ms。该修改用于处理 OV7725 模块在仅 MCU 复位时仍保持并行输出关闭的情况；本轮烧录后仍未产生同步信号。
- 当前最强证据：OV7725 `PID/VER=0x77/0x21`、31 次 SCCB 写入且读失败为 0、GPT10 XCLK 运行、CEU Open/CaptureStart 无错误，但摄像头模块没有向 P414/P415/P708 输出有效 PCLK/HREF/VSYNC。
- 下一步不能再用 RTT 标题判断帧率，也不应继续随机改寄存器；必须在摄像头模块连接器侧和 RA8P1 端分别测量 `XCLK(P109)`、`PCLK(P414)`、`HREF(P415)`、`VSYNC(P708)`，并检查模块供电、PWDN/RST 实际电平及排线连续性。只有确认模块端有波形后，才继续 CEU 软件参数 A/B。
## 2026-08-17 摄像头历史成功路径复核与当前问题归档

### 用户提供的历史成功证据

用户提供的历史任务截图显示，摄像头曾经在同一块 RA8P1 上成功运行：

- `s_connected = 1`：USB 已枚举；
- `s_port_open = 1`：CDC 端口已打开；
- `CEU frame_cnt = 0x33`：已采集 51 帧；
- 图像缓冲区存在持续变化的真实数据；
- `OV7725 PID = 0x77`、`COM10 = 0x02`，引脚状态正常；
- 当时由于 CMake 自动调用 RASC 的临时目录权限问题，使用 e2studio 已生成的 Peripheral 代码，并最终改用 ELF 烧录。

因此，“当前一定是硬件损坏”不能作为结论；历史成功固件/构建链路必须继续作为对照基线。

### 本轮已完成的验证

1. 核对 `flash.jlink`：原脚本烧录的是 `build/Debug/26renesascup_smartmedicine_Ra8p1.srec`。
2. 新增临时 ELF 烧录脚本 `.tmp/flash_current_elf.jlink` 和 `.tmp/flash_current_elf_full.jlink`。
3. 执行过“整片内部 Flash 擦除 → 当前 ELF 写入 → J-Link 下载阶段 Verify → 复位运行”。日志确认：
   - 内部 Flash 擦除完成；
   - ELF 实际编程约 57 KiB；
   - 下载阶段完成 Program/Verify；
   - J-Link 最后仍显示 `Writing target memory failed`，但该提示在本工程历史中属于 Commander 尾部异常，不能单独作为运行失败依据。
4. 当前运行诊断始终为：
   - OV7725 `PID=0x77`、`VER=0x21`；
   - SCCB/IIC 写入 31 次，读失败 0 次；
   - `COM10=0x02`、`COM3=0x50`、`COM4=0x41`；
   - `R_CEU_Open=0`、`R_CEU_CaptureStart=0`、`CAPSR=1`；
   - `CEU frame_cnt=0`，CEU 事件计数仍为 0，图像缓冲区全 0；
   - `PCLK/HREF/VSYNC` 端口采样没有有效跳变。

### 本轮 A/B 修改及结论

- 将 OV7725 最后一个 `COM7` 从 `0x40` 改为历史彩色路径 `0x46`，实机读回正确，但仍无帧；
- 将 `CLKRC` 改为历史 CPK 路径记录的 `0x80`，实机读回正确，但仍无帧；
- 将生成的 CEU `DSEL/HDSEL/VDSEL` 改为 `1/1/1`，实机 `CAMCR=0x0D000010`，配置确实生效，但仍无帧；
- 在 GPT10 `R_GPT_Start` 后调用 `R_GPT_OutputEnable(..., GPT_IO_PIN_GTIOCA)`，并尝试直接设置 GTIOCA 输出使能；实机 `GTIOR` 仍读回 `0x00000001`，尚未观察到输出使能位变化。

当前工作区暂时保留上述 A/B 结果，尚未宣称修复完成。后续应先读取 `g_timer_xclk_ctrl` 的运行态 `p_reg` 指针，确认 FSP 实例实际指向的 GPT 基地址，再处理 GPT 写保护/输出寄存器；不能继续盲目叠加 OV7725 寄存器。

### 当前最强根因线索

当前 GPT10 的 `GTCR=1`、`GTPR=9`、Open/Start 错误为 0，说明计数器在运行；但 `GTIOR=1` 而 RA8P1 头文件定义的 `GTIOCA` 输出使能位为 bit8，说明“计数运行”和“P109 输出有效”可能不是同一状态。需要确认：

1. `g_timer_xclk_ctrl.p_reg` 是否确实指向 `R_GPT10_BASE=0x40322A00`；
2. GPT10 的 GTWP 写保护状态和正确解锁顺序；
3. RASC 生成的 GPT periodic 配置是否只启用了计数而没有真正启用 GTIOCA；
4. P109 的 PFS 复用是否与 GPT10A 对应，并用示波器在 P109 实测约 24/25 MHz XCLK。

只有 P109 XCLK 实测恢复后，OV7725 才可能输出 PCLK/HREF/VSYNC；之后再验证 CEU 帧计数和 RTT 预览。屏幕触摸、IIC0 共用和 LVGL 双行文字问题暂不与当前摄像头诊断混合处理。

### 当前未完成事项

- 摄像头 RTT 预览：未恢复，当前实测 0 FPS；
- 摄像头与屏幕共用 IIC0：暂未重新联调；
- CST826 触摸：之前已确认坐标映射可正确，但摄像头占用 IIC0 时仍需重新验证；
- LVGL 双行标题问题：模拟器正常，实机布局仍需单独处理；
- 临时 `.tmp` J-Link 脚本：本节问题闭环后再清理，避免丢失诊断证据；
- 只有在摄像头帧计数稳定递增后，才恢复屏幕/触摸/RTT 联合测试。

## 2026-08-18 GUI Guider 同步核对（DeepSeek Harness 重构后）

- 已核对 `gui/RA8P1/RA8P1.guiguider`、`gui/RA8P1/generated/` 和 `gui/RA8P1/custom/custom.c`，当前设计工程包含完整十页：`Boot/Home/Pickup/Scan/Medicine/Login/Admin/Store/Logs/Device`，没有缺页或多余业务页。
- DeepSeek Harness 本轮新增的 Boot 封面图 `boot_photo`、主页电赛图标 `home_nuedc` 已写入 `.guiguider` 设计源；重新打开 GUI Guider 可在画布中看到，不能只依赖手写 C 文件。
- 这两项大图在板端采用 OSPI XIP 资源：`src/middleware/src/ospi_icons.c` 提供 `boot_photo` 和 `icon_nuedc_70x42_ARGB8888`，`custom.c` 仅负责运行时挂载，不是第二套页面结构。普通页面控件和跳转仍以 `.guiguider` 为唯一来源。
- 生成源码与设计源的同步规则：打开 `gui/RA8P1/RA8P1.guiguider` 后执行 GUI Guider **Generate Code**，再运行 `gui/tools/sync_generated_chinese.js`（若生成字库发生变化），最后重新构建 Simulator/ARM 工程。不要手工把 `custom.c` 的业务逻辑反向复制进 `.guiguider`。
- 本轮静态审计通过：十个设计页、页面 ID、`boot_photo`、`home_nuedc` 及 OSPI 资源符号均存在；工作区原有二维码自检相关未提交改动已保留，未被本轮 GUI 核对覆盖。
- 当前环境未提供可调用的 `cmake`，因此本轮只能完成文件和结构审计，未在本机重新构建 Windows Simulator；既有 `platform/simulator/build/bin/simulator.exe` 保留不变。

## 2026-08-20 GUI 预览图处理记录

- 按用户要求，本轮未修改 `gui/RA8P1/custom/custom.c`，也未覆盖 DeepSeek Harness 的电机、二维码和取药调试代码。
- `.guiguider` 设计源仍以十页中文界面为准；未执行整页重建脚本，避免覆盖用户当前 GUI 设计。
- 旧的有效 `preview_boot/admin/device/login/logs/pickup/store.png` 已恢复，未保留后台抓取得到的黑色无效图片。
- 当前 Codex 执行环境没有可调用的 CMake/GUI 桌面帧缓冲，无法可靠截取 SDL 模拟器窗口；因此 `Home/Scan/Medicine` 三张新截图暂未伪造或写入工程。需要在本机 GUI Guider/Simulator 可见桌面会话中重新运行后再保存，确保 PPT 使用真实画面。
