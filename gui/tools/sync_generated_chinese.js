/*
 * Keep the checked-in GUI Guider generated sources runnable after changing
 * the editable canvas to Simplified Chinese. A later Ctrl+G generation in
 * GUI Guider 2.0 produces equivalent screen/font references.
 */

const fs = require("fs");
const path = require("path");
const { spawnSync } = require("child_process");

const guiRoot = path.resolve(__dirname, "../RA8P1");
const project = JSON.parse(fs.readFileSync(path.join(guiRoot, "RA8P1.guiguider"), "utf8"));
const screenDir = path.join(guiRoot, "generated/screens");
const fontDir = path.join(guiRoot, "generated/assets/fonts");
const family = "SourceHanSerifSC";
const familyFile = `${family}.otf`;

/* ============================================================================
 * 2026-08-18: Bold typography.
 *  - FONT_SOURCE: the cropped fonts are generated from the BOLD weight of the
 *    family (SourceHanSerifSC-Bold.otf, kept under gui/RA8P1/resources/font/)
 *    so every glyph is visibly heavier on the panel.
 *  - The one-time "+2px size bump" (2026-08-18) is already applied to the
 *    .guiguider design file and the generated screens; this script is
 *    IDEMPOTENT and must NOT bump sizes again, otherwise every run grows the
 *    fonts by another 2px.  New sizes come from the design file alone.
 * ==========================================================================*/
const FONT_SOURCE = path.join(guiRoot, "resources/font", `${family}-Bold.otf`);

const translations = new Map([
  ["查��详情", "日志详情"],
  ["查看详情", "日志详情"],
  ["Smart Medicine Station", "智慧药品工作站"],
  ["Preparing medical services", "正在准备医疗服务"],
  ["Loading interface...", "正在加载界面..."],
  ["Continue", "进入系统"],
  ["System online", "系统在线"],
  ["Good day", "你好"],
  ["Select a service to continue", "请选择需要的服务"],
  ["Scan medicine", "扫码识药"],
  ["Identify a medicine\\npackage", "扫描药品包装二维码"],
  ["Medicine record", "药品记录"],
  ["Review the latest scan\\nresult", "查看最近扫码结果"],
  ["Dispense progress", "出药进度"],
  ["View the mock\\ndispensing flow", "查看模拟出药流程"],
  ["Device status", "设备状态"],
  ["Camera, motor, CAN\\nand network", "摄像头、电机、CAN与网络"],
  ["Open  >", "进入  >"],
  ["Prototype UI  |  640 x 480  |  GUI Guider canvas", "原型界面  |  640 x 480  |  GUI Guider 画布"],
  ["<  Home", "<  返回"],
  ["QR medicine scan", "二维码药品扫描"],
  ["CAMERA PREVIEW", "摄像头预览"],
  ["Keep the QR code inside the frame", "请将二维码保持在取景框内"],
  ["Scanner", "扫描器"],
  ["Ready", "就绪"],
  ["Camera", "摄像头"],
  ["Last result", "上次结果"],
  ["Waiting for\\nQR code", "等待\\n二维码"],
  ["Start scan", "开始扫描"],
  ["Camera capture is enabled only while this page is active.", "仅在本页面启用摄像头采集。"],
  ["Amoxicillin capsules", "阿莫西林胶囊"],
  ["Code: DEMO-2026-001", "编码：DEMO-2026-001"],
  ["Verified", "已验证"],
  ["Dosage", "剂量"],
  ["Batch", "批次"],
  ["Expiry", "有效期"],
  ["Scan another", "重新扫码"],
  ["Demo data will be replaced by the quirc decoding result.", "示例数据后续由 quirc 解码结果替换。"],
  ["Waiting for confirmation", "等待确认"],
  ["Motor 1  |  Channel A  |  1 package", "1号电机  |  A通道  |  1盒"],
  ["Confirm dispense", "确认出药"],
  ["Motor control and CAN acknowledgement are simulated.", "电机控制和 CAN 应答当前使用模拟数据。"],
  ["System health", "系统状态"],
  ["Ready / 15 FPS", "就绪 / 15 FPS"],
  ["Arm coordinates", "机械臂坐标"],
  ["Motor", "电机"],
  ["Idle", "待机"],
  ["Network", "网络"],
  ["Not configured", "未配置"],
  ["Run self-test", "无线调试"],
]);

