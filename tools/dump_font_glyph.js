// Dump glyph bitmaps from a generated LVGL font .c file and render ASCII art.
// Usage: node dump_font_glyph.js <font.c> <unicode1> [unicode2...]
const fs = require("fs");

const file = process.argv[2];
const targets = process.argv.slice(3).map((s) => parseInt(s, 16));
const src = fs.readFileSync(file, "utf8");

// --- parse glyph_bitmap array ---
function parseArray(name, type) {
  const re = new RegExp(`static\\s+[A-Z_ ]*const\\s+${type}\\s+${name}\\[\\]\\s*=\\s*\\{`);
  const markerMatch = src.match(re);
  const marker = markerMatch ? markerMatch[0] : null;
  if (!marker) throw new Error(`array ${name} not found`);
  const start = src.indexOf(marker);
  if (start < 0) throw new Error(`array ${name} not found`);
  const bodyStart = src.indexOf("{", start) + 1;
  const bodyEnd = src.indexOf("};", bodyStart);
  const body = src.slice(bodyStart, bodyEnd);
  const values = [];
  for (const m of body.matchAll(/0x[0-9a-fA-F]+|\b\d+\b/g)) {
    values.push(parseInt(m[0], 16));
  }
  return values;
}

const bitmap = parseArray("glyph_bitmap", "uint8_t");

// --- parse glyph_dsc array (struct with named initializers) ---
const dscMarker = "static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {";
const dStart = src.indexOf(dscMarker);
const dBodyStart = src.indexOf("{", dStart) + 1;
const dBodyEnd = src.indexOf("};", dBodyStart);
const dBody = src.slice(dBodyStart, dBodyEnd);

const glyphs = [];
// split top-level entries: each entry is { .bitmap_index = N, .adv_w = N, ... }
let depth = 0, cur = "";
for (const ch of dBody) {
  if (ch === "{") { depth++; if (depth === 1) cur = ""; }
  else if (ch === "}") { depth--; if (depth === 0) { glyphs.push(cur); cur = ""; continue; } }
  if (depth >= 1) cur += ch;
}

function field(entry, name) {
  const m = entry.match(new RegExp(`\\.${name}\\s*=\\s*(-?\\d+)`));
  return m ? parseInt(m[1], 10) : null;
}

// --- parse cmaps ---
const cmapMarker = "static const lv_font_fmt_txt_cmap_t cmaps[] =";
const cStart = src.indexOf(cmapMarker);
const cBodyStart = src.indexOf("{", cStart) + 1;
const cBodyEnd = src.indexOf("};", cBodyStart);
const cBody = src.slice(cBodyStart, cBodyEnd);
const cmaps = [];
{
  let depth = 0, cur = "";
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

// glyph_id resolution per unicode, mirroring lv_font_fmt_txt.c get_glyph_dsc_id
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
      // glyph_id_ofs_list array
      const name = cfield(cm, "glyph_id_ofs_list");
      const list = parseArray(name, "uint8_t");
      const ofs = list[rcp];
      if (ofs === 0 && letter !== rs) continue;
      return gs + ofs;
    }
    if (type === "LV_FONT_FMT_TXT_CMAP_SPARSE_TINY") {
      const name = cfield(cm, "unicode_list");
      const list = parseArray(name, "uint16_t");
      const idx = list.indexOf(rcp);
      if (idx < 0) continue;
      return gs + idx;
    }
    if (type === "LV_FONT_FMT_TXT_CMAP_SPARSE_FULL") {
      const name = cfield(cm, "unicode_list");
      const list = parseArray(name, "uint16_t");
      const idx = list.indexOf(rcp);
      if (idx < 0) continue;
      const ofsName = cfield(cm, "glyph_id_ofs_list");
      const ofsList = parseArray(ofsName, "uint16_t");
      return gs + ofsList[idx];
    }
  }
  return -1;
}

function renderGlyph(gid) {
  if (gid < 0 || gid >= glyphs.length) return `(glyph ${gid} out of range, total ${glyphs.length})`;
  const e = glyphs[gid];
  const bi = field(e, "bitmap_index");
  const bw = field(e, "box_w");
  const bh = field(e, "box_h");
  const ox = field(e, "ofs_x");
  const oy = field(e, "ofs_y");
  const adv = field(e, "adv_w");
  if (bw <= 0 || bh <= 0) return `(empty glyph: adv=${adv})`;
  // bpp = 4 (from font_dsc), stride 16: rows padded to 16 bytes, 2 px per byte
  const bpp = 4;
  const pxPerByte = 8 / bpp;
  const stride = 16;
  let out = `glyph ${gid}: ${bw}x${bh} ofs(${ox},${oy}) adv=${adv} bitmap_index=${bi}\n`;
  for (let y = 0; y < bh; y++) {
    let line = "";
    for (let x = 0; x < bw; x++) {
      const byteIdx = bi + y * stride + Math.floor(x / pxPerByte);
      const v = (bitmap[byteIdx] >> (x % 2 === 0 ? 4 : 0)) & 0x0f;
      line += v >= 8 ? "##" : v >= 4 ? "++" : v > 0 ? ".." : "  ";
    }
    out += line + "\n";
  }
  return out;
}

for (const u of targets) {
  console.log(`===== U+${u.toString(16).toUpperCase().padStart(4, "0")} (${String.fromCodePoint(u)}) =====`);
  const gid = glyphIdFor(u);
  console.log(`glyph_id = ${gid}`);
  console.log(renderGlyph(gid));
}
