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
    console.log(`\n${page}: import calls / strings decoded${page === 'micro' ? ' per operation' : ' per flow'} (list: + op-list records applied; d: + calls into wasm to mirror references), ${c.chrome}`);
    console.log(`| ${page === 'micro' ? 'op' : 'flow'} | ${arms.join(' | ')} |`);
    console.log(`| --- |${arms.map(() => ' ---: |').join('')}`);
    for (const sg of segs) {
      console.log(`| ${sg} | ${arms.map((a) => { const v = c.arms[a][page]?.[sg]; if (!v) return '-';
        const calls = page === 'micro' ? v.crossings : v.calls; return `${+calls.toFixed(2)} / ${+(v.strings || 0).toFixed(2)}${v.records ? ' (+' + +v.records.toFixed(2) + ' records)' : ''}${v.exports ? ' (+' + +v.exports.toFixed(2) + ' exports)' : ''}`; }).join(' | ')} |`);
    }
  }
  process.exit(0);
}
const rows = all.filter((r) => !r.cut);
const cut = all.length - rows.length;
// Left out: loads marked busy (other processes over OTHERS_MAX), invalid (a window's check failed),
// or, in older files, over the load gate.
const kept = rows.filter((r) => !r.busy && !r.over && !(r.invalid && r.invalid.length));
if (!kept.length) { console.log(`${file}: no load kept (${rows.length} loads, ${cut} cut, all busy or invalid)`); process.exit(0); }
// Quantiles by linear interpolation (type 7), so an even count's median is the mean of the middle two.
const q = (a, p) => { const s = [...a].sort((x, y) => x - y); const h = (s.length - 1) * p, lo = Math.floor(h); return s[lo] + (s[Math.min(lo + 1, s.length - 1)] - s[lo]) * (h - lo); };
const med = (a) => q(a, 0.5);
const ORDER = ['a', 'list', 'b', 'c', 'd', 'e', 'f', 'e_t', 'e_css'];
const arms = [...new Set(kept.map((r) => r.arm))].sort((x, y) => ORDER.indexOf(x) - ORDER.indexOf(y));
const per = Object.fromEntries(arms.map((a) => [a, kept.filter((r) => r.arm === a)]));
const loads = kept.map((r) => r.load);
const reasons = rows.filter((r) => !kept.includes(r)).map((r) => `${r.arm} r${r.round}: ${r.busy ? 'busy ' + r.others + '%' : r.over ? 'over' : r.invalid.join('; ')}`);
console.log(`${file}: ${kept.length} of ${rows.length} loads kept${cut ? `, ${cut} cut by the budget` : ''}; per arm ${arms.map((a) => `${a} ${per[a].length}`).join(', ')}; load averages ${Math.min(...loads)}-${Math.max(...loads)}; ${rows[0].chrome}, CPU x${rows[0].cpu}${rows[0].flags ? ', ' + rows[0].flags : ''}`);
for (const r of reasons) console.log(`  left out: ${r}`);
const head = (label) => { console.log(`| ${label} | ${arms.map((a) => `${a} (n=${per[a].length})`).join(' | ')} |`); console.log(`| --- |${arms.map(() => ' ---: |').join('')}`); };
if (rows[0].mode === 'micro') {
  const kinds = Object.keys(kept[0].micro);
  head('ns per op');
  for (const k of kinds) console.log(`| ${k} | ${arms.map((a) => med(per[a].map((r) => r.micro[k])).toFixed(1)).join(' | ')} |`);
  process.exit(0);
}
const segs = Object.keys(kept[0].windows);
for (const m of metrics) {
  console.log(`\n${m} (ms, median over loads; 25th-75th; / arm a)`);
  head('window');
  for (const s of segs) {
    const base = med((per.a || []).map((r) => r.windows[s][m]));
    const cells = arms.map((a) => {
      const v = per[a].map((r) => r.windows[s][m]);
      const md = med(v);
      const ratio = a === 'a' || !base ? '' : ` (${(md / base).toFixed(2)})`;
      return `${md.toFixed(2)} [${q(v, 0.25).toFixed(2)}-${q(v, 0.75).toFixed(2)}]${ratio}`;
    });
    console.log(`| ${s} | ${cells.join(' | ')} |`);
  }
}
const extra = ['frames', 'long', 'max'];
console.log(`\nframes / long tasks / longest task (ms), medians`);
head('window');
for (const s of segs) {
  console.log(`| ${s} | ${arms.map((a) => extra.map((m) => { const v = med(per[a].map((r) => r.windows[s][m])); return m === 'max' ? v.toFixed(1) : v; }).join(' / ')).join(' | ')} |`);
}
