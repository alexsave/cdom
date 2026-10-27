// Samples the JS/wasm profiler over repeated clicks; prints self time by function.
import { launch, sleep } from './cdp.mjs';
const [name, setup, sel, reps] = [process.argv[2], process.argv[3], process.argv[4], +(process.argv[5] || 10)];
const b = await launch();
try {
  await b.send('Page.navigate', { url: `http://localhost:${process.env.PORT || 8093}/frameworks/keyed/${name}/index.html` });
  for (let i = 0; i < 50 && !(await b.evaluate('!!document.getElementById("run")')); i++) await sleep(100);
  const loop = `(() => { const c = s => document.querySelector(s).click(); for (let i = 0; i < ${reps}; i++) { ${setup ? setup.split(',').map(s => `c('${s}');`).join(' ') : ''} c('${sel}'); } })()`;
  await b.evaluate(loop); // warm up
  await b.send('Profiler.enable'); await b.send('Profiler.setSamplingInterval', { interval: 50 });
  await b.send('Profiler.start');
  await b.evaluate(loop);
  const { result: { profile } } = await b.send('Profiler.stop');
  const self = new Map(), dt = new Map();
  profile.timeDeltas.forEach((d, i) => dt.set(profile.samples[i], (dt.get(profile.samples[i]) || 0) + d));
  let total = 0;
  for (const n of profile.nodes) { const t = dt.get(n.id) || 0; const k = `${n.callFrame.functionName || '(anon)'} ${n.callFrame.url.split('/').pop()}:${n.callFrame.lineNumber}`; self.set(k, (self.get(k) || 0) + t); total += t; }
  [...self].sort((a, b) => b[1] - a[1]).slice(0, 18).forEach(([k, t]) => console.log((t / 1000).toFixed(1).padStart(8), 'ms', (100 * t / total).toFixed(1).padStart(5), '%', k));
  // Hot lines: sampled ticks per source line of the page's own scripts.
  const lines = new Map();
  for (const n of profile.nodes) for (const p of n.positionTicks || []) {
    const k = `${n.callFrame.url.split('/').pop()}:${p.line}`; lines.set(k, (lines.get(k) || 0) + p.ticks);
  }
  const lt = [...lines.values()].reduce((a, b) => a + b, 0);
  console.log('-- hot lines (share of line-attributed ticks)');
  [...lines].sort((a, b) => b[1] - a[1]).slice(0, +(process.env.LINES || 15)).forEach(([k, t]) => console.log((100 * t / lt).toFixed(1).padStart(6), '%', k));
} finally { await b.close(); }
