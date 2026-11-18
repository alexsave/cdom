// Summarizes paired js-framework-benchmark runs (tools/pair.sh results/pairs/<tag>/results): per
// benchmark, each entry's median total and script, the baseline pair's gap (the noise), and each
// entry's ratio to the baseline.
// Usage: node bench/strings/jfbsum.mjs results/pairs/<tag> base base2 other...
import fs from 'node:fs';
const [dir, ...fws] = process.argv.slice(2);
const files = fs.readdirSync(`${dir}/results`);
const get = (fw, b) => {
  const f = files.find((x) => x.startsWith(`${fw}-v`) && x.includes(`_${b}`));
  if (!f) return null;
  const j = JSON.parse(fs.readFileSync(`${dir}/results/${f}`, 'utf8'));
  return j.values;
};
const benches = [...new Set(files.map((f) => f.replace(/^.*-keyed_/, '').replace(/\.json$/, '')))].sort();
const fmt = (x) => (x === undefined ? '-' : x.toFixed(x < 10 ? 2 : 1));
console.log(['benchmark', 'metric', ...fws, 'noise (a2/a)', ...fws.slice(2).map((f) => `${f}/a`)].join('\t'));
for (const b of benches)
  for (const k of ['total', 'script', 'DEFAULT']) {
    const v = fws.map((f) => get(f, b)?.[k]?.median);
    if (v[0] === undefined) continue;
    console.log([b, k, ...v.map(fmt), (v[1] / v[0]).toFixed(3), ...v.slice(2).map((x) => (x / v[0]).toFixed(3))].join('\t'));
  }
