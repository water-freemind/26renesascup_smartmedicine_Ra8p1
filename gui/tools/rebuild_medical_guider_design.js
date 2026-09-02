/*
 * Rebuild the editable GUI Guider canvas for the 640x480 medical UI.
 *
 * Usage:
 *   node gui/tools/rebuild_medical_guider_design.js
 *
 * The script preserves project settings, LVGL settings and GUI Guider system
 * layers. It only replaces business screens and their event definitions.
 */

const fs = require("fs");
const path = require("path");

const projectFile = path.resolve(__dirname, "../RA8P1/RA8P1.guiguider");
const project = JSON.parse(fs.readFileSync(projectFile, "utf8"));
const UI_FONT = "SourceHanSerifSC.otf";

const C = {
  bg: "#f6f7ff",
  surface: "#ffffff",
  text: "#10233f",
  muted: "#687b99",
  line: "#d8e1ef",
  blue: "#1677ff",
  blueSoft: "#eaf2ff",
  green: "#14a66a",
  greenSoft: "#e8f8f1",
  orange: "#f59e0b",
  orangeSoft: "#fff4dc",
  purple: "#7157d9",
  purpleSoft: "#f0edff",
  cyan: "#0891b2",
  cyanSoft: "#e5f7fb",
  red: "#ef5350",
  redSoft: "#fff0f0",
  navy: "#203c83",
  dark: "#18263b",
};

const px = (value) => ({ value, unit: "px" });

function baseStyle(extra = {}) {
  return {
    "LV_PART_MAIN|LV_STATE_DEFAULT": {
      part: "LV_PART_MAIN",
      state: "LV_STATE_DEFAULT",
      ...extra,
    },
  };
}

function position(node, x, y, width, height, align = "LV_ALIGN_TOP_LEFT") {
  return {
    ...node,
    width: width.value,
    width_unit: width.unit,
    height: height.value,
    height_unit: height.unit,
    x: x.value,
    x_unit: x.unit,
    y: y.value,
    y_unit: y.unit,
    align,
  };
}

function label(id, name, text, x, y, options = {}) {
  const width = options.width === undefined ? px(0) : px(options.width);
  const height = options.height === undefined ? px(0) : px(options.height);
  const node = position(
    { id, type: "label", name },
    px(x), px(y),
    options.width === undefined ? { value: 0, unit: "content" } : width,
    options.height === undefined ? { value: 0, unit: "content" } : height,
    options.align || "LV_ALIGN_TOP_LEFT"
  );
  node.style = baseStyle({
    bg_opa: 0,
    text_color: options.color || C.text,
    text_opa: 255,
    text_size: options.size || 18,
    text_family: options.family || UI_FONT,
    text_align: options.textAlign || "LV_TEXT_ALIGN_LEFT",
  });
  node.text = text;
  node.children = [];
  return node;
}

function imageNode(id, name, src, x, y, width, height, options = {}) {
  const node = position({ id, type: "image", name }, px(x), px(y), px(width), px(height),
    options.align || "LV_ALIGN_TOP_LEFT");
  node.style = baseStyle({ image_opa: options.opa === undefined ? 255 : options.opa });
  node.src = `resources/image/${src}`;
  node.src_config = { color_format: options.colorFormat || "ARGB8888" };
  node.children = [];
  return node;
}

function box(id, name, x, y, width, height, options = {}, children = []) {
  const node = position({ id, type: "container", name }, px(x), px(y), px(width), px(height));
  node.lock = false;
  node.style = baseStyle({
    bg_color: options.bg || C.surface,
    bg_opa: options.bgOpa === undefined ? 255 : options.bgOpa,
    border_color: options.border || C.line,
    border_opa: options.borderWidth === 0 ? 0 : 255,
    border_width: options.borderWidth === undefined ? 1 : options.borderWidth,
    radius: options.radius === undefined ? 14 : options.radius,
    pad_top: 0,
    pad_bottom: 0,
    pad_left: 0,
    pad_right: 0,
  });
  node.children = children;
  return node;
}

function button(id, name, text, x, y, width, height, options = {}) {
  const node = position({ id, type: "button", name }, px(x), px(y), px(width), px(height));
  node.style = baseStyle({
    bg_color: options.bg || C.blue,
    bg_opa: 255,
    border_width: options.borderWidth || 0,
    border_opa: options.borderWidth ? 255 : 0,
    border_color: options.border || options.bg || C.blue,
    shadow_width: 0,
    radius: options.radius === undefined ? 10 : options.radius,
  });
  node.children = [label(`${id}_text`, `${name}_text`, text, 0, 0, {
    align: "LV_ALIGN_CENTER",
    color: options.color || "#ffffff",
    size: options.size || 16,
    textAlign: "LV_TEXT_ALIGN_CENTER",
  })];
  return node;
}

