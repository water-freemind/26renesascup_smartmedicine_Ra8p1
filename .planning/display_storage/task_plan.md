# 屏幕RASC与存储方案任务

## 目标

明确屏幕RASC尚未完成的原因，区分可自动配置项和必须实物确认项；核对RA8P1内部MRAM、板载串行Flash与LVGL资源的使用方式，并形成可实施的驱动方案。

## 阶段

- [x] 核对当前RASC、生成代码、显示构建开关和已知阻塞。
- [x] 核对目标器件内部非易失存储能力与FSP驱动支持。
- [x] 修正项目RASC审计基准并验证。
- [x] 明确内部MRAM、外部串行Flash和microSD的职责边界。
- [x] 更新README、工程总览和工作进度。
- [x] 审计现有屏幕驱动、GUI接口、RASC XML和生成代码差异。
- [x] 最小化修改RASC显示时钟/时序并完成无界面Generate核验。
- [x] 完成ST7701S软件复位、DCS包型、错误状态和横屏接入。
- [x] 修复板端GUI构建阻塞并完成全量ARM编译验证。
- [x] 更新文档并准备提交全部当前工作进度；远程推送在本次提交后执行。

## 当前阶段

屏幕驱动与RASC软件闭环已完成：双审计、Generate和全量ARM链接均通过，正在归档并提交。由于当前没有屏幕硬件，实际点亮、背光自动开启和FPC电气验收保留为唯一实物测试项。

## 安全边界

- `configuration.xml`是RASC配置源，未经生成与构建验证不手改生成目录。
- 不猜测尚未实物确认的屏幕RESET、背光和触摸GPIO。
- 不把普通RA的Flash HP/LP驱动套用到RA8P1；RA8P1使用MRAM驱动。
- 不在固件运行时擦写包含正在执行程序和GUI常量的MRAM区域。
- 不修改已经实测通过的ZDT底层协议和到位应答。

## 遇到的错误

| 错误 | 次数 | 处理 |
| --- | ---: | --- |
| 审计搜索包含不存在的CMakePresets文件，`rg`返回1 | 1 | 已确认实际生成配方位于`cmake/GeneratedSrc.cmake`，后续只检查存在文件 |
| 递归搜索整个`C:\Renesas`查找RASC/GCC超过30秒 | 1 | 已停止，改从CMake缓存、PATH和常见安装目录定向定位 |
| PowerShell默认执行策略阻止RASC只读审计脚本 | 1 | 改用独立PowerShell进程的`-ExecutionPolicy Bypass -File`，不修改系统策略 |
| 当前Skill审计脚本不接受旧参数名`ConfigurationPath` | 1 | 读取当前脚本param定义，改用其实际参数名调用 |
| `apply_patch`写入RASC时钟后未正常退出 | 1 | 终止挂起进程并检查diff，确认三个目标文件均已完整写入且无重复内容 |
| 调用`D:\RASC\eclipse\rasc.exe --generate`返回0但生成值仍为旧时钟 | 1 | 找到正确CLI为`rascc.exe`，重新Generate后产物更新并通过双审计 |
| 按旧目录读取`src/middleware/*/camera_preview.*`失败 | 1 | 使用`rg --files`定位整理后的真实文件路径，不按历史结构猜测 |
| 首次GUI全量构建在10%报`LV_DRAW_TRANSFORM_USE_MATRIX requires LV_USE_MATRIX = 1` | 1 | 按LVGL硬依赖同时启用`CONFIG_LV_USE_MATRIX=1`，增量重编译 |
| 第二次构建报`LV_USE_FLOAT is required for lv_matrix` | 1 | RA8P1工程为hard-float ABI，补充`CONFIG_LV_USE_FLOAT=1`后继续验证 |
| 第三次构建到49%时D/AVE2D对LVGL浮点坐标执行位移而失败 | 1 | 不改vendor源码；在RASC中关闭LVGL的D/AVE2D后端，改用官方软件绘图完成矩阵旋转 |
| 沙箱内RASC无法写入全局Eclipse/FSP元数据并提示无设备包 | 1 | 使用已授权的正式`rascc.exe`环境生成，输出无配置错误 |
| 移除DRW栈后CMake仍递归编译全部DRW源码 | 1 | 在工程CMake中按当前RASC配置排除未启用的DRW/DAVE2D源码 |
| 全量链接缺少旧ZDT坐标变量的存储定义 | 1 | 只在应用层补齐四个状态变量定义，不修改已验证ZDT报文和到位逻辑 |
