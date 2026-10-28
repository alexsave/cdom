// Breaks the harness's own traces (results/pairs/<tag>/traces, one per measured iteration) down
// inside the measured window (the click's EventDispatch start to the end of the last Paint or
// Commit on the renderer main thread after it): total, JS, GC on the main thread (top-level GC
// intervals, counted by kind), sampled "(garbage collector)" and "(program)" time from the
// harness's own cpu_profiler samples, and main-thread self time by event name.
// Usage: node tools/hbreak.mjs <entry> <benchmark id e.g. 01_run1k> <trace dir> [<trace dir> ...]
// Prints one line per iteration (with ITER=1) and the medians and means; JSONL with OUT=file.
import fs from 'node:fs';
const [entry, bench, ...dirs] = process.argv.slice(2);
const rows = [];
const GCK = (n) => n === 'MinorGC' || n === 'MajorGC' || n.startsWith('V8.GC_') || n.startsWith('BlinkGC') || n.startsWith('CppGC') || n === 'V8.InterruptHandler' || n.startsWith('V8.StackGuard') || n.startsWith('V8.HandleInterrupts');
for (const dir of dirs) {
  for (const f of fs.readdirSync(dir)) {
    if (!f.startsWith(entry + '-') || !f.includes(`_${bench}_`)) continue;
    const ev = JSON.parse(fs.readFileSync(`${dir}/${f}`, 'utf8')).traceEvents;
    const names = new Map();
    for (const e of ev) if (e.ph === 'M' && e.name === 'thread_name') names.set(`${e.pid}:${e.tid}`, e.args.name);
    const click = ev.find(e => e.name === 'EventDispatch' && e.args?.data?.type === 'click' && e.ph === 'X');
    if (!click) continue;
    const main = (e) => e.pid === click.pid && e.tid === click.tid;
    let end = click.ts + click.dur;
    for (const e of ev) if (main(e) && e.ph === 'X' && (e.name === 'Paint' || e.name === 'Commit') && e.ts > click.ts && e.ts < click.ts + 5e6) end = Math.max(end, e.ts + e.dur);
    const win = ev.filter(e => main(e) && e.ph === 'X' && e.ts >= click.ts && e.ts < end).sort((a, c) => a.ts - c.ts || c.dur - a.dur);
    // GC: top-level GC intervals (a GC event not nested in another GC event), grouped by kind.
    const gc = {}; let gcEnd = -1, gcTotal = 0, gcInScript = 0, gcTop = [];
    const scr = win.filter(e => ['EventDispatch', 'FunctionCall', 'TimerFire', 'FireAnimationFrame', 'RunMicrotasks'].includes(e.name));
    for (const e of win) {
      if (!GCK(e.name)) continue;
      if (e.ts < gcEnd) continue;
      gcEnd = e.ts + e.dur; gcTotal += e.dur; gcTop.push(e);
      const k = e.name; gc[k] = gc[k] || [0, 0]; gc[k][0]++; gc[k][1] += e.dur;
      if (scr.some(s => e.ts >= s.ts && e.ts < s.ts + s.dur)) gcInScript += e.dur;
    }
    // Self time by name.
    const self = {}; const st = [];
    for (const e of win) {
      while (st.length && st[st.length - 1].ts + st[st.length - 1].dur <= e.ts) st.pop();
      const p = st[st.length - 1];
      if (p) self[p.name] = (self[p.name] || 0) - Math.min(e.dur, p.ts + p.dur - e.ts);
      self[e.name] = (self[e.name] || 0) + (e.dur || 0); st.push(e);
    }
    // Sampled profile: ProfileChunk nodes and samples for the profile of the click's thread.
    const prof = ev.find(e => e.name === 'Profile' && e.pid === click.pid && e.tid === click.tid);
    const smp = {};
    if (prof) {
      const nodes = new Map(); let t = prof.args.data.startTime;
      for (const e of ev) {
        if (e.name !== 'ProfileChunk' || e.id !== prof.id) continue;
        const cp = e.args.data.cpuProfile || {};
        for (const n of cp.nodes || []) nodes.set(n.id, n.callFrame.functionName || (n.callFrame.url ? '(anon)' : '(anon)'));
        const s = cp.samples || [], d = e.args.data.timeDeltas || [];
        for (let i = 0; i < s.length; i++) { const dt = d[i]; t += dt; if (t >= click.ts && t < end) { const k = nodes.get(s[i]) || '?'; smp[k] = (smp[k] || 0) + 1; } }
      }
    }
    const it = +f.match(/_(\d+)\.json$/)[1];
    rows.push({ dir, it, total: (end - click.ts) / 1000, click: click.dur / 1000, gcTotal: gcTotal / 1000, gcInScript: gcInScript / 1000, gc, self, smp });
  }
}
if (process.env.OUT) fs.writeFileSync(process.env.OUT, rows.map(r => JSON.stringify(r)).join('\n') + '\n');
const med = a => { const s = [...a].sort((x, y) => x - y); const n = s.length; return n ? (n % 2 ? s[n >> 1] : (s[n / 2 - 1] + s[n / 2]) / 2) : NaN; };
const mean = a => a.reduce((x, y) => x + y, 0) / a.length;
const show = (k, a) => console.log(k.padEnd(34), 'median', med(a).toFixed(3).padStart(8), 'mean', mean(a).toFixed(3).padStart(8));
console.log(`${entry} ${bench}: n=${rows.length}`);
show('total (click to last paint/commit)', rows.map(r => r.total));
show('click EventDispatch', rows.map(r => r.click));
show('main-thread GC, top level', rows.map(r => r.gcTotal));
show('  of which inside the click script', rows.map(r => r.gcInScript));
const kinds = new Set(rows.flatMap(r => Object.keys(r.gc)));
for (const k of kinds) console.log(`  ${k}`.padEnd(34), 'count/iter', (rows.reduce((a, r) => a + (r.gc[k]?.[0] || 0), 0) / rows.length).toFixed(2), 'ms/iter', (rows.reduce((a, r) => a + (r.gc[k]?.[1] || 0), 0) / rows.length / 1000).toFixed(3), 'iters with', rows.filter(r => r.gc[k]).length);
const sk = new Set(rows.flatMap(r => Object.keys(r.smp)));
const sm = [...sk].map(k => [k, rows.reduce((a, r) => a + (r.smp[k] || 0), 0) / rows.length]).sort((a, b) => b[1] - a[1]).slice(0, 12);
console.log('  samples per iter:', sm.map(([k, v]) => `${k} ${v.toFixed(1)}`).join(', '));
const nk = new Set(rows.flatMap(r => Object.keys(r.self)));
const sl = [...nk].map(k => [k, rows.reduce((a, r) => a + (r.self[k] || 0), 0) / rows.length / 1000]).sort((a, b) => b[1] - a[1]).filter(x => x[1] > 0.02).slice(0, 16);
console.log('  self ms per iter:', sl.map(([k, v]) => `${k} ${v.toFixed(3)}`).join(', '));
if (process.env.ITER) for (const r of rows) console.log(r.dir.split('/').slice(-2, -1)[0], r.it, r.total.toFixed(2), r.gcTotal.toFixed(3), JSON.stringify(r.gc));