function screen(id, name, isDefault, children) {
  return {
    id,
    name,
    type: "screen",
    ...(isDefault ? { default_screen: true } : {}),
    style: baseStyle({ bg_color: C.bg, bg_opa: 255 }),
    children,
  };
}

function header(prefix, title, options = {}) {
  const children = [
    box(`${prefix}_brand_icon`, "brand_icon", 18, 11, 30, 34,
      { bg: C.blueSoft, borderWidth: 0, radius: 8 },
      [imageNode(`${prefix}_brand_image`, "brand_image", "icon_brand.png", 0, 2, 30, 30)]),
    label(`${prefix}_header_title`, "header_title", title, 60, 16, { size: 22 }),
  ];
  if (options.back) {
    children.push(button(`${prefix}_back`, "button_back", "<  返回", 500, 10, 122, 36,
      { bg: C.blueSoft, color: C.blue, size: 15 }));
  } else {
    children.push(imageNode(`${prefix}_nuedc`, "nuedc_brand", "icon_nuedc.png", 228, 7, 70, 42));
    children.push(imageNode(`${prefix}_renesas`, "renesas_brand", "icon_renesas.png", 306, 16, 108, 24));
    children.push(box(`${prefix}_online`, "status_online", 430, 9, 196, 38,
      { bg: C.redSoft, border: "#f5c8c8", radius: 9 }, [
        imageNode(`${prefix}_online_asset`, "online_asset", "icon_online.png", 8, 7, 24, 24, { opa: 0 }),
        imageNode(`${prefix}_online_icon`, "status_icon", "icon_offline.png", 8, 7, 24, 24),
        label(`${prefix}_online_text`, "status_text", "系统离线", 40, 10,
          { color: C.red, size: 14, width: 146, textAlign: "LV_TEXT_ALIGN_LEFT" }),
      ]));
  }
  return box(`${prefix}_header`, "header", 0, 0, 640, 56,
    { bg: C.surface, borderWidth: 0, radius: 0 }, children);
}

function homeCard(id, title, subtitle, iconFile, x, y, accent, soft) {
  const card = box(id, id.replace(/^home_/, "card_"), x, y, 294, 142,
    { bg: C.surface, border: C.line, radius: 14 }, [
      box(`${id}_icon_box`, "icon_box", 18, 18, 48, 48,
        { bg: soft, borderWidth: 0, radius: 11 }, [
          imageNode(`${id}_icon`, "icon", iconFile, 6, 6, 36, 36),
        ]),
      label(`${id}_title`, "title", title, 82, 20, { size: 20 }),
      label(`${id}_subtitle`, "subtitle", subtitle, 82, 54,
        { size: 16, color: C.muted, width: 194, height: 44 }),
      label(`${id}_open`, "open_text", "进入  >", 202, 108,
        { size: 16, color: accent, width: 72, textAlign: "LV_TEXT_ALIGN_RIGHT" }),
      box(`${id}_accent`, "accent_line", 18, 126, 54, 5,
        { bg: soft, borderWidth: 0, radius: 3 }),
    ]);
  card.type = "button";
  return card;
}

function managementCard() {
  const card = box("home_management", "card_management", 20, 290, 604, 142,
    { bg: C.surface, border: C.line, radius: 14 }, [
      box("home_management_icon_box", "icon_box", 18, 18, 48, 48,
        { bg: C.purpleSoft, borderWidth: 0, radius: 11 }, [
          imageNode("home_management_icon", "icon", "icon_user.png", 6, 6, 36, 36),
        ]),
      label("home_management_title", "title", "药师管理", 82, 20, { size: 20 }),
      label("home_management_subtitle", "subtitle", "身份认证后可存药、查看取药日志和系统状态", 82, 54,
        { size: 16, color: C.muted, width: 420, height: 44 }),
      box("home_management_badge", "auth_badge", 484, 18, 96, 34,
        { bg: C.purpleSoft, borderWidth: 0, radius: 8 }, [
          label("home_management_badge_text", "auth_badge_text", "需要认证", 0, 8,
            { align: "LV_ALIGN_TOP_MID", size: 14, color: C.purple }),
        ]),
      label("home_management_open", "open_text", "登录  >", 504, 106,
        { size: 16, color: C.purple, width: 76, textAlign: "LV_TEXT_ALIGN_RIGHT" }),
      box("home_management_accent", "accent_line", 18, 126, 72, 5,
        { bg: C.purpleSoft, borderWidth: 0, radius: 3 }),
    ]);
  card.type = "button";
  return card;
}

