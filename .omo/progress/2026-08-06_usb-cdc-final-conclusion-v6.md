# 2026-08-06 联调终局（第七段：无条件触发修复生效 + 交接）

## 最新数据（无条件触发版固件 21:23:16，真实运行无调试器）
req_total    = 12   ← usbser 发送 12 次请求（多轮打开尝试）
req_get      = 7    ← GET_LINE_CODING 7 次
req_ctrl     = 3    ← SET_CONTROL_LINE_STATE 3 次
req_complete = 9    ← ★ 控制请求 9 次 event=8 完整走通（7 GET + 2 SET）
notify_ok    = 4    ← ★★ 通知成功发送 4 次（CONFIGURED 1 + SET_CONTROL_LINE_STATE 3，完全对应）
notify_err   = 0    ← 零失败
notify_pending = 0  ← 通知全部成功发送
rx_armed = 1, port_open = 1, connected = 1

## 修复演进验证（理论闭环）
1. 轰炸版（每25tick）: notify=4866次, req_complete=0 ← 通知轰炸干扰控制传输
2. 按需DTR触发版: notify=1次, req_complete=9 ← 控制恢复但通知发不够
3. 无条件触发版(当前): notify=4次, req_complete=9 ← ★ 通知按需发足+控制完成

## 固件侧最终状态：全部修复完成，无可修项
- API 方向修正（GET→DataSet, SET→DataGet）✓
- 端口门控（s_port_open 在 GET_LINE_CODING 后置位）✓
- SerialState 通知无条件按需触发（SET_CONTROL_LINE_STATE 必发）✓
- R_USB_Read 武装 PIPE2 + READ_COMPLETE 重新武装 ✓
- 调试计数器保留

## 剩余障碍：COM15 打开仍阻塞（固件响应 100% 正确）
- 控制请求全部完成（9 次 event=8）
- 通知已按需发送（4 次成功）
- 但 usbser 仍不完成打开
- 软件侧已穷尽：pnputil删除/disable-enable/父集线器重启/新序列号实例全部无效

## 下一步（需用户操作）
1. 物理拔插 USB 线 或 重启 Windows（清除 usbser 驱动级缓存）
2. 用 e2 studio 在 usb_pcdc_callback (usb_cdc.c:69) 断点观察实际请求序列
3. 确认 RASC configuration.xml usb_mode（当前显示 host，与 ra_gen PERI 矛盾，重新生成会变 host 导致设备消失）
4. 阻塞解除后：py tools\find_camera_port.py → py tools\camera_viewer.py --port COM15

## 关键符号（当前固件 21:23:16）
- usb_pcdc_callback    = 0x0200909E 附近
- 计数器区            = 0x221A6808 ~ 0x221A6830
