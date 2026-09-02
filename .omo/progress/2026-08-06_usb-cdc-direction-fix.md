# 2026-08-06 联调进展（第二次会话续）

## 结论：固件已修复（双重证据），剩余障碍在 Windows usbser 失败缓存

### 1. 方向修复（根本原因）
- usb_cdc.c 回调中 GET_LINE_CODING 误用 R_USB_PeriControlDataGet（接收方向）
- 实际应使用 R_USB_PeriControlDataSet（发送方向，内部 usb_pstd_ctrl_read → 写 FIFO）
- SET_LINE_CODING 反之用 DataGet
- 已交换修正

### 2. GDB trace3 证据（断点模式）
- GET_LINE_CODING: DataSet → CTRL-READ → WRITE-DATA → event=8(COMPLETE) → CTRL-END(CCPL) ✓
- SET_LINE_CODING: DataGet → CTRL-WRITE → event=8 → CTRL-END ✓
- SET_CONTROL_LINE_STATE: CTRL-END ✓

### 3. 真实运行计数器证据（无调试器）
RAM 计数器（J-Link 只读）：
- s_dbg_req_total=4 (0x22180D70)
- s_dbg_req_get=2   (0x22180D6C)
- s_dbg_req_set=1   (0x22180D68)
- s_dbg_req_ctrl=1  (0x22180D64)
- s_dbg_data_get_ok=2 (0x22180D5C)
- s_dbg_data_get_err=0 (0x22180D58)
- s_port_open=1 (0x22180D7C), s_connected=1 (0x22180D7D)

### 4. 已排除的因素
- 摄像头线程 bulk 发送干扰 → 已用 #if 0 禁用仍阻塞
- 端口门控时机（s_port_open 移到 GET_LINE_CODING 之后）→ 仍阻塞
- .NET SerialPort vs Python pyserial → 都阻塞
- pnputil 删除设备节点（管理员）→ 重新枚举后仍阻塞

### 5. 下一步（需要用户物理操作）
- 物理拔插 USB 线 或 重启 Windows，清除 usbser 内核失败缓存
- 然后测试 COM15：py tools\find_camera_port.py
- 若仍失败：用 e2 studio 在 usb_pcdc_callback 断点观察（launch.json 已提供）
- 隔离实验的 #if 0 需恢复（Camera_thread_entry.c 第 71-86 行）
- 调试计数器可保留或删除（usb_cdc.c 顶部 volatile uint32_t s_dbg_*）

### 6. 调试技巧沉淀
- J-Link Commander 的 bp 命令不生效，必须用 GDB
- GDB 断点会自动 continue 的脚本模式可捕获完整事件流（见 gdb_trace3.gdb 思路）
- J-Link mem32/mem8 读 RAM 只暂停毫秒级，可验证真实运行时状态
- usbser 打开序列：SET_LINE_CODING → SET_CONTROL_LINE_STATE → GET_LINE_CODING（GDB 实测）
