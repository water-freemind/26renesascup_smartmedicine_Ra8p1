# LVGL 模拟界面调研记录

## 2026-08-11 电赛品牌标识接入

- 用户提供的全国大学生电子设计竞赛 PNG 已规范化保存为 `gui/RA8P1/resources/image/icon_nuedc.png`；原始图为 816x495，界面使用 70x42 ARGB8888 LVGL 图像数组 `icon_nuedc_70x42_ARGB8888.c`。
- `.guiguider`、设计重建脚本、`gg_image.h`、`gui_guider.h`、`gg_Home.c` 与 `gg_Admin.c` 均已登记该资源。主页和药师管理台页眉的坐标固定为 x=228、y=7，RENESAS 标识位于其右侧，未发生重叠。
- 电赛标识按用户要求不加入启动页。本轮前后 `gg_Boot.c` SHA-256 均为 `EF06C7536E1D28D98B2A137BDAF78D2BEF2957124979CD66D561D351F3EE319D`，确认未覆盖用户自行调整的封面配色。
- GUI Guider 已运行时的单实例转交会额外产生中文名的大尺寸临时 C 文件，MinGW 无法归档该对象；该无引用副本及 `.tmp/guiguider_nuedc` 缓存、`debug.log` 均已清理。后续请先关闭旧 GUI Guider 工程、重新打开 `.guiguider` 再执行 Generate Code。

## 当前已知

- 目标显示：640×480 横屏。
- GUI Guider 工程：`gui/RA8P1/RA8P1.guiguider`。
- `gui/RA8P1/platform/` 用于桌面模拟器，不作为 RA8P1 硬件接口。
- 当前阶段所有摄像头、二维码、CAN、电机和网络状态都使用模拟数据。

## 工程检查结果

- `.guiguider` 格式版本为 2.0.0，LVGL 版本为 9.4.0，画布已是 640×480、RGB565。
- 工程目标为 Simulator，使用 MinGW、SDL2 和鼠标输入。
- `generated/` 已由 GUI Guider 2.0 按七个医疗页面重新生成；旧 Printer 页面源码、图片数组和未引用资源已经清理。
- 模拟器入口为 `platform/simulator/main.c`，调用 `setup_ui()`、`custom_init()` 和 `lv_timer_handler()`。
- 模拟器 CMake 会递归编译 `generated/*.c` 与 `custom/*.c`，模拟业务逻辑可限制在 `gui/RA8P1/custom/`。
- `platform/lvgl/` 是模拟器使用的 LVGL 9.4，不需要接触 RA8P1 硬件工程。
- 本机同时安装 GUI Guider 1.10.1 与 2.0.0；当前工程必须使用 2.0.0 打开。
- GUI Guider 2.0.0 安装目录为 `D:\GUIguider_2.0.0`。
- 当前界面使用按字号实际字符裁剪的 Source Han Serif SC 简体中文字库。
- Windows 显示缩放可能让 SDL 窗口坐标与物理像素不一致，模拟器入口需要声明 DPI 感知。

## 2026-08-11 迁移基线