const home = screen("medical_home", "Home", false, [
  header("home", "智慧药品工作站"),
  box("home_welcome_card", "welcome_card", 20, 68, 604, 58,
    { bg: "#edf5ff", border: "#d6e7ff", radius: 12 }, [
      box("home_welcome_accent", "welcome_accent", 2, 2, 7, 52,
        { bg: C.blue, borderWidth: 0, radius: 4 }),
      label("home_greeting", "greeting", "你好，欢迎使用智慧药品工作站", 22, 10, { size: 19 }),
      label("home_hint", "hint", "安全 · 高效 · 可追溯", 394, 16,
        { size: 14, color: C.blue, width: 184, textAlign: "LV_TEXT_ALIGN_RIGHT" }),
    ]),
  homeCard("home_pickup", "取药", "扫描取药单并执行取药", "icon_pickup_order.png", 20, 138, C.blue, C.blueSoft),
  homeCard("home_identify", "识别药", "扫描药品二维码查看信息", "icon_scan.png", 330, 138, C.green, C.greenSoft),
  managementCard(),
]);

const boot = screen("medical_boot", "Boot", true, [
  box("boot_brand_band", "brand_band", 0, 0, 640, 148,
    { bg: C.navy, borderWidth: 0, radius: 0 }, [
      label("boot_competition", "competition", "瑞萨杯智能医疗终端", 32, 34,
        { size: 20, color: "#ffffff" }),
      label("boot_competition_en", "competition_en", "SMART MEDICINE WORKSTATION", 32, 70,
        { size: 13, color: "#cfe0ff" }),
      imageNode("boot_renesas", "renesas_brand", "icon_renesas.png", 408, 30, 174, 39),
    ]),
  box("boot_logo", "logo", 264, 102, 112, 112, { bg: C.surface, border: "#dbe8fb", radius: 26 }, [
    imageNode("boot_logo_image", "logo_image", "icon_brand.png", 16, 16, 80, 80),
  ]),
  label("boot_title", "title", "智慧药品工作站", 0, 230,
    { align: "LV_ALIGN_TOP_MID", size: 28 }),
  label("boot_subtitle", "subtitle", "瑞萨 RA8P1 智能医疗应用", 0, 274,
    { align: "LV_ALIGN_TOP_MID", size: 16, color: C.muted }),
  box("boot_progress_track", "progress_track", 170, 330, 300, 12,
    { bg: "#dfe8f5", borderWidth: 0, radius: 6 }, [
      box("boot_progress_fill", "progress_fill", 0, 0, 214, 12,
        { bg: C.blue, borderWidth: 0, radius: 6 }),
    ]),
  label("boot_status", "status", "正在加载界面...", 0, 358,
    { align: "LV_ALIGN_TOP_MID", size: 15, color: C.muted }),
  button("boot_continue", "button_continue", "进入系统", 250, 398, 140, 42),
]);

const scan = screen("medical_scan", "Scan", false, [
  header("scan", "识别药品", { back: true }),
  box("scan_preview", "camera_preview", 8, 66, 480, 358,
    { bg: "#263548", border: "#334b68", radius: 12 }, [
      label("scan_preview_hint", "preview_hint", "摄像头预览", 0, 22,
        { align: "LV_ALIGN_TOP_MID", color: "#afbed2", size: 15 }),
      box("scan_qr_frame", "qr_frame", 145, 70, 190, 190,
        { bg: "#263548", border: "#49a0ff", borderWidth: 3, radius: 8 }),
      label("scan_cross", "scan_cross", "+", 0, 157,
        { align: "LV_ALIGN_TOP_MID", color: C.blue, size: 34 }),
      label("scan_instruction", "instruction", "请将二维码保持在取景框内", 0, 308,
        { align: "LV_ALIGN_TOP_MID", color: "#e9f1fb", size: 15 }),
    ]),
  box("scan_panel", "control_panel", 496, 66, 136, 358,
    { bg: C.surface, border: C.line, radius: 12 }, [
      label("scan_state_title", "state_title", "扫描器", 14, 18, { size: 18 }),
      box("scan_state_badge", "state_badge", 12, 56, 112, 34,
        { bg: C.greenSoft, borderWidth: 0, radius: 8 }, [
          label("scan_state", "state", "就绪", 0, 8,
            { align: "LV_ALIGN_TOP_MID", color: C.green, size: 15 }),
        ]),
      label("scan_fps_label", "fps_label", "摄像头", 14, 112, { size: 14, color: C.muted }),
      label("scan_fps", "fps", "15 FPS", 14, 136, { size: 20 }),
      label("scan_result_label", "result_label", "上次结果", 14, 182,
        { size: 14, color: C.muted }),
      label("scan_result", "result", "等待\n二维码", 14, 208,
        { size: 16, width: 108, height: 46 }),
      button("scan_start", "button_start", "开始扫描", 12, 292, 112, 44),
    ]),
]);

