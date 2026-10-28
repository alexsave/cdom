// Ratios with 95% confidence intervals over paired sessions (proof of why direct calls win).
// For each session (results/pairs/proof/<arm>/s<N>) and benchmark: the harness's median of
// entry A divided by entry B's in the same session; then the mean of the log ratios over the
// sessions with a t interval (n = sessions), and the same for the weighted geometric mean over
// the benchmarks (CPU weights from webdriver-ts-results, as tools/ratio.mjs) computed per session.
// Usage: node exp/ci.mjs <arm dir> A/B [A/B ...] [--key total|script]
import fs from 'node:fs';
const args = process.argv.slice(2);
const key = args.includes('--key') ? args[args.indexOf('--key') + 1] : 'total';
const dir = args[0], pairs = args.slice(1).filter((a, i, all) => a.includes('/') && all[i] !== key);
const W = { '01_run1k': 0.64280248137063, '07_create10k': 0.5644449600965534, '09_clear1k_x8': 0.4225836631419211 };
const T = [0, 12.706, 4.303, 3.182, 2.776, 2.571, 2.447, 2.365, 2.306, 2.262, 2.228, 2.201, 2.179, 2.160, 2.145, 2.131, 2.120, 2.110, 2.101, 2.093, 2.086];
const sessions = fs.readdirSync(dir).filter(s => /^s\d+$/.test(s)).sort((a, b) => a.slice(1) - b.slice(1));
const read = (s, f, b) => { const d = `${dir}/${s}/results`; if (!fs.existsSync(d)) return; const n = fs.readdirSync(d).find(x => x.startsWith(`${f}-`) && x.endsWith(`_${b}.json`)); return n ? JSON.parse(fs.readFileSync(`${d}/${n}`, 'utf8')).values[key]?.median : undefined; };
const loads = [];
for (const s of sessions) { try { for (const l of fs.readFileSync(`${dir}/${s}/loads.tsv`, 'utf8').trim().split('\n').slice(1)) { const c = l.split('\t'); if (c[5] === 'ok') loads.push(+c[2]); } } catch {} }
const ci = (xs) => { const n = xs.length, m = xs.reduce((a, b) => a + b, 0) / n; if (n < 2) return [m, NaN, NaN, n]; const sd = Math.sqrt(xs.reduce((a, b) => a + (b - m) ** 2, 0) / (n - 1)); const h = (T[n - 1] || 1.96) * sd / Math.sqrt(n); return [m, m - h, m + h, n]; };
const fmt = ([m, lo, hi, n]) => `${Math.exp(m).toFixed(3)} [${Math.exp(lo).toFixed(3)}, ${Math.exp(hi).toFixed(3)}] n=${n}`;
console.log(`${dir} (${key}; loads at start ${Math.min(...loads)}-${Math.max(...loads)}, ${loads.length} starts)`);
console.log('| ratio | ' + Object.keys(W).join(' | ') + ' | weighted geomean |');
console.log('| --- |' + ' ---: |'.repeat(Object.keys(W).length + 1));
for (const p of pairs) {
  const [A, B] = p.split('/');
  const per = {}, geo = [];
  for (const s of sessions) {
    let l = 0, w = 0, all = true;
    for (const b of Object.keys(W)) {
      const a = read(s, A, b), c = read(s, B, b);
      if (a === undefined || c === undefined) { all = false; continue; }
      (per[b] ||= []).push(Math.log(a / c)); l += W[b] * Math.log(a / c); w += W[b];
    }
    if (all) geo.push(l / w);
  }
  console.log(`| ${A} / ${B} | ` + Object.keys(W).map(b => per[b] ? fmt(ci(per[b])) : '-').join(' | ') + ` | ${geo.length ? fmt(ci(geo)) : '-'} |`);
}
if (process.env.PER) for (const s of sessions) console.log(s, Object.keys(W).map(b => `${b} ` + process.env.PER.split(',').map(f => read(s, f, b)?.toFixed(2)).join('/')).join('  '));
