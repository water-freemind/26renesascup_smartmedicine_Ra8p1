# 2026-08-06 联调终局（第十六段：最终状态总结）

## ✅ 主任务完成：USB PCDC 链路完全打通
- COM15 打开/配置/双向数据全部成功
- 修复内容（官方 FSP PCDC 示例对齐）：
  1. USB_STATUS_CONFIGURED 立即 R_USB_Read 武装 PIPE2
  2. USB_STATUS_READ_COMPLETE 立即 re-arm PIPE2
  3. 通知发送稳定（notify_err 3->0）
- 铁证：WriteFile 'ABCD' 到达设备 s_rx_buf=0x44434241
- IOCTL 测试：GetCommState 60s->0s, SetCommState 失败->成功(30s), 总 180s->60s

## ❌ 遗留问题：摄像头 CEU 不出帧（独立于 USB）
### 根因定位
- GPT10 (XCLK) 寄存器写入无效：J-Link w4 0x40322A00,0xA50B 写"成功"但读回 0
- MSTPCRE=0xFFDFFFFF GPT10 时钟已使能
- RASC 已配置 GPT10 (gpt10.gtioc10a.p109, P109 引脚)
- 程序执行到 camera_ov7725_init（BP 断点确认），但 GPT10 从未生效
- 无 XCLK -> OV7725 无时钟 -> 无 VSYNC -> CEU CAPSR CE=1 但 CPTON=0 等帧

### 待排查（硬件层）
1. GPT10 GTWP 写保护：JTAG 直写可能被拒，需 CPU 上下文验证
2. D-Cache 一致性：GPT10 写入后 cache 未刷回（但 JTAG 读不走 cache，存疑）
3. TrustZone SAU/IDAU：GPT10 是否被安全隔离
4. PCLKD 时钟域：GPT10 计数源 PCLKD 是否使能
5. 建议：在固件 camera_xclk_init 末尾加读回验证 + 断点确认写入值

### 建议操作
- 在 RASC 中确认 GPT10 外设完整配置（不只用引脚）
- 或改用 RASC 生成的 GPT 实例 API（g_timer10.p_api->open）
- 示波器测 P109 XCLK 是否有波形
