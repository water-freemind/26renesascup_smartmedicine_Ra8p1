# CEU 调试重大突破：彩条模式验证 D0-D7 接线 + HREF 7.5% 高定位

日期：2026-08-07

## 本轮重大进展（相对上一轮 md）

### 1. 排除缓存一致性问题（模块1）
- SCB->CCR = 0x000E0201 → **D-Cache 已使能**（bit16）
- MPU->CTRL = 0（未配置 MPU）
- **J-Link 读物理 SRAM（绕过 D-Cache）缓冲区=0** → 不是"写了但读到缓存副本"，是 **CEU 根本没写数据** → 缓存一致性彻底排除

### 2. 排除 MSTP（module-stop）假设
- **R_MSTP->MSTPCRC (0x40203008) = 0xF7FEFFFF → bit16=0** → CEU 不在 module-stop，时钟已解除（R_CEU_Open 的 R_BSP_MODULE_START 生效）
- librarian 的 MSTP 假设被实测排除

### 3. 关键新发现：CE 位能写入并保持
- 之前测试"CE 写入无效"是**脚本带 sleep 延迟**导致误判
- **无延迟立即读：CAPSR=0x01 (CE=1) 持续 26 次读保持**
- 随后 **CSTSR.CPTON=1 出现（CEU 启动传输！）**
- 然后 CE 被清、CPTON 回 0 —— 传输"瞬间"结束
- 结论：**CE=1 → VD 到来 → CPTON=1（启动）→ 立即停止**，无 CPE、无错误、无数据

### 4. 彩条模式决定性实验（重大突破！）
在固件 init 后打开彩条：COM3[0]=1 + DSP_CTRL3[5]=1
- **缓冲区出现数据：`FF57FF57 0000FF57`（行0）+ `8904`/`6904` 连续填充（行1）**
- **证明 D0-D7 物理接线完全正常！CEU 采到了彩条数据！**
- 之前 D0-D7 全 0 是传感器非彩条模式下无输出（配置问题），不是接线问题

### 5. HREF 信号实测 7.5% 高（核心矛盾）
- HREF(P415) 采样：HREF 模式 1/20 高、3/40 高（~7.5%）
- **OV7725 QVGA 理论 HREF 高 = 55.6%**（640/1152 PCLK）
- 7.5% vs 55.6% → **HREF 物理信号异常窄** → CEU 采 1-2 行后 NHD 停止

### 6. HWDTH 实验定位行宽
- HWDTH=160（640B/行）→ CEU 只写 1-2 行就 NHD
- **HWDTH=8（32B/行）→ CEU 连续采集多行不再 NHD！** 行间距 16 字节
- **结论：HREF 实际有效宽度 ≈ 16-32 字节，远小于 QVGA 的 640 字节 → 传感器实际输出窗口极窄**

## 当前配置状态
- COM7=0x46（QVGA RGB565）、COM4=0x41（PLL 4x）、CLKRC=0x00、**COM10=0x02（HREF 模式，已修复）**
- CEIER=0x110201（全中断恢复，CDTOFIE/VBPIE 已加回）
- HDPOL=HIGH（实测能写数据）、CMCYR=0、CAPWR=160/240、CDWDR=160、CFSZR=80/240
- 彩条模式已开启（测试中）

## 关键结论
1. **D0-D7 接线 OK**（彩条采到数据）
2. **PCLK/VD OK**（翻转/evt_vd 暴涨 4889）
3. **HREF 信号 7.5% 高是核心问题** → 传感器实际输出窗口极窄
4. 窗口寄存器配置（HSTART=0x3F/HSIZE=0x50/HOUTSIZE=0x50/EXHCH=0x00）经 Linux 内核 ov772x.c + 野火驱动双重验证**完全正确**（Renesas 官方驱动一字不差）
5. **嫌疑：寄存器写入时序/顺序**（COM7 需最后写、软复位 0x12=0x80 需最先、DSPAUTO=0xAC 被 CONTRAST=0x30 覆盖）

## 待验证（下一步）
1. **修复 DSPAUTO(0xAC) 被覆盖问题**：配置表第 95 行 {0xAC, 0x30}（CONTRAST）覆盖了第 53 行 {0xAC, 0xFF}（DSPAUTO）——0xAC 在 esp32 表里是 DSPAUTO，CONTRAST 应该是 0x9C！这是配置表 bug
2. **重写 COM7 触发窗口重新加载**（Linux 驱动最后写 COM7）
3. 野火完整 Sensor_Config 表获取（从 GitHub）
4. 彩条模式确认后 → 恢复正常模式验证

## 地址速查
- CEU: CAPSR=0x40348000, CAPCR=0x04, CAMCR=0x08, CMCYR=0x0C, CAMOR=0x10, CAPWR=0x14, CAIFR=0x18, CFSZR=0x34, CDWDR=0x38, CDAYR=0x3C, CEIER=0x70, CETCR=0x74, CSTSR=0x7C
- MSTPCRC = 0x40203008（bit16=CEU，实测 0=F7FEFFFF）
- g_ceu_buffer_0=0x220E13A0, g_ceu_buffer_1=0x220BBBA0
- OV7725 diag: s_dbg_ov_com2=0x22106BBC, dsp1=0x22106BB8, dsp2=0x22106BB4, dsp3=0x22106BB0, com8=0x22106BAC, dspauto=0x22106BA8, com10=0x22106BC4, com7=0x22106BD0
