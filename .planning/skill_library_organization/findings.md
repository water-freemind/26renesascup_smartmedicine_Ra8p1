# Skill 库整理结论

## 最终目录策略

- 个人 Skill 唯一可编辑源码库：`C:\Users\Zhanglongsheng\.agents\skills`。
- OpenCode 入口：`C:\Users\Zhanglongsheng\.config\opencode\skills`，其中 5 个目录均为指向统一源码库的 Junction。
- Codex 系统 Skill：`C:\Users\Zhanglongsheng\.codex\skills\.system`，保持原位且未修改。
- `.codex/skills` 不再保存个人 Skill 副本。
- 具体业务工程不再内置个人 Skill，只保存项目 Profile、工具脚本和使用文档。

## 个人 Skill 清单

1. `mspm0-ccs`
2. `planning-with-files-zh`
3. `rasc-configure-ra`
4. `using-superpowers`
5. `word-chat`

所有目录都包含 `SKILL.md`，核心 frontmatter 的 `name` 与目录名一致且 `description` 非空。没有发现嵌套 Git 仓库或真正的密钥文件。

## 无损迁移说明

- OpenCode 的 `mspm0-ccs`、`planning-with-files-zh`、`using-superpowers` 与 `.agents` 原副本逐文件哈希一致。
- `word-chat` 的程序源码一致；差异来自 Inputs、Outputs 和 Python 字节码缓存。
- OpenCode 独有的 31 个 `word-chat` 输入/历史输出已先合并到统一库，没有覆盖同名冲突文件。
- 原副本没有删除，集中保存在：
  `C:\Users\Zhanglongsheng\.agents\migration-backups\2026-08-10-skill-consolidation`。

## 验证结果

- 5 个 OpenCode Junction 的目标均正确。
- `.codex/skills/.system` 仍存在；`.codex/skills/rasc-configure-ra` 活动副本已移出发现目录。
- 工程内 `skills/rasc-configure-ra` 活动副本已移出工程并进入迁移备份。
- 桌面 `C:\Users\Zhanglongsheng\Desktop\Skills.md` 已更新。
- 5 个 Skill 的核心元数据静态检查全部通过。
- 系统只有空的 `py.exe` 启动器，没有实际 Python 3，因此官方 `quick_validate.py` 未能执行；`word-chat` 当前也无法直接运行，后续需安装 Python 3.10+。
- RASC Skill 审计脚本可以运行；当前工程仍报告 MIPI DSI Lane 为 1、项目 Profile 要求 2，这是既有工程配置问题，不属于本轮整理范围。

## GitHub 边界

未来若发布，建议直接把 `.agents/skills` 建成一个独立仓库。必须继续排除 `word-chat` 的 Inputs、Outputs、模板、生成稿和本地隐私配置；Codex `.system` 与迁移备份不得提交。当前没有创建远程仓库或执行推送。
