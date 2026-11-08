// Page-load timeline: resource timings, first paint, and the renderer main-thread slices
// (>= 0.2 ms) between navigation and first paint, from a Chrome trace. One fresh load per sample;
// prints the median first paint over the samples and the full timeline of the last one.
// Usage: node tools/startup.mjs <entry> [samples=5]
import { launch, sleep } from './cdp.mjs';
const name = process.argv[2], N = +(process.argv[3] || 5);
const fps = [];
let last;
for (let s = 0; s < N; s++) {
  const b = await launch();
  const events = [];
  try {
    await b.send('Page.bringToFront');
    b.onEvent = (m) => { if (m.method === 'Tracing.dataCollected') events.push(...m.params.value); };
    await b.send('Tracing.start', { traceConfig: { includedCategories: ['devtools.timeline', 'v8.wasm', 'blink.user_timing', 'loading', 'v8', 'disabled-by-default-devtools.timeline', 'blink'] }, transferMode: 'ReportEvents' });
    await b.send('Page.navigate', { url: `http://localhost:${process.env.PORT || 8093}/frameworks/keyed/${name}/index.html` });
    for (let i = 0; i < 100 && !(await b.evaluate('performance.getEntriesByType("paint").length > 0')); i++) await sleep(20);
    await sleep(200);
    const perf = await b.evaluate(`JSON.stringify({ paint: performance.getEntriesByType('paint').map(e => [e.name, e.startTime]),
      res: performance.getEntriesByType('resource').map(e => [e.name.split('/').pop(), e.startTime, e.responseEnd, e.transferSize]),
      nav: (e => [e.responseEnd, e.domInteractive, e.domContentLoadedEventEnd])(performance.getEntriesByType('navigation')[0]) })`);
    const done = new Promise(r => { const prev = b.onEvent; b.onEvent = (m) => { prev(m); if (m.method === 'Tracing.tracingComplete') r(); }; });
    await b.send('Tracing.end');
    await done;
    last = { perf: JSON.parse(perf), events };
    fps.push(last.perf.paint.find(p => p[0] === 'first-paint')[1]);
  } finally { await b.close(); }
}
fps.sort((a, c) => a - c);
console.log(`${name}: first paint median ${fps[fps.length >> 1].toFixed(1)} ms (${fps.map(f => f.toFixed(1)).join(' ')})`);
const { perf, events } = last;
console.log('nav responseEnd/domInteractive/DCL', perf.nav.map(x => x.toFixed(1)).join(' '));
for (const r of perf.res) console.log('res', r[0].padEnd(20), r[1].toFixed(1), '->', r[2].toFixed(1), r[3]);
const names = new Map();
for (const e of events) if (e.ph === 'M' && e.name === 'thread_name') names.set(`${e.pid}:${e.tid}`, e.args.name);
const nav = events.find(e => e.name === 'navigationStart' && e.args?.data?.isLoadingMainFrame && /index.html/.test(e.args.data.documentLoaderURL || ''));
const t0 = nav ? nav.ts : Math.min(...events.filter(e => e.ts).map(e => e.ts));
const fp = events.find(e => e.name === 'firstPaint' || e.name === 'firstContentfulPaint');
const end = fp ? fp.ts : t0 + 300000;
for (const e of events.sort((a, c) => a.ts - c.ts)) {
  const th = names.get(`${e.pid}:${e.tid}`) || '';
  if (e.ts < t0 || e.ts > end + 1000) continue;
  if (e.ph === 'X' && th === 'CrRendererMain' && e.dur >= 200 || (e.ph !== 'X' && /firstPaint|firstContentfulPaint|wasm|Wasm/.test(e.name)))
    console.log(((e.ts - t0) / 1000).toFixed(2).padStart(8), (e.dur ? (e.dur / 1000).toFixed(2) : '').padStart(6), th.padEnd(16), e.name, e.args?.data?.url ? e.args.data.url.split('/').pop() : '');
}
