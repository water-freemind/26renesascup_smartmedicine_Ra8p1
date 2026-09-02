# 原理图检查发现

本文件保存逐页提取的中间结论；最终确认内容合并到工程总览。

## 文件基线

- 主板：`cpkhmi_ra8p1_v1.1_sch_release.pdf`，2,286,228 bytes，文件名标示硬件版本 v1.1。
- 扩展板：`瑞萨CPKHMI-RA8P1扩展板_原理图.pdf`，323,578 bytes。
- Windows.Data.Pdf 读取页数：主板 13 页，扩展板 2 页，共需检查 15 页。
- 主板PDF SHA-256：`6FED18FDFABBC00E1B8D98983CD6AE8CD9829543104601065714346092AB12C3`。
- 扩展板PDF SHA-256：`E269970677F32990144B91933093A0280619D3C10A12D91B05EB71DB8BAAF9B2`。

## 主板第 1～5 页

- 图纸标题为 `CPKHMI-RA8P1核心板`，Rev V1.1，日期 2026-01-07；图纸目录标示前 8 页功能，PDF 后续还有附加页。
- 第1页 MCU电源：RA8P1 多组 VCC/VCC2/VCL/VLO、模拟参考和 USBHS/MIPI 专用电源均有独立去耦；VBATT 预留 J100 电池接口。核心板可通过阻容装配选择 RA8P1 与 RA8T2 系列，当前原理图默认按 RA8P1/D2 连接。MIPI DSI 差分信号为 P205/P203、P313/P204、P202/P314 等网络，专用 VCC18_MIPI/AVCC_MIPI/VSS_MIPI 通过选配电阻连接。
- 第2页 MCU I/O：主 MCU 引脚完整展开；用户键为 P303，MD 键为 P201，复位键连接 RESET，板载绿色 LED 为 P110。主晶振 Y201=24 MHz，低速晶振 Y202=32.768 kHz；图纸明确使用 USB HS 时主晶振只能选 12/20/24/48 MHz。SWD/J-Link OB 调试桥使用 P208～P211 等信号。J201 引出 VCC、P400、P401、P402、PC15、GND；J202 引出 VCC、P514、P715、P714、P515、GND。
- 第3页 J-Link OB：板载调试器 MCU 为 R7FA4M2AD3CNF（RA4M2），使用独立 12 MHz 晶振和 Type-C 调试 USB；与 RA8P1 通过 SWD/TDI/TDO/RESET 等信号连接。
- 第4页 USB HS：独立 Type-C `JUSB`，USBHS D+/D− 到 RA8P1 P814/P815；有 SR05-N ESD、VBUS TVS/电容、LPW5209AB5F VBUS 负载开关。FUSB303BTMX Type-C 控制器使用 P409/P410/P108 等控制/中断网络；部分 0R/NC 电阻用于在调试 USB 与 USBHS 网络之间选择。
- 第5页 SDRAM：器件 U2=`W9812G2KB`，3.3 V、32-bit DQ0～DQ31，总线包含 A0～A11、BA0/BA1、CLK/CKE/CS/RAS/CAS/WE 和 DQM0～3；图纸要求 SDRAM 走线控制阻抗 50Ω。按该器件常见组织推断容量约16MB，但最终文档应将容量标注为“依据料号，待BOM/实物确认”。

## 主板第 6～9 页

- 第6页 STORAGE MEMORY：核心板本身已经带 microSD/TF 卡座 `JTF`，不是必须再加外置 SD 模块。4-bit SDHI 映射为 PD01=SD0DAT2、P111=SD0DAT3、PD04=SD0CMD、PD05=SD0CLK、PD03=SD0DAT0、PD02=SD0DAT1、PD07=SD0CD。卡座各线有10k上拉和10µF+0.1µF去耦。
- SD卡接口经过 `TXB0108RGYR` 电平转换，卡侧固定 VSYS_3V3；MCU侧 VCC 可按核心板装配选择1.8V/3.3V。`SN74LVC1G04DSFR` 使用卡检测信号控制 TXB_OE。
- 第6页 MIPI屏接口 `J601` 为18针FPC：MIPI DL1=P314/P313、CL=P202/P203、DL0=P204/P205；触控/控制信号包含 P410=I2C1_SCL、P409=I2C1_SDA、P411=LCD_TE。LCD_BL、TP_INT、TP_RST 通过0R/NC电阻可在多个GPIO候选间选择，不能只凭网名假定实际装配连接。
- 第7页板载存储与电源：1.8V由 `ISL80505IRAJZ-T` 产生；外部串行Flash预留两种封装，料号标示 `W25Q256JVEIQ` 或 `AT25SF2561C-MWUB`，接口引脚为 P107=CE、P808=SCK、P100/P803/P103/P101=数据/控制，按256Mbit料号推断容量32MB，需实物/BOM确认。
- 5V到3.3V使用 `ISL80102IRAJZ`，受 `PMIC_EN` 控制；USB电源保护/限流使用 `AP22615AWU-7`，受 `USBPWR_EN` 控制。板上存在VCC_5V、VIN_5V、VSYS_3V3、VCC_1V8等电源域。
- 第8页为3个80pin BTB座完整映射：J1/J2/J3把大部分MCU I/O、电源、MIPI、USBHS和调试信号送往底板/扩展板。J3有等长走线提示，相关高速/并行总线不能随意飞线。
- 第9页版本历史：V1.0（2025-08-28，量产版本）；V1.1（2025-09-17，修正VCC_USB连接）。图纸标题栏导出日期为2026-01-07。

