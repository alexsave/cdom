// Chrome's CPU profile of one page.html load (bench/strings/prof.mjs's method, on this page): top self time
// by function, and the hot lines. Usage: node bench/shared-state/bench/prof.mjs 'entry=L&sc=S7'
import { launch, sleep } from '../../../tools/cdp.mjs';
import { serve } from '../../strings/serve.mjs';
const srv = serve();
const b = await launch();
try {
  await b.send('Profiler.enable');
  await b.send('Profiler.setSamplingInterval', { interval: 50 });
  await b.send('Profiler.start');
  await b.send('Page.navigate', { url: `http://127.0.0.1:${srv.port}/bench/shared-state/bench/page.html?${process.argv[2]}` });
  let r;
  for (let i = 0; i < 1200 && !r; i++) { await sleep(100); try { r = await b.evaluate('window.RESULT'); } catch {} }
  const { result: { profile } } = await b.send('Profiler.stop');
  const self = new Map(), dt = profile.timeDeltas, byId = new Map(profile.nodes.map((n) => [n.id, n]));
  const hits = new Map();
  for (let i = 0; i < profile.samples.length; i++) hits.set(profile.samples[i], (hits.get(profile.samples[i]) || 0) + (dt[i] || 0));
  let total = 0;
  for (const [id, t] of hits) {
    const n = byId.get(id);
    const k = `${n.callFrame.functionName || '(anon)'} ${n.callFrame.url.split('/').pop().split('?')[0]}:${n.callFrame.lineNumber}`;
    self.set(k, (self.get(k) || 0) + t); total += t;
  }
  const med = (v) => v && v.length ? [...v].sort((a, c) => a - c)[v.length >> 1] : null;
  console.log('result', r && (r.error || `K=${r.K} script ${med(r.script)} total ${med(r.total)} clear ${med(r.script2)}`));
  for (const [k, t] of [...self].sort((a, c) => c[1] - a[1]).slice(0, +(process.env.TOP || 15)))
    console.log((100 * t / total).toFixed(1).padStart(5) + '%', (t / 1000).toFixed(1).padStart(8), 'ms', k);
} finally { await b.close(); srv.close(); }
