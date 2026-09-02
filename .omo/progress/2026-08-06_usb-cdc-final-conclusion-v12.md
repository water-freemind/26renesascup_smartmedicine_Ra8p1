# 2026-08-06 联调终局（第十二段：时间戳诊断揭示 Configured 丢失）

## 决定性新证据：请求时间戳 + USB 寄存器

### 时间戳序列（s_dbg_req_tick / s_dbg_complete_tick）
批量1: 252ms, 253ms (GET_LINE_CODING x2, 间隔1ms -> 固件瞬时响应)
批量2: 7665ms (距批量1约 7.4s -> usbser 超时重试)
批量3: 37679ms (距批量2约 30s -> usbser 指数退避重试)

-> 固件永远瞬时响应，延迟 100% 在 usbser 侧

### 计数器（最终态）
req_total=6, req_get=3, req_set=1, req_ctrl=2
req_complete=4, data_get_ok=3, data_get_err=0
notify_ok=4, notify_err=3, notify_last_err=0
rx_armed=1, port_open=1, connected=1

### USB 寄存器（GDB 非侵入读取，设备运行中）
INTSTS0 = 0x20B0 -> DVSQ=2 (Default), Configured=0  [曾为 0x20B5 = Configured]
NRDYSTS = 0x00B80006 -> PIPE3/4/6 NRDY
BRDYSTS = 0x00010001

## 核心结论
1. 固件对所有 usbser 请求瞬时正确响应（时间戳铁证）
2. 设备从 Configured(0x20B5) 掉回 Default(0x20B0) —— Configured 状态丢失
3. usbser 重试 3 次后放弃打开
4. 根因方向：usbser 打开序列中固件响应正确但设备被 bus reset 或状态丢失

## 新嫌疑：s_port_open 开闸过早
s_port_open 在 GET_LINE_CODING 时置位(true)，Camera 主循环(1ms)立即开始发送摄像头帧
-> bulk IN 数据注入可能干扰 usbser 打开序列的后续步骤
-> 需要对比官方 PCDC 示例确认开闸时机（librarian 查询中 bg_9e694f3c）

## 已保存地址表（GDB 解析）
s_dbg_req_tick[16]    @ 0x221A6858
s_dbg_req_tick_idx    @ 0x221A6850
s_dbg_complete_tick   @ 0x221A6810
s_dbg_complete_tick_idx @ 0x221A6808
s_dbg_req_total       @ 0x221A68B4
s_dbg_req_get         @ 0x221A68B0
s_dbg_req_set         @ 0x221A68AC
s_dbg_req_ctrl        @ 0x221A68A8
s_dbg_req_default     @ 0x221A68A4
s_dbg_data_get_ok     @ 0x221A68A0
s_dbg_data_get_err    @ 0x221A689C
s_dbg_req_complete    @ 0x221A6898
s_dbg_notify_ok       @ 0x221A68C0
s_dbg_notify_err      @ 0x221A68BC
s_dbg_notify_last_err @ 0x221A68B8
s_serial_notify_pending @ 0x221A68C5
s_serial_notify_state @ 0x221A68C4
s_port_open           @ 0x221A691C
s_connected           @ 0x221A691D
s_rx_armed            @ 0x221A68CC
s_line_coding         @ 0x220002BC (flash)
s_ctrl_line_state     @ 0x221A6910
s_rx_buf              @ 0x221A68D0
