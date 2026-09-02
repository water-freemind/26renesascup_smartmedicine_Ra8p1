# 屏幕审计发现

## 2026-08-12 起始状态

- 审计目标为桌面屏幕资料、RASC配置、生成代码、ST7701S驱动和LVGL板端入口。
- 已知历史结论存在冲突：早期参考驱动曾按1 Lane记录，官方核心板J601与当前硬件总览基线为2 Lane；必须回到实际屏幕资料和当前XML逐项确认。

## 最终审计结论

- 厂商随附 `README_zh.md` 和 `panel_w280bf036i.c` 均明确：W280BF036I/WLK2802为480×640、DSI 1 Lane、RGB888、Video Burst/LPM；工程Lane=1正确。
- Linux参考模式：pixel clock 22.572 MHz；H=480/30FP/10SYNC/30BP/550 total；V=640/20FP/4SYNC/20BP/684 total，约59.99 Hz。
- 当前GLCDC时钟为LCDCLK 240 MHz / 8 = 30 MHz，按550×684计算约79.73 Hz，与参考不一致。
- 当前生成 `g_mipi_dsi0_cfg` 为HBP20/HFP30、VBP16/VFP20；RASC GUI中的Back Porch字段不能直接当作最终DSI porch，修改后必须检查生成结构。
- 帧缓冲480×640、RGB565、双缓冲、SDRAM段；GLCDC链路输出RGB888；MIPI 1 Lane、VC0、连续时钟、PHY 600 MHz，接口方向基本正确。
- GUI Guider页面按640×480生成，LVGL Port创建480×640显示，工程没有显示旋转调用，板端逻辑分辨率不匹配。
- 厂商TXT初始化序列与 `st7701s_panel.c` 基本逐字节一致；当前C2使用TXT中的01/14，而Linux参考使用07/14，保留为实机对照项。
- 当前额外发送FF 77 01 00 00 00退出Command2；Linux参考也有，合理。
- 硬件Reset和Backlight函数为空，且没有软件Reset 0x01，这是首次点屏阻塞项。
- 所有命令统一按DCS Long Write发送；建议按参数长度选择Short Write 0/1 Param或Long Write。
- POST_OPEN发生在FSP把控制块标记Open之后且视频Start之前，回调时机正确；但初始化错误被忽略，其他MIPI错误事件也未记录。
- `RA8P1_GUI_ENABLE` 默认和当前Debug缓存均为OFF，当前固件不会打开MIPI/LVGL。
- 全量ARM编译时屏幕驱动和RASC生成代码通过，最终因 `gui_app.c` 引用不存在的 `guider_ui.Copy` 在91%失败。
- `tools/rasc_config_profile.json` 错误期望2 Lane，当前审计失败属于Profile假报警，不是XML错误。
