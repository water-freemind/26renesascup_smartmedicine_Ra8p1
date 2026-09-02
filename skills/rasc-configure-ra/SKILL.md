---
name: rasc-configure-ra
description: 安全审查、修改、生成和验证 Renesas RA Smart Configurator/FSP 的 configuration.xml。用于配置 RASC 模块、时钟、引脚、RTOS 线程、中断、ELC 或诊断生成内容与 XML 不一致的问题。
---

# RA Smart Configurator 配置

将 `configuration.xml` 视为唯一配置源；`ra_gen/`、`ra_cfg/` 和生成的 CMake 文件均为输出，不得手改。

1. 先读取 `configuration.xml`、项目的 `tools/rasc_config_profile.json`、`docs/` 中对应硬件说明，并执行项目审计脚本。
2. 对已有属性只修改唯一匹配的 `property[@id]`，同时确认旧值；新增模块、引脚或中断时必须从同版本 FSP 元数据或受控 GUI 差异取得完整结构，禁止猜测 ID。
3. 修改后解析 XML，运行 `tools/rasc_config_audit.ps1`，并使用 `rascc.exe` 生成项目内容。`rasc.exe` 是 GUI 启动器，不适合无人值守生成。
4. 审查 `configuration.xml`、`ra_gen/`、`ra_cfg/` 的差异，确认没有目标芯片、FSP 版本、活动 pin 配置或线程栈意外漂移。
5. 编译完整工程；有硬件时烧录并读取受影响外设状态。记录属性 ID、生成命令、构建结果和未验证项到 `docs/`。

项目特定的命令和显示屏注意事项见 [references/project-workflow.md](references/project-workflow.md)。

