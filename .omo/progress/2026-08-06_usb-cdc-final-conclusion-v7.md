# 2026-08-06 联调终局（第八段：RASC 配置确认 PERI）

## 用户确认：RASC 中 USB Basic 模块 Mode = Peripheral（设备模式）✓
- configuration.xml 文本中的 usb_mode.host 是 RASC 工具显示问题
- 实际生效配置 = PERI，与 ra_gen/Camera_thread.c (.usb_mode=USB_MODE_PERI) 一致
- 与实测完全吻合：设备枚举成功(COM15)、USBHS 高速、控制请求全部完成

## 配置检查闭环
| 项目 | 状态 |
|------|------|
| RASC usb_mode | ✅ PERI (用户确认) |
| ra_gen 生成代码 | ✅ USB_MODE_PERI |
| 运行时行为 | ✅ 设备模式枚举成功 |
| USB 引脚 | ✅ P407/P408=USB_HS, 枚举成功证明接对 |
| 硬件连接 | ✅ USBHS 口（枚举成功即铁证） |

## 固件侧最终状态（21:23:16 无条件触发版，已烧录）
- API 方向修正（GET→DataSet, SET→DataGet）✓
- 端口门控（s_port_open）✓
- SerialState 通知无条件按需触发 ✓ (notify_ok=4, err=0)
- R_USB_Read 武装 PIPE2 ✓ (rx_armed=1)
- 控制请求 9 次 event=8 完整走通 ✓ (req_complete=9, data_get_ok=7, err=0)

## 剩余：COM15 打开仍阻塞
固件响应 100% 正确 + 配置确认正确 + 硬件连接正确
唯一未验证变量：Windows usbser 驱动级缓存 → 需物理拔插 USB 或重启 Windows