const medicine = screen("medical_medicine", "Medicine", false, [
  header("medicine", "药品识别结果", { back: true }),
  box("medicine_card", "medicine_card", 18, 76, 604, 316,
    { bg: C.surface, border: C.line, radius: 16 }, [
      box("medicine_icon_box", "medicine_icon_box", 28, 30, 88, 88,
        { bg: C.greenSoft, borderWidth: 0, radius: 18 }, [
          imageNode("medicine_icon", "medicine_icon", "icon_medicine_bottle.png", 16, 16, 56, 56),
        ]),
      label("medicine_name", "medicine_name", "阿莫西林胶囊", 144, 30, { size: 26 }),
      label("medicine_code", "medicine_code", "编码：DEMO-2026-001", 144, 70,
        { size: 16, color: C.muted }),
      box("medicine_valid_badge", "valid_badge", 144, 104, 104, 34,
        { bg: C.greenSoft, borderWidth: 0, radius: 8 }, [
          label("medicine_valid", "valid_text", "已验证", 0, 8,
            { align: "LV_ALIGN_TOP_MID", color: C.green, size: 15 }),
        ]),
      label("medicine_dose_title", "dose_title", "剂量", 28, 170, { size: 14, color: C.muted }),
      label("medicine_dose", "dose", "0.25 g", 28, 196, { size: 20 }),
      label("medicine_batch_title", "batch_title", "批次", 222, 170, { size: 14, color: C.muted }),
      label("medicine_batch", "batch", "RA8P1-A01", 222, 196, { size: 20 }),
      label("medicine_expiry_title", "expiry_title", "有效期", 416, 170, { size: 14, color: C.muted }),
      label("medicine_expiry", "expiry", "2028-08", 416, 196, { size: 20 }),
      button("medicine_rescan", "button_rescan", "重新扫码", 416, 254, 160, 42,
        { bg: C.green }),
    ]),
]);

const pickup = screen("medical_pickup", "Pickup", false, [
  header("pickup", "取药任务", { back: true }),
  box("pickup_camera_card", "camera_card", 16, 66, 294, 188,
    { bg: C.dark, border: "#334b68", radius: 13 }, [
      label("pickup_camera_title", "camera_title", "取药单摄像头", 14, 12,
        { size: 15, color: "#dce8f7" }),
      box("pickup_camera_frame", "camera_frame", 84, 42, 126, 110,
        { bg: "#22334a", border: "#49a0ff", borderWidth: 3, radius: 8 }, [
          label("pickup_camera_cross", "camera_cross", "+", 0, 35,
            { align: "LV_ALIGN_TOP_MID", size: 34, color: C.blue }),
        ]),
      label("pickup_camera_hint", "camera_hint", "请将取药单二维码放入框内", 0, 160,
        { align: "LV_ALIGN_TOP_MID", size: 13, color: "#afbed2" }),
    ]),
  box("pickup_order_card", "order_card", 322, 66, 302, 188,
    { bg: C.surface, border: C.line, radius: 14 }, [
      box("pickup_order_icon_box", "order_icon_box", 16, 16, 48, 48,
        { bg: C.blueSoft, borderWidth: 0, radius: 10 }, [
          imageNode("pickup_order_icon", "order_icon", "icon_pickup_order.png", 6, 6, 36, 36),
        ]),
      label("pickup_order_title", "order_title", "取药单已识别", 78, 16, { size: 20 }),
      box("pickup_order_badge", "order_badge", 196, 18, 88, 30,
        { bg: C.greenSoft, borderWidth: 0, radius: 8 }, [
          label("pickup_order_badge_text", "order_badge_text", "扫描成功", 0, 7,
            { align: "LV_ALIGN_TOP_MID", size: 13, color: C.green }),
        ]),
      label("pickup_order_number_label", "order_number_label", "取药单号", 16, 80,
        { size: 14, color: C.muted }),
      label("pickup_order_number", "order_number", "RX-20260811-001", 92, 78, { size: 16 }),
      label("pickup_order_total_label", "order_total_label", "药品合计", 16, 112,
        { size: 14, color: C.muted }),
      label("pickup_order_total", "order_total", "3种 / 4盒", 92, 110, { size: 16 }),
      button("pickup_scan_order", "button_scan_order", "重新扫描", 166, 136, 118, 38,
        { bg: C.blueSoft, color: C.blue, size: 14 }),
    ]),
  box("pickup_list_card", "medicine_list_card", 16, 264, 608, 198,
    { bg: C.surface, border: C.line, radius: 14 }, [
      label("pickup_list_title", "list_title", "药品清单", 16, 12, { size: 19 }),
      label("pickup_list_hint", "list_hint", "按坐标依次取药", 112, 15,
        { size: 14, color: C.muted }),
      button("pickup_start", "button_start", "开始取药", 458, 8, 132, 38,
        { bg: C.blue }),
      box("pickup_table_header", "table_header", 14, 52, 580, 28,
        { bg: C.blueSoft, borderWidth: 0, radius: 6 }, [
          label("pickup_header_drug", "header_drug", "药品", 10, 6, { size: 13, color: C.muted }),
          label("pickup_header_qty", "header_qty", "数量", 304, 6, { size: 13, color: C.muted }),
          label("pickup_header_position", "header_position", "坐标", 386, 6, { size: 13, color: C.muted }),
          label("pickup_header_state", "header_state", "状态", 490, 6, { size: 13, color: C.muted }),
        ]),
      label("pickup_drug_1", "drug_name", "阿莫西林胶囊", 24, 88, { size: 15 }),
      label("pickup_qty_1", "quantity", "1盒", 318, 88, { size: 15 }),
      label("pickup_position_1", "position", "A03", 400, 88, { size: 15 }),
      label("pickup_state_1", "state", "待取", 504, 88, { size: 15, color: C.orange }),
      label("pickup_drug_2", "drug_name", "布洛芬缓释胶囊", 24, 120, { size: 15 }),
      label("pickup_qty_2", "quantity", "2盒", 318, 120, { size: 15 }),
      label("pickup_position_2", "position", "B12", 400, 120, { size: 15 }),
      label("pickup_state_2", "state", "待取", 504, 120, { size: 15, color: C.orange }),
      label("pickup_drug_3", "drug_name", "维生素C片", 24, 152, { size: 15 }),
      label("pickup_qty_3", "quantity", "1盒", 318, 152, { size: 15 }),
      label("pickup_position_3", "position", "C07", 400, 152, { size: 15 }),
      label("pickup_state_3", "state", "待取", 504, 152, { size: 15, color: C.orange }),
    ]),
]);

