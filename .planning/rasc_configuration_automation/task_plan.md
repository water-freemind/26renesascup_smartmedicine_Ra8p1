# RASC configuration.xml 自动配置研究计划

## 目标

研究当前 RA8P1 工程 `configuration.xml` 的结构、RASC/FSP 生成关系和可验证修改方式，建立由 Codex 安全配置常见外设、重新生成并编译验证的流程，尽量代替人工操作 RASC GUI。

## 阶段

- [x] 解析当前 configuration.xml 的顶层结构、模块、线程、引脚和时钟节点。
- [x] 查找本机 RASC 命令行生成能力和仓库已有生成命令。
- [x] 对照历史变更，识别稳定 ID、属性和值编码规则。
- [x] 建立自动修改的安全边界、差异和校验策略。
- [x] 编写只读检查工具并归档到工程文档。

## 结论

已具备代替大部分 RASC 手工配置的基础。普通参数和已有模块配置可直接精确修改；新增复杂模块必须从 FSP 6.3.0 器件包或同版本 RASC 样例构造完整节点，再通过审计、headless 生成、差异和编译验证。

Skill 的唯一可编辑源码已统一迁移到 `C:\Users\Zhanglongsheng\.agents\skills\rasc-configure-ra`。Codex 直接从个人 Skill 库发现，OpenCode 使用目录链接，不再保留工程内或 `.codex/skills` 活动副本；当前未创建或推送独立 Skill GitHub 仓库。

## Skill 沉淀阶段

- [x] 按 skill-creator 规范确认本地 Skill 仓库和目标目录。
- [x] 初始化 `rasc-configure-ra` Skill 结构和 UI 元数据。
- [x] 迁移通用审计脚本，编写 XML/RASC 工作流与参考资料。
- [x] 运行 Skill 校验和基础功能测试。
- [x] 在工程文档记录 Skill 的本地位置和后续 GitHub 发布边界。

## 约束

- 本轮研究不改变现有外设配置，不直接编辑 `ra_gen/`。
- 后续实际修改必须以 `configuration.xml` 为源，经 RASC 重新生成后再编译。
- 不猜测 pin/function/module ID；必须从当前 XML、器件包或已验证样例取得。
- 保留用户现有 GUI 和其他未提交修改。

## 错误记录

| 错误 | 次数 | 处理 |
| --- | ---: | --- |
| 并行结构/历史/RASC 文件提取整组返回 exit 1，未提供分项输出 | 1 | 改为分开执行，分别检查 XML 路径、文件筛选和 Git 差异 |
| `rg --files D:\RASC` 未返回安装文件 | 1 | 不重复使用该方式；改为 PowerShell 对已知子目录分层枚举 |
| `rascc.exe --help` 30 秒未退出且无输出 | 1 | 不重复直接启动；改查安装帮助、INI、Eclipse application 扩展和工程历史命令 |
| 审计脚本首次运行把 4 个已存在引脚误报为缺失 | 1 | 将 PowerShell XML 属性枚举改为 pincfg 节点内的精确 XPath 查询 |
| 并行探测 Skill Git 仓库时，非 Git 目录使整组返回 exit 1 | 1 | 先检测 `.git`，再分别查询目录，避免用失败退出码判断普通目录 |
| `init_skill.py` 首次运行失败：`python` 不在 PATH | 1 | 确认未产生半成品；查找 `py.exe` 或本机 Python 绝对路径后重试 |
| `quick_validate.py` 用系统 GBK 读取 UTF-8 SKILL.md，遇到弯引号解码失败 | 1 | SKILL.md 改为纯 ASCII；中文仅放 UI 元数据，再运行官方校验 |
| 最终归档补丁因规划文件段落顺序不匹配被拒绝 | 1 | 拆分文档与规划补丁，按实际文件内容分别更新 |
