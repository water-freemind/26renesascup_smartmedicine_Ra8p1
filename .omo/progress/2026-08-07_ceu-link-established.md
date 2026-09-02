# CEU 链路打通：OV7725 → RA8P1 CEU → Frame Buffer ✅

日期：2026-08-07（晚）

## 决定性结果
- **s_dbg_ceu_frame_cnt = 3**（CEU_EVENT_FRAME_END 触发）
- **evt_hd_miss (NHD) = 0**、evt_cram = 0、evt_vd_err = 0
- **缓冲区有真实图像数据**：`25012501 26012601 27012601 47014601 88018801`
  渐变亮度（0x25→0xE8），非彩条重复模式 → **真实场景图像**

## 最终根因（三个问题叠加，全部已修复）

### 1. COM10=0x42 的 bit6 (HSYNC_EN) —— HREF 7.5% 假象的直接原因
- COM10 bit6 (0x40) = "HREF changes to HSYNC"：**把 HREF 输出引脚重定向为短 HSYNC 脉冲**
- 示波器看到的 "7.5% HREF 高" 实际是 HSYNC 短脉冲，不是完整行有效信号
- 修复：COM10 = **0x02**（仅 VSYNC_NEG，匹配 CEU "帧期间 VSYNC 高"约定）

### 2. 寄存器表 0x90-0xAE 段地址错位 + 无软复位
- 原表从 esp32-camera 复制，**注释/地址全部错位**：0x90 实际是 EDGE0 不是 GAM1、
  GAM 系列在 0x7E-0x8C、MTX 在 0x94-0x99、BRIGHT 在 0x9B、CNST 在 0x9C、
  UVADJ0 在 0x9E、SDE 在 0xA6
- 原表 {0xAC, 0x30}（标注 CONTRAST）**覆盖了 DSPAUTO**（0xAC=0xFF → 0x30）
- **表里根本没有软复位 {0x12, 0x80}**（init 循环的 30ms 延时逻辑从未触发）
- 修复：整段删除错位寄存器，软复位置顶，COM7=0x46 放最后触发窗口重载

### 3. camera_drv.c 手动覆盖 CAPWR/CDWDR/CFSZR —— 画蛇添足
- 旧代码假设 "8-bit data_synchronous 模式 HWDTH 单位是 4-PCLK" → 覆盖为 160/160/80
- **FSP R_CEU_Open() 自动计算是正确的**（HWDTH=640 PCLK、CDWDR=640B、HFCLP=320）
- 手动覆盖 160 << 实际 HREF 高(640 PCLK) → 行数据残缺 → NHD
- 修复：删除覆盖，信任 FSP 默认；保留 CMCYR=0（禁用周期检测，避免 IGHS）

## 关键参考（本次调研确认）
- **OV7725 数据手册 v1.2**（Sparkfun 缓存）：COM10 全 bit 定义、RGB565 高字节在前、
  数据 PCLK 上升沿有效（tSU=15ns/tHD=8ns）、HREF 高=640PCLK@QVGA RGB565
- **Linux 内核 ov772x.c**：COM3/COM4/COM7/COM8/CLKRC/HOUTSIZE 权威定义
- **OpenMV/Zephyr/esp32-camera**：COM10 交叉验证
- **FSP r_ceu.c**（renesas/fsp @ a409855）：CAPWR/CDWDR/CFSZR/CMCYR 计算公式
- **RA8P1 CMSIS 头**（R7KA8P1KF_core0.h）：RA8P1 真实 CAMCR 位定义
  （JPG=bit4、DTARY=bit8、DTIF=bit12、DSEL=bit24 —— 与 RZ/A2M 手册不同！）
- **cpk_examples**（renesas/cpk_examples OV7725 init）：COM7=0x46 QVGA RGB565 配置确认

## 修复后寄存器表（摘要）
```
{0x12,0x80} 软复位 → {0x0D,0x41} PLL4x → {0x11,0x00} CLKRC
→ 窗口 HSTART/HSIZE/VSTART/VSIZE/HREF/HOUTSIZE/VOUTSIZE/EXHCH
→ {0x3D,0x03} COM12 → DSP_CTRL1-4 → {0xAC,0xFF} DSPAUTO
→ {0x13,0xFF} COM8 → COM6/COM9 → {0x15,0x02} COM10
→ BDBASE/DBSTEP/AEW/AEB/VPT/EXHCL/AWB_CTRL3/COM5
→ {0x12,0x46} COM7 (LAST, 触发窗口重载) → 结束
```

## 当前 CEU 配置（信任 FSP 默认）
- CAPWR.HWDTH=640 PCLK、VWDTH=240
- CDWDR=640B/行
- CFSZR.HFCLP=320、VFCLP=240
- CMCYR=0（禁用周期检测）
- CAMCR: HDPOL=0(高)、VDPOL=0(高)、JPG=1(Data Sync)、DTIF=0(8bit)、DSEL=0(上升沿)

## 待办
- [x] 关闭彩条模式（camera_app_init 测试代码已删）
- [x] 真实图像数据验证
- [ ] 双缓冲 ping-pong 连续采集验证（frame_cnt 持续增长）
- [ ] USB 输出/LVGL 显示验证
- [ ] 字节序验证（RGB565 高字节在前，必要时 CDOCR swap 调整）
