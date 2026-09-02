# EAN-13 条码识别优化 — 工作记录

> 工程：RA8P1 智能药品工作站（Cortex-M85 / FSP 6.3.0 / FreeRTOS / LVGL 9.3）
> 主题：提高**纸盒（实物印刷）条码**的识别率（手机屏条码已能识别）
> 最后更新：2026-08-19

---

## 1. 当前结论（重要）

- ✅ **手机屏条码可以扫到**（模块大 5–10px + 自发光、对比度高）。
- ❌ **纸盒印刷条码目前识别率不足**（模块小 1.5–2.6px + 反射光对比度低 + 反光/模糊 + 印刷图案干扰）。
- 用户已确认：**黑白画面足够扫条码**，颜色对解码无益，灰度即可。
- 用户决定：**"暂时先这样"**，本阶段工作暂停，本文档保存进度，后续可据此恢复。

---

## 2. 阶段目标

1. 让 EAN-13 解码器在"纸盒场景"（暗背景、弱对比、模糊、反光、两侧印刷图案、条码偏小）下能识别。
2. 保持手机屏场景、原测试集不回归。
3. 不引入误报（无条码画面必须 0 误报）。

---

## 3. 纸盒识别失败的根因分析

基于实机抓帧 + 模拟帧（`tools/gen_box_scene.py`）逐帧分析得出 6 个根因：

| # | 根因 | 现象 | 对策 |
|---|------|------|------|
| 1 | **整行 Otsu 被背景主导** | 暗背景 90 占画面 70% 时，整行阈值 thr=199 贴白底，条码区分割临界 | 候选段检测 + **段内局部 Otsu** |
| 2 | **起始 guard 白 run 与 L/G 字符起始白合并** | 3px 模块实测 白3+白6=白9，`[3,9,3]` 被 2.5 倍容差拒绝 | `start_guard_ok`：白 run 允许 0.4–5 倍黑 run |
| 3 | **条码左侧深色背景与起始 guard 黑条合并** | 形成 227px 超长黑 run，guard 检测跳过、解码从错误位置开始（right5 overflow / parity fail） | 候选段**段外置白（静区）** |
| 4 | **结束 guard 第 3 个黑 run 被右侧深色背景吞并** | 超长黑段导致 end shape 失败 | 结束 guard **只检查前两个 run** |
| 5 | **两侧印刷图案被并进候选段** | 图案（90/140 弱过渡 50）被段内四峰 Otsu 分成黑白交替，干扰解码 | 显著过渡阈值 24→48 + 段内**多阈值 ×1.2**（图案整体归一侧） |
| 6 | **lowcontrast 测试用例建模失真** | 无白静区且黑条 100/背景 90（对比 10，物理不可分） | 测试用例加 qz=40px 白静区（印刷标准要求静区 ≥11 模块） |

> 关键事实：**真实纸盒条码两侧必有白静区**（≥11 模块，印刷标准），深色块只在静区外 —— 段外置白（静区）策略据此成立。

---

## 4. 解码器改动明细（`src/middleware/src/barcode_1d.c`，650 行）

已编译 + 已烧录（788480 bytes 下载+验证成功）。

### 4.1 新增/重构函数

- **`median5()`**：5 值中值排序网络（替代原 3 行中值），竖直方向抗噪更强。
- **`otsu_threshold(hist, lo, hi, total)`**：从原内联 Otsu 提取为函数；取"最后一个达到最大类间方差的 t"；uint64 防溢出；mean_b/mean_f 差值取绝对值。
- **`start_guard_ok(a,b,c)`**：起始 guard 专用判定——黑1:黑1 ≈1:1（±2.5 倍）+ 白 run ∈ [0.4, 5]×黑（因 guard 白与 L/G 字符起始白合并）。**已删除原 `guard_ratio_ok`**（不再使用）。
- **`find_barcode_segment(gray_row, w, &s, &e)`**：灰度显著过渡（|diff|>48，原 24）定位条码密集区；间距 ≤16px 连续、段长 ≥40px。
  - uint32 回绕语义注意：`last_trans` 初值 0xFFFFFFFF，`(x-last_trans)<=16U` 在 C 中 x=0 时结果为 1 会误判 cur_s=0，但后续 x≥16 即断开，实际结果正确（实测 seg=(225,431)/(180,475)）。

### 4.2 `ean13_decode_binary_line` 门槛放宽

- 起始 guard total 下限 6 → **5**（支持更小模块）。
- 中间 guard：5-run total ≥5、比例 ∈ [0.4, 2.5]（`mid[i]*5 >= avg*2 && mid[i]*2 <= avg*5`）。
- 结束 guard：只检查 a:b（total 4–60，比例 ±2.5），第 3 个黑 run 允许被背景吞并。
- 误差预算 err_budget：`7*2` → `7*3*4*12*mod_est`（随估计模块宽度缩放，允许更多像素级误差）。

### 4.3 `ean13_decode_frame` 流程

1. 竖直 **5 行中值**（y-2..y+2）→ med。
2. 整行 Otsu → bin（保底）。
3. `find_barcode_segment` → 段内 Otsu thr1 → **3 档阈值**（thr1、thr1×85/100、thr1×120/100）：
   - 每档先 `memset(bin1, 0, w)` **段外置白（静区）**，段内按该档阈值重判；
   - 每档**正反双向**解码；
   - 任一档成功即返回。
4. 全部失败 → 回退整行 bin 正反双向。
5. 栈开销：med/bin/bin1/rev 各 1024B。

---

## 5. 测试工具链（`tools/`）

