# 自绘扩展板审查发现

## 必须修改

- ATX 24pin 网名与标准编号明显不一致，必须按键位、焊盘和实物线束逐针核对。
- 不得用 ATX +12 V/-12 V 作为电机 24 V。
- TJA1042TK/3 裸露 EP 必须接 GND。
- P6 pin34 应为 P805，不是 PB05。
- 外部 PWM 端子必须补 GND。

## 功能和可靠性

- WLK P2 不带 BLC、T_RST、INT、P_RST，应增加 P3 辅助控制口。
- ESP UART 方向正确，但缺外部启动上拉、局部去耦和 GPIO0 下载入口。
- CAN 端子建议增加 GND，分裂终端应可选。
- 电机支路需要保险、反接、浪涌保护和峰值电流核算。

## 已核对

- CAN P402/P704 与当前 RASC 一致。
- TJA1042 VCC=5 V、VIO=3.3 V、STB 下拉方向正确。
- ESP TXD→P500、ESP RXD←P501 方向正确。
- P903/P904/P807/PA07 可做 GPT PWM；P903/P904 共享 GPT11 频率。
- 摄像头 P8 与当前 OV7725 一致；模块侧 20pin 待核。
- MIPI Lane0/Lane1/Clock/I2C 逻辑映射基本正确；FPC 方向和 PCB 质量待核。

## 待确认

- `CAM_HSYNC` 改为 `CAM_HREF`；`OV5462` 疑似应为 `OV5642`。
- EDA 数据未能直接读取，本轮未修改 EDA 文件。
- 连接器方向、封装号、MIPI 阻抗、ESP 天线净空和大电流能力不能由截图确认。
