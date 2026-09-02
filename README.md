# RA8P1 智能医疗设备

基于 Renesas RA8P1、FreeRTOS、FSP、LVGL 和 GUI Guider 的智能药品存取设备固件工程。项目面向二维码取药、药品识别、存药管理以及 XYZ 龙门机械臂控制。

## 主要功能

- OV7725 摄像头采集、灰度预览与 quirc 二维码解码
- 640×480 横向 LVGL 医疗业务界面与中文字体
- ST7701S MIPI 显示屏、CST816S 触摸输入
- ZDT 电机 CAN 协议与 XYZ 龙门机械臂应用接口
- ESP-01S 网络扩展、USB/RTT 调试与设备状态管理

## 工程结构

```text
src/app/             应用业务与 XYZ 龙门机械臂接口
src/driver/          摄像头、屏幕、触摸、电机等硬件驱动
src/middleware/      USB、RTT、二维码等中间件
third_party/quirc/   第三方二维码解码库
gui/RA8P1/           GUI Guider 设计文件、生成代码和模拟器
ra_gen/              RASC/FSP 生成代码
docs/                软件工程总览和硬件总览
```

## 当前状态（2026-09-02，比赛版本）

- Git 基线：`main` 与 `origin/main` 均指向 `70fa36f`（比赛版本），归档时工作区干净，共 70 个提交。
- 详细的软件进度、Git 里程碑、测试结论和待办事项请查看 [`docs/RA8P1_工程总览.md`](docs/RA8P1_工程总览.md)；硬件资料请查看 [`docs/RA8P1_硬件总览.md`](docs/RA8P1_硬件总览.md)。

