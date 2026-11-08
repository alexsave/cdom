#!/bin/bash
# First paint and bytes from the harness's own size run (40_), R rounds interleaved across the
# entries (one load per round each), each start load-gated; prints median first paint per entry.
# Usage: TAG=x R=11 FWS="vanillajs cdomb cdomp" tools/fpruns.sh
set -u
top="$(cd "$(dirname "$0")/.." && pwd)"
for r in $(seq 1 ${R:-11}); do TAG="${TAG:?TAG}/r$r" COUNT= "$top/tools/pair.sh" 40_ > /dev/null 2>&1; done
node -e '
const fs = require("fs"), dir = process.argv[1], fws = process.argv[2].split(" ");
const v = {}, sz = {}, loads = [];
for (const r of fs.readdirSync(dir)) {
  if (!r.startsWith("r")) continue;
  for (const l of fs.readFileSync(`${dir}/${r}/loads.tsv`, "utf8").trim().split("\n").slice(1)) loads.push(+l.split("\t")[2]);
  for (const f of fs.readdirSync(`${dir}/${r}/results`)) {
    const fw = fws.find(x => f.startsWith(x + "-")); if (!fw) continue;
    const j = JSON.parse(fs.readFileSync(`${dir}/${r}/results/${f}`, "utf8"));
    if (f.includes("43_first-paint")) (v[fw] ||= []).push(j.values.DEFAULT?.median ?? j.values.total?.median ?? Object.values(j.values)[0].median);
    if (f.includes("42_size-compressed")) sz[fw] = Object.values(j.values)[0].median;
  }
}
const med = a => { const s = [...a].sort((x, y) => x - y); return s[s.length >> 1]; };
const m0 = med(v[fws[0]]);
for (const f of fws) console.log(f.padEnd(12), "first paint median", med(v[f]).toFixed(1), "ratio", (med(v[f]) / m0).toFixed(3), "n", v[f].length, "compressed KB", sz[f], "| all", v[f].map(x => x.toFixed(0)).join(" "));
console.log("loads at start: min", Math.min(...loads), "max", Math.max(...loads));
' "$top/results/pairs/$TAG" "${FWS:-vanillajs cdomp}"