for (const name of fs.readdirSync(screenDir).filter((item) => item.endsWith(".c"))) {
  const file = path.join(screenDir, name);
  let source = fs.readFileSync(file, "utf8");
  for (const [from, to] of translations) source = source.split(`\"${from}\"`).join(`\"${to}\"`);
  source = source.replace(/lv_font_montserratMedium_(\d+)/g, `lv_font_${family}_$1`);
  /* Font sizes are defined by the .guiguider design only (the one-time +2px
   * bump was applied to the design and screens on 2026-08-18).  Do NOT bump
   * references here: this script is idempotent. */
  if (name === "gg_Home.c") {
    source = source.replace(
      /ui->Home\.(card_(?:pickup|identify|store|device)) = lv_obj_create\(ui->Home\.screen\);/g,
      "ui->Home.$1 = lv_button_create(ui->Home.screen);"
    );
    source = source.replace(
      "lv_obj_set_height(ui->Home.welcome_card_welcome_accent, 58);",
      "lv_obj_set_height(ui->Home.welcome_card_welcome_accent, 52);"
    );
    source = source.replace(
      "lv_obj_set_x(ui->Home.welcome_card_welcome_accent, 0);",
      "lv_obj_set_x(ui->Home.welcome_card_welcome_accent, 2);"
    );
    source = source.replace(
      "lv_obj_set_y(ui->Home.welcome_card_welcome_accent, 0);",
      "lv_obj_set_y(ui->Home.welcome_card_welcome_accent, 2);"
    );
  }
  if (name === "gg_Logs.c") {
    source = source.replace(
      /lv_label_set_text\(ui->Logs\.button_export_button_export_text, "[^"]*"\);/,
      'lv_label_set_text(ui->Logs.button_export_button_export_text, "日志详情");'
    );
  }
  fs.writeFileSync(file, source, "utf8");
}

const charsBySize = new Map();
function walk(node) {
  if (!node || typeof node !== "object") return;
  if (node.type === "label") {
    const style = node.style?.["LV_PART_MAIN|LV_STATE_DEFAULT"];
    if (style?.text_family === familyFile) {
      const size = style.text_size;
      const chars = charsBySize.get(size) || new Set(project.projectSettings.fontConfig.base_chars);
      for (const character of node.text || "") chars.add(character);
      charsBySize.set(size, chars);
    }
  }
  for (const value of Object.values(node)) if (value && typeof value === "object") walk(value);
}
walk(project.UI.screen_list);

