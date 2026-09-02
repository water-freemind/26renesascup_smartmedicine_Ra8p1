# 2026-08-06 联调终局（第十一段：IOCTL 测试揭示 60 秒延迟真相）

## 重大发现：不是永久阻塞，而是响应延迟约 60 秒
IOCTL 逐项测试（win32 API）：
- CreateFile:      0.0s  ✅ 设备句柄正常打开
- GetCommState:   60.0s  ✅ 成功但耗时 60 秒！
- SetCommState:  120.0s  ❌ 失败（超时）
- GetCommTimeouts: 0.0s  ✅
- SetCommTimeouts: 0.0s  ✅
- SetupComm:       0.0s  ✅
- CloseHandle:    30.0s  ⚠️ 也慢

长等待测试：Open() 在 210 秒后报"信号灯超时时间已到"（WaitForSingleObject 超时）

## 解读
- 此前所有测试脚本只等 25-40 秒就超时 → 误判为"永久阻塞"
- 真实情况：固件对 usbser 的控制请求【全部正确响应】（req_complete=9, data_get_ok=7, err=0, notify_ok=4）
- 但每个控制请求在 usbser 侧耗时约 5 秒（12 次请求 × 5 秒 ≈ 60 秒）
- 5 秒是 Windows usbser 的 URB 超时重试间隔 → 说明 usbser 认为每个请求都"超时失败"后重试
- 但固件明明响应了（event=8 发生）→ 矛盾指向：固件响应可能【太慢】或【响应时机/顺序】有问题

## 已排除
- 任务优先级饥饿：PCD_TSK_PRI = configMAX_PRIORITIES-1 = 4（BSP_CFG_RTOS==2），Camera 线程=2 → USB 任务最高优先级，无饥饿
- configUSE_TIME_SLICING = 0，configMAX_PRIORITIES = 5
- 通知轰炸（已改按需触发，notify_ok=4 而非 4866）
- RASC 配置（用户确认 Peripheral）
- 硬件 USBHS 口（枚举成功）

## 新假设
固件控制请求响应在 GDB 断点模式下即时完成（CTRL-END），但真实运行时 usbser 感知为超时重试（5 秒/次）。
可能原因：真实运行时某个环节阻塞了 USB 中断处理或 PIPE0 控制传输 ~5 秒。
候选：Camera 线程 s_port_open=true 后开始 bulk IN 发送，与 PIPE0 控制传输竞争？

## 下一步（需在真实运行时验证）
1. 在 usb_pcdc_callback 设断点并【测量两次回调间的时间间隔】确认每次请求到达固件的实际间隔
2. 若间隔 ~5 秒 → usbser 确实在超时重试 → 固件响应有延迟
3. 若间隔 ~0 秒 → 固件即时收到但 event=8 后 usbser 仍不满意 → 响应内容/顺序问题
