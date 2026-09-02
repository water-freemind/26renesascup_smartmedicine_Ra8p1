# 2026-08-06 J-Link 硬件断点调试方法（记录入 OMO）

## 为什么用 J-Link BP 而非 GDB
- GDB + GDBServer 连接时容易把 CPU halt 在 __ISB() 等任意位置，干扰实时运行
- J-Link Commander 的 BP (Breakpoint) 用硬件断点，命中时精确停在目标函数，其余时间程序正常运行
- 与 e2 studio 的 Renesas GDB Debugging (SEGGERJLINKARM) 等效，都是 J-Link 硬件断点

## J-Link BP 调试命令模板（R7KA8P1KF_CPU0）
\\\jlink
device R7KA8P1KF_CPU0
si SWD
speed 4000
connect
r            ; 复位
BP 0x2008D34 ; 设断点 camera_ov7725_init
g            ; 运行
sleep 5000   ; 等 5 秒（断点命中则 CPU halt，否则继续运行）
regs         ; 看 PC/寄存器（确认是否命中）
mem32 0x40400020, 4  ; 读 CEU 中断使能
q
\\\

## 关键符号地址（v13 固件）
- camera_app_init      @ 0x02008DC0
- camera_ov7725_init   @ 0x02008D34
- R_CEU_Open           @ 0x020036F8
- g_frame_ready        @ 0x220BBAE9 (0x220BBAE8 处读 4 字节)
- s_seq (USB帧序号)     @ 0x221A6918
- CEU CEIER            @ 0x40400020

## 注意点
- J-Link 连接时断点残留可能误判（PC 停在旧断点地址）
- 每次复位运行 CycleCnt 不同（0x02296F03~0x0229A119 ≈ 0.5s@480MHz）→ 程序在正常运行到断点
- 若只设 R_CEU_Open 断点而 PC 停在 ov7725_init → 需先 BC 清断点再验证
