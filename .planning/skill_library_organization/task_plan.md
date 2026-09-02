# 个人 Skill 库统一整理计划

## 目标

将用户维护的 Skill 统一到一个明确的本地源码库，区分个人 Skill、Codex 系统 Skill 和工具发现入口，消除 `rasc-configure-ra` 等重复实体，并更新桌面 `Skills.md` 总结。

## 方案基线

- 个人 Skill 唯一源码库：`C:\Users\Zhanglongsheng\.agents\skills`。
- Codex 系统 Skill：`C:\Users\Zhanglongsheng\.codex\skills\.system`，保持原位，不纳入个人仓库。
- Codex/OpenCode 若需要额外路径，只作为链接或同步入口，不保留第二份可编辑源码。

## 阶段

- [已完成] 审计既有个人 Skill、RASC Skill 副本、OpenCode 目录及嵌套 Git/敏感文件。
- [已完成] 确认统一目录结构和 GitHub 发布边界。
- [已完成] 将 RASC Skill 合并到个人库，并将活动重复实体移出发现路径。
- [已完成] 更新 `Skills.md`、工程总览和 README 中的路径说明。
- [已完成] 校验所有 Skill 核心元数据、发现路径和仓库状态。

## 安全约束

- 不移动或修改 Codex `.system` Skills。
- 迁移前比较副本哈希，不覆盖较新的内容。
- 不删除用户 Skill 数据、模板或依赖；先识别嵌套仓库和大文件。
- 不创建 GitHub 远程或推送，除非用户明确授权具体仓库。

## 错误记录

| 错误 | 次数 | 处理 |
| --- | ---: | --- |
| 递归审计输出过大并被截断 | 1 | 改为逐 Skill 输出文件数、大小和风险计数，不再列出全部文件 |
| 当前终端没有 `python` 命令，结构校验未启动 | 1 | 已找到 `py.exe` 启动器，继续核查实际解释器 |
| `py.exe` 存在但系统未安装任何 Python 3 | 1 | 不安装新依赖；改用 PowerShell 静态检查 Skill 元数据，并记录 word-chat 环境缺口 |
| RASC 项目审计报告 MIPI Lane 为 1、基线要求 2 | 1 | 属于既有工程配置问题，不在本轮 Skill 整理中修改 XML |
| 最终只读检查误用当前 PowerShell 不支持的 `||` | 1 | 拆分为独立命令并行执行，不再使用该语法 |
