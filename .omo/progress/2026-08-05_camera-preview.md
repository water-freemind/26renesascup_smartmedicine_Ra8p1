# 摄像头预览实现进度 — 2026-08-05 (最终版)

## 会话恢复信息（/clear 后如何继续）

- **项目**: `D:\Renesas_project\26renesascup_smartmedicine_Ra8p1`（瑞萨杯 RA8P1 智能药盒）
- **任务**: OV7725 摄像头 → USB PCDC 发送到 PC 实时预览（当前）→ 后续可启用 MIPI 屏显
- **计划文件**: `.omo/plans/camera-preview-plan.md`
- **用户决策**: ①PC 预览用板载 USB 口（确认 USBHS）②**当前只走 PC 预览，MIPI 屏显禁用**（编译开关控制）
- **当前状态**: ✅ **全部代码完成 + RASC USBHS 配置完成 + 构建零警告零错误**

---

## 一、已完成 ✅

### 1. 硬件/USB 最终配置（用户 RASC 已完成）
- RASC 生成 `g_basic0_cfg`（ra_gen/Camera_thread.c）:
  - `usb_mode = USB_MODE_PERI` ✅（外设模式）
  - `usb_speed = USB_SPEED_HS` ✅（高速）
  - `module_number = 1` ✅（USB_IP1 = USBHS）
- `r_usb_basic_cfg.h`: `USB_CFG_MODE = PERI(2)`, `USB_CFG_PCDC_USE` 已定义 ✅
- USBHS 中断向量已注册（vector_data.c: usbhs_interrupt_handler 等）✅
- `ra/fsp/src/r_usb_pcdc/r_usb_pcdc_descriptor.c` 保留 ✅（RASC 缺陷补偿，重新生成后未被删）
- 中断 Priority12 满足 FreeRTOS 约束

### 2. 新增源码（src/ 下）
| 文件 | 内容 | 验证状态 |
|---|---|---|
| `src/app/camera_convert.c/h` | YCbCr422(UYVY)→RGB565 + 最近邻缩放（按行转换，scratch 仅 640×2B） | 宿主测试 **17/17 PASS** |
| `src/app/usb_cdc.c/h` | USB PCDC 帧传输：8B 帧头(magic+w+h+seq) + 512B 分包 + 信号量；`__has_include` 保护 | ✅ 已链接 (nm 确认) |
| `src/app/camera_preview.c/h` | 跨线程帧缓冲 480×360 + `lv_image_dsc_t`（供 MIPI 屏显，当前未用） | ✅ 编译通过 |
| `tools/camera_viewer.py` | PC 上位机：pyserial 收帧 + OpenCV/Pillow 显示 + s 存图 | ✅ py_compile 通过 |

### 3. 修改的现有文件
- `src/Camera_thread_entry.c`: 帧就绪 → `camera_preview_lvgl_ready()` 屏显分支（自动跳过）+ `usb_cdc_is_connected()` USB 分支（320×240）→ 重启采集
- `src/LVGL_thread_entry.c`: **新增 `CAMERA_PREVIEW_SCREEN_ENABLE` 编译开关（默认 0 = 禁用屏显）** + mipi_dsi0_callback
- `src/app/camera_app.h`: 新增 `CAMERA_IMAGE_W/H` 宏

### 4. 构建验证
- ✅ `cmake -S . -B build\Debug` + `cmake --build ... --target ...elf` → **零错误零警告（exit 0）**
- ✅ `arm-none-eabi-nm` 确认 elf 含: `usb_cdc_send_frame`, `usb_cdc_init`, `R_USB_Open`, `R_USB_Write`（HS 驱动）, `camera_preview_lvgl_ready`
- ✅ 产物: `build/Debug/26renesascup_smartmedicine_Ra8p1.elf` (2026/8/5 22:16, 1.5MB)

### 5. 内存验证
- RAM 1872KB（0x22000000~0x221d4000）
- 大缓冲: s_usb_preview 150KB + s_screen_preview 337KB + g_camera_frame_buffer 600KB + s_preview_buf 337KB
- 最大用到 1754KB，余量 ~117KB（ucHeap 256KB 在 0x220007a8，独立区，无冲突）
- framebuffer 在 SDRAM（128MB），不占内部 RAM

