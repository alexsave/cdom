// Ratio to vanillajs per benchmark from one tools/pair.sh run: median total (and script) of each
// framework divided by vanillajs's in the same run, the load at each start, and the weighted
// geometric mean of the ratios (CPU weights from webdriver-ts-results).
// Usage: node tools/ratio.mjs results/pairs/<tag> cdomb,cdomp
import fs from 'node:fs';
const dir = process.argv[2];
const fws = ['vanillajs', ...(process.argv[3] || 'cdomp').split(',')];
const cpu = ['01_run1k', '02_replace1k', '03_update10th1k_x16', '04_select1k', '05_swap1k', '06_remove-one-1k', '07_create10k', '08_create1k-after1k_x2', '09_clear1k_x8'];
const W = [0.64280248137063, 0.5607178150466176, 0.5643800750716564, 0.1925635870170522, 0.13200612879341714, 0.5277091212292658, 0.5644449600965534, 0.5508359820582848, 0.4225836631419211];
const files = fs.readdirSync(`${dir}/results`);
const read = (f, b) => { const n = files.find(x => (x.startsWith(`${f}-v`) || x.startsWith(`${f}-keyed`)) && x.endsWith(`_${b}.json`)); return n ? JSON.parse(fs.readFileSync(`${dir}/results/${n}`, 'utf8')) : null; };
const loads = new Map();
try { for (const l of fs.readFileSync(`${dir}/loads.tsv`, 'utf8').trim().split('\n').slice(1)) { const [b, f, before] = l.split('\t'); loads.set(`${b}|${f}`, before); } } catch {}
const med = (f, b, k) => read(f, b)?.values?.[k]?.median;
const f1 = v => v === undefined ? '-' : v.toFixed(v < 10 ? 2 : 1);
console.log('| benchmark | ' + fws.map(f => `${f} total (script)`).join(' | ') + ' |');
console.log('| --- |' + fws.map(() => ' ---: |').join(''));
const logs = fws.map(() => 0); let wsum = 0; let have = fws.map(() => true);
cpu.forEach((b, i) => {
  const v = fws.map(f => med(f, b, 'total')), s = fws.map(f => med(f, b, 'script'));
  if (v[0] === undefined) return;
  console.log(`| ${b} | ` + fws.map((f, j) => v[j] === undefined ? '-' :
    `${f1(v[j])} (${f1(s[j])})${j ? ' ' + (v[j] / v[0]).toFixed(3) : ''} @${loads.get(`${b.slice(0, 3)}|${f}`) ?? '?'}`).join(' | ') + ' |');
  v.forEach((x, j) => { if (x === undefined) have[j] = false; else logs[j] += W[i] * Math.log(x / v[0]); });
  wsum += W[i];
});
console.log('| weighted geomean vs vanillajs | ' + logs.map((l, j) => have[j] ? Math.exp(l / wsum).toFixed(3) : '(partial) ' + Math.exp(l / wsum).toFixed(3)).join(' | ') + ' |');
