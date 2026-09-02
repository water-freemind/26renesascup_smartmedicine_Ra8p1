# 2026-08-10 发布归档发现

- 当前分支为 `main`，远程为 `git@github.com:water-freemind/26renesascup_smartmedicine_Ra8p1.git`。
- 上一提交为 `6852747 更新640x480 GUI工程与屏幕规划`。
- 待归档内容包括：640×480 医疗 GUI、官方板硬件索引、自绘扩展板审查、RASC 审计工具和个人 Skill 统一管理记录。
- `configuration.xml` 当前没有未提交修改；已知 MIPI DSI Lane 仍为 1，而硬件基线要求 2。
- GUI Guider `generated/` 共 65 个源码/资源文件，约 4.5 MB，是模拟器构建所需生成源码，不是构建输出。
- 原 GUI 计划文件位于 `gui/` 根目录，与工程文档管理约定不一致，将统一迁移到 `.planning/lvgl_gui/`。
- GUI Simulator 构建通过；当前 `configuration.xml` 的唯一项目审计失败仍是已知的 MIPI Lane 差异。