const login = screen("medical_login", "Login", false, [
  header("login", "药师身份认证", { back: true }),
  box("login_card", "login_card", 112, 78, 416, 346,
    { bg: C.surface, border: C.line, radius: 18 }, [
      box("login_icon_box", "login_icon_box", 170, 22, 76, 76,
        { bg: C.purpleSoft, borderWidth: 0, radius: 18 }, [
          imageNode("login_icon", "login_icon", "icon_user.png", 10, 10, 56, 56),
        ]),
      label("login_title", "login_title", "管理药师登录", 0, 112,
        { align: "LV_ALIGN_TOP_MID", size: 24 }),
      label("login_hint", "login_hint", "认证后可进行存药、查阅日志和设备管理", 0, 150,
        { align: "LV_ALIGN_TOP_MID", size: 14, color: C.muted }),
      box("login_staff_box", "staff_box", 42, 188, 332, 44,
        { bg: "#f8faff", border: C.line, radius: 9 }, [
          label("login_staff_label", "staff_label", "药师工号", 14, 13,
            { size: 14, color: C.muted }),
          label("login_staff_value", "staff_value", "PH-001", 210, 12,
            { size: 16, width: 104, textAlign: "LV_TEXT_ALIGN_RIGHT" }),
        ]),
      box("login_password_box", "password_box", 42, 242, 332, 44,
        { bg: "#f8faff", border: C.line, radius: 9 }, [
          imageNode("login_password_icon", "password_icon", "icon_password.png", 12, 10, 24, 24),
          label("login_password_label", "password_label", "登录密码", 46, 13,
            { size: 14, color: C.muted }),
          label("login_password_value", "password_value", "******", 210, 12,
            { size: 16, width: 104, textAlign: "LV_TEXT_ALIGN_RIGHT" }),
        ]),
      button("login_submit", "button_submit", "登录管理台", 42, 300, 332, 42,
        { bg: C.purple }),
    ]),
]);

function adminCard(id, title, subtitle, iconFile, x, accent, soft) {
  const card = box(id, id.replace(/^admin_/, "admin_card_"), x, 160, 188, 206,
    { bg: C.surface, border: C.line, radius: 14 }, [
      box(`${id}_icon_box`, "icon_box", 58, 22, 72, 72,
        { bg: soft, borderWidth: 0, radius: 16 }, [
          imageNode(`${id}_icon`, "icon", iconFile, 8, 8, 56, 56),
        ]),
      label(`${id}_title`, "title", title, 0, 108,
        { align: "LV_ALIGN_TOP_MID", size: 20 }),
      label(`${id}_subtitle`, "subtitle", subtitle, 16, 140,
        { size: 14, color: C.muted, width: 156, height: 38, textAlign: "LV_TEXT_ALIGN_CENTER" }),
      label(`${id}_open`, "open_text", "进入  >", 100, 180,
        { size: 14, color: accent, width: 70, textAlign: "LV_TEXT_ALIGN_RIGHT" }),
    ]);
  card.type = "button";
  return card;
}