/* Runtime text not present in the design canvas: custom.c / gui_app.c /
 * sys_log set these labels at runtime (network badge, scan state, device
 * rows, logs page entries, button feedback, uptime, auth states...).
 *
 * Flash budget: 13 sizes x full CJK set overflows the 1 MB flash.  Only the
 * sizes that display ARBITRARY content (scan payload / log text) get the
 * complete src-extracted set; the other sizes only need their own fixed
 * status words.  This keeps every glyph the UI can show present without
 * bloating the big sizes (26-36px) which are titles only. */
{
  /* Fixed status words per size (from the runtime labels that use them). */
  const drugDbChars = "阿莫西林胶囊维生素C片布洛芬缓释";
  /* Common punctuation used by runtime strings but absent from base_chars:
   * U+00B7 middle dot ("系统就绪 · 触摸正常"), full-width colon/parens,
   * ellipsis, comma.  Merge into every size. */
  const runtimePunct = "·…：（）；，、—–【】《》？!条";
  /* Extra common medicine-name characters (future drug DB entries). */
  const medicineChars =
    "头孢氨苄克洛西林阿奇霉素红霉素罗红霉素左氧氟沙星环丙沙星诺氟沙星甲硝唑替硝唑奥硝唑" +
    "奥美拉唑泮托拉唑兰索拉唑雷贝拉唑莫沙必利多潘立酮甲氧氯普胺蒙脱石散洛哌丁胺地衣芽孢杆菌" +
    "双歧杆菌乳杆菌酪酸梭菌复方黄连素板蓝根感冒灵清热解毒双黄连口服液藿香正气水十滴水保和丸" +
    "健胃消食片山楂麦芽神曲对乙酰氨基酚布洛芬双氯芬酸钠塞来昔布依托考昔美洛昔康萘普生吲哚美辛" +
    "阿司匹林氯吡格雷华法林利伐沙班达比加群低分子肝素肝素钠硝酸甘油单硝酸异山梨酯硝苯地平氨氯地平" +
    "非洛地平缬沙坦氯沙坦厄贝沙坦坎地沙坦替米沙坦奥美沙坦培哚普利贝那普利福辛普利卡托普利" +
    "美托洛尔比索洛尔阿替洛尔普萘洛尔索他洛尔胺碘酮普罗帕酮地高辛辛伐他汀阿托伐他汀瑞舒伐他汀" +
    "普伐他汀洛伐他汀氟伐他汀非诺贝特苯扎贝特吉非罗齐二甲双胍格列本脲格列齐特格列美脲瑞格列奈" +
    "阿卡波糖伏格列波糖吡格列酮罗格列酮达格列净恩格列净卡格列净西格列汀维格列汀沙格列汀利格列汀" +
    "胰岛素甘精胰岛素门冬胰岛素地特胰岛素糖皮质激素泼尼松甲泼尼龙地塞米松氢化可的松倍他米松" +
    "氯雷他定西替利嗪非索非那定依巴斯汀咪唑斯汀左西替利嗪地氯雷他定异丙嗪氯苯那敏赛庚啶" +
    "孟鲁司特氨茶碱多索茶碱沙丁胺醇特布他林异丙托溴铵噻托溴铵布地奈德氟替卡松糠酸莫米松" +
    "左甲状腺素甲巯咪唑丙硫氧嘧啶碳酸钙维生素D维生素K铁剂叶酸维生素B维生素E复合维生素矿物质" +
    "电解质口服补液盐葡萄糖氯化钠氯化钾碳酸氢钠乳酸钠山梨醇甘露醇甘油果糖羟乙基淀粉右旋糖酐" +
    "白蛋白球蛋白免疫球蛋白丙种球蛋白破伤风抗毒素狂犬病疫苗乙肝疫苗流感疫苗肺炎疫苗HPV疫苗" +
    "碘伏酒精双氧水生理盐水无菌纱布绷带创可贴体温计血压计血糖仪试纸针筒输液器导管敷料";
  const runtimeBySize = new Map([
    /* 15px：日志行（sysLogCjk 自动合并）+ 固定状态词。二维码解码线程已创建/上次
     * 等词来自 sys_log 格式串，sysLogCjk 已覆盖；这里再显式兜底保证。 */
    [15, "信息成功警告错误时间内容等待扫描取药单号药品合计日志已清空取药单重新扫描存药确认库台账初始化完成界面启动完成触摸屏重连成功摄像头初始化成功采集停止扫码成功开始扫描查看详情待机采集中已识别就绪系统在线离线正常运行操作药师已登录未登录请先登录药师账号当前药师余盒种待取已取待扫描批次有效期编码已验证未验证解创建" + drugDbChars],
    /* 16px：登录提示（工号或密码错误）/Store 徽章（待扫码）/Home 认证徽章 */
    [16, "系统在线离线运行已登录未登录请先登录药师账号当前药师批次有效期余盒种日志总数成功异常待工号或密码错误"],
    /* 17px：Device 摄像头行（就绪 / 上次 X FPS）+ Medicine 未验证 + 系统状态词 */
    [17, "系统在线离线就绪采集中已识别待机待取已取库存待扫描正常异常摄像头触摸运行自检自检网络电机机械臂坐标上次未" + drugDbChars + "种盒"],
    [18, "开始扫描查看详情确认存药重新扫码等待扫描确认失败已确认取药完成取药单已识别扫描成功" + drugDbChars + "种盒"],
    [20, "余盒种已登录未登录请先登录药师账号当前药师日志总数成功异常"],
    /* 22px：Pickup 取药单标题（取药单扫描 / 取药单已识别） */
    [22, "待机信息成功警告错误扫描取药单已识别"],
    [23, "等待扫描确认存药重新扫码" + drugDbChars],
    [28, "等待扫描确认存药重新扫码" + drugDbChars],
    /* 24px：Device 自检摘要（自检通过 / 自检 N 项异常） */
    [24, "自检通过项异常"],
  ]);
  /* Merge punctuation into every size; medicine-name characters only into
   * the sizes that display scan-result drug names (17/18/23px).  28px is the
   * Medicine page title which only shows drug_db entries (drugDbChars
   * already merged via runtimeBySize) - keep it slim (big glyphs cost flash). */
  {
    const allSizes = [...charsBySize.keys()];
    for (const size of allSizes) {
      const cur = new Set(charsBySize.get(size) || []);
      for (const ch of runtimePunct) cur.add(ch);
      if (size === 17 || size === 18 || size === 23) {
        for (const ch of medicineChars) cur.add(ch);
      }
      charsBySize.set(size, cur);
    }
  }
  for (const [size, chars] of runtimeBySize) {
    if (chars.length === 0) continue;
    const cur = new Set(charsBySize.get(size) || []);
    for (const ch of chars) cur.add(ch);
    charsBySize.set(size, cur);
  }

  /* Sizes that display arbitrary content: only the logs page rows (15px)
   * truly show arbitrary text (sys_log entries).  For 15px we merge exactly
   * the characters that sys_log_add() strings can produce (extracted from
   * the C sources) instead of every string literal in the tree - the latter
   * includes unrelated driver comments/strings and costs ~35 KB of flash.
   * Scan payloads / medicine names (18/23/28px) are either ASCII codes or
   * come from drug_db whose Chinese characters are in runtimeBySize. */
  const sysLogCjk = new Set();
  {
    const srcRoot = path.resolve(__dirname, "../../src");
    (function collect(dir) {
      for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
        const full = path.join(dir, entry.name);
        if (entry.isDirectory()) {
          collect(full);
        }
        else if (entry.name.endsWith(".c") || entry.name.endsWith(".h")) {
          const text = fs.readFileSync(full, "utf8");
          const matches = text.match(/sys_log_add\([^)]*"[^"]*"/g);
          if (matches) {
            for (const s of matches) {
              const q = s.match(/"([^"]*)"/);
              if (q) {
                for (const ch of q[1]) {
                  if (ch.charCodeAt(0) > 127) {
                    sysLogCjk.add(ch);
                  }
                }
              }
            }
          }
        }
      }
    })(srcRoot);
  }
  {
    const cur = new Set(charsBySize.get(15) || []);
    for (const ch of sysLogCjk) cur.add(ch);
    charsBySize.set(15, cur);
  }
}

