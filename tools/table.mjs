// Reads webdriver-ts/results and prints medians side by side plus the weighted geometric mean
// (factor = median / fastest median among the listed frameworks, CPU weights from webdriver-ts-results).
import fs from 'node:fs';
const dir = process.argv[2] || new URL('../jfb/webdriver-ts/results/', import.meta.url).pathname;
const fws = (process.argv[3] || 'vanillajs-keyed,react-hooks-v19.2.0-keyed,cdom-v0.1.0-keyed,dioxus-v0.7.2-keyed,wasm-bindgen-v0.2.84-keyed').split(',');
const short = f => f.replace(/-v[\d.]+-keyed$|-keyed$/, '');
const cpu = ['01_run1k', '02_replace1k', '03_update10th1k_x16', '04_select1k', '05_swap1k', '06_remove-one-1k', '07_create10k', '08_create1k-after1k_x2', '09_clear1k_x8'];
const W = [0.64280248137063, 0.5607178150466176, 0.5643800750716564, 0.1925635870170522, 0.13200612879341714, 0.5277091212292658, 0.5644449600965534, 0.5508359820582848, 0.4225836631419211];
const mem = ['21_ready-memory', '22_run-memory', '23_update5-memory', '24_run5-memory', '25_run-clear-memory', '26_run-10k-memory'];
// Optional loads.tsv (from tools/final.sh): cells show "value @load-before".
const loadFile = process.argv[4], tag = process.argv[5] || '';
const loads = new Map();
if (loadFile) for (const l of fs.readFileSync(loadFile, 'utf8').trim().split('\n').slice(1)) {
  const [b, f, before, after, , , t] = l.split('\t');
  if ((t || '') === tag) loads.set(`${b}|${f}`, before);
}
const dirOf = f => f.replace(/-v[\d.]+-keyed$|-keyed$/, '');
const at = (b, f) => { const l = loads.get(`${b.slice(0, 3)}|${dirOf(f)}`); return l ? ` @${l}` : ''; };
const read = (f, b) => { try { return JSON.parse(fs.readFileSync(`${dir}/${f}_${b}.json`, 'utf8')); } catch { return null; } };
const med = (f, b, k = 'total') => read(f, b)?.values?.[k]?.median;
const fmt = v => v === undefined ? '-' : v.toFixed(v < 10 ? 2 : 1);
const out = [];
const row = cells => out.push('| ' + cells.join(' | ') + ' |');
row(['benchmark (ms, median)', ...fws.map(short)]); row(['---', ...fws.map(() => '---:')]);
const logs = fws.map(() => 0); let wsum = 0;
cpu.forEach((b, i) => {
  const v = fws.map(f => med(f, b));
  const best = Math.min(...v.filter(x => x !== undefined));
  row([b, ...v.map((x, j) => x === undefined ? '-' : `${fmt(x)} (${(x / best).toFixed(2)})${at(b, fws[j])}`)]);
  v.forEach((x, j) => { if (x !== undefined) logs[j] += W[i] * Math.log(x / best); });
  wsum += W[i];
});
row(['**weighted geometric mean**', ...logs.map(l => `**${Math.exp(l / wsum).toFixed(2)}**`)]);
for (const b of mem) { const v = fws.map(f => med(f, b, 'DEFAULT')); if (v.some(x => x !== undefined)) row([b + ' (MB)', ...v.map((x, j) => fmt(x) + at(b, fws[j]))]); }
const st = fws.map(f => read(f, '30_startup')?.values);
if (st.some(Boolean)) for (const k of Object.keys(st.find(Boolean))) row(['30_startup ' + k, ...st.map(s => fmt(s?.[k]?.median))]);
const sz = fws.map(f => read(f, '40_sizes')?.values);
if (sz.some(Boolean)) for (const k of Object.keys(sz.find(Boolean))) row(['40_sizes ' + k, ...sz.map(s => fmt(s?.[k]?.median))]);
console.log(out.join('\n'));
