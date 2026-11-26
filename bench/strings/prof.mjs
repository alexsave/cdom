// Chrome's CPU profile of one page load (micro.html or jsread.html with a query): top self time by function.
// Usage: node bench/strings/prof.mjs 'jsread.html?read=td&len=9'
import { launch, sleep } from '../../tools/cdp.mjs';
import { serve } from './serve.mjs';
const srv = serve();
const b = await launch();
try {
    await b.send('Profiler.enable');
    await b.send('Profiler.setSamplingInterval', { interval: 50 });
    await b.send('Profiler.start');
    await b.send('Page.navigate', { url: `http://127.0.0.1:${srv.port}/bench/strings/${process.argv[2]}` });
    let r;
    for (let i = 0; i < 600 && !r; i++) { await sleep(100); try { r = await b.evaluate('window.RESULT'); } catch {} }
    const { result: { profile } } = await b.send('Profiler.stop');
    const self = new Map(), dt = profile.timeDeltas, byId = new Map(profile.nodes.map(n => [n.id, n]));
    const hits = new Map();
    for (let i = 0; i < profile.samples.length; i++) hits.set(profile.samples[i], (hits.get(profile.samples[i]) || 0) + (dt[i] || 0));
    let total = 0;
    for (const [id, t] of hits) { const n = byId.get(id); const k = `${n.callFrame.functionName || '(anon)'} ${n.callFrame.url.split('/').pop()}:${n.callFrame.lineNumber}`; self.set(k, (self.get(k) || 0) + t); total += t; }
    console.log('result', JSON.stringify(r));
    for (const [k, t] of [...self].sort((a, c) => c[1] - a[1]).slice(0, 12)) console.log((100 * t / total).toFixed(1).padStart(5) + '%', k);
} finally { await b.close(); srv.close(); }