- 模拟器入口先调用 `setup_ui(&guider_ui)` 加载 GUI Guider 生成的旧页面，再调用 `custom_init()`。
- `custom_init()` 已停止调用 `medical_ui_init()`；`.guiguider` 生成页面是唯一运行界面来源。
- GUI Guider 不能从手写 LVGL C 代码反向生成设计画布，因此必须直接重建 `.guiguider` 页面节点。
- `RA8P1.guiguider` 是 JSON，顶层包含 `projectSettings`、`lvConf` 和 `UI`；`UI.screen_list` 当前有3个系统Layer和9个旧Printer页面。
- `UI.event_list` 按控件ID保存事件，设计文件中的加载页面事件可生成 `lv_screen_load_anim` 调用。
- 当前工程元数据仍保留 `application.name=Printer`，这是 GUI Guider 安装模板标识，不应为了界面标题改名；用户可见标题由页面控件定义。
- 当前机器只找到这一份 `.guiguider` 项目，没有可直接复制的另一份医疗页面设计文件。
- 项目设置已是 GUI Guider 2.0 / LVGL 9.4 / 640×480 / RGB565，尺寸配置本身无需重建。
- 现有设计文件已提供 `screen/container/button/label/image/slider/arc` 的有效节点样例；医疗页面可主要使用 screen、container、button、label 和 slider，避免依赖未知控件格式。
- 医疗代码的主要视觉规格已提取：56px公共顶部栏、主页4张294×142卡片、扫码页480×360预览区、药品确认604×316卡片、出药476×288卡片、设备状态584×306卡片以及启动页。
- 最终页面为 `Boot`、`Home`、`Pickup`、`Scan`、`Medicine`、`Store`、`Device`，替换旧Printer业务页面并保留3个系统Layer。
- `slider` 与 `arc` 节点格式和生成代码已经从旧模板验证，可分别用于可编辑的出药进度条与环形进度指示。
- GUI Guider 安装目录只有 Electron GUI 入口和 `app.asar`，公开文档只描述界面中的 Generate Code/Ctrl+G，尚未发现官方命令行生成入口。
- 为确保设计可重复构建，已新增受版本控制的 Node.js 设计重建脚本：保留项目设置和系统Layer，只替换业务页面与事件。
- 本机 Node.js 可用，但全局模块只有 Codex/OpenCode 相关包，没有 `asar`；不需要联网安装。
- GUI Guider 的 `app.asar` 是标准未加密 ASAR，约230 MB，头部 JSON 从偏移16开始且可直接解析；可以只读定位内部生成器代码。
- 一次性输出多个 `app.asar` 内部生成器文件导致输出超过上下文限制；后续只提取入口签名或直接采用已验证的 `.guiguider` 节点模型。
- 2026-08-11 GUI Guider 再次生成代码时只更新了时间戳和生成文件头，设计文件仍然是 9 个 Printer 页面，证明“运行医疗模拟器”不会反向更新设计画布。
- ASAR 内已定位完整生成链：`backend/uic/index.js`、`uic_generator.js`、`screen_generater.js`、`event_generater.js` 及IPC处理器；下一步直接读取其调用参数，优先使用内部生成器而不是GUI键盘自动化。
## 2026-08-11 图片资源结论

- 当前医疗 GUI 不依赖旧 Printer 图片资源，可以安全规划清理，但删除必须在用户明确授权后执行。
- GUI Guider 不保证清理已经失去引用的 `generated/assets/images/*.c`，生成后应额外检查陈旧生成物。
- 药品图片长期方案优先采用 microSD 动态资源，GUI Guider 工程只保留占位图和少量通用图标。
- 用户已明确授权清理，`resources/image` 内24张旧 Printer PNG 已全部删除，释放509,053字节；当前目录保留7张实际引用的业务图标。

## 2026-08-11 简体中文界面结论

- GUI Guider 2.0 自带 `SourceHanSerifSC.otf`，可以直接作为可移植的中文字体来源，不需要把完整字体文件复制进仓库。
- 当前 Simulator 配置 `LV_USE_FONT_COMPRESSED=0`，因此中文字体必须生成非压缩格式；当前使用 4 bpp、16 字节 stride，并按字号实际字符裁剪。
- 七个页面当前使用10个字号：14、15、16、18、20、22、26、28、34、52。动态药品名称若出现当前字符集外汉字，需要扩展字库后重新生成。
- `custom_init()` 已停止调用旧英文动态原型，`.guiguider` 生成页面成为唯一运行界面来源。
- Windows Simulator 已分别直达 Home、Pickup、Scan、Medicine、Store、Device 页面截图验证，中文无乱码且未发现明显溢出。

## 2026-08-11 新业务流程与图标

- 用户确认主页固定为四个业务部分：取药、识别药、存药、系统状态。
- 取药流程：扫描取药单，解析药品坐标，控制电机/机械臂取药，最后放到取药处。
- 识别药功能继续保留；存药管理页面同时容纳存药控制和取药详情日志。
- `C:/Users/Zhanglongsheng/Desktop/icons` 有9张透明 PNG，尺寸为200×200（扫码图为205×200），风格适合卡片图标。
- 建议映射：处方发药图标→取药，扫码图标→识别药，入库箱图标→存药，医疗服务箱→系统状态；电机、在线、网络、药片图标用于二级状态。
- 最终资源选择保留取药单、扫码、入库、系统、药瓶、电机和药片7张图标；在线与网络图标因未引用删除。GUI Guider 生成9个不同显示尺寸的图像数组。

## 2026-08-11 品牌图标与页面清理

