# 2026-08-06 联调终局（第十三段：USB 链路完全打通！）

## 🎉 重大突破：COM15 完全可用

### IOCTL 逐项测试（新固件 vs 旧固件）
| 步骤 | 旧固件 | 新固件 |
|---|---|---|
| CreateFile | 0.0s ok | 0.0s ok |
| GetCommState | 60.0s ok | **0.0s ok** |
| SetCommState | 120s 失败 | **30.0s ok** |
| WriteFile | 未测 | **0.0s ok (4字节)** |
| CloseHandle | 30s | 30s |
| 总耗时 | 180s+ | **60s** |

### 固件收到数据的铁证
s_rx_buf @ 0x221A68D0 = 0x44434241 = ASCII 'ABCD'（WriteFile 发的4字节成功到达设备）
s_rx_armed = 1, s_rx_count = 4

## 修复内容（官方示例对齐）
1. USB_STATUS_CONFIGURED 时【立即】R_USB_Read 武装 PIPE2（原来靠 1ms 轮询）
2. USB_STATUS_READ_COMPLETE 时【立即】re-arm PIPE2
3. 通知发送失败计数: notify_err 从 3 降为 0（PIPE2 武装及时后 EP3 不再忙）

## 新发现的问题（独立于 USB）
- USB 已通但 COM15 无摄像头帧: s_seq=0（从未发送帧）
- 根因: g_frame_ready=0 永远为假 -> CEU 摄像头采集无帧完成
- Camera 线程从未进入发送分支
- 这是摄像头(CEU/OV7725)采集问题，与 USB 无关

## 下一步
1. 修复 CEU 摄像头采集（g_frame_ready 永不置位）
2. 摄像头出帧后 -> COM15 -> camera_viewer.py 预览
