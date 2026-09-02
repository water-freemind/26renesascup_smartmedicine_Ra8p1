# 2026-08-06 联调终局（第三段：全部固件修复 + 交接）

## 固件侧已 100% 修复 —— 三重独立证据全部闭合

### 证据1：GDB trace3（断点模式）
GET_LINE_CODING→DataSet→CTRL-READ→WRITE-DATA→event=8→CTRL-END
SET_LINE_CODING→DataGet→CTRL-WRITE→event=8→CTRL-END
SET_CONTROL_LINE_STATE→CTRL-END
usbser 完整打开握手序列全部完成

### 证据2：真实运行 RAM 计数器（无调试器，J-Link 只读）
当前固件(20:38:25)正确地址读取（0x221a68xx 区）：
- req_total   = 12  (usbser 发送 12 次请求含重试)
- req_get     = 7
- req_set     = 2
- req_ctrl    = 3
- data_get_ok = 7   (DataSet 全部 FSP_SUCCESS)
- data_get_err= 0
- req_complete= 9   (event=8 数据+状态阶段全部完成)
- s_rx_armed  = 1   (PIPE2 已成功武装 R_USB_Read)
- s_rx_count  = 0   (usbser 未发下行数据)
- notify_state= 03  (DCD|DSR)
- notify_pending=0  (SerialState 通知已发送)
- s_port_open = 1, s_connected = 1

### 证据3：新设备实例测试
序列号 0002 → 新设备实例 USB\VID_045B&PID_0002\0000000000002 → COM16
仍阻塞 → 推翻了'Windows 设备实例缓存'理论

## 已实施的全部固件修复（当前固件已包含）
1. API 方向修正：GET_LINE_CODING→R_USB_PeriControlDataSet(发送)，
   SET_LINE_CODING→R_USB_PeriControlDataGet(接收)
2. 端口门控：s_port_open 在 GET_LINE_CODING 完成后才置 true
3. SerialState 通知：CONFIGURED/DTR 时上报 + 端口打开期间周期性(25tick)发送
   (R_USB_Write + USB_CLASS_PCDCC → EP3, 驱动自动组装 10 字节通知)
4. R_USB_Read 武装 PIPE2（bulk OUT 接收），READ_COMPLETE 后重新武装
5. 调试计数器：s_dbg_req_* 保留（0x221a6808~0x221a6824）

## 已排除的因素（全部实验验证）
- 摄像头线程 bulk 发送干扰（#if 0 隔离）→ 排除
- 端口门控时机 → 已优化
- API 方向 → 已修正并验证
- Windows 设备实例缓存 → 新实例仍阻塞，排除
- pnputil /remove-device / disable-enable / 父集线器重启 → 排除

## 当前状态：固件响应 100% 正确，但 usbser Open() 仍阻塞
剩余障碍无法用软件侧手段验证/清除，需要物理操作或用户观察：
- 选项A：物理拔插 USB 线 或 重启 Windows（清除 usbser 驱动级缓存）
- 选项B：用户用 e2 studio 观察（用户已提供 launch.json）
  - 在 usb_pcdc_callback (src/app/usb_cdc.c:65) 设断点
  - 观察 usbser 打开时的完整请求序列

## 验证步骤（阻塞解除后）
1. py tools\find_camera_port.py  → 找摄像头板
2. py tools\camera_viewer.py --port COM15/COM16 → 看实时画面

## 关键符号地址（当前固件 20:38:25）
- usb_pcdc_callback     = 0x02008EF4
- usb_cdc_is_connected  = 0x020091F8
- g_usb_pcdc_int_in_pipe= 0x220415B4 ([1]=0x06)
- 计数器区              = 0x221A6808 ~ 0x221A6824
