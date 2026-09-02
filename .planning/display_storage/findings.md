# 屏幕RASC与存储发现

## 2026-08-12

- 当前器件为`R7KA8P1KFLCAC`，FSP 6.3.0，代码非易失存储为1 MiB Code MRAM。
- 生成文件明确显示`BSP_DATA_FLASH_SIZE_BYTES=0`，该器件没有普通Data Flash。
- 本机FSP定义RA8P1不支持Flash HP/LP，但支持MRAM；正确运行时擦写驱动是`r_mram`。
- GUI字体、图标和页面常量若声明为`const`，由链接器直接放入Code MRAM，不需要运行时Flash驱动。
- `r_mram`支持擦写Code MRAM，但操作期间不能访问Code MRAM；相关代码需在RAM运行，通常还要禁用中断。它不适合作为本项目频繁更新日志/库存的首选存储。
- 板上候选W25Q256/AT25SF256约32 MiB串行Flash更适合GUI大资源、药品图片和持久数据；实际焊接型号需按BOM或实物确认。
- 当前项目RASC审计Profile仍错误要求MIPI 2 Lane；屏幕资料与生成配置均确认应为1 Lane。
- 当前已知RASC显示结构完整，但像素时钟、最终porch、横屏旋转仍需闭环修正。
- 当前只接三组排线时，可先用电源/共地、MIPI Clock±和Lane0±组成最简显示链路；Lane1即使物理带出也保持软件禁用，LCD_TE在Video Mode下不要求。
- ST7701S显示初始化命令经MIPI DSI DCS发送，SDA/SCL保留现有连接，但其在控制板或触摸中的具体用途待实物确认。
- 硬件RESET不是首次点屏的绝对前置条件；驱动应先发送DCS Software Reset `0x01`并等待约120 ms，再执行初始化、Sleep Out和Display On。
- 背光线能否省略取决于控制板是否自动/固定开启背光。上电全黑时再排查BLC/LCD_BL；有背光无图像时检查DSI、时钟、FPC和初始化；有图像无触摸不算显示故障。
- TP_INT/TP_RST仅在实际屏幕带触摸并需要触控时接入，触摸与LCD显示分开验收。
- 代码审计确认当前生成值仍为LCDCLK 240 MHz `/1`、GLCDC `/8`，得到30 MHz像素时钟；目标首测值为LCDCLK `/2`、GLCDC `/5`，得到24 MHz。
- `st7701s_send()`当前把0参数、1参数和多参数DCS命令全部编码为`MIPI_CMD_ID_DCS_LONG_WRITE`，需依据长度选择DCS Short Write 0/1 Param或Long Write。
- 初始化序列当前没有DCS `0x01` Software Reset；在三组排线不含硬件RESET时这是明确缺项。
- `mipi_dsi0_callback()`忽略初始化返回值和非POST_OPEN错误事件，无法可靠诊断面板初始化失败。
- `src/app/src/gui_app.c`仍引用GUI设计中已删除的`guider_ui.Copy`页面，是完整板端GUI构建的已知阻塞。
- `CAMERA_PREVIEW_SCREEN_ENABLE`在源码默认值为0；即使链接GUI，LVGL线程也不会打开显示，需由统一构建选项控制并在GUI固件中启用。
- FSP `rm_lvgl_port`使用RGB565直接双帧缓冲；仅调用`lv_display_set_rotation()`不足以旋转物理像素，必须同时启用`LV_DRAW_TRANSFORM_USE_MATRIX`并调用`lv_display_set_matrix_rotation()`，这样保留GLCDC 480×640原生帧缓冲，LVGL逻辑画布为640×480。
- GUI Guider已有Pickup摄像头框和Scan预览容器；预览图像可在`gui_app.c`运行时作为底层子对象创建，无需手改自动生成页面源码。
- FSP 6.3的LVGL D/AVE2D后端把`lv_value_precise_t`直接传给`D2_FIX4`位移宏；启用矩阵所需的`LV_USE_FLOAT`后该类型变成float，供应商后端无法编译。当前采用LVGL软件绘图后端完成矩阵旋转，不修改vendor源码；RASC同步移除DRW子栈，后续升级FSP或自定义旋转路径时再评估恢复。
- 最终GLCDC H/V总周期为550/684，24 MHz下理论约63.8 FPS；FSP生成的DSI active/sync/back/front字段为H `480/10/20/30`、V `640/4/16/20`，实物首测需检查边缘完整性。
- 完整GUI当前Flash只读内容约891.5 KiB，仍在1 MiB Code MRAM内，不需要为现有固定资源立即接入内部MRAM写驱动；药品图片和可更新数据仍应使用microSD/OSPI。