- 用户指定 `医疗服务.png` 作为启动页和各业务页左上角统一品牌图标，并指定 `在线.png` 用于主页右上角在线状态。
- 资源规范名分别为 `icon_brand.png` 和 `icon_online.png`；原 `icon_system.png` 与品牌图来源相同，已合并为单一源文件，避免重复维护。
- 页面底部“原型界面 / GUI Guider画布 / 模拟数据”等开发说明不属于正式产品界面，应从 `.guiguider` 设计源移除；模拟状态仍在工程文档中保留说明。
- GUI Guider 2.0 内置 `ResourceConverter` 可在本机离线把 PNG 按目标尺寸转换为 LVGL 9 ARGB8888 C 数组；本轮生成30×30、36×36、80×80品牌图和24×24在线图并通过模拟器编译。
- GUI Guider 2.0 本轮从最近工程卡片加载时出现 Electron 主窗口消失，但 `.guiguider` JSON、页面树和17处图片引用均通过独立校验；设计源与生成源码已按同一资源映射同步。

## 2026-08-11 C 模拟器构建报错根因

- GUI Guider 的“生成代码”完整成功，报错实际发生在随后“构建 C 模拟器”阶段。
- `custom/medical_ui.c` 是已停用的旧英文动态页面原型，但模拟器 CMake 会递归编译 `custom/*.c`；该文件仍引用 `lv_font_montserratMedium_16/18/20/28`。
- GUI Guider 每次生成都会按当前设计重写 `generated/assets/fonts/gg_font.h`，其中只有 SourceHanSerifSC 声明，因此旧原型编译时报 Montserrat 标识符未声明。
- 正确修复是删除不再调用的 `medical_ui.c/.h`，让 `.guiguider` 页面保持唯一界面来源；不应继续修改自动生成的 `gg_font.h` 兼容死代码。

## 2026-08-11 取药整单与药师权限设计

- 普通用户主页只公开取药、识别药和药师管理；存药、日志、系统状态属于管理权限，统一由 `Login → Admin` 导航进入。
- `Pickup` 采用上半区摄像头预览与订单摘要、下半区多药品表格；示例整单为3种药、4盒，逐行显示药名、数量、坐标和执行状态。
- 板端取药建议采用 `WAITING_SCAN/SCANNING/ORDER_READY/DISPENSING/COMPLETE/ERROR` 状态机；二维码解析成功后停止采集并把整单转成顺序任务队列。
- 登录页目前仅为 Simulator 流程示意。真实认证必须由应用层校验凭据、维护会话与超时，不得在 LVGL 自动生成源码内保存密码或直接判定权限。
- GUI Guider 打开旧设计时会持有内存缓存；外部重建设计前应先关闭工程，否则缓存可能把旧页面树覆盖回 `.guiguider`。

## 2026-08-11 瑞萨品牌、日志拆分与ESP-01S状态

- 桌面新增素材中，`renesas.png` 为216×48横向品牌图，适合启动页品牌带和主页页眉；`离线.png`、`日志.png`、`密码.png`、`登录.png` 均为200×200透明图标，可分别用于连接状态、日志、密码和药师身份。
- 在线与离线图标必须以相同24×24尺寸生成，运行时才能由 `gui_set_esp01s_online()` 无布局变化地切换。
- 存药和日志共用一页限制了信息密度；新结构将 `Store` 专用于药品、批次、仓位容量、机械臂和安全确认，将 `Logs` 专用于汇总指标和整单记录表。
- 新视觉以瑞萨深蓝为品牌色、医疗蓝为主色，青绿色表示正常、橙色表示操作提醒、红色表示离线/异常；避免在同一页面大量使用不相关的高饱和颜色。
- GUI Guider 本轮重建设计共10个业务页面、305个控件ID，无重复ID。

## 2026-08-11 状态文案与日志乱码复核

- 用户已在 GUI Guider 中自行调整 Boot 封面配色；本轮采用定向文本补丁，没有运行 `rebuild_medical_guider_design.js`，避免覆盖用户视觉修改。
- 修改前后 Boot 设计对象 SHA-256 均为 `cd4f35542a9006632529e461d931c63d19934f7ea456134dd83b3a81c2dd8071`；`gg_Boot.c` SHA-256 均为 `ef06c7536e1d28d98b2a137bdaf78d2bef2957124979cd66d561d351f3ee319d`。
- 日志截图中的“查□详情”来自旧模拟器实例和未同步的裁剪字库；当前设计源、生成源码均为“日志详情”，13 px 字库也包含“日志详情”全部字符。
- 页眉对用户统一显示“系统在线/系统离线”；底层状态来源仍是 ESP-01S 驱动/网络状态机，不把具体模块型号暴露在普通界面文案中。
