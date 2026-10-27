// Chrome trace of repeated clicks: self time per trace event name on the renderer main thread,
// summed over the measured clicks and divided by their count (ms per click).
// The JS profiler calls style, layout and paint "(idle)"; the trace does not.
// Usage: node tools/trace.mjs <entry> <setupSelector|''> <selector> [reps=10]
// Each rep: click setup (if any), wait two frames, click the selector, wait two frames.
import { launch, sleep } from './cdp.mjs';
const [name, setup, sel, reps] = [process.argv[2], process.argv[3], process.argv[4], +(process.argv[5] || 10)];
const b = await launch();
const events = [];
try {
  await b.send('Page.navigate', { url: `http://localhost:${process.env.PORT || 8093}/frameworks/keyed/${name}/index.html` });
  for (let i = 0; i < 50 && !(await b.evaluate('!!document.getElementById("run")')); i++) await sleep(100);
  await b.send('Page.bringToFront');
  const frames = `new Promise(r => { let k = 0; const t = setTimeout(r, 500); const f = () => ++k >= 2 ? (clearTimeout(t), r()) : requestAnimationFrame(f); requestAnimationFrame(f); })`;
  const one = `(async () => { const c = s => document.querySelector(s).click(); ${setup ? `c('${setup}'); await ${frames};` : ''} performance.mark('m0'); c('${sel}'); await ${frames}; performance.mark('m1'); })()`;
  for (let i = 0; i < 3; i++) await b.evaluate(one); // warm up
  b.onEvent = (m) => { if (m.method === 'Tracing.dataCollected') events.push(...m.params.value); };
  await b.send('Tracing.start', { traceConfig: { includedCategories: ['devtools.timeline', 'v8.execute', 'blink.user_timing', 'v8', 'disabled-by-default-devtools.timeline', 'blink', 'cc', 'gpu'] }, transferMode: 'ReportEvents' });
  for (let i = 0; i < reps; i++) { await b.send('Page.bringToFront'); await b.evaluate(one); }
  const done = new Promise(r => { const prev = b.onEvent; b.onEvent = (m) => { prev(m); if (m.method === 'Tracing.tracingComplete') r(); }; });
  await b.send('Tracing.end');
  await done;
} finally { await b.close(); }
// Main thread of the renderer: the thread named CrRendererMain.
const names = new Map();
for (const e of events) if (e.ph === 'M' && e.name === 'thread_name') names.set(`${e.pid}:${e.tid}`, e.args.name);
const marks = events.filter(e => e.name === 'm0').map(e => e.ts).sort((a, c) => a - c);
const ends = events.filter(e => e.name === 'm1').map(e => e.ts).sort((a, c) => a - c);
const inWin = (e) => marks.some((m, i) => e.ts >= m && e.ts < ends[i]);
const byThread = new Map();
for (const e of events) {
  if (e.ph !== 'X' || !inWin(e)) continue;
  const t = names.get(`${e.pid}:${e.tid}`) || `${e.tid}`;
  if (!byThread.has(t)) byThread.set(t, []);
  byThread.get(t).push(e);
}
for (const [t, evs] of byThread) {
  if (!/CrRendererMain|Compositor|VizCompositor|CrGpuMain/.test(t)) continue;
  evs.sort((a, c) => a.ts - c.ts || c.dur - a.dur);
  const self = new Map(), stack = [];
  for (const e of evs) {
    while (stack.length && stack[stack.length - 1].ts + stack[stack.length - 1].dur <= e.ts) stack.pop();
    const p = stack[stack.length - 1];
    if (p) self.set(p.name, (self.get(p.name) || 0) - Math.min(e.dur, p.ts + p.dur - e.ts));
    self.set(e.name, (self.get(e.name) || 0) + (e.dur || 0));
    stack.push(e);
  }
  const rows = [...self].filter(([, v]) => v > 5 * marks.length).sort((a, c) => c[1] - a[1]).slice(0, 14);
  console.log(`-- ${t} (ms per click, ${marks.length} clicks)`);
  for (const [k, v] of rows) console.log((v / 1000 / marks.length).toFixed(3).padStart(9), k);
}
