# USB-CDC COM15 打开挂死：深度排查（Windows 侧）🔍

日期：2026-08-07 20:27
状态：⏸ 已暂停测试，进度已保存

## 当前固件状态（工作区）
- `src/app/usb_cdc.c` = **af4c9df 原版**（399 行，git checkout 恢复）
  - `poll_serial_notify` 仅 pending 时单次发送（fire-and-forget，成功即清）
  - **无孤儿 return**（此前检测是正则误报，实际函数结构干净）
  - `s_port_open = true` 在 GET_LINE_CODING 分支置位
  - `is_connected = s_connected && s_port_open`
  - 两处 10s 超时（send_frame/send_raw）
- `r_usb_pcdc_descriptor.c` bInterval = **0x10**（原始值，FS 行183 + HS 行276，与 HEAD 一致）
- **build/Debug .elf = 已用 af4c9df 编译的最新固件**（16:xx 编译，烧录成功，Verifying flash 100%）
- git status：`M src/app/usb_cdc.c`（相对 HEAD 9f4e99a，因恢复为 af4c9df 所以有 diff）

## 核心矛盾（决定性问题）
**同一固件（af4c9df）重启前 OPEN OK**（write_bulk_cnt=468、帧头 `55AA 0140 00F0` 收到、notify_cb_cnt=12）
→ **PC 重启后烧录同一代码，COM15 枚举正常但 Open() 永久挂死**（pyserial/.NET/裸 CreateFile 全部 HANG >10s）

→ 固件代码本身能工作，问题指向 **Windows usbser 环境状态**。

## 固件侧证据（J-Link 读 RAM，全部正常 ✅）
| 符号 | 值 | 含义 |
|---|---|---|
| s_connected | 1 | 枚举完成 |
| s_port_open | 1 | GET_LINE_CODING 已收（usbser 打开握手进行中）|
| s_dbg_req_total | 12 | USB_STATUS_REQUEST 共 12 次 |
| s_dbg_req_get | 7 | **GET_LINE_CODING 7 次（异常，正常 1 次）** |
| s_dbg_req_ctrl | 3 | SET_CONTROL_LINE_STATE 3 次 |
| s_dbg_req_set | 2 | SET_LINE_CODING 2 次 |
| s_dbg_data_get_ok | 7 | DataSet 全部 FSP_SUCCESS |
| s_dbg_data_get_err | 0 | 零失败 |
| s_dbg_notify_ok | 4~202 | EP3 通知发送成功 |
| s_dbg_notify_err | 0 | 零失败 |
| s_dbg_notify_cb_cnt | 3~12 | EP3 通知被主机读取确认 |
| s_dbg_write_bulk_cnt | 25~73 | BULK 数据持续流动 |
| s_dbg_req_complete | 待读 | **未读取**（下一步重点：与 req_total 对比）|

**GET_LINE_CODING ×7 分析**：SET/CTRL 只有 2/3 次而 GET 有 7 次——usbser 在 GET 阶段反复重试，
说明它认为设备对 GET_LINE_CODING 的响应不完整。FSP 侧机制：
- BEMP 的 WRITEEND/WRITESHRT 分支只清 BEMPENB，状态阶段由主机 OUT ZLP 触发 BRDY 完成（标准流程）
- `s_dbg_req_complete`（USB_STATUS_REQUEST_COMPLETE event=8）若远小于 req_total → 状态阶段缺失/未完成

## 已排除的假设（全部实测无效）
1. ❌ 孤儿 `{ return; }` 阻塞 poll_serial_notify —— 正则误报，af4c9df 无此 bug
2. ❌ bInterval 0x10→0x01（EP3 轮询加速）—— notify_cb_cnt 提升但 Open 仍挂
3. ❌ DCD 边沿 0→1（枚举时 DCD=0，SET_CONTROL_LINE_STATE 时置 1）—— 仍挂
4. ❌ P0 修复：s_dcd_delivered 门控 bulk（WRITE_COMPLETE 区分 PCDCC/BULK）—— 仍挂
5. ❌ P1：send_frame 超时 10s→100ms —— 仍挂
6. ❌ P2：SET_CONTROL_LINE_STATE 触发 DCD notify —— 仍挂
7. ❌ 低频兜底（200 poll ~2s 重发通知）—— 仍挂
8. ❌ 周期重发（每 5 poll）—— notify_err=7375 干扰 PIPE0，有害，已废弃
9. ❌ 管理员脚本清理（pnputil /remove-device + 重启 usbser 服务 + 重新枚举）—— 仍挂
10. ❌ 禁用/启用设备 —— Disable-PnpDevice 权限失败，Enable 后 COM15 回来但 Open 仍 HANG

