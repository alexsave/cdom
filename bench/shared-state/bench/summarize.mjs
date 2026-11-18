// Reads ../results/times.tsv, memory.tsv and crossings.tsv and writes ../results/summary.tsv: per
// (scenario, entry, series) the median of the pass medians, their min and max (the spread), the passes, K
// and the samples per pass; per entry the S7 memory per row (touched and grown, minus L's) and the S10
// pages; and the crossings of one operation. Prints the same. Usage: node bench/shared-state/bench/summarize.mjs
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
const res = process.env.RES || path.join(path.dirname(fileURLToPath(import.meta.url)), '../results');
const tsv = (f) => { const [h, ...r] = fs.readFileSync(path.join(res, f), 'utf8').trim().split('\n'); const k = h.split('\t'); return r.map((l) => Object.fromEntries(l.split('\t').map((v, i) => [k[i], v]))); };
const median = (v) => { const s = [...v].sort((a, b) => a - b); return s.length % 2 ? s[s.length >> 1] : (s[s.length / 2 - 1] + s[s.length / 2]) / 2; };
const out = [];
const put = (...a) => { out.push(a.join('\t')); console.log(a.join('\t')); };
const times = tsv('times.tsv');
const cross = fs.existsSync(path.join(res, 'crossings.tsv')) ? tsv('crossings.tsv') : [];
const order = [];
const groups = new Map();
for (const r of times) {
  const key = `${r.scenario}\t${r.entry}\t${r.series}`;
  if (!groups.has(key)) { groups.set(key, []); order.push(key); }
  groups.get(key).push(r);
}
put('scenario', 'entry', 'series', 'median_of_pass_medians_ms', 'min_ms', 'max_ms', 'passes', 'K', 'samples_per_pass', 'failed', 'crossings', 'dom_writes', 'other');
for (const key of order) {
  const g = groups.get(key), [sc, e, series] = key.split('\t');
  const ok = g.filter((r) => r.median_ms !== '' && !isNaN(+r.median_ms));
  const bad = g.filter((r) => !ok.includes(r)).map((r) => r.median_ms);
  const m = ok.map((r) => +r.median_ms);
  const c = cross.find((x) => x.scenario === sc && x.entry === e && (series.startsWith('clear') ? x.op === 'clear' : x.op !== 'clear'));
  put(sc, e, series, m.length ? median(m).toFixed(5) : '', m.length ? Math.min(...m).toFixed(5) : '', m.length ? Math.max(...m).toFixed(5) : '',
    m.length, [...new Set(ok.map((r) => r.K))].join('/'), [...new Set(ok.map((r) => r.n))].join('/'), bad.join(' '), c ? c.crossings : '', c ? c.dom_writes : '', c ? c.other : '');
}
if (fs.existsSync(path.join(res, 'memory.tsv'))) {
  const mem = tsv('memory.tsv');
  const by = (sc, e) => mem.filter((r) => r.scenario === sc && r.entry === e);
  const d = (rs, f) => median(rs.map(f));
  const s7 = (e) => { const rs = by('S7', e); return rs.length ? { touched: d(rs, (r) => (r.touchedKB_after - r.touchedKB_before) * 1024), grown: d(rs, (r) => (r.pages_after - r.pages_before) * 65536), size: +rs[0].row_struct_bytes, n: rs.length } : null; };
  const L = s7('L');
  put('');
  put('S7 memory, op_build(10000) on a fresh page', 'entry', 'row_struct_bytes', 'touched_bytes_per_row', 'touched_minus_L_per_row', 'grown_bytes_per_row', 'grown_minus_L_per_row', 'passes');
  for (const e of ['A', 'B', 'C', 'E', 'L']) {
    const x = s7(e); if (!x) continue;
    put('S7mem', e, x.size, (x.touched / 1e4).toFixed(1), L ? ((x.touched - L.touched) / 1e4).toFixed(1) : '', (x.grown / 1e4).toFixed(1), L ? ((x.grown - L.grown) / 1e4).toFixed(1) : '', x.n);
  }
  put('');
  put('S10 memory after 5 keystrokes on a 1 MB textarea', 'entry', 'pages_median', 'pages_min', 'pages_max', 'touchedKB_median', 'passes');
  for (const e of ['A', 'C', 'E']) {
    const rs = by('S10', e); if (!rs.length) continue;
    const p = rs.map((r) => +r.pages_after);
    put('S10mem', e, median(p), Math.min(...p), Math.max(...p), median(rs.map((r) => +r.touchedKB_after)), rs.length);
  }
}
fs.writeFileSync(path.join(res, 'summary.tsv'), out.join('\n') + '\n');
