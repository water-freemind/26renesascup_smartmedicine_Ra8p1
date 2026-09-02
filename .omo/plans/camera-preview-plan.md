# 摄像头 → PC 预览 + MIPI 屏预览 实现计划

## 背景与现状（探索结论）

| 模块 | 状态 | 关键事实 |
|---|---|---|
| 摄像头 OV7725 + CEU | ✅ 驱动已写好，采集链路通 | 640×480 YCbCr422 → `g_camera_frame_buffer`（614400B），`g_frame_ready` 标志，Camera 线程每帧重启采集 |
| USB | ⚠️ **需用户在 RASC 添加** | FSP 6.3.1 pack 含 `r_usb_basic`/`r_usb_pcdc`；**板载口 = USBHS(USB_IP1)**，P407/P408=USB_HS 已配好；PCDC **支持 Hi Speed**（仅禁 Low Speed）；需选 `USB Speed=Hi Speed` + `USB Module Number=USB_IP1`；UCLK=48MHz；实例默认名 `g_basic0`/`g_pcdc` |
| MIPI 屏 GLCDC+DSI+LVGL | ✅ RASC 已配好 | 480×854 RGB565，双帧缓冲 `fb_background[2][...]`；LVGL 实例 **`g_lvgl_port`**（`g_lvgl_port_ctrl`/`g_lvgl_port_cfg` 已在 ra_gen/common_data.c）；`RM_LVGL_PORT_Open()` 内部自动 GLCDC Open+Start+创建 lv_display，**必须先 `lv_init()`**；LV_COLOR_DEPTH=16 |
| 构建 | ✅ GLOB 自动收集 | `src/*.c` 自动进构建，新建源文件无需改 CMake；configuration.xml 变更时构建前自动跑 RASC |

## 格式链路

```
OV7725 (YCbCr422 640×480)
  → CEU DMA → g_camera_frame_buffer [614400B]  (g_frame_ready 标志)
  → ① YCbCr422→RGB565 转换（新模块 camera_convert.c）
  → ② 缩放：PC 预览用 160×120；MIPI 屏用 480×360（保持比例显示在 480×854 上）
  → ③ 出口 A：USB CDC 分包发送 → PC Python 显示
  → ③ 出口 B：LVGL lv_image 填帧 → MIPI 屏显示
```

## 阶段 1：摄像头驱动完善 + PC 预览

### 1.1 新建 `src/app/camera_convert.c/.h`（不依赖 RASC，可立即做）
- `camera_ycbcr422_to_rgb565(src, dst, w, h)`：整帧 YCbCr422→RGB565（整数近似 BT.601）
- `camera_scale_rgb565_nearest(src, sw, sh, dst, dw, dh)`：最近邻缩放
- 单元测试友好：纯函数无外设依赖

### 1.2 新建 `src/app/usb_cdc.c/.h`（依赖 RASC 生成 g_basic0/g_pcdc）
- `usb_cdc_init()`：`R_USB_Open` + `R_USB_PCDC_Open`
- `usb_cdc_send_frame(rgb565, w, h)`：包头（magic+w+h+seq）+ 分包发送（PCDC 最大包 64B/1024B，分包+回调完成同步）
- 帧发送节流：USBHS 高速 480Mbps（实际 ~40MB/s），320×240×2=153.6KB → 理论上百 FPS，实际按 30 FPS 节流即可；带宽远大于 USBFS
- 回调：`usb_pcdc_callback` 处理 WRITE_COMPLETE，用信号量通知

### 1.3 Camera 线程接线（修改 `src/Camera_thread_entry.c`）
- 帧就绪 → 转 RGB565 → 缩放 → （优先）USB 发送 / （屏幕初始化后）填屏

### 1.4 PC 上位机（新建 `tools/camera_viewer.py`）
- pyserial 读包 → 校验 magic/序号 → OpenCV imshow 实时预览 + 按 s 存图

## 阶段 2：MIPI 屏实时预览

### 2.1 LVGL 线程初始化（修改 `src/LVGL_thread_entry.c`）
- `lv_init()` → `RM_LVGL_PORT_Open(&g_lvgl_port_ctrl, &g_lvgl_port_cfg)` → `lv_timer_handler` 循环
- 创建 camera 预览 `lv_image` widget（480×360 位于屏幕中上），data 指向缩放后的 RGB565 缓冲

### 2.2 Camera 线程填屏
- 缩放 480×360 RGB565 → 写入 image buffer → `lv_image_set_src` / `lv_obj_invalidate` 刷新
- 注意线程安全：LVGL 操作集中在 LVGL 线程或加锁；Camera 线程只写缓冲 + 置 dirty 标志，LVGL 线程 `lv_timer_handler` 里检测并刷新

## 依赖与顺序

| 步骤 | 依赖 | 可执行时机 |
|---|---|---|
| 1.1 camera_convert | 无 | ✅ 完成（宿主测试 17/17） |
| 1.2 usb_cdc | RASC 生成 g_basic0/g_pcdc | ✅ 完成（已编译通过） |
| 1.3 Camera 线程接线 | 1.1 + 1.2 | ✅ 完成 |
| 1.4 PC 上位机 | 无（需 pyserial/opencv） | ✅ 完成（tools/camera_viewer.py） |
| 2.1 LVGL 初始化 | RASC 已生成 g_lvgl_port | ✅ 完成 |
| 2.2 填屏 | 1.1 + 2.1 | ✅ 完成 |

## 执行状态（2026-08-05 最终）

**✅ 全部完成：USBHS 配置（用户 RASC）+ 全部代码 + 构建零警告零错误。**

**当前模式：仅 PC 预览**（`CAMERA_PREVIEW_SCREEN_ENABLE=0`，MIPI 屏显禁用）
- 板端: 烧录 `build/Debug/26renesascup_smartmedicine_Ra8p1.elf` → USBHS 口插电脑
- PC 端: `pip install pyserial opencv-python` → `python tools\camera_viewer.py --port COM3`
- 启用屏显: LVGL_thread_entry.c 顶部开关改 1 重新编译

**仅剩待办**: 烧录 + PC 联调验证（用户回来操作）。详细进度/恢复信息见 `.omo/progress/2026-08-05_camera-preview.md`

## 验证标准
- 阶段1：`cmake --build` 零错误零警告（src/ 严格检查）；PC 端 `python tools/camera_viewer.py --port COMx` 看到实时画面
- 阶段2：`cmake --build` 零错误；MIPI 屏显示摄像头画面（当前通过开关禁用，可随时启用）
- 不修改 `configuration.xml` / `ra_gen/`（RASC 归用户操作）；`ra/` 仅创建 descriptor 文件（RASC 缺陷补偿）
