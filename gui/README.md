# RA8P1 图形界面

本目录保存 640×480 智慧药箱界面及桌面模拟器工程。当前阶段仅用于界面和交互验证，不连接 RA8P1 摄像头、二维码、CAN、电机、MIPI 屏或 ESP-01S。

## 工具与入口

- GUI Guider：2.0.0
- LVGL：9.4.0（桌面模拟器版本）
- 可编辑设计工程：`RA8P1/RA8P1.guiguider`
- 设计重建脚本：`tools/rebuild_medical_guider_design.js`
- 模拟器入口：`RA8P1/platform/simulator/main.c`
- 自定义回调入口：`RA8P1/custom/custom.c`
- GUI Guider 回调入口：`RA8P1/custom/custom.c`

## 当前页面

GUI Guider 设计文件中已经直接建立以下十个可编辑画布：

1. `Boot`：启动页
2. `Home`：医疗卡片式主页
3. `Pickup`：取药任务页，包含取药单摄像头预览、扫描摘要和多药品数量/坐标/状态清单
4. `Scan`：药品二维码识别页，480×358 预览区加右侧状态栏
5. `Medicine`：药品识别结果页
6. `Login`：管理药师身份认证页
7. `Admin`：认证后的药师管理台
8. `Store`：独立存药管理页，显示药品批次、仓位容量、目标坐标和机械臂状态
9. `Logs`：独立取药日志页，显示任务汇总、整单结果和耗时明细
10. `Device`：设备状态页，第二项显示机械臂实时 X/Y/Z 逻辑坐标

主页公开入口为“取药、识别药、药师管理”。存药、取药日志和系统状态只从药师身份认证后的管理台进入，不再直接展示给普通用户。取药主流程为“打开摄像头 → 扫描整张取药单 → 生成多药品坐标队列 → 机械臂逐项取药 → 送至取药口”；摄像头画面、登录结果、二维码结果、药品坐标、机械臂状态、存药动作和日志目前均为模拟数据。后续接入硬件时，应保留页面层，只替换数据与命令接口。

当前登录页仅用于模拟页面流程，不能作为真实安全认证。板端接入时必须在业务层校验工号/密码或刷卡凭据，维护认证会话和超时退出；LVGL 只负责收集输入和显示结果，不能把固定密码或权限判断写在自动生成页面中。

## GUI Guider 使用说明

必须使用 GUI Guider 2.0 打开 `RA8P1/RA8P1.guiguider`。如果打开时仍显示旧 Printer 页面，请关闭旧工程窗口并重新打开该文件；GUI Guider 不会把手写 LVGL C 代码反向转换成设计画布。

页面结构和基础跳转现在以 `.guiguider` 为权威来源。按 `Ctrl+G` 或点击 **Generate Code** 后，GUI Guider 会重建 `generated/`。生成完成后再编译 Simulator；不要直接维护 `generated/` 中的页面结构。

`RA8P1/resources/image/` 中旧 Printer 模板的24张未引用图片已于2026-08-11删除，共释放509,053字节。当前保留14张实际使用的 PNG，并按显示尺寸生成18个 LVGL 图像数组。`icon_brand.png` 用作医疗品牌图标，`icon_renesas.png` 用于启动页与页眉的瑞萨比赛品牌；`icon_nuedc.png` 仅用于主页和药师管理台页眉，位于 RENESAS 左侧，不改动用户调整过的启动页配色；`icon_online.png`/`icon_offline.png` 用于 ESP-01S 状态切换。页面底部的开发占位文字已经移除。以后新增图标或药品图片时，应从 GUI Guider 的资源管理器导入，不要只把文件复制进目录。

如需从确定的布局基线重新构建设计文件，可在仓库根目录执行：

```powershell
D:\node\node.exe gui\tools\rebuild_medical_guider_design.js
```

该脚本保留工程尺寸、LVGL 配置和系统 Layer，只替换业务页面及页面跳转。当前设计已统一使用简体中文和 GUI Guider 2.0 自带的 `SourceHanSerifSC.otf`；字库只包含各字号实际使用的界面字符，避免打包完整中文字库。

如果 `Ctrl+G` 重新生成后需要同步仓库内的中文页面源码和裁剪字库，可执行：

```powershell
D:\node\node.exe gui\tools\sync_generated_chinese.js
```

脚本调用 GUI Guider 内置的 `lv_font_conv`，生成非压缩、16 字节 stride 的 LVGL 字体；该格式与当前 Simulator 的 `LV_USE_FONT_COMPRESSED=0` 配置一致。动态药品名称若包含当前界面字符集之外的汉字，需要在设计文本/字符集内补充后重新生成字库。

## 当前过渡状态

GUI Guider 2.0 已按新设计生成 `Boot/Home/Pickup/Scan/Medicine/Login/Admin/Store/Logs/Device` 页面源码和事件，Windows Simulator 已完整编译成功。启动、主页、取药、登录、管理台、存药、日志和设备状态等关键页面均已截图验收，中文和右侧边界没有溢出。运行界面直接采用 `.guiguider` 生成页面；已经停用且会干扰 GUI Guider 重新生成字体的旧英文动态原型 `custom/medical_ui.c/.h` 已删除，后续模拟业务逻辑直接写入 `custom.c` 或拆分为不创建第二套页面的业务模块。

2026-08-12设备页第二项已改为“机械臂坐标 / X:Y:Z”。新增文字后已重新运行 `sync_generated_chinese.js`，13个字号的裁剪字库均已重建，解决旧16号字库缺少“坐标”等字形造成的乱码；Simulator随后完整编译通过。以后每次新增中文文本都必须再次同步字库。

## ESP-01S 状态接入

Simulator 默认显示“系统离线”。板端 ESP-01S 驱动或网络业务层确认连接状态变化后，在 LVGL 线程上下文调用：

```c
gui_set_esp01s_online(&guider_ui, connected);
```

接口会保存最新状态，并同步更新主页和药师管理台页眉的在线/离线图标、文字与状态色；即使状态先于页面创建到达，低频 LVGL 刷新也会在懒加载页面创建后自动补应用。LVGL 页面不应自行探测 UART，也不应把收到单条 AT 响应直接视为网络在线；建议由 ESP-01S 状态机综合串口可用、模块就绪和网络连接结果后发布布尔状态。

## 机械臂坐标接入

设备状态页原有的 CAN FD 项已替换为机械臂实时坐标。电机业务层完成电机位置到机械臂逻辑坐标的换算后，应通过线程安全消息把坐标送入 LVGL 线程，再调用：

```c
gui_set_arm_coordinates(&guider_ui, x, y, z);
```

界面显示格式为 `X:0  Y:0  Z:0`。接口参数暂定为有符号整数逻辑坐标，不直接代表电机编码器脉冲；最终单位（例如毫米或仓位索引）应由机械结构标定方案统一确定。该接口只更新显示，不修改 ZDT 电机协议、CAN 报文或到位应答逻辑。

## 独立编译

模拟器使用 GUI Guider 2.0 安装目录自带的 CMake、Ninja 和 MinGW。构建输出位于 `RA8P1/platform/simulator/build/bin/simulator.exe`，构建目录不应提交到 Git。可设置 `MEDICAL_UI_PAGE=Home/Pickup/Scan/Medicine/Login/Admin/Store/Logs/Device` 后启动模拟器，直接检查指定页面。
