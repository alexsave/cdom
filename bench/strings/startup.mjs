// Startup and memory of each page, one fresh headless Chrome per load, the variants interleaved round by
// round. Per load: first paint and first contentful paint, cd_main's start, end and duration (cd_start
// wrapped from a script injected before the page's own), the table install (window.__cdomLitsMs, the
// proto shim), the JS heap after a GC, and wasm memory (pages, and touched 4 KB pages: any nonzero byte).
// With CYCLES=5 a keyed page then runs five create-1k-and-clear cycles and is measured again.
// Usage: R=15 node bench/strings/startup.mjs name=path ...   (path under the repo root, served locally)
// Prints one TSV line per load, then the medians.
import { launch, sleep } from '../../tools/cdp.mjs';
import { serve } from './serve.mjs';
const R = +(process.env.R || 15), CYCLES = +(process.env.CYCLES || 0);
const pages = process.argv.slice(2).map((a) => a.split('='));
// litsms here: from the module's instantiation resolving to cd_start, for every design alike (the
// shim's own work before cd_main: the exports wrapper, on this branch the table install, the views and
// the layout read). The shim has no install timer of its own, so the install is this minus A's.
const HOOK = `(() => {
  const wrap = (p) => p.then((r) => {
    window.__instT = performance.now();
    const inst = r.instance || r;
    const ex = {};
    for (const k of Object.keys(inst.exports)) ex[k] = inst.exports[k];
    const start = ex.cd_start;
    ex.cd_start = (...a) => { const t0 = performance.now(); if (window.__cdomLitsMs === undefined) window.__cdomLitsMs = t0 - window.__instT; try { return start(...a); } finally { window.__main = [t0, performance.now()]; } };
    window.__mem = ex.memory;
    const instance = { exports: ex };
    return r.instance ? { instance, module: r.module } : instance;
  });
  const s = WebAssembly.instantiateStreaming, i = WebAssembly.instantiate;
  WebAssembly.instantiateStreaming = (...a) => wrap(s.apply(WebAssembly, a));
  WebAssembly.instantiate = (...a) => wrap(i.apply(WebAssembly, a));
})();`;
const MEM = `(() => {
  const b = new Uint8Array(window.__mem.buffer), P = 4096;
  let touched = 0;
  for (let p = 0; p < b.length; p += P) { const w = new Uint32Array(b.buffer, p, P / 4); for (let k = 0; k < w.length; k++) if (w[k]) { touched++; break; } }
  return [b.length / 65536, touched * 4];
})()`;
const srv = serve();
const rows = [];
const cols = ['page', 'round', 'fp', 'fcp', 'main0', 'main1', 'mainms', 'litsms', 'heapKB', 'pages', 'touchedKB', 'heap5KB', 'pages5', 'touched5KB'];
console.log(cols.join('\t'));
for (let r = 0; r < R; r++) {
  for (const [name, path] of pages) {
    const b = await launch();
    try {
      await b.send('Page.addScriptToEvaluateOnNewDocument', { source: HOOK });
      await b.send('Page.navigate', { url: `http://127.0.0.1:${srv.port}/${path}` });
      for (let i = 0; i < 200 && !(await b.evaluate('performance.getEntriesByType("paint").length > 1 && !!window.__main')); i++) await sleep(20);
      await sleep(300);
      const t = await b.evaluate(`[...performance.getEntriesByType('paint').map(e => e.startTime), ...window.__main, window.__cdomLitsMs ?? -1]`);
      await b.send('HeapProfiler.collectGarbage');
      const heap = (await b.send('Runtime.getHeapUsage')).result.usedSize / 1024;
      const [pg, touched] = await b.evaluate(MEM);
      const row = { page: name, round: r, fp: t[0], fcp: t[1], main0: t[2], main1: t[3], mainms: t[3] - t[2], litsms: t[4], heapKB: heap, pages: pg, touchedKB: touched };
      if (CYCLES) {
        for (let c = 0; c < CYCLES; c++) {
          await b.evaluate(`document.getElementById('run').click()`);
          await sleep(50);
          await b.evaluate(`document.getElementById('clear').click()`);
          await sleep(50);
        }
        await b.send('HeapProfiler.collectGarbage');
        row.heap5KB = (await b.send('Runtime.getHeapUsage')).result.usedSize / 1024;
        [row.pages5, row.touched5KB] = await b.evaluate(MEM);
      }
      rows.push(row);
      console.log(cols.map((c) => (typeof row[c] === 'number' ? +row[c].toFixed(3) : row[c] ?? '')).join('\t'));
    } finally { await b.close(); }
  }
}
const med = (a) => { const s = [...a].sort((x, y) => x - y); return s[s.length >> 1]; };
console.log('# medians');
console.log(['page', 'n', ...cols.slice(2)].join('\t'));
for (const [name] of pages) {
  const g = rows.filter((x) => x.page === name);
  console.log([name, g.length, ...cols.slice(2).map((c) => (g[0][c] === undefined ? '' : +med(g.map((x) => x[c])).toFixed(3)))].join('\t'));
}
srv.close();