/* Remove stale font files whose sizes are no longer referenced after the
 * bump (e.g. 13/14/19/34px), otherwise the CMake glob still links them. */
{
  const referenced = new Set([...charsBySize.keys()]);
  const stale = fs.readdirSync(fontDir).filter((f) => {
    const m = f.match(/^lv_font_SourceHanSerifSC_(\d+)\.c$/);
    return m && !referenced.has(Number(m[1]));
  });
  for (const f of stale) {
    fs.unlinkSync(path.join(fontDir, f));
    console.log(`Removed stale font ${f}`);
  }
}

const guiderHome = process.env.GUI_GUIDER_HOME || "D:/GUIguider_2.0.0";
const electron = path.join(guiderHome, "GUIGuider.exe");
const converter = path.join(guiderHome, "resources/app.asar/node_modules/lv_font_conv/lv_font_conv.js");
const sourceFont = FONT_SOURCE;
for (const [size, chars] of [...charsBySize].sort((a, b) => a[0] - b[0])) {
  const fontName = `lv_font_${family}_${size}`;
  const output = path.join(fontDir, `${fontName}.c`);
  const result = spawnSync(electron, [converter, "--size", String(size), "--bpp", "4", "--format", "lvgl",
    "--font", sourceFont, "--symbols", [...chars].join(""), "--no-kerning",
    "--lv-include", "lvgl.h", "--lv-font-name", fontName, "-o", output], {
      env: { ...process.env, ELECTRON_RUN_AS_NODE: "1" }, encoding: "utf8",
  });
  if (result.status !== 0) throw new Error(result.stderr || `Font conversion failed: ${fontName}`);
  console.log(`Generated ${path.relative(guiRoot, output)} (${chars.size} glyphs)`);
}

const headerFile = path.join(fontDir, "gg_font.h");
let header = fs.readFileSync(headerFile, "utf8");
const missingDeclarations = [];
for (const size of [...charsBySize.keys()].sort((a, b) => a - b)) {
  const chinese = `LV_FONT_DECLARE(lv_font_${family}_${size});`;
  if (!header.includes(chinese)) missingDeclarations.push(chinese);
}
if (missingDeclarations.length) {
  const marker = header.lastIndexOf("#ifdef __cplusplus");
  header = `${header.slice(0, marker)}${missingDeclarations.join("\n")}\n\n${header.slice(marker)}`;
}
fs.writeFileSync(headerFile, header, "utf8");
