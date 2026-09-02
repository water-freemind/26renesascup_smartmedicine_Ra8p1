# CEU 调试：PFS 假警报排除 + FSP 公式错误定位 + HD 事件真义

日期：2026-08-07

## 本轮关键结论（推翻之前的错误方向）

### 1. PFS "P414/P415 未配置" 是读错地址的假警报 ✅ 已排除
- 之前读 0x40400838/0x4040083C 判断 P414/P415 PSEL=0 —— **地址算错了**
- PFS 基址 0x40400800，每个 PORT 占 0x40（64B），**PORT4 在 0x40400900**（不是 0x40400800）
- 正确地址：
  - P400(D0)  = 0x40400900 → 0x0F010000 (PSEL=15=CEU) ✅
  - **P414(PCLK) = 0x40400938 → 0x0F010000 (PSEL=15=CEU) ✅**
  - **P415(HD)   = 0x4040093C → 0x0F010000 (PSEL=15=CEU) ✅**
  - P708(VD)    = 0x404009E0 → 0x0F010000 (PSEL=15=CEU) ✅
- **RASC 引脚配置全部正确生效**，之前"引脚复用问题"方向错误

### 2. RASC 极性/边沿配置正确 ✅
configuration.xml (line 530-555):
- hsync_polarity = high ✅（OV7725 HREF 高有效）
- vsync_polarity = high ✅
- dsel/hdsel/vdsel = rising
- 8-bit data_synchronous, QVGA 320×240, RGB565(byte_swapping 全开)

### 3. 重大发现：CEU_EVENT_HD = 0x100 是"HD 接收正常"事件，不是错误！
r_ceu.h line 70: `CEU_EVENT_HD = 0x00000100 ///< HD received (HD)`
- 历史观察"CETCR 只剩 HD(0x100)" **不是错误残留**，是 HD 信号正常接收的证据
- 之前误以为 HD 检测不到 —— 实际 HD 一直在正常工作

### 4. 重大发现：FSP 公式在 8-bit QVGA 下算错 CAPWR.HWDTH
r_ceu.c Open() 计算（line 96-102）:
```c
bytes_per_cycle = data_bus_width + 1;   // 8-bit → 1
bytes_per_line  = 320 * 2 = 640;
cycles_per_byte = (8bit ? 2 : 1) = 2;
cycles_per_line = 640 / 1 = 640;        // → CAPWR.HWDTH = 640
hfclp = 640 / 2 = 320;                  // → CFSZR.HFCLP = 320
```
- **硬件单位：8-bit data_synchronous 模式 HWDTH 单位是 4-PCLK**
- 实际应为：QVGA 640B/行 ÷ 4 = **160**（不是 640）
- FSP 默认 640 → 2560 PCLK/行 >> 实际行周期(~784 PCLK) → **IGHS 触发**
- CDWDR 也应 = 160（4B units），FSP 默认 640 → 写越界 → **CDTOF**
- **这就是 FSP 默认配置导致 IGHS/CDTOF 级联错误的根本原因**

### 5. 验证过的正确组合（历史数据支持）
HWDTH=160 + CMCYR=0 + CDWDR=160 + CFSZR(HFCLP=80, VFCLP=240)
→ IGHS/NHD/VBP/CDTOF 全清除，CETCR 只剩 HD(0x100)（正常事件）
→ 但 FRAME_END 仍 = 0（CE 被清）—— 这是当前唯一未解问题

## 下一步行动
1. camera_drv.c：恢复 HWDTH=160/CDWDR=160/CFSZR(80,240)/CMCYR=0 修正（FSP 默认值有 bug）
2. 检查 COM7=0x47（可疑：CPK 标准 QVGA RGB565 = 0x46，0x47 多 bit0）
3. 查 CE 被清 + FRAME_END=0 的根因（FSP CaptureStart 仅置 CE 位，错误会经 ISR→回调→recover）
4. 用 J-Link 实机验证 CETCR/CAPSR/CSTSR 状态

## 关键地址/寄存器速查
- PFS: R_PFS_BASE=0x40400800, PORT4=0x40400900 (P414=+0x38, P415=+0x3C), PORT7=+0x1C0
- CEU: CAPSR=0x40348000, CAPCR=0x04, CAMCR=0x08, CMCYR=0x0C, CAPWR=0x14, CFSZR=0x34, CDWDR=0x38, CEIER=0x70, CETCR=0x74
- CEU_EVENT: FRAME_END=0x1, HD=0x100, VD=0x200, CDTOF=0x10000, IGHS=0x20000, IGVS=0x40000, VBP=0x100000, NHD=0x1000000, NVD=0x2000000
