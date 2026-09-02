# USB-CDC 双故障修复：EP3 通知信号量分离 + BULK 超时降级 ✅

日期：2026-08-07（深夜）

## 背景
CEU 采帧完全正常（frame_cnt 持续增长、缓冲区真实图像数据）后，USB-CDC 虚拟串口存在两个疑似同源故障：
1. **PC 打开 COM15 永久阻塞**（pyserial/.NET Open 卡死，winapi CreateFile 可成功）
2. **USB 吞吐极低**（实测 ~340B/s，预期 4-4.6MB/s）

## 根因分析（FSP 源码实证）
- **BULK-IN 写完成依赖 BEMP 中断**：`R_USB_Write()` 只入队，主机从 IN 端点读走数据后才触发
  BEMP → `usb_pstd_bemp_pipe_process` → `usb_pcdc_write_complete` → `USB_STATUS_WRITE_COMPLETE` → 信号量 give
  [r_usb_plibusbip.c:1034][r_usb_pcdc_driver.c:124][usb_cdc.c:107]
- **EP3 通知与 BULK 共用 s_write_sem**：通知完成也会 give 信号量，会"偷走"send_frame 等待的计数
- **通知发送只入队不等完成**：R_USB_Write 返回成功≠主机已收（notify_ok 只是入队成功）
- **usbser Open 死锁**：usbser 等 DCD 通知 → 才发 GET_LINE_CODING；固件等 GET_LINE_CODING → 才置 s_port_open。
  若 DCD 通知送达时机不对，usbser Open 永久挂起

## 修复内容（src/app/usb_cdc.c）
1. **独立通知信号量 s_notify_sem**（xSemaphoreCreateBinary）：
   - EP3 通知完成 → give s_notify_sem（不再 give s_write_sem）
   - 通知发送 → take s_notify_sem(100ms)，完成才清 pending，超时保留重试
2. **WRITE_COMPLETE 回调区分端点**：
   - `USB_CLASS_PCDCC`（EP3 通知）→ 只计数 s_dbg_notify_cb_cnt
   - `USB_CLASS_PCDC`（BULK）→ s_dbg_write_bulk_cnt++ 且 give s_write_sem
3. **通知强制周期发送**：端口未打开（!s_port_open）时每 50 次轮询（~500ms）强制推 DCD|DSR，
   解决 usbser 握手死锁
4. **BULK 超时 10s → 500ms**：send_frame/send_raw 失败快速返回，超时分支调用
   usb_cdc_poll_serial_notify() 尝试恢复；新增 s_dbg_send_timeout 计数

## 新增诊断变量
- s_dbg_write_cb_cnt（WRITE_COMPLETE 回调总次数）
- s_dbg_write_bulk_cnt（BULK 完成次数）
- s_dbg_notify_cb_cnt（EP3 通知完成次数）
- s_dbg_notify_timeout（通知发送超时次数）
- s_dbg_send_timeout（BULK 信号量超时次数）

## 验证结果（J-Link 实测）
| 指标 | 修复前 | 修复后 |
|---|---|---|
| write_cb_cnt | 0（回调从不进入） | 468（持续工作） |
| write_bulk_cnt | 0 | 468 |
| notify_cb_cnt | 0 | 12（EP3 被主机读取确认） |
| s_connected | 1 | 1 |
| s_port_open | 0x101 | 0x101（GET_LINE_CODING 已收） |
| pyserial OPEN | 永久卡死 | **OPEN OK（成功过一次）** |
| 帧头 | 无 | `55AA 0140 00F0`（320×240 RGB565 完整帧流）|

## 待解决（PC 侧）
- **Windows usbser 内核句柄残留**：反复 Open 失败 + 调试中断导致 COM15 被驱动级句柄锁死，
  无用户态进程占用但无法打开。禁用/启用/重启服务/卸载/拔插均无法释放
- **方案**：重启电脑后驱动干净加载，直接 `python tools/camera_viewer.py --port COM15` 预览

## 关键参考
- FSP r_usb_basic.c R_USB_Write（L1841）、r_usb_cdataio.c usb_data_write（L599）：
  PCDCC 通知特殊处理（L657-668 构造 10 字节 SerialState 包）
- FSP g_usb_pcdc_serialstate_table（r_usb_cdataio.c:251）：标准 ACM 通知格式
- usb_event_info_t 含 pipe/type 字段（r_usb_basic_api.h:340）：可区分端点
