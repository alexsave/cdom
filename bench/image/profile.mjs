// The image bench's profile (docs/image-simd-hand.md): the C arm on one input in headless Chrome under the CDP
// Profiler, wasm function names from the module's name section, self time by function over REPS timed runs after one
// warm-up run. Usage: node bench/image/profile.mjs <build dir> <input> [reps=8] [box=900]
// Prints "share% ms function" lines, the top TOP (default 14), and the total sampled time.
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { launch, sleep } from '../../tools/cdp.mjs';
const here = path.dirname(new URL(import.meta.url).pathname);
const [dir, input, reps = 8, box = 900] = [process.argv[2], process.argv[3], +(process.argv[4] || 8), +(process.argv[5] || 900)];
const srv = http.createServer((q, r) => {
    const f = path.join(here, decodeURIComponent(q.url.split(/[?#]/)[0]));
    fs.readFile(f, (e, b) => { if (e) { r.writeHead(404); r.end(); return; } r.writeHead(200, { 'content-type': f.endsWith('.html') ? 'text/html' : 'application/octet-stream' }); r.end(b); });
}).listen(0);
const base = `http://127.0.0.1:${srv.address().port}`;
const b = await launch();
try {
    await b.send('Page.navigate', { url: `${base}/${dir}/index.html` });
    for (let i = 0; i < 200 && !(await b.evaluate('!!window.READY')); i++) await sleep(25);
    await b.evaluate(`arm('C', ${JSON.stringify(`${base}/inputs/${input}`)}, ${box})`);   // warm-up: tier-up, caches
    await b.send('Profiler.enable');
    await b.send('Profiler.setSamplingInterval', { interval: 100 });
    await b.send('Profiler.start');
    let ms = 0;
    for (let i = 0; i < reps; i++) ms += (await b.evaluate(`arm('C', ${JSON.stringify(`${base}/inputs/${input}`)}, ${box})`)).ms;
    const { result: { profile } } = await b.send('Profiler.stop');
    const dt = new Map();
    profile.timeDeltas.forEach((d, i) => dt.set(profile.samples[i], (dt.get(profile.samples[i]) || 0) + d));
    const self = new Map();
    let total = 0;
    for (const n of profile.nodes) {
        const t = dt.get(n.id) || 0;
        const k = n.callFrame.functionName || '(anon)';
        self.set(k, (self.get(k) || 0) + t); total += t;
    }
    console.log(`# ${input} ${dir} ${b.version}: ${reps} runs, resize ${(ms / reps).toFixed(1)} ms mean, sampled ${(total / 1000 / reps).toFixed(1)} ms per run`);
    const idle = (self.get('(idle)') || 0) + (self.get('(program)') || 0) + (self.get('(garbage collector)') || 0);
    const busy = total - idle;
    [...self].filter(([k]) => !k.startsWith('(')).sort((a, c) => c[1] - a[1]).slice(0, +(process.env.TOP || 14))
        .forEach(([k, t]) => console.log(`${(100 * t / busy).toFixed(1).padStart(5)}%  ${(t / 1000 / reps).toFixed(1).padStart(7)} ms  ${k}`));
} finally { await b.close(); srv.close(); }
