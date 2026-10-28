// Bootstrap 95% interval of the weighted geometric mean of B/A over the CPU benchmarks of one or
// more pair runs: each iteration resamples every (framework, benchmark) run's iterations with
// replacement and takes medians, as tools/ratio.mjs does once.
// Usage: node tools/ci.mjs cdomp0 cdomf results/pairs/fix1 [results/pairs/fix2 ...] [--skip 04_,05_]
import fs from 'node:fs';
const args = process.argv.slice(2);
const skip = args.includes('--skip') ? args[args.indexOf('--skip') + 1].split(',') : [];
const [A, B, ...dirs] = args.filter((a, i) => a !== '--skip' && args[i - 1] !== '--skip');
const cpu = ['01_run1k', '02_replace1k', '03_update10th1k_x16', '04_select1k', '05_swap1k', '06_remove-one-1k', '07_create10k', '08_create1k-after1k_x2', '09_clear1k_x8'];
const W = [0.64280248137063, 0.5607178150466176, 0.5643800750716564, 0.1925635870170522, 0.13200612879341714, 0.5277091212292658, 0.5644449600965534, 0.5508359820582848, 0.4225836631419211];
const read = (dir, f, b) => { const n = fs.readdirSync(`${dir}/results`).find((x) => x.startsWith(`${f}-v`) && x.endsWith(`_${b}.json`));
  return n ? JSON.parse(fs.readFileSync(`${dir}/results/${n}`, 'utf8')).values.total.values : null; };
const med = (v) => { const s = [...v].sort((x, y) => x - y); return s[s.length >> 1]; };
let seed = 7; const rnd = () => (seed = (seed * 1103515245 + 12345) >>> 0) / 4294967296;
const pick = (v) => Array.from(v, () => v[Math.floor(rnd() * v.length)]);
for (const dir of dirs) {
  const data = cpu.map((b) => skip.some((s) => b.startsWith(s)) ? null : [read(dir, A, b), read(dir, B, b)]);
  const gm = (f) => { let l = 0, w = 0; data.forEach((d, i) => { if (!d || !d[0] || !d[1]) return; l += W[i] * Math.log(f(d[1]) / f(d[0])); w += W[i]; }); return Math.exp(l / w); };
  const point = gm(med);
  const boot = Array.from({ length: 2000 }, () => gm((v) => med(pick(v)))).sort((x, y) => x - y);
  console.log(`${dir}: ${B}/${A} weighted geomean ${point.toFixed(3)}, 95% interval ${boot[50].toFixed(3)}-${boot[1949].toFixed(3)}${skip.length ? ' (without ' + skip.join(' ') + ')' : ''}`);
}
