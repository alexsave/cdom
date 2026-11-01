// Tables from run.mjs's results: medians over loads per arm (one load per arm per round, the arms
// interleaved), with the 25th-75th percentile, and each arm's median over arm a's.
//   node micro/bridge/summary.mjs <results.jsonl> [metric ...]     metrics default: script total
// Loads that waited past the gate (over) are left out and counted.
import fs from 'node:fs';

const [file, ...want] = process.argv.slice(2);
const metrics = want.length ? want : ['script', 'total'];
const all = fs.readFileSync(file, 'utf8').trim().split('\n').map((l) => JSON.parse(l));
const counted = all.filter((r) => r.mode === 'count');
if (counted.length) { // run.mjs count: import calls / strings decoded (/ op-list records) per flow, the last session
  const c = counted.at(-1), arms = Object.keys(c.arms);
  for (const page of ['port', 'keyed', 'micro']) {
    const segs = [...new Set(arms.flatMap((a) => Object.keys(c.arms[a][page] || {})))];
    if (!segs.length) continue;
    console.log(`\n${page}: import calls / strings decoded${page === 'micro' ? ' per operation' : ' per flow'} (list: + records applied), ${c.chrome}`);
    console.log(`| ${page === 'micro' ? 'op' : 'flow'} | ${arms.join(' | ')} |`);
    console.log(`| --- |${arms.map(() => ' ---: |').join('')}`);
    for (const sg of segs) {
      console.log(`| ${sg} | ${arms.map((a) => { const v = c.arms[a][page]?.[sg]; if (!v) return '-';
        const calls = page === 'micro' ? v.crossings : v.calls; return `${+calls.toFixed(2)} / ${+(v.strings || 0).toFixed(2)}${v.records ? ' (+' + +v.records.toFixed(2) + ')' : ''}`; }).join(' | ')} |`);
    }
  }
  process.exit(0);
}
const rows = all;
const kept = rows.filter((r) => !r.over);
const q = (a, p) => { const s = [...a].sort((x, y) => x - y); return s[Math.min(s.length - 1, Math.floor(s.length * p))]; };
const med = (a) => q(a, 0.5);
const arms = [...new Set(kept.map((r) => r.arm))];
const loads = kept.map((r) => r.load);
console.log(`${file}: ${kept.length} loads (${rows.length - kept.length} over the gate left out), loads ${Math.min(...loads)}-${Math.max(...loads)}, ${rows[0].chrome}, CPU x${rows[0].cpu}${rows[0].flags ? ', ' + rows[0].flags : ''}`);
if (rows[0].mode === 'micro') {
  const kinds = Object.keys(kept[0].micro);
  console.log(`| ns per op | ${arms.join(' | ')} |`);
  console.log(`| --- |${arms.map(() => ' ---: |').join('')}`);
  for (const k of kinds) {
    console.log(`| ${k} | ${arms.map((a) => med(kept.filter((r) => r.arm === a).map((r) => r.micro[k])).toFixed(1)).join(' | ')} |`);
  }
  process.exit(0);
}
const segs = Object.keys(kept[0].windows);
for (const m of metrics) {
  console.log(`\n${m} (ms, median of ${Math.round(kept.length / arms.length)} loads; 25th-75th; / arm a)`);
  console.log(`| window | ${arms.join(' | ')} |`);
  console.log(`| --- |${arms.map(() => ' ---: |').join('')}`);
  for (const s of segs) {
    const base = med(kept.filter((r) => r.arm === 'a').map((r) => r.windows[s][m]));
    const cells = arms.map((a) => {
      const v = kept.filter((r) => r.arm === a).map((r) => r.windows[s][m]);
      const md = med(v);
      const ratio = a === 'a' || !base ? '' : ` (${(md / base).toFixed(2)})`;
      return `${md.toFixed(2)} [${q(v, 0.25).toFixed(2)}-${q(v, 0.75).toFixed(2)}]${ratio}`;
    });
    console.log(`| ${s} | ${cells.join(' | ')} |`);
  }
}
const extra = ['frames', 'long', 'max'];
console.log(`\nframes / long tasks / longest task (ms), medians`);
console.log(`| window | ${arms.join(' | ')} |`);
console.log(`| --- |${arms.map(() => ' ---: |').join('')}`);
for (const s of segs) {
  console.log(`| ${s} | ${arms.map((a) => extra.map((m) => { const v = med(kept.filter((r) => r.arm === a).map((r) => r.windows[s][m])); return m === 'max' ? v.toFixed(1) : v; }).join(' / ')).join(' | ')} |`);
}
