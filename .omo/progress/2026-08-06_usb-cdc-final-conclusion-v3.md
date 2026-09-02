# 2026-08-06 联调终局（第四段：RASC 配置检查）

## 用户建议方向：检查 RASC 配置 / 看硬件 → 发现重大配置矛盾

### 发现：configuration.xml 与 ra_gen 的 usb_mode 不一致
| 文件 | usb_mode | 说明 |
|------|----------|------|
| configuration.xml (RASC源) | host | ⚠️ 若重新生成会得到主机固件，设备枚举消失 |
| ra_gen/Camera_thread.c (编译依据) | USB_MODE_PERI | 当前固件实际是设备模式 |
| 运行时 g_basic0_ctrl | usb_mode 字段 | 设备枚举成功(COM16)证明是 PERI |

- 时间戳：configuration.xml 22:10:52 vs Camera_thread.c 22:10:54（几乎同时，同批生成）
- 结论：当前编译固件 = PERI（正确），但 configuration.xml 源配置漂移为 host
- 风险：用户下次在 RASC 重新生成代码，会得到 host 固件，设备直接消失
- 需要用户在 RASC 里确认 usb_mode 实际显示，若为 host 需改回 Peripheral

### 硬件检查结论（从代码侧推断）
1. USB 线接在 USBHS 口：✅ 正确（设备枚举成 COM16 本身就是证明——USBHS 的 DP/DM 是专用引脚，USBFS 在 P814/P815）
2. pin_data.c 实际配置：P407/P408 = USB_HS（VBUS/VBUSEN），P814/P815 = USB_FS（DP/DM 残留配置）
3. VBUS 有效（VBSTS=1）、时钟 24MHz 正确、高速枚举（RHST=3）—— 之前已全部确认
4. 硬件连接大概率正确，无需改线

### 固件侧最终状态（三重证据闭合）
1. GDB trace3：所有 usbser 控制请求完整走通（event=8 + CTRL-END）
2. 真实运行 RAM 计数器：req_total=12, get=7, set=2, ctrl=3, data_get_ok=7, err=0, complete=9
3. 新设备实例（序列号 0002 → COM16）仍阻塞 → 排除 Windows 实例缓存

### 已实施全部固件修复（当前固件 20:38:25 已包含）
1. API 方向修正：GET_LINE_CODING→DataSet(发送)，SET_LINE_CODING→DataGet(接收)
2. 端口门控：s_port_open 在 GET_LINE_CODING 后置位
3. SerialState 通知：CONFIGURED/DTR 上报 + 周期性(25tick)发送
4. R_USB_Read 武装 PIPE2 + READ_COMPLETE 重新武装
5. 调试计数器保留（0x221A6808~0x221A6824, 0x221A682C~0x221A6830）

### 剩余障碍与下一步
- COM16 打开仍阻塞（固件响应 100% 正确）
- 需用户操作：①确认 RASC usb_mode 显示 ②物理拔插 USB 或重启 Windows
- 然后运行：py tools\find_camera_port.py / py tools\camera_viewer.py --port COM15
