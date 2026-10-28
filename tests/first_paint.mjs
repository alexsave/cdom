// First contentful paint of the rows page three ways, from one build on one server (8701):
//  (a) ssr, JS off   the server's page with Emulation.setScriptExecutionDisabled
//  (b) ssr, resumed  the same page: the shim only puts a click listener on the mount. At the
//                    load event the hook clicks row 5's label; that click loads the wasm, and
//                    "click delivered" is when cd_event returns (the row is selected), from
//                    navigation start; "cd_start(_state) call" is the virtual first render
//  (c) csr           /csr/, the benchmark page from the same app.wasm. It starts with no rows,
//                    so the hook clicks #run (1000) or #runlots (10000) the moment cd_start
//                    returns; its FCP is the empty shell's, and "rows painted" is the first
//                    frame after the rows were built.
// Medians of RUNS loads each, cache disabled. Each timed loop waits for the 1-minute load
// average to fall under LOADMAX (default 3; bounded). Times are ms from navigation start.
// Usage: node tests/first_paint.mjs  (builds ssr/rows first)
import { spawn, execFileSync } from 'node:child_process';
import path from 'node:path';
import { launch, sleep } from '../tools/cdp.mjs';
const top = path.resolve(path.dirname(new URL(import.meta.url).pathname), '..');
const RUNS = +(process.env.RUNS || 5), BASE = 'http://127.0.0.1:8701', LOADMAX = +(process.env.LOADMAX || 3);
execFileSync(path.join(top, 'ssr/rows/build.sh'), { stdio: 'inherit' });
const srv = spawn(path.join(top, 'ssr/rows/out/server'), ['--port', '8701'], { stdio: ['ignore', 'ignore', 'inherit'] });
// Records when the app's first frame is done (cd_start / cd_start_state returning) and when the
// first event is delivered (cd_event returning).
const HOOK = `
for (const k of ['instantiateStreaming', 'instantiate']) { const f = WebAssembly[k];
  WebAssembly[k] = async function (...a) { const r = await f.apply(this, a); const e = { ...r.instance.exports };
    for (const g of ['cd_start', 'cd_start_state']) { const h = e[g]; e[g] = (...b) => { const t = performance.now(), v = h(...b); window.__ready = performance.now(); window.__start = window.__ready - t; return v; }; }
    const ev = e.cd_event; e.cd_event = () => { ev(); if (window.__delivered == null) window.__delivered = performance.now(); };
    return { module: r.module, instance: { exports: e } }; }; }
const q = new URLSearchParams(location.search), rowsTarget = +(q.get('rows') || 0);
if (rowsTarget) { // csr: ask for the rows the moment the app has started, then note when they paint
  const t = setInterval(() => { if (!window.__ready) return; clearInterval(t);
    document.getElementById(rowsTarget === 1000 ? 'run' : 'runlots').click(); window.__rowsBuilt = performance.now();
    requestAnimationFrame(() => setTimeout(() => { window.__rowsPainted = performance.now(); }, 0)); }, 0); }
if (q.get('click')) addEventListener('load', () => { window.__clicked = performance.now();
  document.querySelector('tbody tr:nth-child(5) td:nth-child(2) a').click(); });
`;
const load1 = () => +execFileSync('sysctl', ['-n', 'vm.loadavg']).toString().trim().split(/\s+/)[1];
async function calm() { let l = load1(); if (process.env.NOCALM) return l; for (let i = 0; i < 120 && l >= LOADMAX; i++) { await sleep(5000); l = load1(); } return l; }
const med = a => { const s = a.filter(x => x != null).sort((x, y) => x - y); return s.length ? s[s.length >> 1] : null; };
const b = await launch();
const rows = [];
try {
  for (let i = 0; i < 50; i++) { try { await fetch(`${BASE}/?n=0`); break; } catch { await sleep(100); } }
  await b.send('Network.enable'); await b.send('Network.setCacheDisabled', { cacheDisabled: true });
  await b.send('Page.addScriptToEvaluateOnNewDocument', { source: HOOK });
  const E = b.evaluate;
  for (const n of [1000, 10000]) {
    for (const v of ['ssr, JS off', 'ssr, resumed', 'csr']) {
      const url = v === 'csr' ? `${BASE}/csr/?rows=${n}` : v === 'ssr, resumed' ? `${BASE}/?n=${n}&click=1` : `${BASE}/?n=${n}`;
      await b.send('Emulation.setScriptExecutionDisabled', { value: v === 'ssr, JS off' });
      const la = await calm();
      const m = { fcp: [], ready: [], resp: [], rows: [], start: [], clicked: [], delivered: [] };
      for (let r = 0; r < RUNS + 1; r++) { // the first load warms the server and Chrome; dropped
        await b.send('Page.navigate', { url: 'about:blank' }); await sleep(100);
        await b.send('Page.navigate', { url });
        let s = null;
        for (let i = 0; i < 400; i++) {
          await sleep(25);
          s = await E(`JSON.stringify({ fcp: performance.getEntriesByName('first-contentful-paint')[0]?.startTime ?? null,
            resp: performance.getEntriesByType('navigation')[0]?.responseEnd || null, ready: window.__ready ?? null,
            rows: window.__rowsPainted ?? null, start: window.__start ?? null, clicked: window.__clicked ?? null,
            delivered: window.__delivered ?? null, sel: document.querySelectorAll('tbody tr.danger').length,
            n: document.querySelectorAll('tbody tr').length })`).then(JSON.parse).catch(() => null);
          if (s && s.fcp != null && s.n === n && (v !== 'csr' || s.rows != null) && (v !== 'ssr, resumed' || s.sel === 1)) break;
        }
        if (!s || s.n !== n || (v === 'ssr, resumed' && s.sel !== 1)) throw new Error(`${v} n=${n}: page did not get there (${JSON.stringify(s)})`);
        if (r === 0) continue;
        for (const k of Object.keys(m)) m[k].push(s[k]);
      }
      const row = { n, variant: v, responseEnd: med(m.resp), fcp: med(m.fcp), clicked: med(m.clicked), delivered: med(m.delivered),
        firstClick: v === 'ssr, resumed' ? med(m.delivered.map((d, i) => d - m.clicked[i])) : null,
        startCall: med(m.start), rowsPainted: med(m.rows), load: la };
      rows.push(row);
      console.log(JSON.stringify(row));
    }
  }
  await b.send('Emulation.setScriptExecutionDisabled', { value: false });
} finally { await b.close(); srv.kill('SIGTERM'); }
const f = x => x == null ? '-' : x.toFixed(1);
console.log(`\nmedian of ${RUNS} (ms from navigation start)\n| n | page | response end | FCP | click at (load) | click delivered | first click took | cd_start(_state) call | rows painted | load |\n|---|---|---|---|---|---|---|---|---|---|`);
for (const r of rows) console.log(`| ${r.n} | ${r.variant} | ${f(r.responseEnd)} | ${f(r.fcp)} | ${f(r.clicked)} | ${f(r.delivered)} | ${f(r.firstClick)} | ${f(r.startCall)} | ${f(r.rowsPainted)} | ${r.load.toFixed(2)} |`);