- **继电器云端联动（RELAY_CTRL，2026-08-21）**：按《硬件指导_继电器控制RELAY_CTRL.md》适配——新增上行 `RELAY_CTRL{relay 1~8, state 1/0}`（云端广播给网页 WebHID 控制 USB 继电器，与板载 GPIO 继电器独立）；取放流程自动联动：取药/存药开始(PH_ENABLE)关 2 号 → Z 轴伸出(PH_Z_DOWN/DOWN2)开 1 号 → 完成/失败开 2 号并关 1 号（幂等上报，防吸盘常吸）。协议已并入权威手册 §8.5，子流程文档存档 `docs/硬件指导_继电器控制RELAY_CTRL.md`。**已编译，待接板烧录实机验证。**
- **储药搬运闭环（STORAGE_PLACE / PLACE_FINISHED，2026-08-21）**：按云端《硬件指导_储药搬运STORAGE_PLACE.md》实现存药——下行 `STORAGE_PLACE`（taskId + item{drugId,drugName,coord,layer,x}）解析后，`pickup_test` 新增存药模式 `PickupTest_StartPlace(x,y)`：到取药口(暂存区)抓 1 盒 → 放到 `layer`/`x` 目标仓 → 回零备机；完成后上行 `PLACE_FINISHED{taskId,coord,status}`（SUCCESS/FAILED，失败附 ALARM）。机械臂忙时新任务直接 FAILED，存药进行中忽略新任务。Store 存药页同步云端任务状态：执行中显示药品/目标仓/「执行中」并禁用「确认存药」，完成 10s 内显示「已完成/已入库」，结束后恢复手动确认（库存台账）。协议权威版已并入 `docs/硬件对接手册_MCU_LVGL.md` §8.4，子流程说明存档 `docs/硬件指导_储药搬运STORAGE_PLACE.md`。**已烧录，待实机验证网页下发 → 机械臂存药 → 网页 DONE 闭环。**
- **取药格子宽度按药盒宽度自动规划（2026-08-21 凌晨）**：新增"药盒宽度"参数（默认 40mm，每种药不一样可调）；格子宽度 0=自动 = **药盒宽 + 10mm 抓取余量**，下限 = **夹爪最小间距 35mm(3.5cm)**，上限不超 X 量程/格数；参数版本 v3。夹爪开口实测：0 脉冲=125mm、-28000=52mm（用户不会放夹爪夹不住的东西）。步进电机 1.8°+XYZ 16 细分=3200 脉冲/圈，与实测标定自洽。已烧录 flash_out38 并板端验证（version=3、box_width=40、slot_width=0 自动、归零正常）。
- **取药逻辑测试全流程实机跑通（2026-08-20）**：按用户验收要求实现取药流程状态机——**必须先收好 Z 轴才能移动 XY 轴**（硬约束，防 Z 撞柜/药盒）、**XY 一起动**（并行，`Gantry_MoveXYTo`）、**零点 = 第一个药柜**。取药流程：使能电机（含解堵转）→ 回零 → XY→药柜格子 → Z伸出 → 夹爪闭合 → Z收回 → XY→暂存区 → 放下 → 完成；目标位置/动作参数全部来自**参数系统**（`src/app/pickup_params.c`，OSPI 主+备双份 CRC16 持久化，GUI 参数设置页可改）。量程定稿（用户实机测得）：**X 406mm / Y 296.3mm(12700脉冲) / Z 151.8mm(6600脉冲) / 抓手 0~-28000 负向**，超量程会堵转——参数页校验 + OSPI 加载钳制 + 点击前检查三层防护。**取药调试页**：Device 页入口，点哪个格子就取哪个（镜像布局：第1层最下、第1格最右），含全体归零/急停/状态行。上电自动归零（用户原 ZDT_Gozero_ALL 时序：Z→2s→X+Y+抓手→1.5s）。实测关键：移动/回零**固定时序估算不依赖应答**（应答只有 Z 回零、X 移动可靠且丢帧）、命令连发 3 次抗丢帧、同步模式不动用 sync=false。继电器驱动就绪（P904 真空泵/P807 电磁阀1/PA07 电磁阀2，GPIO 初始低）。详见 `docs/RA8P1_工程总览.md` 最新章节与 `.planning/取药逻辑测试_工作记录.md`。**已烧录 flash_out37，待实机复测第1层/第3层取药。**
- **EAN-13 药品商品条码识别（存药页）**：Store（存药）扫码识别药盒**商品条形码**（EAN-13，13 位，非溯源码）——自写解码器 `src/middleware/barcode_1d.c`（无第三方库），复用 QR 链路灰度流多行扫描：完整 guard/L/G/R 码解析 + 校验位防误报、**镜像支持**（OV7725 HFLIP 画面水平翻转恢复）、垂直 3 行平均降噪；host 单测 12/12 通过（正向/镜像/倾斜 12°/模糊/噪声）；接入解码线程（每帧先 EAN-13 后 QR，对取药单扫码零干扰），Store 页识别成功显示 **13 位序列号**。
- **ESP-01S 串口栈落地（方案 B：SCI8 定稿）**：`configuration.xml` 新增 `g_uart8`（`r_sci_b_uart`，SCI8/P500/P501，**115200 8N1**，RXI/TXI/TEI/ERI 优先级 12）+ **`Network_thread`（优先级 1，栈 8192B，不高于 LVGL）** + SCICLK 使能；P514/P515 改为 GPIO 输出高控制 **ESP EN/RST**；SCI9 复用已移除（二选一定稿方案 B）。ESP-01S 已预配置 TCP 透传，固件侧 `esp01s_uart` 驱动（`src/middleware/`）提供**中断收发 + 1024B RX 环形缓冲**（容纳整行 JSON），`src/Network_thread_entry.c` 使能模块→开 UART→按行转系统日志。
- **ESP-01S 云端数据协议（硬件对接手册唯一权威，2026-08-24）**：云端发布《硬件对接手册（MCU + ESP-01S + LVGL）》（`docs/硬件对接手册_MCU_LVGL.md`），**替代**《ESP01S_通信协议.md》《MCU_LVGL_对接指南.md》《MCU_串口对接ESP01S_指导文档.md》《取药二维码协议.md》四份文档；固件侧按手册升级：新增下行 `CONNECTED` 连接确认处理与**三态连接状态机**（CONNECTING/ONLINE/OFFLINE，40s 无云端下行自动回连接中），LVGL 徽章/Device 网络行/无线调试页三态显示，二维码解析兼容新格式（`taskId`+`items[].drugId`，保留旧格式）。协议层 `src/middleware/esp01s_proto.c/.h`：心跳 30s、逐药上报 `PICKUP_SCANNED`、下行 `CONNECTED`/`PING`/`DISPENSE_ACTION`（逐货位出药回报 `ACTION_FINISHED`）。详见 `docs/硬件对接手册_MCU_LVGL.md`。
- **摄像头升 VGA 640x480，完整取药单 JSON 码实机识别成功（2026-08-19）**：OV7725 按 openmv 权威驱动配置 VGA（关键 `COM7=0x00`，QVGA 的 0x40 是分辨率位不改无效）+ CEU 640x480 + 采集/解码大缓冲全部下沉 SDRAM（quirc 图像 307KB 用 SDRAM 静态池）。解码直接 640x480（不再软件放大），配合镜像翻转重试（OV7725 HFLIP）、1 帧即发布、3x3 平滑重试——**45x45 完整取药单 JSON（77B）之前 QVGA 扫不了，现在实机稳定识别**（25x25 短码更不在话下）。
- **取药单 JSON 解析 + 滚动清单**：固件轻量解析取药单 JSON（`oid` 单号 + `i[]` 药品列表，最多 16 项）——单号栏显示解析出的单号（不再显示挤成一团的原始 JSON）；**药品清单按单内实际药品动态填充，超过 3 种可在清单区上下滑动**（原静态 3 行示例已隐藏）。
- **二维码识别链路已真正打通（2026-08-19 早）**：修复两处根因——① `feed_frame` 尺寸判空 bug（`g_width/g_height` 从未初始化 → 解码是死代码）；② 解码线程栈 8KB 溢出（`decode_gray` 局部结构体 12.9KB 栈帧 → HardFault，栈增至 24KB）。**板端自检实测**：嵌入的 MED-001 与取药单 JSON 在 M85 上精确解码、无 fault、可重复（Device 页"运行自检"或 `py tools\probe_qr_selftest.py 1|2`）。
- **图标/图片资源已外部化到 OSPI（内部 flash 释放约 1MB）**：W25Q256（32MB）分区为字体区（`黑体_simhei.ttf`，9.29MB，tiny_ttf 运行时渲染任意 GB2312 中文）+ **图标区**（偏移 `0x950000` 起，XIP 映射 `0x80950000`，承载 17 个业务 icon + nuedc + Boot 封面 640x332）；图标数据零拷贝直读，不再占内部 flash。加图流程：图片放 `gui/RA8P1/resources/image/` → `py tools/gen_ospi_icons.py` → `py tools/ospi_burn_icons.py <elf> build/ospi_icons.bin`（**图标更新无需重烧固件**）。
- **Boot 启动页：封面照片 + 白色粗体文字**：`cover_640x332.png` 封面铺满下部白色区域（最底层）；title/subtitle/status 三行文字白色 + 深色粗描边（模拟加粗）；**标题居中已修复**（防换行固定 280px 框内文字改为居中，此前整体左偏 ~35px）。Boot 页不放 nuedc。
- **Home 页 renesas 左侧 nuedc 图标**：运行时以 renesas 品牌图为锚点相对定位（`x-8-70`、垂直居中），跟随移动；数据在 OSPI 图标区。
- **登录页软键盘 + 记住用户名/密码**：默认预填 `renesas / 1234`（大小写不敏感）但可修改——工号弹出大写软键盘、密码弹出数字软键盘；"记住用户名/记住密码"复选框（默认勾选免每次输入）；提交校验通过进管理台，错误红色提示留在登录页。
- **内存与存储占用（当前构建）**：内部 Flash **782,336B / 1MB（74.6%）**；内部 SRAM **1,746,212B / 1.83MB（91.1%）**（含 LVGL 堆 512KB + FreeRTOS 堆 256KB；解码线程栈 24KB 来自 FreeRTOS 堆，不占 BSS）；外部 SDRAM（VGA 后 CEU 缓冲 1.2MB + 解码缓冲 ~1.5MB + 帧缓冲 3.5MB，共约 6.2MB / 16MB）；外部 OSPI **10.3MB / 32MB（32.2%）**（字体 9.29MB + 图标 1.0MB，剩余 ~21.7MB 可继续加图）。详见 `docs/RA8P1_工程总览.md`。
- **Git 历史已整理**：63 个提交按功能主线合并为 **9 个**（整理前后 tree hash 一致，内容不变），已 force push 到远程；整理前完整历史备份在 `.tmp/git_backup_20260818_212500.bundle`。
- **tiny_ttf 外部 Flash 动态字体**：OSPI 烧入 9.29MB 黑体 simhei TTF，LVGL 界面/日志全面运行时渲染，任意 GB2312 中文不缺字（编译期字库仅 164 字）；烧录通道 `tools/ospi_burn_ttf.py`（J-Link + RTT down）。
- **药师登录验证已落地**：管理台入口需登录（renesas / 1234），提交校验，错误红色提示；登出即注销会话。实现于 `gui/RA8P1/custom/custom.c`（固件与模拟器通用）。
- **Device 运行自检可用**：点击"运行自检"依次检查触摸/摄像头/网络/LVGL 四项并写日志，摘要显示"自检通过"或"自检 N 项异常"；同时异步触发**二维码板端自检**（嵌入已知二维码解码，结果写日志，验证 quirc 在 M85 上可用）。
- **二维码扫描已接入识别页**：Camera 线程只提取 Y 灰度（`qr_decoder_feed_frame`，640x480），独立低优先级解码线程（24KB 栈）执行 quirc（镜像翻转重试 + 1 帧即发布 + 3x3 平滑重试），Scan 页实时显示解码内容、Pickup 页识别取药单并解析 JSON，不阻塞 LVGL/触摸。
- **管理页卡死已修复**：LVGL 独立内存池 64KB→256KB（多页面懒创建 + 500ms 刷新导致池耗尽/碎片化 → NULL 解引用 HardFault）；quirc assert 关闭（Debug 构建 assert 触发 → __BKPT+while(1) 死循环）。
- 屏幕（ST7701S MIPI + LVGL）与 CST816S 触摸已点亮，中文界面正常；LVGL 9.3.0 字体 stride 上游 bug 已移植修复到 `ra/lvgl`。
- **通信协议**：`docs/ESP01S_通信协议.md` 定稿 **v1.1**（2026-08-19）——固件侧二维码识别、取药单 JSON 解析、**ESP-01S SCI8 串口栈与透传收发驱动**均已落地，`scan` 上报与取药单二维码格式（§10）具备实机数据；物理层/传输层/消息定义无变更。
- 待办：网络状态机与 JSON 行编解码（基于已就绪的 `esp01s_uart` 收发）、`scan/pickup/stock/log/alarm` 业务消息接入、EAN-8 短码支持（按需）、登录记住勾选持久化、一维条码库评估（当前为自写 EAN-13，够用则无需）、机械臂动作接入、库存持久化、后端 TCP+WebSocket 服务与联调验收（协议 §12）。

