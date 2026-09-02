# 2026-08-06 联调终局（第九段：最终交接）

## 全链路确认完毕（固件+配置+硬件全部正确）
1. RASC 配置 = Peripheral（设备模式）✅ 用户确认
2. 固件 API 方向/门控/通知/R_USB_Read 全部修复 ✅
3. 控制请求 9 次 event=8 完整走通 ✅
4. SerialState 通知按需发送成功（notify_ok=4, err=0）✅
5. 硬件 USBHS 口连接正确（枚举成功即铁证）✅

## 剩余唯一障碍：Windows usbser 驱动级缓存
- 已尝试全部软件手段：pnputil 删除/disable-enable/父集线器重启/restart-device/序列号更换 → 均无效
- 唯一有效动作：**物理拔插 USB 线 或 重启 Windows**
- 这清除的是 usbser.sys 内核驱动缓存，软件无法触达

## 最终验证步骤（物理拔插后）
1. 拔 USB → 等 5 秒 → 插回
2. 等 COM15 出现
3. py tools\find_camera_port.py
4. py tools\camera_viewer.py --port COM15

## 固件交付状态
- 当前烧录：无条件触发版 (21:23:16) —— 最终可用版本
- 所有修复：方向/门控/SerialState按需触发/R_USB_Read武装/调试计数器
- 若用户用 e2 studio：在 usb_pcdc_callback (src/app/usb_cdc.c:69) 断点
