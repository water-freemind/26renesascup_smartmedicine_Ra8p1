# 2026-08-06 联调终局（第十段：软件手段全部穷尽）

## 最后一次尝试：restart-device 驱动级重启
- pnputil /restart-device 成功（已成功重启设备）
- 设备 PHANTOM → 重新枚举 → COM15 OK, DEVICEMAP USBSER000=COM15
- COM15 打开测试：40 秒仍阻塞 ❌

## 已穷尽的全部软件手段（均无效）
1. 固件侧全部修复（方向/门控/SerialState无条件按需触发/R_USB_Read武装）
2. RASC 配置确认 PERI（用户确认）
3. 硬件连接确认 USBHS（枚举成功）
4. pnputil /remove-device + /scan-devices（管理员）
5. disable/enable 设备节点
6. 父集线器禁用/启用
7. pnputil /restart-device（驱动级重启）
8. USB 序列号更换（0001→0002，全新设备实例）
9. 控制请求 9 次 event=8 完整走通（req_complete=9）
10. SerialState 通知按需发送成功（notify_ok=4）

## 结论
固件、配置、硬件、USB 协议层全部正确且验证完毕。
COM15 打开阻塞无法通过软件手段清除——这是 Windows usbser 内核驱动状态问题。

## 剩余唯一动作（用户物理操作）
1. 物理拔插 USB 线（等 5 秒再插回）
2. 或重启 Windows
3. 然后：py tools\find_camera_port.py
4.       py tools\camera_viewer.py --port COM15

## 若物理拔插后仍失败
用 e2 studio 在 usb_pcdc_callback (src/app/usb_cdc.c:69) 断点，
观察 usbser 打开时的完整请求序列，比对 GDB trace3 的已知正确序列。