---

## 二、烧录与联调步骤（用户回来后的操作指引）

### ⚠️ 2026-08-05 23:30 诊断修复（重要）
**问题**: 烧录后 PC 无虚拟串口（`GetPortNames`/`DEVICEMAP` 全空）
**根因**: 原代码先 `camera_app_init()` 后 `usb_cdc_init()`，而 `camera_app_init()` 内部 OV7725/CEU 失败会 `while(1)` 死循环 → USB 初始化永远不执行
**修复**:
1. `camera_app.c`: `camera_app_init()` 改返回 `bool`，失败不再死循环
2. `Camera_thread_entry.c`: **USB 先初始化** → 摄像头后初始化 → 主循环 `if (cam_ok && g_frame_ready)` 处理帧
3. 新固件: `build/Debug/26renesascup_smartmedicine_Ra8p1.elf` (2026/8/5 23:28, 零警告)
4. 新增 `tools/find_camera_port.py`: 自动扫描 COM 口找摄像头帧头 0x55 0xAA

**用户需重新烧录新 elf 再测试**。若重新烧录后 PC 仍无串口 → 是 USBHS 枚举/硬件问题（检查 USB 线、口、供电）；若串口出现但无画面 → 摄像头问题。

### 板端
1. 用 e2 studio / J-Flash 烧录 `build/Debug/26renesascup_smartmedicine_Ra8p1.elf`
2. 板载 USB 口插电脑（USBHS 口）
3. 上电运行：Camera 线程采集 → 转换 → USB 发送

### PC 端
1. 安装依赖: `pip install pyserial opencv-python`（或 pyserial + Pillow）
2. 查串口号（设备管理器 → 端口，应出现虚拟 COM 口，如 COM3）
3. 运行: `python tools\camera_viewer.py --port COM3`
4. 界面显示实时画面，按 `s` 存图，`q` 退出
5. 若报错: 检查 pyserial 是否安装 / 换 `--port` / 加 `--baud 115200`

### 常见问题
- PC 无虚拟串口 → 检查 USB 线是否接 USBHS 口（非调试口）、枚举是否成功（设备管理器有无感叹号）
- 画面花屏 → 大概率 OV7725 配置与 CEU 格式问题，检查 `g_ov7725_config` 与 `g_ceu0_extended_cfg`
- 颜色不对 → YCbCr422 字节序或 BT.601 系数需微调

---

## 三、后续待做（可选）

1. **启用 MIPI 屏显**: `src/LVGL_thread_entry.c` 改 `CAMERA_PREVIEW_SCREEN_ENABLE` 为 1 重新编译
   - 启用后 Camera 线程自动走屏显分支（480×360 居中显示）
2. 处理 Momus 计划评审（bg_3121608f）: 结论 **OKAY 无阻塞**，唯一建议（单元测试）已通过宿主测试 17/17 满足
3. 联调验证记录（PC 画面帧率/清晰度）回填本文件

---

## 四、关键技术决策（勿改）

1. **USB 帧协议**: `[0x55][0xAA][w_hi][w_lo][h_hi][h_lo][seq_hi][seq_lo]` + RGB565 数据，PC 端按此解析
2. **USB 预览分辨率 320×240**（USBHS 高速带宽充足）；屏显分支 480×360
3. **YCbCr422 字节序 = UYVY**（CEU input_order cb0y0cr0y1）：每 4 字节 Cb,Y0,Cr,Y1 → 2 像素
4. **BT.601 limited range** 整数转换（298/409/100/208/516 系数）
5. **usb_cdc 空实现保护**: `__has_include("r_usb_basic_api.h")` —— RASC 无 USB 时编译为空壳
6. **LVGL 线程模型**: Camera 线程只写帧缓冲+置标志，LVGL 线程 lv_timer_handler 前检测刷新
7. **屏显开关**: `CAMERA_PREVIEW_SCREEN_ENABLE`（LVGL_thread_entry.c 顶部），默认 0=仅 PC 预览
8. **mipi_dsi0_callback**: 已在 LVGL_thread_entry.c 提供空实现（RASC 引用需实现）