const admin = screen("medical_admin", "Admin", false, [
  header("admin", "药师管理台"),
  label("admin_welcome", "welcome", "当前药师：PH-001", 20, 76, { size: 18 }),
  label("admin_hint", "hint", "请选择管理功能", 20, 108, { size: 15, color: C.muted }),
  button("admin_logout", "button_logout", "退出登录", 500, 72, 122, 36,
    { bg: C.purpleSoft, color: C.purple, size: 14 }),
  adminCard("admin_store", "存药管理", "核对药品、坐标和数量", "icon_store.png", 20, C.orange, C.orangeSoft),
  adminCard("admin_logs", "取药日志", "查看整单取药明细记录", "icon_log.png", 226, C.cyan, C.cyanSoft),
  adminCard("admin_device", "系统状态", "摄像头、电机、CAN与网络", "icon_brand.png", 432, C.purple, C.purpleSoft),
]);

function logRow(id, time, order, result, y, color) {
  return box(id, "log_row", 14, y, 274, 64,
    { bg: "#fbfcff", border: "#edf1f7", radius: 9 }, [
      label(`${id}_time`, "time", time, 12, 10, { size: 14, color: C.muted }),
      label(`${id}_order`, "order", order, 66, 10, { size: 14 }),
      label(`${id}_result`, "result", result, 198, 36,
        { size: 14, color, width: 62, textAlign: "LV_TEXT_ALIGN_RIGHT" }),
    ]);
}

const store = screen("medical_store", "Store", false, [
  header("store", "存药管理", { back: true }),
  box("store_drug_card", "drug_card", 16, 68, 608, 84,
    { bg: C.surface, border: C.line, radius: 14 }, [
      box("store_icon_box", "icon_box", 16, 14, 56, 56,
        { bg: C.orangeSoft, borderWidth: 0, radius: 13 }, [
          imageNode("store_icon", "store_icon", "icon_store.png", 4, 4, 48, 48),
        ]),
      label("store_drug_name", "drug_name", "阿莫西林胶囊", 90, 15, { size: 21 }),
      label("store_drug_meta", "drug_meta", "批次 RA8P1-A01  ·  有效期 2028-08", 90, 49,
        { size: 14, color: C.muted }),
      box("store_verified_badge", "verified_badge", 494, 24, 96, 34,
        { bg: C.greenSoft, borderWidth: 0, radius: 8 }, [
          label("store_verified_text", "verified_text", "药品已核验", 0, 8,
            { align: "LV_ALIGN_TOP_MID", size: 14, color: C.green }),
        ]),
    ]),
  box("store_parameter_card", "parameter_card", 16, 164, 294, 282,
    { bg: C.surface, border: C.line, radius: 14 }, [
      label("store_parameter_title", "title", "存药参数", 18, 16, { size: 20 }),
      label("store_position_label", "position_label", "目标药仓", 18, 62,
        { size: 14, color: C.muted }),
      label("store_position", "position", "B12", 190, 58,
        { size: 20, width: 76, textAlign: "LV_TEXT_ALIGN_RIGHT" }),
      label("store_quantity_label", "quantity_label", "入库数量", 18, 106,
        { size: 14, color: C.muted }),
      label("store_quantity", "quantity", "20盒", 190, 102,
        { size: 18, width: 76, textAlign: "LV_TEXT_ALIGN_RIGHT" }),
      label("store_operator_label", "operator_label", "操作药师", 18, 150,
        { size: 14, color: C.muted }),
      label("store_operator", "operator", "PH-001", 174, 146,
        { size: 18, width: 92, textAlign: "LV_TEXT_ALIGN_RIGHT" }),
      button("store_read_drug", "button_read_drug", "重新识别", 18, 212, 112, 42,
        { bg: C.blue }),
      button("store_confirm", "button_confirm", "确认存药", 148, 212, 128, 42,
        { bg: C.orange }),
    ]),
  box("store_warehouse_card", "warehouse_card", 324, 164, 300, 282,
    { bg: C.surface, border: C.line, radius: 14 }, [
      label("store_warehouse_title", "title", "仓位与设备检查", 18, 16, { size: 20 }),
      label("store_capacity_label", "capacity_label", "B12 当前容量", 18, 60,
        { size: 14, color: C.muted }),
      label("store_capacity", "capacity", "8 / 30 盒", 168, 58,
        { size: 18, width: 106, textAlign: "LV_TEXT_ALIGN_RIGHT" }),
      box("store_capacity_track", "capacity_track", 18, 94, 256, 10,
        { bg: "#e5ebf4", borderWidth: 0, radius: 5 }, [
          box("store_capacity_fill", "capacity_fill", 0, 0, 68, 10,
            { bg: C.cyan, borderWidth: 0, radius: 5 }),
        ]),
      label("store_slot_state_label", "slot_state_label", "仓位状态", 18, 132,
        { size: 14, color: C.muted }),
      label("store_slot_state", "slot_state", "可用", 202, 128,
        { size: 18, color: C.green, width: 72, textAlign: "LV_TEXT_ALIGN_RIGHT" }),
      label("store_motor_state_label", "motor_state_label", "机械臂", 18, 172,
        { size: 14, color: C.muted }),
      label("store_motor_state", "motor_state", "待命", 202, 168,
        { size: 18, color: C.green, width: 72, textAlign: "LV_TEXT_ALIGN_RIGHT" }),
      box("store_safety_note", "safety_note", 18, 214, 256, 46,
        { bg: C.orangeSoft, borderWidth: 0, radius: 8 }, [
          label("store_safety_text", "safety_text", "确认后机械臂将执行入库", 0, 14,
            { align: "LV_ALIGN_TOP_MID", size: 14, color: C.orange }),
        ]),
    ]),
]);

