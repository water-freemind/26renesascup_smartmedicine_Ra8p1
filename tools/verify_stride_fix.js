// Verify: with the FIXED stride formula (width_in_bytes), no glyph's data
// region extends past the bitmap array (the old buggy formula overruns for
// glyphs wider than 16px, spilling into the next glyph).
const fs = require("fs");
const path = require("path");
const dir = "gui/RA8P1/generated/assets/fonts";

function parseArray(src, name, type) {
  const re = new RegExp(`static\\s+[A-Z_ ]*const\\s+${type}\\s+${name}\\[\\]\\s*=\\s*\\{([\\s\\S]*?)\\n\\};`);
  const m = src.match(re);
  if (!m) return null;
  const vals = [];
  for (const v of m[1].matchAll(/0x[0-9a-fA-F]+|\b\d+\b/g)) vals.push(parseInt(v[0], 16));
  return vals;
}

let totalBad = 0;
for (const f of fs.readdirSync(dir).filter((x) => x.endsWith(".c"))) {
  const src = fs.readFileSync(path.join(dir, f), "utf8");
  const bitmap = parseArray(src, "glyph_bitmap", "uint8_t");
  if (!bitmap) continue;
  const dscM = src.match(/static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc\[\] = \{([\s\S]*?)\n\};/);
  if (!dscM) continue;
  let depth = 0, cur = "", gids = [];
  for (const ch of dscM[1]) {
    if (ch === "{") { depth++; if (depth === 1) cur = ""; }
    else if (ch === "}") { depth--; if (depth === 0) { gids.push(cur); cur = ""; continue; } }
    if (depth >= 1) cur += ch;
  }
  let bad = 0, worst = "";
  for (let i = 1; i < gids.length; i++) {
    const g = gids[i];
    const biM = g.match(/\.bitmap_index\s*=\s*(\d+)/);
    const bwM = g.match(/\.box_w\s*=\s*(\d+)/);
    const bhM = g.match(/\.box_h\s*=\s*(\d+)/);
    if (!biM || !bwM || !bhM) continue;
    const bi = +biM[1], bw = +bwM[1], bh = +bhM[1];
    const wBytes = Math.ceil((bw * 4) / 8);
    const stride = Math.ceil(wBytes / 16) * 16;
    const need = bi + bh * stride;
    if (need > bitmap.length) { bad++; worst = `glyph${i}:bi=${bi} ${bw}x${bh}`; }
  }
  if (bad) { totalBad += bad; console.log(`${f}: ${bad} glyphs overrun (${worst})`); }
}
console.log(`total overrun: ${totalBad}  -> ${totalBad === 0 ? "OK: fixed stride never overruns" : "STILL BROKEN"}`);