## 常用命令

```powershell
# 编译 Debug 固件
& 'C:\Users\Zhanglongsheng\.renesas\platform\cmake\3.31.8\cmake-3.31.8-windows-x86_64\bin\cmake.exe' --build build\Debug --parallel 4
# 烧录固件
& 'C:\Users\Zhanglongsheng\.renesas\platform\DebugComp\Dialog\ARM\Segger\JLink.exe' -CommanderScript flash.jlink
# 生成图标区数据（新增/修改 gui/RA8P1/resources/image/ 后执行）
py tools\gen_ospi_icons.py
# 烧录图标区（固件 dsc 指向 OSPI 不变时无需重烧固件）
py tools\ospi_burn_icons.py build\Debug\26renesascup_smartmedicine_Ra8p1.elf build\ospi_icons.bin
# GUI Guider 重新生成导致字体膨胀、flash 超支时恢复精简字库
powershell -NoProfile -ExecutionPolicy Bypass -File tools\restore_guider_fonts.ps1
# 重新生成二维码板端自检源（修改 tools/gen_qr_selftest.py 内 payload 后执行，需 pip install qrcode）
py tools\gen_qr_selftest.py
# 板端二维码自检：复位 → J-Link 直写触发 → 读结果（1=MED-001, 2=取药单 JSON）
py tools\probe_qr_selftest.py 1
# 读板端系统日志（J-Link）
py tools\read_syslog.py
# RTT 摄像头预览
powershell -NoProfile -ExecutionPolicy Bypass -File tools\rtt_camera_viewer.ps1
# GUI 中文字库同步（界面文字变更后执行）
node gui\tools\sync_generated_chinese.js
```

## 开发入口

- 软件进度、架构、测试结论和后续任务：[`docs/RA8P1_工程总览.md`](docs/RA8P1_工程总览.md)
- 原理图、引脚、电源、屏幕和硬件验收信息：[`docs/RA8P1_硬件总览.md`](docs/RA8P1_硬件总览.md)
- RASC 配置：编辑 `configuration.xml` 后使用 Smart Configurator 重新生成，勿直接修改 `ra_gen/`。
- GUI 设计：使用 `gui/RA8P1/RA8P1.guiguider` 在 GUI Guider 中打开和维护。

README 仅作为工程入口和概要介绍；详细工作日志、调试过程与阶段性结论统一归档到两个总览文档中。