function detailLogRow(id, time, order, items, result, duration, y, color) {
  return box(id, "detail_log_row", 14, y, 580, 46,
    { bg: "#fbfcff", border: "#edf1f7", radius: 7 }, [
      label(`${id}_time`, "time", time, 10, 14, { size: 13, color: C.muted }),
      label(`${id}_order`, "order", order, 72, 14, { size: 13 }),
      label(`${id}_items`, "items", items, 256, 14, { size: 13 }),
      label(`${id}_result`, "result", result, 382, 14, { size: 13, color }),
      label(`${id}_duration`, "duration", duration, 494, 14,
        { size: 13, color: C.muted, width: 66, textAlign: "LV_TEXT_ALIGN_RIGHT" }),
    ]);
}

const logs = screen("medical_logs", "Logs", false, [
  header("logs", "取药日志", { back: true }),
  box("logs_total_card", "metric_card", 16, 68, 190, 70,
    { bg: C.blueSoft, borderWidth: 0, radius: 12 }, [
      label("logs_total_label", "metric_label", "今日取药", 16, 12, { size: 14, color: C.muted }),
      label("logs_total_value", "metric_value", "12 单", 16, 36, { size: 20, color: C.blue }),
    ]),
  box("logs_success_card", "metric_card", 224, 68, 190, 70,
    { bg: C.greenSoft, borderWidth: 0, radius: 12 }, [
      label("logs_success_label", "metric_label", "成功完成", 16, 12, { size: 14, color: C.muted }),
      label("logs_success_value", "metric_value", "11 单", 16, 36, { size: 20, color: C.green }),
    ]),
  box("logs_error_card", "metric_card", 432, 68, 192, 70,
    { bg: C.redSoft, borderWidth: 0, radius: 12 }, [
      label("logs_error_label", "metric_label", "异常任务", 16, 12, { size: 14, color: C.muted }),
      label("logs_error_value", "metric_value", "1 单", 16, 36, { size: 20, color: C.red }),
    ]),
  box("logs_table_card", "log_table_card", 16, 150, 608, 312,
    { bg: C.surface, border: C.line, radius: 14 }, [
      imageNode("logs_icon", "log_icon", "icon_log.png", 16, 12, 32, 32),
      label("logs_table_title", "table_title", "最近取药记录", 60, 16, { size: 19 }),
      button("logs_export", "button_export", "日志详情", 472, 10, 118, 34,
        { bg: C.cyanSoft, color: C.cyan, size: 13 }),
      box("logs_table_header", "table_header", 14, 54, 580, 28,
        { bg: "#edf3fb", borderWidth: 0, radius: 6 }, [
          label("logs_header_time", "header_time", "时间", 10, 6, { size: 13, color: C.muted }),
          label("logs_header_order", "header_order", "取药单号", 72, 6, { size: 13, color: C.muted }),
          label("logs_header_items", "header_items", "药品", 256, 6, { size: 13, color: C.muted }),
          label("logs_header_result", "header_result", "结果", 382, 6, { size: 13, color: C.muted }),
          label("logs_header_duration", "header_duration", "用时", 516, 6, { size: 13, color: C.muted }),
        ]),
      detailLogRow("logs_row_1", "10:32", "RX-0811-001", "3种 / 4盒", "已完成", "42秒", 90, C.green),
      detailLogRow("logs_row_2", "10:18", "RX-0811-002", "1种 / 1盒", "已完成", "26秒", 140, C.green),
      detailLogRow("logs_row_3", "09:54", "RX-0811-003", "2种 / 3盒", "异常", "18秒", 190, C.red),
      detailLogRow("logs_row_4", "09:20", "RX-0811-004", "2种 / 2盒", "已完成", "35秒", 240, C.green),
    ]),
]);

