# RA8P1 屏幕资料、RASC 与驱动审计计划

## 目标

以 `C:\Users\Zhanglongsheng\Desktop\屏幕` 中的面板/转接板资料为依据，完整审计当前 `configuration.xml`、RASC生成代码、ST7701S驱动、LVGL线程和构建接入，输出确定错误、待实物确认项与可执行修复顺序。本轮先只读检查，不改RASC和驱动。

## 阶段

- [已完成] 盘点屏幕资料、格式、版本和可提取内容。
- [已完成] 审计 `configuration.xml` 的GLCDC、MIPI DSI、MIPI PHY、LVGL Port、D/AVE2D和线程配置。
- [已完成] 对照 `ra_gen` 生成结构、时序、Lane、像素格式、中断和引脚。
- [已完成] 审计ST7701S初始化表、DCS发送、回调、复位/背光钩子及LVGL入口。
- [已完成] 运行只读审计与静态构建验证，整理结论并更新工程文档。

## 固定边界

- `configuration.xml` 是RASC配置源，`ra_gen/`是生成结果。
- 未确认资料和配置差异前不修改XML，不运行Generate，不覆盖生成目录。
- 用户当前工作树已有文档拆分变更，必须完整保留。

## 错误记录

| 错误 | 次数 | 处理 |
| --- | ---: | --- |
| 首次调用审计脚本使用了不存在的 `-ConfigurationPath` 参数 | 1 | 阅读脚本参数后改用 `-Path` |
| PowerShell执行策略阻止直接运行Skill脚本 | 1 | 使用 `powershell -ExecutionPolicy Bypass -File` 只读执行 |
| 本机无默认Python/PDF命令行提取器，Edge无头PDF渲染因GPU进程失败 | 1 | 使用厂商TXT、ZIP内README/驱动和已有原理图归档交叉验证；未将PDF视觉渲染作为唯一依据 |
| `cmake` 不在PATH | 1 | 从构建缓存读取并使用Renesas平台CMake绝对路径 |
| 全量ARM编译在91%失败 | 1 | 定位为 `gui_app.c` 引用已删除的 `guider_ui.Copy`，屏幕底层源文件已编译通过 |
