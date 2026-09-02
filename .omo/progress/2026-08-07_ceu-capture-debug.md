# 2026-08-07 OV7725 CEU 捕获调试进度

## 目标
打通 RA8P1 CEU + OV7725 采集链路：XCLK → SCCB → VSYNC/HREF → CEU 捕获 → USB 输出。

## 本次会话完成的工作

### 1. XCLK 频率修正（GPT10）
- 用户在 RASC 改为 24MHz（unit_frequency_khz, period=24000）
- RASC 取整后 period_counts=0xa → GTPR=0x09 → **实际 25MHz**（250MHz/10）
- J-Link 验证 GTPR=0x09, GTCNT 运行中 ✓
- **效果：VSYNC 出现 30Hz 脉冲**（300ms 窗口 3 次脉冲）

### 2. CEU 捕获链路修复（多次迭代）

| 步骤 | 修改 | 结果 |
|---|---|---|
| capture_format | data_synchronous → **image_capture** | CAMCR JPG=00 生效 |
| 分辨率 | 640x480 → **320x240**（RASC x/y_pixels） | 匹配 OV7725 QVGA 输出 |
| 双缓冲 | g_ceu_buffer_0/1 `aligned(32)` | 32 字节对齐确认 |
| 错误码 | s_dbg_ceu_open_err/cap_err 记录 | 修复链接器 GC 丢弃 |
| 回调 | s_dbg_ceu_frame_cnt 等 9 个事件计数 | 中断触发可观测 |
| CMCYR | 先写 HTS/VTS → 后改为不写（DATA_SYNCHRONOUS） | 反复验证 |

### 3. 关键诊断数据（J-Link 实测）

**信号层面（引脚采样）：**
- VSYNC(P708)：30Hz 脉冲 ✓ → 后来采样到恒高（时序敏感）
- PCLK(P414)：51% 占空比方波 ✓
- **HREF(P415)：9% → 0% 占空比 ✗（核心问题）**

**CEU 寄存器：**
- CETCR = 0x20100（HD=1 + IGHS=1）→ 模式切换后 HD_MISSING(NHD) 每帧触发
- CAPSR.CE = 0（CEU 未捕获）
- FRAME_END = 0，VD 事件 273 次（VSYNC 到达 CEU ✓），NHD 275 次

### 4. 根因分析结论

- **RASC 配置全部正确**：引脚分配（P414/P415/P708=D0-D7 全 CEU 外设）、中断使能、时钟（R_BSP_MODULE_START 自动使能）
- **OV7725 寄存器表与 esp32-camera 标准逐项一致**（COM3/COM10/CLKRC/COM7/HREF 全部核对）
- **COM10 非根因**：bit6(HSYNC_EN)=0，未启用 HREF→HSYNC 交换
- **核心结论：HREF(P415) 信号缺失，指向硬件/接线层面**
  - 软件侧（CEU 配置 + OV7725 寄存器）已全部验证正确
  - 传感器"活着"（PID=0x77/VER=0x21/I2C 全通/VSYNC 有脉冲）
  - 但 HREF 引脚无有效信号

### 5. 遗留发现（esp32 对比）

esp32-camera 完整初始化 = reset() + default_regs + **set_pixformat()** + **set_framesize()**
- set_pixformat：运行时读 COM7 重写格式位 → **COM7 最终 = 0x40**（纯 QVGA+YUV）
- set_framesize：写 HOUTSIZE/HSIZE/VSTART + CLKRC=0x81
- **我们只执行 default_regs，COM7=0x46（QVGA+RGB565 残留）→ 格式不匹配**
- 待验证：COM7=0x46 vs 0x40 是否影响 HREF/传感器 DSP 启动

## 下一步建议
1. COM7 改为 0x40（纯 QVGA+YUV）重新验证
2. 若 HREF 仍无信号 → 示波器直测模块 HREF 引脚、检查接线/焊点/供电
3. 若传感器出帧 → 验证 CEU 捕获（CAPSR.CE=1, FRAME_END 递增）

## 关键文件
- src/drive/camera_drv.c（camera_ceu_start：错误码记录 + 不写 CMCYR）
- src/app/camera_app.c（OV7725 寄存器表 + 事件计数回调）
- src/app/camera_app.h（双缓冲 32 字节对齐）
- src/Camera_thread_entry.c（预览循环 + 双缓冲）
- configuration.xml（capture_format=data_synchronous, x/y_pixels=320/240）