function statusRow(prefix, title, value, y, color, iconFile = null) {
  const children = [
    label(`${prefix}_title`, "title", title, iconFile ? 48 : 14, 15, { size: 16 }),
    label(`${prefix}_value`, "value", value, 300, 15,
      { size: 15, color, width: 226, textAlign: "LV_TEXT_ALIGN_RIGHT" }),
  ];
  if (iconFile) children.push(imageNode(`${prefix}_icon`, "status_icon", iconFile, 14, 14, 24, 24));
  return box(`${prefix}_row`, `${prefix}_row`, 20, y, 544, 52,
    { bg: "#fbfcff", border: "#edf1f7", radius: 10 }, children);
}

const device = screen("medical_device", "Device", false, [
  header("device", "设备状态", { back: true }),
  box("device_card", "device_card", 28, 76, 584, 322,
    { bg: C.surface, border: C.line, radius: 16 }, [
      label("device_summary", "summary", "系统状态", 20, 16, { size: 22 }),
      statusRow("device_camera", "摄像头", "就绪 / 15 FPS", 54, C.green),
      statusRow("device_arm_coord", "机械臂坐标", "X:0  Y:0  Z:0", 112, C.cyan),
      statusRow("device_motor", "电机", "待机", 170, C.orange),
      statusRow("device_network", "ESP-01S 网络", "离线", 228, C.red, "icon_offline.png"),
    ]),
  button("device_self_test", "button_self_test", "无线调试", 236, 416, 168, 42,
    { bg: C.purple }),
]);

function loadScreen(target, delay = 0) {
  return {
    type: "load_screen",
    name: "load_screen",
    id: "load_screen",
    screen: target,
    animation: "LV_SCREEN_LOAD_ANIM_FADE_IN",
    time: 180,
    delay,
    is_delete: false,
  };
}

const events = {
  medical_boot: { LV_EVENT_SCREEN_LOADED: { load_screen: loadScreen("medical_home", 1200) } },
  boot_continue: { LV_EVENT_CLICKED: { load_screen: loadScreen("medical_home") } },
  home_pickup: { LV_EVENT_CLICKED: { load_screen: loadScreen("medical_pickup") } },
  home_identify: { LV_EVENT_CLICKED: { load_screen: loadScreen("medical_scan") } },
  home_management: { LV_EVENT_CLICKED: { load_screen: loadScreen("medical_login") } },
  pickup_back: { LV_EVENT_CLICKED: { load_screen: loadScreen("medical_home") } },
  scan_back: { LV_EVENT_CLICKED: { load_screen: loadScreen("medical_home") } },
  medicine_back: { LV_EVENT_CLICKED: { load_screen: loadScreen("medical_home") } },
  login_back: { LV_EVENT_CLICKED: { load_screen: loadScreen("medical_home") } },
  login_submit: { LV_EVENT_CLICKED: { load_screen: loadScreen("medical_admin") } },
  admin_logout: { LV_EVENT_CLICKED: { load_screen: loadScreen("medical_home") } },
  admin_store: { LV_EVENT_CLICKED: { load_screen: loadScreen("medical_store") } },
  admin_logs: { LV_EVENT_CLICKED: { load_screen: loadScreen("medical_logs") } },
  admin_device: { LV_EVENT_CLICKED: { load_screen: loadScreen("medical_device") } },
  store_back: { LV_EVENT_CLICKED: { load_screen: loadScreen("medical_admin") } },
  logs_back: { LV_EVENT_CLICKED: { load_screen: loadScreen("medical_admin") } },
  device_back: { LV_EVENT_CLICKED: { load_screen: loadScreen("medical_admin") } },
  medicine_rescan: { LV_EVENT_CLICKED: { load_screen: loadScreen("medical_scan") } },
};

const systemLayers = project.UI.screen_list.filter((item) => item.type !== "screen");
project.UI.screen_list = [...systemLayers, boot, home, pickup, scan, medicine, login, admin, store, logs, device];
project.UI.event_list = events;
project.UI.variable_setting = { condition: {}, lv_anim_t: {} };
project.projectSettings.fontConfig.default_family = UI_FONT;
project.projectSettings.fontConfig.enable_base_chars = 1;
project.projectSettings.fontConfig.base_chars =
  "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz +-.:|/%<>";
// Keep the original Simulator/Printer metadata pair. In GUI Guider this is the
// installed application template identifier, not the user-facing UI title.
if (project.metadata && project.metadata.application) {
  project.metadata.application.name = "Printer";
}
project.description = "640x480 智慧药品工作站简体中文界面";
project.lastModified = new Date().toISOString();

fs.writeFileSync(projectFile, `${JSON.stringify(project, null, 2)}\n`, "utf8");
console.log(`Rebuilt ${projectFile}`);
console.log(`Screens: ${project.UI.screen_list.filter((item) => item.type === "screen").map((item) => item.name).join(", ")}`);
