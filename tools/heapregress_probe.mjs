// heap-regress: what 22_run-memory does (load, click #run once, wait for the first row) on each named entry,
// and the wasm memory and cdom's heap (results/heapregress/probe.c) before and after the click.
// Usage: PORT=8193 node tools/heapregress_probe.mjs cdom-hrpbase,cdom-hrp [clicks, default "run"]
import { launch, sleep } from './cdp.mjs';
const fws = process.argv[2].split(',');
const reps = +(process.env.REPS || 1);
const clicks = (process.argv[3] || 'run').split(',');
const hook = `(() => {
  const keep = (r) => { const i = r.instance || r; if (i.exports && i.exports.cd_heap_probe) window.__inst = i; return r; };
  const s = WebAssembly.instantiateStreaming, n = WebAssembly.instantiate;
  if (s) WebAssembly.instantiateStreaming = (...a) => s.apply(WebAssembly, a).then(keep);
  WebAssembly.instantiate = (...a) => n.apply(WebAssembly, a).then(keep);
})();`;
const probe = `(() => { const e = __inst.exports; e.cd_heap_probe(16); const w = new Uint32Array(e.memory.buffer, 16, 8);
  return { pages: e.memory.buffer.byteLength / 65536, lo: w[0], ptr: w[1], end: w[2], freeSmall: w[3], freeRuns: w[4], nRuns: w[5], topRun: w[6], nSmall: w[7] }; })()`;
const b = await launch(['--js-flags=--expose-gc']);
try {
    await b.send('Page.addScriptToEvaluateOnNewDocument', { source: hook });
    for (let rep = 0; rep < reps; rep++) for (const f of (rep % 2 ? [...fws].reverse() : fws)) {
        await b.send('Page.navigate', { url: `http://localhost:${process.env.PORT || 8193}/frameworks/keyed/${f}/index.html` });
        for (let i = 0; i < 200 && !(await b.evaluate('!!(document.getElementById("run") && window.__inst)')); i++) await sleep(50);
        const before = await b.evaluate(probe);
        const after = [];
        for (const c of clicks) {
            await b.evaluate(`(async () => { document.getElementById(${JSON.stringify(c)}).click();
        await new Promise(r => requestAnimationFrame(() => setTimeout(r, 0))); })()`);
            after.push([c, await b.evaluate(probe)]);
        }
        const measure = `(async () => { window.gc({type:'major',execution:'sync',flavor:'last-resort'}); await new Promise(r => setTimeout(r, 40));
      const m = await performance.measureUserAgentSpecificMemory(); return m.bytes + ' ' + JSON.stringify(m.breakdown.filter(x => x.bytes).map(x => [x.bytes, x.types.join('+')])); })()`;
        const mem = await b.evaluate(measure);
        // GROW=n: the memory grown by n pages after that, and measured again (does the measure count wasm memory?)
        const grown = process.env.GROW ? await b.evaluate(`(() => { __inst.exports.memory.grow(${+process.env.GROW}); return 0; })()`).then(() => b.evaluate(measure)) : null;
        console.log(f, 'before', JSON.stringify(before));
        for (const [c, a] of after) console.log(f, 'after', c, JSON.stringify(a));
        console.log(f, 'measureUserAgentSpecificMemory', mem);
        if (grown) console.log(f, `after growing ${process.env.GROW} pages`, grown);
    }
} finally { await b.close(); }
