// Simulate the BOARD's LVGL 9.3 decoder (stride computed from box_w in px)
// vs the SIMULATOR's LVGL 9.4 decoder (stride from width_in_bytes) for a glyph.
// Usage: node sim_stride.js <font.c> <unicode>
const fs = require("fs");
const file = process.argv[2];
const target = parseInt(process.argv[3], 16);
const src = fs.readFileSync(file, "utf8");

function parseArray(name, type) {
  const re = new RegExp(`static\\s+[A-Z_ ]*const\\s+${type}\\s+${name}\\[\\]\\s*=\\s*\\{([\\s\\S]*?)\\n\\};`);
  const m = src.match(re);
  if (!m) return null;
  const vals = [];
  for (const v of m[1].matchAll(/0x[0-9a-fA-F]+|\b\d+\b/g)) vals.push(parseInt(v[0], 16));
  return vals;
}
const bitmap = parseArray("glyph_bitmap", "uint8_t");

const dscMarker = "static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {";
const dStart = src.indexOf(dscMarker);
const dBodyStart = src.indexOf("{", dStart) + 1;
const dBodyEnd = src.indexOf("};", dBodyStart);
const dBody = src.slice(dBodyStart, dBodyEnd);
const glyphs = [];
{ let depth = 0, cur = "";
  for (const ch of dBody) {
    if (ch === "{") { depth++; if (depth === 1) cur = ""; }
    else if (ch === "}") { depth--; if (depth === 0) { glyphs.push(cur); cur = ""; continue; } }
    if (depth >= 1) cur += ch;
  }
}
function field(e, name) {
  const m = e.match(new RegExp(`\\.${name}\\s*=\\s*(-?\\d+)`));
  return m ? parseInt(m[1], 10) : null;
}

// find glyph id for unicode
const cmapMarker = "static const lv_font_fmt_txt_cmap_t cmaps[] =";
const cStart = src.indexOf(cmapMarker);
const cBodyStart = src.indexOf("{", cStart) + 1;
const cBodyEnd = src.indexOf("};", cBodyStart);
const cBody = src.slice(cBodyStart, cBodyEnd);
const cmaps = [];
{ let depth = 0, cur = "";
  for (const ch of cBody) {
    if (ch === "{") { depth++; if (depth === 1) cur = ""; }
    else if (ch === "}") { depth--; if (depth === 0) { cmaps.push(cur); cur = ""; continue; } }
    if (depth >= 1) cur += ch;
  }
}
function cfield(e, name) {
  const m = e.match(new RegExp(`\\.${name}\\s*=\\s*([^,}]+)`));
  return m ? m[1].trim() : null;
}
function glyphIdFor(letter) {
  for (const cm of cmaps) {
    const rs = parseInt(cfield(cm, "range_start"), 10);
    const rl = parseInt(cfield(cm, "range_length"), 10);
    const gs = parseInt(cfield(cm, "glyph_id_start"), 10);
    const type = cfield(cm, "type").trim();
    if (letter < rs || letter >= rs + rl) continue;
    const rcp = letter - rs;
    if (type === "LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY") return gs + rcp;
    if (type === "LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL") {
      const list = parseArray(cfield(cm, "glyph_id_ofs_list"), "uint8_t");
      const ofs = list[rcp];
      if (ofs === 0 && letter !== rs) continue;
      return gs + ofs;
    }
    if (type === "LV_FONT_FMT_TXT_CMAP_SPARSE_TINY") {
      const list = parseArray(cfield(cm, "unicode_list"), "uint16_t");
      const idx = list.indexOf(rcp);
      if (idx < 0) continue;
      return gs + idx;
    }
    if (type === "LV_FONT_FMT_TXT_CMAP_SPARSE_FULL") {
      const list = parseArray(cfield(cm, "unicode_list"), "uint16_t");
      const idx = list.indexOf(rcp);
      if (idx < 0) continue;
      const ofsList = parseArray(cfield(cm, "glyph_id_ofs_list"), "uint16_t");
      return gs + ofsList[idx];
    }
  }
  return -1;
}

const gid = glyphIdFor(target);
if (gid < 0) { console.log("glyph not found"); process.exit(1); }
const e = glyphs[gid];
const bi = field(e, "bitmap_index");
const bw = field(e, "box_w");
const bh = field(e, "box_h");
console.log(`U+${target.toString(16).toUpperCase()} glyph ${gid}: ${bw}x${bh} bi=${bi}`);

// Decode with a given per-row stride (bytes), bpp=4, mirroring LVGL exactly:
// line_rem starts at strideIn; each consumed byte decrements it; after the row
// bitmap_in advances by line_rem (total strideIn bytes per row).
function decode(strideIn) {
  const out = [];
  let p = bi;
  for (let y = 0; y < bh; y++) {
    let line = [];
    let line_rem = strideIn;
    for (let x = 0; x < bw; x++) {
      const v = x % 2 === 0 ? (bitmap[p] >> 4) & 0xf : bitmap[p] & 0xf;
      if (x % 2 === 1) { line_rem--; p++; }
      line.push(v >= 8 ? "##" : v >= 4 ? "++" : v > 0 ? ".." : "  ");
    }
    out.push(line.join(""));
    p += line_rem;
  }
  return out.join("\n");
}

console.log("\n===== SIMULATOR LVGL 9.4 (stride = ROUND_UP(ceil(bw*bpp/8), 16)) =====");
const wBytes = Math.ceil((bw * 4) / 8);
const stride9_4 = Math.ceil(wBytes / 16) * 16;
console.log(`width_in_bytes=${wBytes} stride=${stride9_4}`);
console.log(decode(stride9_4));

console.log("\n===== BOARD LVGL 9.3 (stride = ROUND_UP(bw, 16)) =====");
const stride9_3 = Math.ceil(bw / 16) * 16;
console.log(`stride=${stride9_3}`);
console.log(decode(stride9_3));
