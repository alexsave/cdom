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
// Then the wasm prefetch (shim/cdom.js: after the load event the module is fetched, compiled
// and instantiated, and nothing of it runs until an event), against the click-triggered load
// and the other places the fetch could start. Each variant is the shipped shim with one line
// changed, served from its own copy of ssr/rows/out (server --root):
//   click    the line removed: the first event fetches and compiles (before the prefetch)
//   load     the shipped shim: prefetch on the load event
//   preload  the shipped shim plus <link rel=preload as=fetch crossorigin fetchpriority=low>
//            for app.wasm, put in <head> by the hook before the parser runs (where a server
//            would write it), so the bytes download during the parse and compile at load
//   resume   prefetch() called inside cdom.resume, as the parser reaches the end of <body>
//   idle     prefetch from requestIdleCallback after the load event
// For each, at n = 1000 and 10000, row 5's label is clicked 50, 500 and 2000 ms after the load
// event: FCP, when the module was instantiated, and how long that first click took (click to
// cd_event returning). The check: on the shipped shim a click 2000 ms after load must find the
// module instantiated already in every run and take under half the click-triggered latency.
// Medians of RUNS loads each, cache disabled. Each timed loop waits for the 1-minute load
// average to fall under LOADMAX (default 3; bounded). Times are ms from navigation start.
// Usage: node tests/first_paint.mjs  (builds ssr/rows first)
//        PART=prefetch|paint to run one half; VARIANTS="click load" to choose prefetch variants.
// Exit status 1 when the prefetch check fails.
import { spawn, execFileSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { launch, sleep } from '../tools/cdp.mjs';
const top = path.resolve(path.dirname(new URL(import.meta.url).pathname), '..');
const RUNS = +(process.env.RUNS || 5), BASE = 'http://127.0.0.1:8701', LOADMAX = +(process.env.LOADMAX || 3);
execFileSync(path.join(top, 'ssr/rows/build.sh'), { stdio: 'inherit' });
const PART = process.env.PART || 'all';
let srv = spawn(path.join(top, 'ssr/rows/out/server'), ['--port', '8701'], { stdio: ['ignore', 'ignore', 'inherit'] });
// Records when the app's first frame is done (cd_start / cd_start_state returning) and when the
// first event is delivered (cd_event returning).
const HOOK = `
for (const k of ['instantiateStreaming', 'instantiate']) { const f = WebAssembly[k];
  WebAssembly[k] = async function (...a) { const r = await f.apply(this, a); window.__inst ??= performance.now(); const e = { ...r.instance.exports };
    for (const g of ['cd_start', 'cd_start_state']) { const h = e[g]; e[g] = (...b) => { const t = performance.now(), v = h(...b); window.__ready = performance.now(); window.__start = window.__ready - t; return v; }; }
    const ev = e.cd_event; e.cd_event = () => { ev(); if (window.__delivered == null) window.__delivered = performance.now(); };
    return { module: r.module, instance: { exports: e } }; }; }
const q = new URLSearchParams(location.search), rowsTarget = +(q.get('rows') || 0);
if (rowsTarget) { // csr: ask for the rows the moment the app has started, then note when they paint
  const t = setInterval(() => { if (!window.__ready) return; clearInterval(t);
    document.getElementById(rowsTarget === 1000 ? 'run' : 'runlots').click(); window.__rowsBuilt = performance.now();
    requestAnimationFrame(() => setTimeout(() => { window.__rowsPainted = performance.now(); }, 0)); }, 0); }
const clickAt = +(q.get('click') || 0), click5 = () => { window.__clicked = performance.now();
  document.querySelector('tbody tr:nth-child(5) td:nth-child(2) a').click(); };
if (clickAt) addEventListener('load', () => clickAt === 1 ? click5() : setTimeout(click5, clickAt)); // 1: in the load event itself
if (q.get('preload')) { const l = document.createElement('link'); l.rel = 'preload'; l.as = 'fetch'; l.crossOrigin = '';
  l.setAttribute('fetchpriority', 'low'); l.href = 'app.wasm'; document.documentElement.appendChild(l); }
`;
const load1 = () => +execFileSync('sysctl', ['-n', 'vm.loadavg']).toString().trim().split(/\s+/)[1];
async function calm() { let l = load1(); if (process.env.NOCALM) return l; for (let i = 0; i < 120 && l >= LOADMAX; i++) { await sleep(5000); l = load1(); } return l; }
const med = a => { const s = a.filter(x => x != null).sort((x, y) => x - y); return s.length ? s[s.length >> 1] : null; };
// The prefetch variants: the shipped shim with its one prefetch line changed (see the header).
const LINE = '    addEventListener("load", prefetch);\n';
const VARIANTS = { click: '', load: LINE, preload: LINE, resume: '    prefetch();\n', idle: '    addEventListener("load", () => requestIdleCallback(prefetch));\n' };
const pf = [], DELAYS = [50, 500, 2000];
let pfFail = 0;
async function prefetchPart() {
  const shim = fs.readFileSync(path.join(top, 'ssr/rows/out/cdom.js'), 'utf8');
  if (!shim.includes(LINE)) throw new Error('the shim has no prefetch line to vary');
  for (const v of (process.env.VARIANTS || 'click load preload resume idle').split(' ')) {
    const root = path.join(top, 'ssr/rows/out/prefetch', v);
    fs.mkdirSync(root, { recursive: true });
    for (const f of ['app.wasm', 'css']) fs.cpSync(path.join(top, 'ssr/rows/out', f), path.join(root, f), { recursive: true });
    fs.writeFileSync(path.join(root, 'cdom.js'), shim.replace(LINE, VARIANTS[v]));
    srv.kill('SIGTERM'); await sleep(300);
    srv = spawn(path.join(top, 'ssr/rows/out/server'), ['--port', '8701', '--root', root], { stdio: ['ignore', 'ignore', 'inherit'] });
    for (let i = 0; i < 50; i++) { try { await fetch(`${BASE}/?n=0`); break; } catch { await sleep(100); } }
    for (const n of [1000, 10000]) for (const d of DELAYS) {
      const url = `${BASE}/?n=${n}&click=${d}${v === 'preload' ? '&preload=1' : ''}`;
      const m = { fcp: [], inst: [], lat: [], instBefore: [], wasmStart: [], loadEv: [] }, loads = [];
      for (let r = 0; r < RUNS + 1; r++) { // the first load warms the server and Chrome; dropped
        const la = await calm();
        await b.send('Page.navigate', { url: 'about:blank' }); await sleep(100);
        await b.send('Page.navigate', { url });
        let s = null;
        for (let i = 0; i < 400; i++) {
          await sleep(25);
          s = await b.evaluate(`JSON.stringify({ fcp: performance.getEntriesByName('first-contentful-paint')[0]?.startTime ?? null,
            inst: window.__inst ?? null, clicked: window.__clicked ?? null, delivered: window.__delivered ?? null,
            wasmStart: performance.getEntriesByType('resource').find(e => /app\\.wasm/.test(e.name))?.startTime ?? null,
            loadEv: performance.getEntriesByType('navigation')[0]?.loadEventStart || null,
            sel: document.querySelectorAll('tbody tr.danger').length })`).then(JSON.parse).catch(() => null);
          if (s && s.fcp != null && s.delivered != null && s.sel === 1) break;
        }
        if (!s || s.delivered == null || s.sel !== 1) throw new Error(`prefetch ${v} n=${n} click+${d}: the click was not delivered (${JSON.stringify(s)})`);
        if (r === 0) continue;
        loads.push(la); m.fcp.push(s.fcp); m.inst.push(s.inst); m.lat.push(s.delivered - s.clicked); m.instBefore.push(s.inst < s.clicked);
        m.wasmStart.push(s.wasmStart); m.loadEv.push(s.loadEv);
      }
      const row = { variant: v, n, clickAfterLoad: d, fcp: med(m.fcp), loadEvent: med(m.loadEv), wasmRequested: med(m.wasmStart), instantiated: med(m.inst),
        firstClick: med(m.lat), instantiatedBeforeClick: m.instBefore.filter(Boolean).length, runs: m.lat.length,
        loads: [Math.min(...loads), Math.max(...loads)], all: m.lat.map(x => +x.toFixed(1)) };
      pf.push(row); console.log(JSON.stringify(row));
    }
  }
  const f = x => x == null ? '-' : x.toFixed(1);
  console.log(`\nprefetch: median of ${RUNS} (ms from navigation start; first click = click to cd_event returning)\n| variant | n | click after load | FCP | load event | wasm requested | instantiated | first click took | instantiated before the click | loads |\n|---|---|---|---|---|---|---|---|---|---|`);
  for (const r of pf) console.log(`| ${r.variant} | ${r.n} | ${r.clickAfterLoad} | ${f(r.fcp)} | ${f(r.loadEvent)} | ${f(r.wasmRequested)} | ${f(r.instantiated)} | ${f(r.firstClick)} | ${r.instantiatedBeforeClick}/${r.runs} | ${r.loads[0].toFixed(2)}-${r.loads[1].toFixed(2)} |`);
  // The check (the shipped shim against the click-triggered one), when both ran.
  for (const n of [1000, 10000]) {
    const ld = pf.find(r => r.variant === 'load' && r.n === n && r.clickAfterLoad === 2000), ck = pf.find(r => r.variant === 'click' && r.n === n && r.clickAfterLoad === 2000);
    if (!ld || !ck) continue;
    const okb = ld.instantiatedBeforeClick === ld.runs && ld.firstClick < ck.firstClick / 2;
    if (!okb) pfFail++;
    console.log(`${okb ? 'ok  ' : 'FAIL'} n=${n}: a click 2000 ms after load found the module instantiated in ${ld.instantiatedBeforeClick}/${ld.runs} runs and took ${f(ld.firstClick)} ms against ${f(ck.firstClick)} click-triggered`);
  }
}
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
      if (PART === 'prefetch') continue;
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
  if (PART !== 'paint') await prefetchPart();
} finally { await b.close(); srv.kill('SIGTERM'); }
const f = x => x == null ? '-' : x.toFixed(1);
console.log(`\nmedian of ${RUNS} (ms from navigation start)\n| n | page | response end | FCP | click at (load) | click delivered | first click took | cd_start(_state) call | rows painted | load |\n|---|---|---|---|---|---|---|---|---|---|`);
for (const r of rows) console.log(`| ${r.n} | ${r.variant} | ${f(r.responseEnd)} | ${f(r.fcp)} | ${f(r.clicked)} | ${f(r.delivered)} | ${f(r.firstClick)} | ${f(r.startCall)} | ${f(r.rowsPainted)} | ${r.load.toFixed(2)} |`);
if (pfFail) { console.log(`${pfFail} prefetch check(s) FAILED`); process.exit(1); }
