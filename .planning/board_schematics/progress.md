# 原理图归档进度

## 2026-08-10

- 已检查核心板13页、扩展板2页，共15页原理图。
- 已记录两份PDF的版本、页数、路径和SHA-256。
- 已归档核心板电源树、时钟、J-Link、USBHS、SDRAM、串行Flash、microSD、MIPI和用户IO。
- 已归档扩展板P4、P5、P6、P7、P8、P9、P10、P14接口。
- 已交叉核对SDHI、OV7725、CAN、USBHS和MIPI的当前工程状态。
- 已将详细硬件知识写入 `docs/RA8P1_工程总览.md` 第20节。
- 已补充P8摄像头1～16脚逐针定义，避免重新查图确认D0～D7顺序。
- 已增加只需实物/BOM确认的未决项清单。
- 已在README增加板卡版本、存储、USBHS、MIPI、摄像头、CAN和SDHI摘要。
- 已明确固定规则：总览是详细事实来源，README是精简入口，两者必须同步。
- 已执行 `git diff --check`，无Markdown空白错误；README到总览第20节的相对链接和标题锚点已核对。
- Git仅报告仓库现有LF/CRLF行尾转换提示，不影响文档内容。
- 已在工程总览第12节和README明确Codex进度保存约定：`.planning/<任务名>/`保存任务过程，`.omo/`保留OpenCode历史，总览保存长期权威结论。
