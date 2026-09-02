# 2026-08-06 联调终局（第十四段：摄像头 CEU 诊断结论）

## USB 链路：✅ 完全打通（里程碑）
- COM15 打开/配置/写数据全部成功（IOCTL 测试 60s，原 180s+）
- WriteFile 'ABCD' -> s_rx_buf=0x44434241 铁证
- 修复：CONFIGURED 立即武装 PIPE2 + READ_COMPLETE re-arm（官方示例对齐）
- notify_err 3->0

## 摄像头链路：❌ CEU 不出帧
### J-Link BP 断点证据（方法见 .omo/progress/2026-08-06_jlink-bp-debug-method.md）
- CEIER @ 0x40400020 = 0xB9FF -> R_CEU_Open 已执行（中断已配置）
- CAPSR @ 0x40400004 = 0xCFC5 -> CE=1 捕获已启动
- CSTSR @ 0x40400000 = 0x3000 -> CPTON=0 未捕获到帧
- g_frame_ready @ 0x220BBAE9 = 0 -> 无帧完成中断
- -> CEU 已启动但等 OV7725 VSYNC(VD)，传感器无输出

### GPT10 XCLK 疑点
- GPT10 基址 0x40322A00，GTWP 读=0（应=0xA50B）
- camera_xclk_init (0x2009394) 已执行但 GPT10 寄存器全 0
- -> GPT10 模块时钟可能未使能 或 GPIO 配置问题

## 下一步（摄像头硬件排查）
1. 确认 GPT10 模块时钟（R_BSP_MODULE_START(FSP_IP_GPT,10)）是否使能
2. 确认 XCLK 引脚（GPT10 GTIOCA）在 RASC 中配置为输出
3. 用示波器测 XCLK 引脚是否有 25MHz 方波
4. 确认 OV7725 PWDN/RST 引脚电平正确
5. 确认 I2C 能读到 OV7725 ID (0x77)

## 已排除
- USB 固件逻辑（完全正确）
- CEU 中断配置（CEIER=0xB9FF 正确）
- CEU 捕获启动（CAPSR CE=1）
