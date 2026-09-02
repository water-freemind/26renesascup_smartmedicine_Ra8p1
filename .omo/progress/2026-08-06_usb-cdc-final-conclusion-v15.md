# 2026-08-06 联调终局（第十五段：GPT10 XCLK 写入无效的硬件证据）

## J-Link 硬件断点 + 反汇编决定性证据
1. camera_xclk_init @ 0x2009394 是独立函数（未内联），反汇编确认：
   - 写入 0x40322A00 (R_GPT10_BASE) 的 GTWP=0xA50B, GTCR(偏移0x2C), GTPR(偏移0x64) 等
   - 编译值确认 r3 = 0x40322A00（安全地址）
2. 但 J-Link 读 0x40322A00 和 0x50322A00（NS地址）都是 0
3. MSTPCRE @ 0x40203010 = 0xFFDFFFFF -> GPT10 模块时钟已使能（bit21=0）
4. J-Link 直接 w4 写入 0x40322A00, 0xA50B 显示 "Writing ... OK" 但 mem32 读回无输出

## 结论
- GPT10 (XCLK) 硬件写入无效 -> OV7725 无 XCLK 时钟 -> 无 VSYNC -> CEU 不出帧
- 这是摄像头硬件/时钟配置问题，与 USB 联调无关

## 可能的根因（待验证）
1. GPT10 时钟位在 MSTPCRE 的 bit21 是否正确（R_BSP_MODULE_START(FSP_IP_GPT,10)）
2. GPT10 引脚 (GTIOCA10) 是否在 RASC 中配置为输出复用
3. GPT10 是否被 Security/TrustZone 保护（SAU/IDAU 配置）
4. PCLKD 时钟是否开启（GPT10 计数源）

## 排查建议
- 在 RASC 中确认 GPT10 已添加并配置（当前代码直接操作寄存器，RASC 无对应配置可能）
- 确认 PCLKD 已使能（GPT10 的计数时钟）
- 用示波器测 XCLK 引脚
- 若 RASC 未配置 GPT10 外设，需要在 RASC 添加 GPT10 + 配置 GTIOCA10 输出

## USB 状态（已完成）
✅ COM15 完全可用，WriteFile 数据到达设备（s_rx_buf='ABCD'）