## 最新关键线索（本次会话）
**VSCode Renesas 扩展加载了 serialport 库**：
- 进程：`Code.exe` PID=26868（NodeService utility 进程，VSCode 主进程）
- 模块：`@serialport+bindings-cpp.node`
- 来源：`renesaselectronicscorporation.renesas-debug...` 和 `renesas-smart-configurator` 扩展目录
- ⚠️ 这解释了"重启前能开、重启后挂死"：**VSCode 重启后自动恢复，Renesas 扩展可能自动连接 COM15**
- ⚠️ 待验证：该进程是否真正持有 COM15 句柄（句柄枚举脚本 NtQuerySystemInformation 权限被限制返回 0）

## 设备信息
- VID_045B&PID_0002&REV_0200，Service=usbser，PortName=COM15
- usbser 驱动：C:\WINDOWS\system32\drivers\usbser.sys，State=Running
- 驱动版本：10.0.26100.8521（Win11 24H2）
- Location：Port_#0001.Hub_#0009
- 注册表映射：`\Device\USBSER000 : COM15`（HARDWARE\DEVICEMAP\SERIALCOMM）
- ⚠️ 观察：GetPortNames 曾短暂不含 COM15（符号链接丢失，CreateFile err=2），随后恢复

## 符号地址（当前 af4c9df 固件）
```
s_dbg_req_complete   0x2215b990
s_dbg_data_get_err   0x2215b994
s_dbg_data_get_ok    0x2215b998
s_dbg_req_default    0x2215b99c
s_dbg_req_ctrl       0x2215b9a0
s_dbg_req_set        0x2215b9a4
s_dbg_req_get        0x2215b9a8
s_dbg_req_total      0x2215b9ac
s_dbg_notify_err     0x2215b9b8
s_dbg_notify_ok      0x2215b9bc
s_serial_notify_pending 0x2215b9c1 (b)
s_rx_count           0x2215b9c4
s_rx_armed           0x2215b9c8
s_port_open          0x2215ba18
s_connected          0x2215ba19
```

## 下一步行动（恢复测试时）
1. **读 `s_dbg_req_complete`** 与 req_total 对比 → 判断 GET_LINE_CODING 状态阶段是否完成
2. **验证 VSCode Renesas 扩展持有 COM15**：
   - 临时关闭 VSCode（或禁用 Renesas 扩展）→ 立即测 COM15 Open
   - 或管理员权限重跑句柄扫描脚本（`C:\Users\ZHANGL~1\AppData\Local\Temp\opencode\scan_com_handles.py`）
3. 若确认是 Renesas 扩展抢占 → 在 VSCode 设置里关闭自动串口连接（renesas-debug / smart-configurator 的 auto-connect 选项）
4. 若排除 → 考虑物理拔插 USB / 换 USB 口 / 另一台 Windows 主机交叉验证

## 参考文件
- `.omo/progress/2026-08-06_usb-cdc-dual-fix.md`（EP3 通知信号量分离 + BULK 超时降级，pyserial OPEN OK 首次验证）
- `src/app/usb_cdc.c`（当前 af4c9df 原版）
- `ra/fsp/src/r_usb_pcdc/r_usb_pcdc_descriptor.c`（bInterval=0x10）
- 管理员清理脚本：`C:\Users\ZHANGL~1\AppData\Local\Temp\opencode\reset_com15_admin.ps1`
- 句柄扫描：`C:\Users\ZHANGL~1\AppData\Local\Temp\opencode\scan_com_handles.py`
- 打开测试：`C:\Users\ZHANGL~1\AppData\Local\Temp\opencode\` 下 open_test.py / open_v3.py / cf_v2.py / enum2.py 等
