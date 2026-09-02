# 屏幕RASC与存储进度

## 2026-08-12

- 已核对当前显示配置、生成参数、构建开关和屏幕驱动。
- 已确认当前默认固件关闭板端GUI，完整GUI仍被旧Copy页面绑定阻塞。
- 已确认RA8P1使用Code MRAM且无Data Flash，不能使用`r_flash_hp/lp`方案。
- 已将RASC项目审计Profile和项目审计脚本中的MIPI Lane基准统一由错误的2改为厂商要求的1，准备复跑审计。
- RASC项目审计已复跑通过：目标/FSP、活动Pin配置、线程优先级、MIPI 1 Lane和显示尺寸均符合当前基线。
- 已核对RA8P1 FSP 6.3时钟模型，确认保持PLL2R 240 MHz、LCDCLK /2、GLCDC /5可得到24 MHz首测像素时钟，并保持现有USB 60 MHz来源不变。
- 已将内部Code MRAM、外部OSPI Flash和microSD的职责边界，以及后续驱动接入前置条件同步到README、工程总览和硬件总览。
- 已把三组排线的最简点屏条件、1 Lane约束、DCS软件复位流程、自动背光判断方法和触摸独立验收规则写入硬件总览，并同步修正工程总览与README中的屏幕任务策略。
- 用户授权继续完成屏幕驱动与RASC配置，并在构建验证后提交、同步远程；已将原计划扩展为完整闭环任务。
- 已完成第一轮代码/RASC审计，锁定30 MHz像素时钟、DCS包型、缺少软件复位、回调错误不可见、GUI旧Copy页面引用和屏显默认关闭六项问题。
- 已最小化修改RASC源配置：LCDCLK由`/1`改为`/2`、GLCDC由`/8`改为`/5`，目标像素时钟24 MHz；porch与1 Lane保持不变，并把新时钟约束加入项目审计。
- XML解析、通用审计和项目审计均通过；第一次用GUI入口`rasc.exe`执行Generate虽返回0，但生成文件未更新，已按产物核验规则判为失败并继续定位正确CLI入口。
- 已使用`D:\RASC\eclipse\rascc.exe`和GCC 13.3.1完成无界面Generate；生成结果为LCDCLK `/2`、GLCDC `/5`、MIPI 1 Lane、`video_mode_delay=111`，USB60CLK不变，双重RASC审计再次通过。
- 已完成屏幕软件链路修改：ST7701S加入DCS `0x01`软件复位和按参数数量选择短/长包，MIPI回调保存事件/状态并暴露致命错误；LVGL加入480×640物理帧缓冲到640×480逻辑画布的矩阵旋转；GUI应用层移除旧Copy页面引用，动态接入Pickup/Scan预览，并只在两个页面请求摄像头采集；GUI固件默认开关改为ON。
- 首次GUI全量构建在10%确认LVGL矩阵旋转还要求`LV_USE_MATRIX=1`；已补齐该编译依赖，准备增量构建。
- 已在RASC中关闭LVGL D/AVE2D绘制后端并移除DRW子栈，将一致性规则加入审计；重新Generate无配置错误，双审计通过。
- 已在CMake排除未配置的DRW/DAVE2D源码，避免生成器递归收集未启用驱动。
- 已在旧ZDT应用层补齐四个坐标状态变量的唯一存储定义，未修改协议指令、拆包发送或到位应答。
- 完整GUI、ST7701S、MIPI/GLCDC、摄像头、quirc与机械臂接口已全量ARM编译链接成功，并生成S-record。
- ELF统计：Flash只读内容约891.5 KiB，未超过1 MiB Code MRAM；SDRAM no-init约1.17 MiB，片内零初始化区约1.42 MiB。
- README、工程总览和硬件总览已同步为当前屏幕软件闭环状态，实物点屏仍待FPC到位。
