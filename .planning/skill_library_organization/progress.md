# Skill 库整理进度

## 2026-08-10

- 已读取桌面 `Skills.md`，列出 `.agents`、`.codex` 和工程内 Skill 分布。
- 初步确定“个人源码统一到 `.agents/skills`，系统 Skill 保持 `.codex/skills/.system`”的方向。
- 首次递归审计输出过大，已调整为精简的逐 Skill 统计。
- 已将 `rasc-configure-ra` 复制到统一个人库 `.agents/skills`。
- 已确认 OpenCode 前四个 Skill 的程序源码与统一库一致；`word-chat` 独有的 31 个输入/历史输出已无损合并。
- OpenCode 的 5 个 Skill 入口已改为目录链接；原副本、工程副本与 `.codex/skills` 副本集中保存到可恢复迁移备份区。
- 桌面 `Skills.md` 已重写为统一目录清单；README 与工程总览已更新路径说明。
- 链接和目录分布验证通过；5 个 Skill 的核心元数据静态校验全部通过。
- 官方 Python 校验器因系统没有实际 Python 3 无法执行，已记录为环境缺口；没有擅自安装依赖。
- RASC Skill 实测可启动审计，准确报告工程当前 MIPI DSI Lane 为 1、Profile 要求 2。