## 五、构建命令（PowerShell）
```powershell
$env:Path = "C:\MinGW\bin;C:\Users\Zhanglongsheng\.renesas\platform\cmake\3.31.8\cmake-3.31.8-windows-x86_64\bin;$env:Path"
cmake -S . -B build\Debug
cmake --build build\Debug --target 26renesascup_smartmedicine_Ra8p1.elf
```

## 六、git 状态
- 未提交（用户未要求）。新增: camera_convert/usb_cdc/camera_preview/camera_viewer.py/descriptor.c/计划与进度文件
- 修改: configuration.xml/ra_gen/*（用户 RASC 改动）+ src/*（我的改动）

---

## 七、2026-08-06 00:20 USB 枚举诊断（关键进展）

### 症状时间线
| 时间 | 事件 |
|---|---|
| 23:30:15 | VID_0000 固件枚举（VID=0 无 COM 口）|
| 23:33:26 | **VID_045B 固件枚举** → usbser 绑定 → **"Starting device" 卡 2 分钟**（23:33:27→23:35:27 超时）→ COM15 端口对象未建立 |
| 23:39 | 新固件（测试彩条诊断版）编译完成 |
| 23:45+ | 拔插测试：设备 PresentOnly 在线但 DEVICEMAP 空、COM15 打不开（FileNotFoundError）|
| 00:00+ | 按 RESET 后**重新枚举成功**：Device Parameters 出现 `PortName=COM15` + `SymbolicName=串行端口接口 GUID`（a5dcbf10...=GUID_DEVINTERFACE_SERENUM）→ usbser 端口注册完成 ✅ |
| 00:10+ | 但**打开 COM15 永久阻塞**（>150 秒）→ usbser 等固件 CDC 握手（SET_LINE_CODING/SET_CONTROL_LINE_STATE）响应 |

### 已排除的假设
1. **固件代码问题** ❌ —— 设备能枚举（EP0 控制传输 OK，VID/PID/描述符被 Windows 正确读取）、usbser 能绑定、COM15 能注册 → 固件 USB 栈运行正常
2. **中断优先级** ❌ —— USB 中断 ipl=12 ≥ configMAX_SYSCALL(1)，允许 FromISR API
3. **描述符结构** ❌ —— CDC 2 接口（通信+数据）+ 3 端点（INT IN EP3 + BULK IN EP1/OUT EP2），HS 512B / FS 64B 均正确
4. **时钟配置（初判）** ❌ **已修正** —— RA8 设计指南确认：USBHS **非 CL-Only 模式**用 MOSC/XTAL(12/20/24/48MHz) 作 USBMCLK + 内部 PLL；USB60CLK(60MHz) **仅 CL-Only 模式需要**。当前 `USBMCLK=XTAL 24MHz`（匹配驱动 `USB_USBMCLK_HZ=BSP_CFG_XTAL_HZ`）→ **时钟配置正确**

### 当前聚焦问题（未解决）
**usbser 打开 COM15 阻塞** = usbser 发送 CDC 类请求（SET_LINE_CODING 0x20 / SET_CONTROL_LINE_STATE 0x22 / GET_LINE_CODING 0x21）后等待固件 ACK，固件未应答。

已定位 FSP 请求转发路径（r_usb_pstdrequest.c）：
- `usb_peri_class_request_wnss`（SET_CONTROL_LINE_STATE，PCDC-only 分支 2015-2035 行）→ `usb_set_event(USB_STATUS_REQUEST)` → **转发给应用回调**（我们的 `usb_pcdc_callback` 未处理该事件！只有 CONFIGURED/DETACH/SUSPEND/WRITE_COMPLETE）→ 但之后仍有 `usb_pstd_ctrl_end(USB_CTRL_END)` 结束控制传输
- **待验证**: USB_STATUS_REQUEST 事件未处理是否导致控制传输未正确 ACK；或中断层面未送达

### 下一步（J-Link 在线，用户已授权直接调用）
1. J-Link Commander（`C:\Users\Zhanglongsheng\.renesas\platform\DebugComp\Dialog\ARM\Segger\JLink.exe`，设备名 `R7KA8P1KF_CPU0`）连接成功，SWD DPIDR=0x6BA02477，CPUID=0x411FD231（Cortex-M85）
2. 读取: PC 寄存器（确认固件在运行）/ USBHS 寄存器（SYSCFG/USBSTA 状态）/ `s_connected` 变量
3. 确认 USB 是否已配置（DVSQ 状态）、是否有中断活动
4. 依据寄存器状态决定: 改回调处理 USB_STATUS_REQUEST / 或改 RASC（用户配合）

---

## 八、2026-08-06 00:40 J-Link 深入诊断（重大突破）

### J-Link 探测工具
- `C:\Users\Zhanglongsheng\.renesas\platform\DebugComp\Dialog\ARM\Segger\JLink.exe` (V7.96n)
- 设备名: `R7KA8P1KF_CPU0`（launch.json 确认），板载调试器 J-Link OB-RA4M2 (S/N 1082654941)
- GDB Server: `JLinkGDBServerCL.exe` 端口 2331；GDB: `platform\DebugComp\RA\arm-none-eabi-gdb.exe`

### 固件符号地址（nm 确认）
| 符号 | 地址 |
|---|---|
| usb_pcdc_callback | 0x02008C70 |
| usb_cdc_send_frame | 0x02008CD8 |
| usb_cdc_init | 0x02008CAC |
| usb_peri_class_request | 0x02006518 |
| usbhs_interrupt_handler | 0x02007A90 |
| s_connected | 0x221A67E8 |
| g_basic0_ctrl | 0x220415D4 |
| g_usb_apl_callback | 0x22040D20 |

### 探测结果（决定性）
| 检查项 | 结果 | 结论 |
|---|---|---|
| **s_connected** | **0x01** | ✅ **固件已收到 USB_STATUS_CONFIGURED，枚举完成！** |
| g_basic0_ctrl | 0x01 | ✅ USB 控制块激活 |
| SYSCFG (0x00) | 0x0191 | ✅ USBE=1, DPRPU=1（D+ 上拉，设备在总线上）|
| PLLSTA (0x06) | 0x0001 | ✅ **PLLLOCK=1，USB PLL 锁定** |
| DVSTCTR0 (0x08) | 0x0003 | ✅ **RHST=3，高速连接建立** |
| INTENB0 (0x30) | 0x9D00 | ✅ CTRE/DVSE/VBSE/RSME 使能 |
| BRDYENB/NRDYENB/BEMPENB | 全 0 | ⚠️ 数据管道中断未使能（但非根因）|
| INTSTS0 (0x40) | 0x20B1→20B5 | ⚠️ VALID=1（**usbser 请求已到达硬件！**）、CTRT=1（控制中断挂起）、DVSQ=101（Suspend）|
| **g_usb_apl_callback[1]** | **0x02008C71** | ✅ 回调注册正确（Thumb 位）|
| ISER0 bit5 | 0x001F0020 | ✅ IRQ5（USBHS）NVIC 已使能 |
| 向量表 IRQ5 @0x2000054 | 0x02007A91 | ✅ = usbhs_interrupt_handler（**向量表正确！**）|
| IPR IRQ5 | 0xC0 = 优先级12 | ✅ 匹配 hsipl=12 |
| **usbhs_interrupt_handler 断点** | **25 秒未命中** | ❌ **USB 中断从未进入 ISR！** |
| **usb_peri_class_request 断点** | **25 秒未命中** | ❌ **CDC 类请求从未到达处理函数！** |

### 根因定位（核心矛盾）
**USB 外设收到了 usbser 的请求（INTSTS0.VALID=1、CTRT=1 中断挂起），NVIC 已使能、向量表正确、优先级正确，但 CPU 从未进入 usbhs_interrupt_handler！**

两个候选：
1. **PRIMASK=01（全局中断屏蔽）**：halt 时观察到 PRIMASK=1，PC 停在 `R_BSP_RegisterProtectDisable` 的 WFI 序列（0x0200850E）。**若这是运行态** → 所有中断被屏蔽 → USB ISR 永不执行。但 PRIMASK=01 也可能是 **J-Link halt 的副作用**（J-Link 常 halt 时屏蔽中断）→ 需验证
2. **DVSQ=101（Suspend 状态）**：设备处于挂起，VBSTS=0（VBUS 无效？）→ 需确认 VBUS 引脚配置

### 待验证（下一步）
- [ ] 确认 PRIMASK=01 是运行态还是 halt 副作用（用 GDB 连 JLink，`monitor` + 运行中采样）
- [ ] 检查 VBUS 检测引脚 P408 配置（configuration.xml: `p408.usbhs.usbhs_vbus`）与 VBSTS=0 原因
- [ ] 若 PRIMASK 运行态=1 → 找谁调用了 `__disable_irq()` 未恢复（R_BSP_RegisterProtectDisable 死锁？）
- [ ] 若 VBUS 问题 → 可能需要 RASC 调整 VBUS/BC 配置（USB_CFG_VBUS=HIGH, USB_CFG_BC=ENABLE）

---

## 九、2026-08-06 00:55 寄存器精确解读修正（重要）

### 之前误读纠正
| 寄存器 | 之前误读 | 精确解读 |
|---|---|---|
| INTSTS0=0x20B1 | "DVSQ=101 Suspend, VBSTS=0 VBUS无效" | **DVSQ=011 Configured, VBSTS=1 VBUS有效** ✅ |
| INTSTS0=0x28B9 | - | **VALID=1（usbser请求已接收!）, CTRT=1（控制中断挂起）, DVSQ=011 Configured, VBSTS=1** |
| DVSTCTR0=0x0003 | - | RHST=3 高速连接 ✅ |
| LPSTS=0x4000 | - | SUSPENDM=1（UTMI SuspendM 控制，正常）|
| INTENB0=0x9D00 | - | **CTRE=1(控制中断使能), DVSE=1, RSME=1, VBSE=1** ✅；BRDYE/NRDYE/BEMPE=0（数据管道中断未使能，非根因）|
| ISER0=0x001F0020 | - | IRQ5（USBHS）NVIC 已使能 ✅ |
| IPR IRQ5=0xC0 | - | 优先级 12，匹配 hsipl ✅ |
| 向量表[21] | "IRQ5=0x02007A6D 错误" | **[21]@0x2000054=0x02007A91=usbhs_interrupt_handler ✅ 正确！**（之前读错偏移，[20]才是0x7A6D=USBFS_FIFO1）|

### 决定性事实
**设备 Configured + VBUS 有效 + VALID=1（usbser 请求已到硬件）+ CTRT=1（控制中断挂起）+ 所有中断使能位正确 + NVIC 使能 + 向量表正确 → 但 usbhs_interrupt_handler 断点 25 秒未命中！**

### 关于 PRIMASK=01 的澄清
- 0x0200850E 属于 **`rm_freertos_port_sleep_preserving_lpm`**（FreeRTOS 低功耗 idle 钩子）
- 该函数标准模式：`__disable_irq(); DSB; WFI; ISB; __enable_irq()` —— **PRIMASK=1 是 WFI 睡眠期间正常状态**（SysTick 挂起唤醒 WFI 后 CPSIE 恢复）
- CycleCnt 持续增长 = CPU 周期性被 SysTick 唤醒 → **固件完全正常，无死锁** ✅

### 新假设（核心）
**usbser 打开 COM15 时设备是否真的产生了 USB 中断？** 需要决定性实验验证。

### 下一步实验（决定性）
**断点 + 拔插 USB**：设置断点 usbhs_interrupt_handler → go → 拔插 USB 线（强制重新枚举，枚举必有控制请求 → 必有中断）→ 观察断点是否命中：
- **命中** → ISR 正常，问题在 usbser 打开握手（Windows 侧或 CDC 层）
- **不命中** → 中断真有问题（NVIC/TrustZone/安全状态）

### RASC 需用户确认项（若确认根因）
1. **VBUS 引脚**: P408 是否正确连接 VBUS 检测（USB_CFG_VBUS=HIGH 表示 VBUS 高有效）？
2. **BC 功能**: USB_CFG_BC=ENABLE（电池充电检测）是否需要？
3. **时钟**: 已确认非 CL-Only 模式 USBMCLK=XTAL 24MHz 正确，无需改

### 关键结论（勿忘）
- **固件 USB 枚举完全成功**（s_connected=1, RHST=3 高速, PLL 锁定）
- **usbser 请求已到达 USB 硬件**（VALID=1, CTRT=1）但 **ISR 未执行**
- 问题在"中断送达 CPU"这一层，不在描述符/回调/配置

### 关键结论（勿忘）
- **usbser 已成功注册 COM15 端口**（比 23:33 进一大步），阻塞点仅在"打开时 CDC 握手"这一层
- 若确认固件 USB 外设已 configured → 问题=应用回调未响应 CDC 控制请求 → 需在 `usb_pcdc_callback` 增加 USB_STATUS_REQUEST 处理（ACK 控制传输）
- J-Link: 板载 OB-RA4M2 (S/N 1082654941), SWD, VTref=3.3V

---

## 十、2026-08-06 01:10 断点校准实验（重大修正！推翻前结论）

### ⚠️ 关键发现：J-Link Commander 的 bp 命令在此环境未生效！
**校准实验**：在 SysTick_Handler（0x020083CC，每 1ms 必然触发）设断点 + go + sleep 10s → **未命中**！
- 但 CycleCnt 持续增长（8E8EA567→E2A9A51D）= CPU 在运行、SysTick 在触发
- **SysTick 断点都不命中 → J-Link `bp` 命令本身没生效（可能需 Flash 断点支持/特殊设置）**

### 推论（推翻前面结论）
**之前"usbhs_interrupt_handler 断点 25 秒未命中"→ 无效！** 中断可能一直在正常工作！
所有 J-Link Commander `bp` 断点实验结论作废（bp 0x02007A90 / 0x02006518 均未生效）。

### 改用 GDB 断点（已验证有效）
- GDB (`platform\DebugComp\RA\arm-none-eabi-gdb.exe`) + GDBServer (端口 2331)
- GDB 日志确认: `Setting breakpoint @ 0x02008C70, BPHandle = 0x0001` ✅ **GDB 断点真正生效**
- GDB 连接时读 PC=0x0200850E（idle WFI，正常）、s_connected 待确认
- GDB 脚本已设 `break usb_pcdc_callback` + continue，等待触发打开 COM15 验证回调是否被调用

### 最新寄存器状态（usbser 打开阻塞期间采样）
| 寄存器 | 值 | 解读 |
|---|---|---|
| **ISPR0** (NVIC 挂起) | **0x00000000** | **NVIC 层无任何中断挂起（含 IRQ5）** |
| ISER0 | 0x001F0020 | IRQ5 使能 ✅ |
| INTSTS0 | 0x20B5 | **DVSQ=011 Configured、VBSTS=1 VBUS有效、SOFR=1（主机发SOF）、VALID=0（无待处理请求）、CTRT=0（无控制中断挂起）** |
| INTENB0 | 0x9D00 | CTRE/DVSE/RSME/VBSE 使能 ✅ |

### 新判断（重大转向）
**usbser 打开 COM15 阻塞期间，USB 设备完全空闲正常——主机没有发送任何 USB 请求到达设备！**
- NVIC 无挂起（ISPR0=0）+ INTSTS0 无 VALID/CTRT → **usbser 的阻塞发生在 Windows 侧**
- setupapi 证据：23:33 usbser "Starting device" 卡 2 分钟（23:33:27→23:35:27）→ **usbser.sys 初始化超时**，强制继续后端口对象部分建立（PortName=COM15 已写但打开时等内部锁）

### 下一步（待执行）
1. **GDB 断点 usb_pcdc_callback + 触发 COM15 打开** → 确认固件回调是否被调用（区分固件 vs Windows 侧）
2. 若回调不触发 → Windows 侧问题 → **以管理员权限删除设备节点**（pnputil /remove-device 需管理员）→ 重新插拔让 usbser 干净初始化
3. 若回调触发 → 固件侧问题 → 在 usb_pcdc_callback 增加 USB_STATUS_REQUEST 处理
4. 尝试: `pnputil /remove-device "USB\VID_045B&PID_0002\0000000000001"`（管理员）或设备管理器卸载设备

---

## 十一、2026-08-06 01:30 根因 100% 确认！（GDB 断点捕获请求序列）

### 决定性实验：GDB 断点 usb_pcdc_callback + 触发 COM15 打开
**GDB 断点命中！** 之前 J-Link Commander `bp` 不生效是假象（校准实验：SysTick 断点 10 秒也不命中但 CycleCnt 在变 → J-Link bp 命令此环境无效）。GDB 断点（BPHandle=0x0001）真正生效。

### GDB 捕获的 usbser 请求序列（打开 COM15 期间）
```
>>> EVENT=7 type=0x2221 req=0x0021 val=0x0000 idx=0x0000 len=0   (×4 次)
>>> EVENT=7 type=0x21a1 req=0x00a1 val=0x0000 idx=0x0000 len=7
```
- **EVENT=7 = USB_STATUS_REQUEST**（枚举: POWERED=0...CONFIGURED=3...DETACH=6, **REQUEST=7**）
- **type=0x2221** → bRequest=0x22 = **SET_CONTROL_LINE_STATE**（无数据阶段），×4 次 = **usbser 在重试！**
- **type=0x21a1** → bRequest=0x21 = **GET_LINE_CODING**, wLength=**7** → **需要固件返回 7 字节 line coding 数据！**

### 根因（结论）
**usbser 打开 COM15 时发送 GET_LINE_CODING（控制读，wLength=7），固件必须返回 7 字节 line coding（波特率4B+停止位1B+校验1B+数据位1B）。FSP 驱动把请求经 usb_set_event(USB_STATUS_REQUEST) 转发给应用回调 usb_pcdc_callback，但我们的回调未处理 USB_STATUS_REQUEST → 数据阶段无响应 → usbser 永久阻塞。SET_CONTROL_LINE_STATE 重试 4 次佐证握手失败。**

### 已排除（全部确证正常）
- ✅ 固件 USB 中断正常（GDB 断点命中回调）
- ✅ 枚举成功（s_connected=1, Configured, VBUS 有效, RHST=3 高速）
- ✅ NVIC/向量表/优先级/中断使能全部正确
- ✅ usbser 请求确实到达固件（EVENT=7）
- ❌ **唯一问题：回调缺 USB_STATUS_REQUEST 处理**

### 修复方案（已实现 ✅）
在 `usb_pcdc_callback` 增加 USB_STATUS_REQUEST 处理：
1. **GET_LINE_CODING (0x21)**: `R_USB_PeriControlDataGet(&g_basic0_ctrl, (uint8_t*)&s_line_coding, 7U)` 返回 7 字节 line coding（115200-8N1）
2. **SET_LINE_CODING (0x20)**: `R_USB_PeriControlDataSet(&g_basic0_ctrl, (uint8_t*)&s_line_coding, 7U)` 接收主机写入
3. **SET_CONTROL_LINE_STATE (0x22)**: 驱动自动 ACK，仅记录 DTR/RTS 到 s_ctrl_line_state
4. **default**: `R_USB_PeriControlStatusSet(USB_SETUP_STATUS_STALL)` 未支持请求

### 关键 API（librarian 官方确认，bg_5cff66da）
- `R_USB_PeriControlDataGet` → 控制 IN（device→host），PERI 模式内部调 usb_pstd_ctrl_write 写 FIFO
- `R_USB_PeriControlDataSet` → 控制 OUT（host→device）
- `R_USB_PeriControlStatusSet` → ACK/STALL（设 CCPL 位）
- bRequest 提取：`p_info->setup.request_type & USB_BREQUEST (0xFF00)`
- 宏：`USB_PCDC_GET_LINE_CODING=0x2100, SET_LINE_CODING=0x2000, SET_CONTROL_LINE_STATE=0x2200`
- 参考：r_usb_basic.c:3611/3665/3718, r_usb_pcdc_api.h:35-75

### 修复验证状态
- ✅ `src/app/usb_cdc.c` 已修改（新增 s_line_coding 结构体 + USB_STATUS_REQUEST case）
- ✅ 编译成功：`build/Debug/26renesascup_smartmedicine_Ra8p1.elf` (2026/8/6 02:25, 1.53MB) 零错误零警告
- ⏳ **待用户烧录验证**：烧录 02:25 固件 → 打开 COM15 → 预期 usbser 握手完成不再阻塞 → camera_viewer.py 显示画面
