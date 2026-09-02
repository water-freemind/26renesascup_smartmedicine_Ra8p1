# 本项目工作流

## 审计

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\rasc_config_audit.ps1 `
  -Path .\configuration.xml -Profile .\tools\rasc_config_profile.json
```

## 无界面生成

```powershell
& 'D:\RASC\eclipse\rascc.exe' -nosplash --launcher.suppressErrors --generate `
  --devicefamily ra --compiler GCC --toolchainversion 13.3.1 `
  --buildconfiguration Debug (Resolve-Path .\configuration.xml)
```

`rascc.exe` 成功后应检查 `ra_gen/common_data.c` 中对应配置是否真的更新。不要只看进程退出码。

## MIPI 屏幕不变量

- 屏幕：WLK2802MIPI-15P V2 / W280BF036I，ST7701S，480×640，RGB888，**1 条数据 Lane**（Clock + DL0）。
- 供应商时序：HFP/HS/HBP = 30/10/30，VFP/VS/VBP = 20/4/20，Burst，连续时钟。
- 一条 Lane 的 PHY 速率必须满足 RGB888 视频带宽；项目当前采用 PLL 1100 MHz（约 550 Mb/s 线速）。
- 面板初始化必须发送 DCS 软件复位 `0x01`，并以 DCS 包发送完整供应商初始化表。

