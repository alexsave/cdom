// One harness-shaped measured click under a trace: fresh page, the harness's warm-up clicks,
// then the measured click traced. Prints renderer-main-thread self time by event name (ms),
// summed over S fresh samples and divided by S.
// Usage: node tools/onetrace.mjs <entry> <warmup selectors, comma separated> <selector> [S=5]
import { launch, sleep } from './cdp.mjs';
const [name, warm, sel] = process.argv.slice(2, 5), S = +(process.argv[5] || 5);
const self = new Map();
for (let s = 0; s < S; s++) {
  const b = await launch();
  const events = [];
  try {
    await b.send('Page.navigate', { url: `http://localhost:${process.env.PORT || 8093}/frameworks/keyed/${name}/index.html` });
    for (let i = 0; i < 100 && !(await b.evaluate('!!document.getElementById("run")')); i++) await sleep(50);
    await b.send('Page.bringToFront');
    const frame = `new Promise(r => { const t = setTimeout(r, 300); requestAnimationFrame(() => { clearTimeout(t); setTimeout(r, 0); }); })`;
    await b.evaluate(`(async () => { for (const s of ${JSON.stringify(warm ? warm.split(',') : [])}) { document.querySelector(s).click(); await ${frame}; } })()`);
    await sleep(300);
    b.onEvent = (m) => { if (m.method === 'Tracing.dataCollected') events.push(...m.params.value); };
    await b.send('Tracing.start', { traceConfig: { includedCategories: ['devtools.timeline', 'v8.execute', 'v8', 'v8.wasm', 'blink.user_timing', 'disabled-by-default-devtools.timeline'] }, transferMode: 'ReportEvents' });
    await b.evaluate(`(async () => { performance.mark('m0'); document.querySelector(${JSON.stringify(sel)}).click(); await ${frame}; await ${frame}; performance.mark('m1'); })()`);
    const done = new Promise(r => { const prev = b.onEvent; b.onEvent = (m) => { prev(m); if (m.method === 'Tracing.tracingComplete') r(); }; });
    await b.send('Tracing.end');
    await done;
  } finally { await b.close(); }
  const names = new Map();
  for (const e of events) if (e.ph === 'M' && e.name === 'thread_name') names.set(`${e.pid}:${e.tid}`, e.args.name);
  const m0 = events.find(e => e.name === 'm0')?.ts, m1 = events.find(e => e.name === 'm1')?.ts;
  const evs = events.filter(e => e.ph === 'X' && e.ts >= m0 && e.ts < m1 && names.get(`${e.pid}:${e.tid}`) === 'CrRendererMain').sort((a, c) => a.ts - c.ts || c.dur - a.dur);
  const stack = [];
  for (const e of evs) {
    while (stack.length && stack[stack.length - 1].ts + stack[stack.length - 1].dur <= e.ts) stack.pop();
    const p = stack[stack.length - 1];
    if (p) self.set(p.name, (self.get(p.name) || 0) - Math.min(e.dur, p.ts + p.dur - e.ts));
    self.set(e.name, (self.get(e.name) || 0) + (e.dur || 0));
    stack.push(e);
  }
}
for (const [k, v] of [...self].filter(([, v]) => v / S > 20).sort((a, c) => c[1] - a[1]).slice(0, 25)) console.log((v / 1000 / S).toFixed(3).padStart(8), k);
