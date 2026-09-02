# 2026-08-07 CEU 捕获突破：CPK 配置复刻后数据写入成功

## 重大突破
复刻 Renesas CPK 官方 RA8D1+OV7725 示例配置后，CEU 缓冲从"全 0"变为"有数据"！
（0x80 42 80 42 80 02 ...）——CEU 捕获链路打通。

## 关键修复组合（缺一不可）
1. **DATA_SYNCHRONOUS** 模式（非 IMAGE_CAPTURE）—— CPK 标准
2. **byte_swapping 8bit+16bit+32bit 全交换** —— CPK 标准
3. **VSYNC 高有效 + COM10=0x02** —— CPK 标准
4. **RGB565 输出**（COM7=0x46，非 YUV 0x40）—— CPK 标准
5. **DSEL/HDSEL/VDSEL=1** 采样边沿 —— RASC 枚举映射 bug 修复
6. **CPEIE-only 中断掩码** —— 排除 NHD/HD/VBP 干扰中断
7. **CAPWR.HWDTH=1280** —— 8-bit 总线每字节 2 PCLK（RGB565 320px×2B=640B/行→1280 PCLK）

## 关键参考：CPK 官方示例
- 仓库: github.com/renesas/cpk_examples
- 路径: cpkexp_ekra8x1/ceu_cpkexp_ra8d1_ep/e2studio_llvm/
- 文件: src/CAMERA/ceu.c + ov7725.c + configuration.xml
- 已下载到: C:\Users\ZHANGL~1\AppData\Local\Temp\opencode\cpk\
- 关键: ceu.c 在 YUV→RGB 转换前调用 SCB_EnableDCache()（启用 D-Cache）
- CPK 用 VGA 640x480 RGB565（x_pixels=640, y_pixels=480）→ RAM 溢出，回退 QVGA

## 遗留问题
- 缓冲只有前几字节有数据（80 42 80 42 80 02...），后面全 0
- FRAME_END(CPE)=0 从未触发——CEU 未"完成"一帧
- CETCR 有 VBP(bit20)/IGHS(bit17) 错误位
- CAPWR=0x00F00500（HWDTH=1280 已应用但 FRAME_END 仍=0）

## 下一步方向
1. CAPWR.HWDTH=1280 已应用仍无效 → 可能不是 1280
2. 验证前几字节数据的真实性（是否 CEU 写入 vs 残留）
3. VBP 根因：OV7725 帧时序 vs CEU 期望
4. 可能需示波器确认传感器实际输出

## 当前配置状态
- configuration.xml: data_synchronous / 320x240 / byte_swapping全 / VSYNC high / burst x1
- camera_app.c: COM7=0x46 / COM10=0x02 / CLKRC=0x80 / 无彩条
- camera_drv.c: CPEIE-only / CAPWR.HWDTH=1280 / CTNCP清除 / DSEL+HDSEL+VDSEL=1 / CMCYR 不覆盖
- camera_app.c 回调: 9 事件计数 + VBP/HD_MISSING 恢复(ceu_recover_from_error)