| 工具 | 用途 |
|------|------|
| `gen_ean13_test.py` | EAN-13 PNG 生成器（含 blur/mirror/noise/tilt/thin/BIG/PRINT 系列） |
| `gen_box_scene.py` | **纸盒场景 raw 帧生成器**（640×480）：qz=40px 白静区，side_pat 移到静区外；场景 small2px / small1p5 / small2px_bright / lowcontrast / blur2px / side2px / side1p5 |
| `ean13_host_main.c` | host 解码入口，argv: `<w> <h> <input.raw>`（**文件输入**，规避 stdin ~144KB 截断；%zu 改 %lu 消除 MinGW 警告） |
| `test_ean13_host.py` | 回归测试，期望值从文件名数字段提取 |
| `dump_qr_frame.py` | 实机抓帧 + 诊断计数 + 输出 PNG/raw/hist |
| `dbg_ean13_frame.py` / `dbg_new_frame.py` | Python 复刻解码器逐步调试（已同步 start_guard_ok、中间 guard 新容差、结束 guard 只查前两个 run；用 `& 0xFFFFFFFF` 模拟 C 的 uint32 回绕语义） |

---

## 6. 验证结果

### 6.1 Host 回归（改后）

- 原测试集：**17/19**（2 个 FAIL 为 PRINT 系列宽 >1024px 被 `ean13_decode_frame` 的 w>1024 拒绝；**固件 VGA 640 不受影响**，属测试集问题非解码器问题）。
- 纸盒模拟场景：small2px / lowcontrast / blur2px / side2px / small2px_bright **全过**（改前全 FAIL）。
- 无条码帧：**0 误报**。

### 6.2 实机验证（烧录后）

- 抓帧两轮：decode_count=159/206，ean13 try=159/207，**ok=0**。
- 帧分析：画面顶部 y=10–90 有大物体渐变+文字；中部 y=260–320 亮块+反光；**均无条码竖条结构**（高过渡仅 37–42 次，真条码 120+）。
- 结论：**条码不在画面 / 被遮挡 / 太小**，而非解码器失败——已指导用户重新摆放（条码占画面宽度 30–60%，平放正对、防反光）。

---

## 7. 解码器当前能力边界

| 维度 | 支持 | 不支持 |
|------|------|--------|
| 模块宽度 | ≥ ~1.7px（guard total≥5） | 1px（95px 宽条码）——物理极限 |
| 对比度 | ≥ ~70 | 更低（物理不可分） |
| 其他 | 模糊 / 镜像（HFLIP）/ 两侧印刷图案 / 暗背景（有白静区时） | — |

---

## 8. 构建与烧录（当前基线）

```powershell
# 构建（生成 .srec）
C:\MinGW\bin\mingw32-make.exe -C build\Debug -j8
# 烧录
C:\Users\zhanglongsheng\.renesas\platform\DebugComp\Dialog\ARM\Segger\JLink.exe -CommanderScript flash.jlink
```

- 工具链：gcc_arm `C:\Users\zhanglongsheng\.eide\tools\gcc_arm\bin\arm-none-eabi-gcc.exe`
- 烧录：device R7KA8P1KF；"Writing target memory failed" 为**已知误报**，788480 bytes 下载+验证成功。
- 诊断变量：`s_dbg_*`（J-Link 直读，`SCB_CleanDCache_by_Addr` 后可见）、`s_qr_frame_snapshot`（640×480 @0x684df620）、`s_dbg_ean13_try/ok/fail`、`s_dbg_qr_frame_mean`、`s_dbg_qr_last_status`。
- 解码链路：`qr_decoder.c` 解码线程每帧先 `ean13_decode_frame`（成功 type=EAN13）失败再 quirc。
- 镜像：OV7725 COM3=0x50（HFLIP|SWAP_YUV）→ 画面水平镜像，EAN-13 需整行像素反转恢复正向再解码。

---

## 9. 后续恢复步骤（下次继续时的 TODO）

1. [ ] 让用户重新摆放**纸盒条码**（占画面宽度 40%+，竖条竖直平放正对，表面防反光），停住后：
   - 用 `tools/dump_qr_frame.py` 抓帧；
   - 先确认画面里真的有条码竖条结构（过渡行 100+），再检查 ean13 ok 计数是否增长；
   - 条码可见仍失败 → `tools/dbg_new_frame.py` 逐步定位失败点；
   - 条码不可见 → 指导调整位置/距离。
2. [ ] 可选验证：打印大条码 `tools/gen_ean13_test.py` 生成的 `ean13_PRINT_WHITE_6901234567892.png`（或手机全屏显示）——大模块 + 白底，验证整条链路。
3. [ ] 若纸盒条码模块 <1.7px（贴太近也不行，太远更不行），说明镜头焦距/分辨率物理极限，需换方案（如更高分辨率输入、或提示用户放大条码）。
4. [ ] 误报回归：保持无条码帧 0 误报。

---

## 10. 相关文件清单

| 文件 | 说明 |
|------|------|
| `src/middleware/src/barcode_1d.c` | EAN-13 解码器（本次核心改动，已烧录） |
| `src/middleware/src/qr_decoder.c` | 解码线程（未改动，调用 ean13_decode_frame） |
| `tools/gen_box_scene.py` | 纸盒场景 raw 生成器（新增） |
| `tools/dbg_ean13_frame.py`、`tools/dbg_new_frame.py` | Python 复刻解码器（新增，同步 C 逻辑） |
| `tools/ean13_host_main.c`、`tools/test_ean13_host.py` | host 解码入口 + 回归测试 |
| `tools/dump_qr_frame.py` | 实机抓帧 + 诊断 |
| `.tmp/qr_test/` | 测试产物：`box_*.raw`（纸盒场景）、`ean13_*.png`（原测试图）、`frame_latest.raw/png`、`frame_ascii*.txt`（实机帧可视化） |
| `flash.jlink` | 烧录脚本（device R7KA8P1KF） |
