// Check glyph coverage: every label text in the .guiguider design + runtime
// strings set by custom.c/gui_app.c must be covered by the generated fonts.
// Usage: node check_font_coverage.js
const fs = require("fs");
const path = require("path");

const guiRoot = path.resolve(__dirname, "../gui/RA8P1");
const project = JSON.parse(fs.readFileSync(path.join(guiRoot, "RA8P1.guiguider"), "utf8"));
const fontDir = path.join(guiRoot, "generated/assets/fonts");
const familyFile = "SourceHanSerifSC.otf";

// --- parse one generated font .c: return {size, chars:Set, glyphIds:Map} ---
function parseFont(file) {
  const src = fs.readFileSync(file, "utf8");
  const m = src.match(/static const uint8_t glyph_id_ofs_list_0\[\] = \{([\s\S]*?)\n\};/);
  const cmaps = [];
  const cmapRe = /\{[\s\S]*?\.range_start\s*=\s*(\d+)[\s\S]*?\.range_length\s*=\s*(\d+)[\s\S]*?\.glyph_id_start\s*=\s*(\d+)[\s\S]*?\.unicode_list\s*=\s*([^,]+)[\s\S]*?\.glyph_id_ofs_list\s*=\s*([^,]+)[\s\S]*?\.list_length\s*=\s*(\d+)[\s\S]*?\.type\s*=\s*([^,}]+)/g;
  let cm;
  while ((cm = cmapRe.exec(src))) {
    cmaps.push({
      range_start: +cm[1], range_length: +cm[2], glyph_id_start: +cm[3],
      unicode_list: cm[4].trim(), glyph_id_ofs_list: cm[5].trim(),
      list_length: +cm[6], type: cm[7].trim(),
    });
  }
  function parseArray(name, type) {
    const re = new RegExp(`static\\s+[A-Z_ ]*const\\s+${type}\\s+${name}\\[\\]\\s*=\\s*\\{([\\s\\S]*?)\\n\\};`);
    const mm = src.match(re);
    if (!mm) return [];
    const vals = [];
    for (const v of mm[1].matchAll(/0x[0-9a-fA-F]+|\b\d+\b/g)) vals.push(parseInt(v[0], 16));
    return vals;
  }
  const chars = new Set();
  for (const c of cmaps) {
    if (c.type === "LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY") {
      for (let i = 0; i < c.range_length; i++) chars.add(c.range_start + i);
    } else if (c.type === "LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL") {
      const list = parseArray(c.glyph_id_ofs_list, "uint8_t");
      for (let i = 0; i < list.length; i++) {
        if (list[i] !== 0 || i === 0) chars.add(c.range_start + i);
      }
    } else if (c.type === "LV_FONT_FMT_TXT_CMAP_SPARSE_TINY" || c.type === "LV_FONT_FMT_TXT_CMAP_SPARSE_FULL") {
      const list = parseArray(c.unicode_list, "uint16_t");
      for (const v of list) chars.add(c.range_start + v);
    }
  }
  return { size: parseInt(path.basename(file).match(/_(\d+)\.c$/)[1], 10), chars };
}

// --- collect all labels from the design ---
const labelsBySize = new Map(); // size -> Set of chars
function walk(node) {
  if (!node || typeof node !== "object") return;
  if (node.type === "label") {
    const style = node.style?.["LV_PART_MAIN|LV_STATE_DEFAULT"];
    if (style?.text_family === familyFile && node.text) {
      const size = style.text_size;
      const set = labelsBySize.get(size) || new Set();
      for (const ch of node.text) set.add(ch);
      labelsBySize.set(size, set);
    }
  }
  for (const v of Object.values(node)) if (v && typeof v === "object") walk(v);
}
walk(project.UI.screen_list);

// --- runtime strings from custom.c / gui_app.c ---
const srcRoot = path.resolve(__dirname, "../src");
function collectRuntimeStrings() {
  const files = [
    path.join(srcRoot, "app/src/gui_app.c"),
    path.join(guiRoot, "custom/custom.c"),
  ];
  const out = [];
  for (const f of files) {
    if (!fs.existsSync(f)) continue;
    const s = fs.readFileSync(f, "utf8");
    for (const m of s.matchAll(/"([^"\n]{2,})"/g)) out.push(m[1]);
  }
  return out;
}
const runtimeStrings = collectRuntimeStrings();
const runtimeBySize = new Map();
for (const s of runtimeStrings) {
  // find which font size this string will render with — heuristic: match
  // against labels with same text in generated screens
  const size = 14; // fallback: check against every font
  const set = runtimeBySize.get(size) || new Set();
  for (const ch of s) set.add(ch);
  runtimeBySize.set(size, set);
}

// --- load fonts ---
const fonts = new Map();
for (const f of fs.readdirSync(fontDir).filter((x) => x.endsWith(".c"))) {
  const ff = parseFont(path.join(fontDir, f));
  fonts.set(ff.size, ff);
}

let issues = 0;
console.log("=== Design labels coverage ===");
for (const [size, chars] of [...labelsBySize].sort((a, b) => a[0] - b[0])) {
  const font = fonts.get(size);
  if (!font) { console.log(`size ${size}: NO FONT FILE`); issues++; continue; }
  const missing = [...chars].filter((c) => c !== "\n" && !font.chars.has(c.codePointAt(0)));
  if (missing.length) {
    console.log(`size ${size}: MISSING ${missing.length}: ${missing.map((c) => `${c}(U+${c.codePointAt(0).toString(16).toUpperCase()})`).join(" ")}`);
    issues++;
  } else {
    console.log(`size ${size}: OK (${chars.size} chars)`);
  }
}

console.log("\n=== Runtime strings vs every font ===");
for (const s of runtimeStrings) {
  const missingAny = [];
  for (const [size, font] of fonts) {
    const miss = [...s].filter((c) => c !== "\n" && !font.chars.has(c.codePointAt(0)));
    if (miss.length) missingAny.push(`size${size}:${miss.map((c) => c.codePointAt(0).toString(16).toUpperCase()).join("")}`);
  }
  if (missingAny.length) {
    console.log(`"${s}" -> ${missingAny.join("  ")}`);
    issues++;
  } else {
    console.log(`"${s}" -> OK`);
  }
}

console.log(`\n${issues === 0 ? "ALL COVERED" : issues + " ISSUE(S)"}`);
