// The per-function time split of the C arm (docs/image-simd.md): V8's CPU profiler through tools/cdp.mjs, on a fresh page
// for each repetition (cold, as run.mjs runs it) or, with warm, on the second call in one page (tiered up).
// Self time by wasm function name (the module is built unstripped), summed over the repetitions, as ms per run.
// Usage: node bench/image/profile.mjs <arm, as run.mjs: C-js, C-js-simde, ...> <input> [box=900] [reps=9] [cold|warm]
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { launch, sleep } from '../../tools/cdp.mjs';

const here = path.dirname(new URL(import.meta.url).pathname);
const [arm, input, box = 900, reps = 9, mode = 'cold'] = [process.argv[2], process.argv[3], ...process.argv.slice(4)];
const srv = http.createServer((q, r) => {
  const f = path.join(here, decodeURIComponent(q.url.split(/[?#]/)[0]));
  if (!f.startsWith(here)) { r.writeHead(403); r.end(); return; }
  fs.readFile(f, (e, b) => { if (e) { r.writeHead(404); r.end(); return; } r.writeHead(200, { 'content-type': f.endsWith('.html') ? 'text/html' : 'application/octet-stream' }); r.end(b); });
}).listen(0);
const base = `http://127.0.0.1:${srv.address().port}`;
const b = await launch();
const self = new Map();
let total = 0, runs = 0;
try {
  for (let r = 0; r < +reps; r++) {
    await b.send('Page.navigate', { url: `${base}/build-${arm.slice(2)}/index.html` });
    for (let i = 0; i < 200 && !(await b.evaluate('!!window.READY')); i++) await sleep(25);
    const call = `arm('C', ${JSON.stringify(`${base}/inputs/${input}`)}, ${+box})`;
    if (mode === 'warm') await b.evaluate(call);
    await b.send('Profiler.enable');
    await b.send('Profiler.setSamplingInterval', { interval: 100 });
    await b.send('Profiler.start');
    await b.evaluate(call);
    const { result: { profile } } = await b.send('Profiler.stop');
    const dt = new Map();
    profile.timeDeltas.forEach((d, i) => dt.set(profile.samples[i], (dt.get(profile.samples[i]) || 0) + d));
    for (const n of profile.nodes) {
      const t = dt.get(n.id) || 0, name = n.callFrame.functionName || '(anonymous)';
      if (/^\((idle|program|root)\)$/.test(name)) continue;
      self.set(name, (self.get(name) || 0) + t);
      total += t;
    }
    runs++;
  }
} finally { await b.close(); srv.close(); }
console.log(`${arm} ${input} box ${box} ${mode}, ${runs} runs: self time per run, ms (share of all sampled time)`);
for (const [k, t] of [...self].sort((a, b) => b[1] - a[1]).slice(0, +(process.env.TOP || 22)))
  console.log((t / 1000 / runs).toFixed(2).padStart(8), 'ms', (100 * t / total).toFixed(1).padStart(5), '%', k);
console.log((total / 1000 / runs).toFixed(2).padStart(8), 'ms total sampled');
