// Analyze the .guiguider design: list every label's text_size and its parent
// widget's width/height, to plan a safe "+2px bold" size bump.
const fs = require("fs");
const path = require("path");
const guiRoot = path.resolve("gui/RA8P1");
const project = JSON.parse(fs.readFileSync(path.join(guiRoot, "RA8P1.guiguider"), "utf8"));
const familyFile = "SourceHanSerifSC.otf";

const rows = [];
function walk(node, parentSize) {
  if (!node || typeof node !== "object") return;
  const st = node.style?.["LV_PART_MAIN|LV_STATE_DEFAULT"] || {};
  const w = st.width != null ? st.width : (node.width != null ? node.width : null);
  const h = st.height != null ? st.height : (node.height != null ? node.height : null);
  const size = st.text_size;
  if (node.type === "label") {
    const fam = st.text_family;
    rows.push({
      id: node.id, name: node.name, size,
      family: fam, text: (node.text || "").replace(/\n/g, "\\n"),
      parent: parentSize ? `${parentSize.w}x${parentSize.h}` : "?",
      own: w != null || h != null ? `${w ?? "-"}x${h ?? "-"}` : "content",
    });
  }
  const cur = { w, h };
  for (const v of Object.values(node)) if (v && typeof v === "object") walk(v, cur);
}
walk(project.UI.screen_list, null);

const bySize = new Map();
for (const r of rows) {
  if (!bySize.has(r.size)) bySize.set(r.size, []);
  bySize.get(r.size).push(r);
}
for (const [size, list] of [...bySize].sort((a, b) => a[0] - b[0])) {
  console.log(`\n===== size ${size} (${list.length} labels) =====`);
  for (const r of list) {
    console.log(`  ${r.id} [${r.name}] parent=${r.parent} own=${r.own} family=${r.family} text="${r.text}"`);
  }
}
