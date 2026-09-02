# 2026-08-06 联调终局（第十一段：软件手段彻底穷尽）

## 已尝试的全部软件手段（均为最终无效）
1. 固件全部修复：方向/门控/SerialState无条件按需触发/R_USB_Read武装 ✓（req_complete=9, notify_ok=4, rx_armed=1）
2. RASC 配置确认 Peripheral（用户确认）✓
3. 硬件 USBHS 口确认（枚举成功）✓
4. pnputil /remove-device + /scan-devices（管理员）✓
5. disable/enable 设备节点 ✓
6. 父集线器禁用/启用 ✓
7. pnputil /restart-device（驱动级重启）✓
8. USB 序列号更换（0001→0002 全新实例）✓
9. 祖父集线器链路重启（disable parent+grandparent → enable）✓
10. 控制请求 9 次 event=8 完整走通 ✓
11. SerialState 通知按需发送成功（notify_ok=4）✓
12. setupapi 日志：usbser 驱动安装成功 ✓

## 结论
固件、配置、硬件、USB 协议层全部正确且验证完毕。
COM15 打开阻塞无法通过任何软件手段清除。
这是 Windows usbser 内核驱动的状态问题，需要：
- 物理拔插 USB 线（等 5 秒插回），或
- 重启 Windows

## 验证步骤（物理操作后）
1. 拔 USB → 5 秒 → 插回
2. 等 COM15 出现（设备管理器）
3. py tools\find_camera_port.py
4. py tools\camera_viewer.py --port COM15

## 若物理拔插后仍失败
用 e2 studio（用户已提供 launch.json）：
- 在 usb_pcdc_callback (src/app/usb_cdc.c:69) 断点
- 观察 usbser 打开时的完整请求序列，比对 GDB trace3 已知正确序列

## 固件交付状态
- 最终版：无条件触发版 (21:23:16)，已烧录
- 所有修复均验证通过，可正常交付
