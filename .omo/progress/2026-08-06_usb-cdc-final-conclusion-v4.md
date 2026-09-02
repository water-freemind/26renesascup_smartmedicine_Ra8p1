# 2026-08-06 联调终局（第五段：RASC 配置矛盾彻底澄清）

## RASC 配置检查最终结论

### 关键事实链
1. HEAD 提交的 configuration.xml **根本没有 USB 模块**（r_usb_basic/r_usb_pcdc 都不存在）
2. 工作区 configuration.xml 是**未提交的新增**（51 行插入）——USB 模块是最近通过 RASC 添加的
3. configuration.xml 中 usb_mode = host（主机模式）
4. 但 ra_gen/Camera_thread.c 生成的是 USB_MODE_PERI（设备模式）
5. 设备枚举成功（COM16 出现）→ **证明当前编译固件确实是 PERI 模式**（若是 host 固件不可能枚举为 USB 设备）

### 结论
- **当前固件 = PERI 设备模式，正确**（枚举成功即铁证）
- configuration.xml 的 host 是**潜在风险**：若用户下次在 RASC 重新生成代码，会得到 host 固件，设备枚举直接消失
- 用户需在 RASC 中确认 usb_mode 显示，若为 Host 需改回 Peripheral 再生成
- 这不是当前阻塞的直接原因（当前固件正确），但必须告知用户

## 硬件检查清单（供用户核对）
1. ✅ USB 线接 USBHS 口（COM16 枚举成功即证明——USBHS DP/DM 为专用引脚，USBFS 在 P814/P815）
2. ✅ VBUS 有效（VBSTS=1）、24MHz 时钟、高速枚举（RHST=3）——均已确认
3. ✅ pin_data.c：P407/P408=USB_HS（VBUS/VBUSEN），P814/P815=USB_FS（DP/DM 残留）
4. 无需改线，硬件连接大概率正确

## 固件侧最终状态（三重证据闭合）
1. GDB trace3：所有控制请求完整走通
2. 真实运行计数器：req_total=12, get=7, set=2, ctrl=3, complete=9, err=0
3. 新设备实例（序列号 0002→COM16）仍阻塞 → 排除 Windows 实例缓存

## 剩余障碍
- COM16 打开仍阻塞（固件响应 100% 正确）
- 需用户：①确认/修正 RASC usb_mode ②物理拔插或重启
- 然后运行 camera_viewer.py

## 已实施固件修复（当前固件 20:38:25）
1. API 方向修正（GET→DataSet, SET→DataGet）
2. 端口门控（s_port_open）
3. SerialState 通知（周期性 25tick）
4. R_USB_Read 武装 PIPE2 + READ_COMPLETE 重新武装
5. 调试计数器保留
