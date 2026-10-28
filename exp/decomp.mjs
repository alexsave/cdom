// Where the gap sits (proof of why direct calls win): for each session of an arm directory,
// runs tools/hbreak.mjs over the harness's own traces of each entry and benchmark, takes the
// per-session median of each main-thread phase (self time: the click's script, style, layout,
// pre-paint, paint, GC), and prints the difference A - B per phase as a mean over sessions with
// a 95% t interval (n = sessions), in ms.
// Usage: node exp/decomp.mjs <arm dir> A B
import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
const [dir, A, B] = process.argv.slice(2);
const here = path.dirname(new URL(import.meta.url).pathname);
const T = [0, 12.706, 4.303, 3.182, 2.776, 2.571, 2.447, 2.365, 2.306, 2.262, 2.228];
const med = a => { const s = [...a].sort((x, y) => x - y); const n = s.length; return n % 2 ? s[n >> 1] : (s[n / 2 - 1] + s[n / 2]) / 2; };
const PH = { total: r => r.total, script: r => (r.self.FunctionCall || 0) / 1000 + (r.self.EventDispatch || 0) / 1000, style: r => (r.self.UpdateLayoutTree || 0) / 1000,
  layout: r => (r.self.Layout || 0) / 1000, prepaint: r => (r.self.PrePaint || 0) / 1000, paint: r => ((r.self.Paint || 0) + (r.self.Commit || 0) + (r.self.Layerize || 0)) / 1000,
  'gc (main thread)': r => r.gcTotal, 'gc samples': r => (r.smp['(garbage collector)'] || 0) };
const sessions = fs.readdirSync(dir).filter(s => /^s\d+$/.test(s));
const cache = (s, e, b) => { const f = `${dir}/${s}/hb_${e}_${b}.jsonl`; if (!fs.existsSync(f)) execFileSync('nice', ['-n', '10', 'node', path.join(here, '../tools/hbreak.mjs'), e, b, `${dir}/${s}/traces`], { env: { ...process.env, OUT: f }, stdio: 'ignore' });
  return fs.readFileSync(f, 'utf8').trim().split('\n').filter(Boolean).map(l => JSON.parse(l)); };
for (const b of ['01_run1k', '07_create10k', '09_clear1k_x8']) {
  const d = {}, ma = {}, mb = {};
  for (const s of sessions) {
    if (!fs.existsSync(`${dir}/${s}/traces`)) continue;
    const ra = cache(s, A, b), rb = cache(s, B, b);
    if (!ra.length || !rb.length) continue;
    for (const [k, f] of Object.entries(PH)) { const x = med(ra.map(f)), y = med(rb.map(f)); (d[k] ||= []).push(x - y); (ma[k] ||= []).push(x); (mb[k] ||= []).push(y); }
  }
  console.log(`${b}: ${A} - ${B}, ms per click (median per session, then mean over ${d.total?.length || 0} sessions, 95% t interval)`);
  for (const k of Object.keys(PH)) {
    const xs = d[k]; if (!xs) continue; const n = xs.length, m = xs.reduce((a, c) => a + c, 0) / n;
    const sd = n > 1 ? Math.sqrt(xs.reduce((a, c) => a + (c - m) ** 2, 0) / (n - 1)) : NaN, h = (T[n - 1] || 1.96) * sd / Math.sqrt(n);
    const mean = a => a.reduce((x, y) => x + y, 0) / a.length;
    console.log(`  ${k.padEnd(17)} ${A} ${mean(ma[k]).toFixed(3).padStart(8)}  ${B} ${mean(mb[k]).toFixed(3).padStart(8)}  diff ${m.toFixed(3).padStart(7)} [${(m - h).toFixed(3)}, ${(m + h).toFixed(3)}]`);
  }
}