## 主板附加页与扩展板

- 主板第10页是目录；第11、12页是核心板正反面装配/丝印定位图；第13页是电源路径示意，明确 JUSB、JDBG、VCC_5V/VIN_5V、3.3V、1.8V、MIPI/USBHS专用电源与BTB供电脚关系。
- 主板布局可见两个Type-C口（JDBG调试、JUSB USBHS）、JTF卡座、J601 18pin MIPI FPC、J201/J202扩展排针、用户键/MD键/RESET、LED和J100 VBATT接口。
- 扩展板图纸标题为“瑞萨CPKHMI-RA8P1扩展板”，Revision V1.0.1.0，日期2026-04-08，共2页。第1页是3个80pin核心板连接器P1/P2/P3映射；第2页是功能排针和USB/限流电路。
- 扩展板 P5 `ADC DAC`：奇数侧 P000/P001/P002/P003/P004/P005/P014，偶数侧 P006/P007/P008/P009/P010/P011/P015；另有 AVCC0/AVSS0。
- 扩展板 P4 `CAN I2C MIC 按键`：1/2=P402/P704，3/4=P514/P515，5/6=P500/P810，7/8=P501/P811，9/10=P502/P812，11/12=P206/P902，13/14=P306/P910，15/16=P309/P913。
- 扩展板 P8 `摄像头`：1/2=P410/P708，3/4=P409/P415，5/6=P400/P709，7/8=P405/P401，9/10=P700/P406，11/12=P702/P701，13/14=P414/P703，15/16=P710/P109。与本工程 OV7725 的 I2C、VSYNC/HREF、D0～D7、PCLK、PWDN/RST、XCLK 映射一致。
- 扩展板 P7 `PWM POE`：1/2=P105/P914，3/4=P104/P600，5/6=PD06/P304，7/8=P102/P106，9/10=P712/PB07，11/12=P403/P711，13/14=P912/P903，15/16=P404/P904，17/18=P911/P807，19/20=P915/PA07。
- 扩展板 P6 `Serial Port`：1/2=3V3；3/4=P601/P715；5/6=P602/P714；7/8=P603/P713；9/10=P604/PB04；11/12=P605/PB02；13/14=PB00/PB03；15/16=P706/PB06；17/18=P707/PB05；19/20=PB01/P907；21/22=P705/P909；23/24=P802/P908；25/26=P801/P307；27/28=P804/P906；29/30=P800/P513；31/32=P311/P806；33/34=P905/P805；35/36=P310/P511；37/38=P312/P512；39=P308；40未连接。
- 扩展板 P9 为6个3V3脚，P10为8个GND脚。P14为USB Type-C，使用 USB_P/USB_N 与 USB_5V；U1=`AP22615AWU-7` 对 USB_5V 到 VIN_5V 做限流保护。
- 扩展板P4/P7/P8等标题只是功能建议，是否真为CAN/UART/PWM/PDM必须以RA8P1复用表和RASC当前配置为准；同一端口不能同时承担冲突外设。

## 与当前工程的交叉核对

- `ra_gen/pin_data.c` 已将 P111、PD01、PD02、PD03、PD04、PD05、PD07 配置为 `IOPORT_PERIPHERAL_SDHI_MMC`，与核心板第6页的SDHI接线完全一致。
- 当前 `ra_gen/`、`ra_cfg/` 和应用源码中未发现 `g_sdmmc`/`r_sdhi`/FatFS 文件系统实例，因此现状是“引脚复用已生成，SDHI驱动和文件系统尚未接入”。
- 扩展板P8摄像头排针与现有OV7725文档和RASC配置一致，不需要重新定义摄像头接线。
- P402/P704 同时在扩展板P4作为CAN候选，符合当前 `g_canfd0` 的 RX/TX 实际配置。
- 核心板USBHS使用P814/P815并有独立JUSB Type-C，与现有USBHS工程配置一致。
- MIPI DSI差分引脚与现有RASC配置方向一致；J601上的背光、触控中断/复位存在装配选项，硬件联调前应根据实物0R电阻确认实际GPIO。
