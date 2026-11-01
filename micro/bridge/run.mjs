// The bridge bench driver: every arm (micro/bridge/build.sh's out/) in one headless Chrome, fresh
// loads, the arms interleaved in each round (the first arm rotates), load-gated, over CDP.
//   node micro/bridge/run.mjs <mode> [rounds] [cpu] [arms]
// modes:
//   port   the foolish cdom port's real flows, traced (devtools.timeline): a deal, a tap and the
//          Attack pill and the run after it, the bot's step and its run, a drag (40 mouse moves a
//          frame apart over the table) and its drop, and a transfer, whose shield flies as a ghost
//          drawn every frame. Each flow is a window between user timing marks; per window: the
//          main thread's tasks summed by what they ran (script, style, layout, paint, gc), frames,
//          long tasks.
//   keyed  cdom's js-framework-benchmark entry through the benchmark's operations, traced the same.
//   micro  micro/bridge/bridge.html: ns per operation per kind.
//   count  the counting builds through the port's and the keyed page's flows and the micro, untimed:
//          import calls and strings decoded per flow (one load per arm).
// Results append as JSON lines to micro/bridge/results/<mode>_x<cpu>.jsonl; summary.mjs reads them.
// LOADMAX (default 4): each load waits, bounded (180 s), for the 1-minute load average under it.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import http from 'node:http';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const OUT = path.join(HERE, 'out');
const RESULTS = path.join(HERE, 'results');
const PORT_POC = process.env.PORT_POC || '/Users/alex/Dev/foolish-cdom-port/foolish/c/cdom_poc';
const FOOLISH = path.resolve(PORT_POC, '../../..', 'foolish');
const MODE = process.argv[2];
const ROUNDS = Number(process.argv[3] || 3), CPU = Number(process.argv[4] || 1);
const DEFAULT_ARMS = { port: 'a,list,b,c,d,e,f,e_t,e_css', keyed: 'a,list,b,c,d,e', micro: 'a,list,b,c,d,e,f', count: 'a,list,b,c,d,e,f,e_t,e_css' };
const ARMS = (process.argv[5] || DEFAULT_ARMS[MODE] || '').split(',').filter(Boolean);
const LOADMAX = Number(process.env.LOADMAX || 4);
const HTTP_PORT = 8741, CDP_PORT = 8742;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
if (!DEFAULT_ARMS[MODE]) { console.error('modes: port keyed micro count'); process.exit(2); }

// ---- the server: out/ first, then the foolish tree (the port's /src/styles), cross-origin isolated
const TYPES = { '.html': 'text/html', '.css': 'text/css', '.js': 'text/javascript', '.wasm': 'application/wasm',
  '.png': 'image/png', '.svg': 'image/svg+xml', '.woff2': 'font/woff2', '.jpg': 'image/jpeg', '.json': 'application/json' };
function serve() {
  const server = http.createServer((req, res) => {
    const u = decodeURIComponent(new URL(req.url, 'http://x').pathname);
    let f = null;
    for (const root of [OUT, FOOLISH]) {
      const c = path.join(root, u);
      if (c.startsWith(root) && fs.existsSync(c) && fs.statSync(c).isFile()) { f = c; break; }
    }
    if (!f) { res.writeHead(404); res.end(); return; }
    res.writeHead(200, { 'content-type': TYPES[path.extname(f)] || 'application/octet-stream', 'cache-control': 'no-store',
      'cross-origin-opener-policy': 'same-origin', 'cross-origin-embedder-policy': 'require-corp' });
    fs.createReadStream(f).pipe(res);
  });
  return new Promise((r) => server.listen(HTTP_PORT, '127.0.0.1', () => r(server)));
}
const ORIGIN = `http://127.0.0.1:${HTTP_PORT}`;

// ---- headless Chrome over CDP (one per process, killed by its own PID; the target closed in finally)
async function launch() {
  const busy = await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`).then(() => true, () => false);
  if (busy) throw new Error(`a Chrome already listens on ${CDP_PORT}: stop it by its PID first`);
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'cdom-bridge-chrome-'));
  const flags = (process.env.CHROME_FLAGS || '').split(' ').filter(Boolean);
  const proc = spawn('/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    ['--headless=new', `--remote-debugging-port=${CDP_PORT}`, `--user-data-dir=${dir}`, '--no-first-run',
      '--no-default-browser-check', '--disable-extensions', '--hide-scrollbars', '--window-size=1024,768', ...flags, 'about:blank'],
    { stdio: 'ignore' });
  let ws, targetId;
  const close = async () => {
    try { if (targetId) await send('Target.closeTarget', { targetId }); } catch { /* gone */ }
    try { ws?.close(); } catch { /* gone */ }
    proc.kill('SIGTERM');
    for (let n = 0; n < 20 && proc.exitCode === null && proc.signalCode === null; n++) await sleep(100);
    if (proc.exitCode === null && proc.signalCode === null) proc.kill('SIGKILL');
    try { fs.rmSync(dir, { recursive: true, force: true }); } catch { /* busy */ }
  };
  let ver;
  for (let i = 0; i < 60 && !ver; i++) {
    try { ver = await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`)).json(); } catch { await sleep(200); }
  }
  if (!ver) { await close(); throw new Error('chrome did not start'); }
  ws = new WebSocket(ver.webSocketDebuggerUrl);
  await new Promise((r, j) => { ws.onopen = r; ws.onerror = j; });
  let id = 0;
  const pend = new Map(), listeners = [];
  ws.onmessage = (m) => {
    const d = JSON.parse(m.data);
    if (d.id && pend.has(d.id)) { pend.get(d.id)(d); pend.delete(d.id); return; }
    for (const l of listeners) if (l.method === d.method) l.fn(d.params);
  };
  const send = (method, params = {}, sessionId) => new Promise((r) => {
    const i = ++id; pend.set(i, r); ws.send(JSON.stringify({ id: i, method, params, sessionId }));
  });
  ({ result: { targetId } } = await send('Target.createTarget', { url: 'about:blank' }));
  const { result: { sessionId } } = await send('Target.attachToTarget', { targetId, flatten: true });
  const s = async (m, p) => { const r = await send(m, p, sessionId); if (r.error) throw new Error(`${m}: ${r.error.message}`); return r.result; };
  await s('Page.enable'); await s('Runtime.enable');
  await s('Emulation.setDeviceMetricsOverride', { width: 1024, height: 768, deviceScaleFactor: 1, mobile: false });
  const evaluate = async (expr) => {
    const r = await s('Runtime.evaluate', { expression: expr, returnByValue: true, awaitPromise: true });
    if (r.exceptionDetails) throw new Error(r.exceptionDetails.exception?.description || JSON.stringify(r.exceptionDetails));
    return r.result.value;
  };
  const listen = (method, fn) => { const l = { method, fn }; listeners.push(l); return () => listeners.splice(listeners.indexOf(l), 1); };
  const goto = async (url) => {
    const loaded = new Promise((r) => { const off = listen('Page.loadEventFired', () => { off(); r(); }); });
    await s('Page.navigate', { url }); await loaded;
  };
  const until = async (expr, ms = 10000) => {
    for (let n = 0; n < ms / 50; n++) { if (await evaluate(expr)) return; await sleep(50); }
    throw new Error(`timed out waiting for ${expr}`);
  };
  return { send: s, sendBrowser: send, evaluate, listen, goto, until, close, version: ver.Browser };
}

// ---- load gate
async function gate() {
  const t0 = Date.now();
  while (os.loadavg()[0] >= LOADMAX && Date.now() - t0 < 180000) await sleep(5000);
  return { load: +os.loadavg()[0].toFixed(2), waited: Math.round((Date.now() - t0) / 1000), over: os.loadavg()[0] >= LOADMAX };
}

// ---- the trace: devtools.timeline and user timing, collected per load
async function traceStart(b) {
  const events = [];
  const off = b.listen('Tracing.dataCollected', (p) => { for (const e of p.value) events.push(e); });
  await b.send('Tracing.start', { categories: 'devtools.timeline,blink.user_timing', transferMode: 'ReportEvents' });
  return async () => {
    const done = new Promise((r) => { const o = b.listen('Tracing.tracingComplete', () => { o(); r(); }); });
    await b.send('Tracing.end'); await done; off();
    return events;
  };
}
const KIND = { UpdateLayoutTree: 'style', RecalculateStyles: 'style', Layout: 'layout', Paint: 'paint', PrePaint: 'paint',
  Layerize: 'paint', 'Decode Image': 'other', ImageDecodeTask: 'other', MinorGC: 'gc', MajorGC: 'gc', 'V8.GC_SCAVENGER': 'gc',
  'V8.GC_MARK_COMPACTOR': 'gc', 'V8.GCIncrementalMarking': 'gc', 'V8.GCFinalizeMC': 'gc', BlinkGC: 'gc' };
// Per window between consecutive marks: tasks summed, script being what is left of a task after the
// rest (framepace.mjs's rule), frames (tasks that painted), long tasks (over 50 ms).
function windows(events, names) {
  const marks = events.filter((e) => e.cat && e.cat.includes('blink.user_timing') && names.includes(e.name));
  if (!marks.length) throw new Error('no marks in the trace');
  const { pid, tid } = marks[0];
  const mine = events.filter((e) => e.pid === pid && e.tid === tid && e.ph === 'X');
  const tasks = mine.filter((e) => e.name === 'RunTask').sort((x, y) => x.ts - y.ts);
  const kids = mine.filter((e) => KIND[e.name]).sort((x, y) => x.ts - y.ts);
  const order = names.map((n) => marks.find((m) => m.name === n)).filter(Boolean).sort((x, y) => x.ts - y.ts);
  const end = events.filter((e) => e.name === 'bridge:end' && e.pid === pid)[0];
  const out = {};
  for (let k = 0; k < order.length; k++) {
    const from = order[k].ts, to = k + 1 < order.length ? order[k + 1].ts : (end ? end.ts : Infinity);
    const w = { total: 0, script: 0, style: 0, layout: 0, paint: 0, gc: 0, other: 0, frames: 0, long: 0, max: 0, tasks: 0 };
    for (const t of tasks) {
      if (t.ts < from || t.ts >= to) continue;
      const ms = t.dur / 1000;
      const parts = { style: 0, layout: 0, paint: 0, gc: 0, other: 0 };
      let painted = false;
      // nested kids of one kind (a Layout inside a Layout) count once: only the outermost
      let edge = -1;
      for (const e of kids) {
        if (e.ts < t.ts) continue;
        if (e.ts > t.ts + t.dur) break;
        if (e.name === 'Paint' || e.name === 'PrePaint') painted = true;
        if (e.ts < edge) continue;
        parts[KIND[e.name]] += e.dur / 1000;
        edge = e.ts + e.dur;
      }
      const rest = Object.values(parts).reduce((a, v) => a + v, 0);
      w.total += ms; w.tasks++;
      w.script += Math.max(0, ms - rest);
      for (const k2 of Object.keys(parts)) w[k2] += parts[k2];
      if (painted) w.frames++;
      if (ms > 50) w.long++;
      if (ms > w.max) w.max = ms;
    }
    for (const k2 of Object.keys(w)) if (typeof w[k2] === 'number' && !Number.isInteger(w[k2])) w[k2] = +w[k2].toFixed(3);
    out[order[k].name.slice(4)] = w;
  }
  return out;
}

// ---- input
const centre = (b, sel) => b.evaluate(`(() => { const e = document.querySelector(${JSON.stringify(sel)}); if (!e) return null;
  const r = e.getBoundingClientRect(); return { x: r.left + r.width / 2, y: r.top + r.height / 2 }; })()`);
const mouse = (b, type, at, buttons) => b.send('Input.dispatchMouseEvent',
  { type, x: at.x, y: at.y, button: type === 'mouseMoved' && !buttons ? 'none' : 'left', buttons, clickCount: type === 'mouseMoved' ? 0 : 1 });
async function click(b, at) { await mouse(b, 'mousePressed', at, 1); await mouse(b, 'mouseReleased', at, 0); }
const mark = (b, name) => b.evaluate(`performance.mark(${JSON.stringify('seg:' + name)}), 0`);

// ---- the port's flows
const HAND_6S = '[data-hand-plane] [data-card="0-5"]';
const PILL = (name) => `[...document.querySelectorAll('[data-touch-interactive] > [style*="z-index: 999"] button')].find((e) => e.textContent.trim() === ${JSON.stringify(name)})`;
const PORT_SEGS = ['deal', 'select', 'play', 'bot', 'drag', 'drop', 'transfer'];
const busy = (b) => b.evaluate('window.app.then((x) => x.app_anim_busy())');
async function settle(b, ms, limit = 15000) { // a fixed wait, then until the animations are done (bounded)
  await sleep(ms);
  const t0 = Date.now();
  while (await busy(b)) { if (Date.now() - t0 > limit) throw new Error('animations never ended'); await sleep(200); }
}
async function portLoad(b, arm, count) {
  await b.send('Storage.clearDataForOrigin', { origin: ORIGIN, storageTypes: 'indexeddb' });
  await b.goto(`${ORIGIN}/port/${arm}${count ? '_count' : ''}/index.html?notex&debug`);
  await b.until('window.appReady');
  await b.until('window.app.then((x) => x.app_game_store(1) > 0)');
  if (await b.evaluate('window.app.then((x) => x.app_game_store(2))')) throw new Error('a kept game was adopted');
  await b.evaluate('window.app.then((x) => x.app_debug_hold(1))');
  await sleep(500);
  const counts = {};
  const snap = () => (count ? b.evaluate('({ ...globalThis.__cdomCross })') : null);
  let before = await snap();
  const seg = async (name, fn) => {
    await mark(b, name);
    await fn();
    if (count) { const now = await snap(); counts[name] = delta(before, now); before = now; }
  };
  const stop = count ? null : await traceStart(b);
  await sleep(200);
  await seg('deal', async () => { await b.evaluate('window.app.then((x) => { setTimeout(() => x.app_debug_deal(), 0); })'); await settle(b, 5500); });
  // the golden board for the play, the bot and the drag, set in a window of its own ('prep'), so
  // the setup is in none of the measured ones
  await mark(b, 'prep');
  await b.evaluate('window.app.then((x) => x.app_debug_board(0))'); await settle(b, 600);
  if (count) before = await snap();
  const six = await centre(b, HAND_6S);
  await seg('select', async () => { await click(b, six); await sleep(400); });
  const pill = await b.evaluate(`(() => { const e = ${PILL('Attack')}; if (!e) return null; const r = e.getBoundingClientRect(); return { x: r.left + r.width / 2, y: r.top + r.height / 2 }; })()`);
  if (!pill) throw new Error('no Attack pill');
  await seg('play', async () => { await click(b, pill); await settle(b, 1800); });
  await b.until('window.app.then((x) => x.app_debug_due())');
  if (count) before = await snap();
  await seg('bot', async () => { await b.evaluate('window.app.then((x) => x.app_debug_release())'); await settle(b, 2200); });
  await mark(b, 'prep2');
  await b.evaluate('window.app.then((x) => x.app_debug_board(0))'); await settle(b, 600);
  const from = await centre(b, HAND_6S);
  const onto = await centre(b, '[data-table-container] [data-battle-index][data-card="3-5"]');
  if (count) before = await snap();
  await seg('drag', async () => {
    await mouse(b, 'mousePressed', from, 1);
    const path = [];
    for (let k = 1; k <= 20; k++) path.push({ x: from.x + (512 - from.x) * k / 20, y: from.y + (300 - from.y) * k / 20 });
    for (let k = 1; k <= 10; k++) path.push({ x: 512 + (onto.x - 512) * k / 10, y: 300 + (onto.y - 300) * k / 10 });
    for (let k = 1; k <= 10; k++) path.push({ x: onto.x + (512 - onto.x) * k / 10, y: onto.y + (300 - onto.y) * k / 10 });
    for (const at of path) { await mouse(b, 'mouseMoved', at, 1); await sleep(16); }
    await sleep(100);
  });
  await seg('drop', async () => { await mouse(b, 'mouseReleased', { x: 512, y: 300 }, 0); await settle(b, 1800); });
  await mark(b, 'prep3');
  await b.evaluate('window.app.then((x) => x.app_debug_transfer())'); await settle(b, 600);
  await b.until('window.app.then((x) => x.app_debug_due())');
  if (count) before = await snap();
  await seg('transfer', async () => { await b.evaluate('window.app.then((x) => x.app_debug_release())'); await settle(b, 2200); });
  await b.evaluate(`performance.mark('bridge:end'), 0`);
  if (count) return { counts };
  const events = await stop();
  const w = windows(events, [...PORT_SEGS, 'prep', 'prep2', 'prep3'].map((n) => 'seg:' + n));
  delete w.prep; delete w.prep2; delete w.prep3;
  return { windows: w };
}

// ---- the keyed page's operations (js-framework-benchmark's)
const KEYED_SEGS = ['create1k', 'replace1k', 'update', 'select', 'swap', 'remove', 'clear1k', 'create10k', 'clear10k', 'append'];
async function keyedLoad(b, arm, count) {
  await b.goto(`${ORIGIN}/keyed/${arm}${count ? '_count' : ''}/index.html`);
  await b.until('!!document.getElementById("run") && !!window.cdom');
  await sleep(300);
  const counts = {};
  const snap = () => (count ? b.evaluate('({ ...globalThis.__cdomCross })') : null);
  let before = await snap();
  const stop = count ? null : await traceStart(b);
  await sleep(200);
  const op = async (name, sel, wait) => {
    const at = await centre(b, sel);
    if (!at) throw new Error(`no ${sel}`);
    if (count) before = await snap();
    await mark(b, name);
    await click(b, at);
    await sleep(wait);
    if (count) { const now = await snap(); counts[name] = delta(before, now); before = now; }
    await mark(b, 'gap_' + name); // the next lookup is in a window of its own
  };
  await op('create1k', '#run', 600);
  await op('replace1k', '#run', 600);
  await op('update', '#update', 400);
  await op('select', 'tbody tr:nth-child(2) td:nth-child(2) a', 400);
  await op('swap', '#swaprows', 400);
  await op('remove', 'tbody tr:nth-child(4) td:nth-child(3) a span', 400);
  await op('clear1k', '#clear', 400);
  await op('create10k', '#runlots', 3000);
  await op('clear10k', '#clear', 1200);
  await op('run_for_append', '#run', 600);
  await op('append', '#add', 800);
  await b.evaluate(`performance.mark('bridge:end'), 0`);
  if (count) { delete counts.run_for_append; return { counts }; }
  const events = await stop();
  const names = [...KEYED_SEGS, 'run_for_append'].flatMap((n) => ['seg:' + n, 'seg:gap_' + n]);
  const w = windows(events, names);
  for (const k of Object.keys(w)) if (k.startsWith('gap_') || k === 'run_for_append') delete w[k];
  return { windows: w };
}

function delta(a, b) {
  const d = {};
  let calls = 0;
  for (const k of Object.keys(b)) {
    const v = (b[k] | 0) - (a[k] | 0);
    if (!v) continue;
    if (k[0] === '$') d[k.slice(1)] = v; else { d[k] = v; calls += v; }
  }
  d.calls = calls;
  return d;
}

// ---- micro
async function microLoad(b, arm, count) {
  await b.goto(`${ORIGIN}/micro/${arm}${count ? '_count' : ''}/index.html${count ? '?count=1' : ''}`);
  for (let n = 0; n < 1200; n++) {
    const r = await b.evaluate('window.RESULT');
    if (r) { if (r.error) throw new Error(r.error); return r; }
    await sleep(100);
  }
  throw new Error('micro: no result');
}

// ---- main
fs.mkdirSync(RESULTS, { recursive: true });
const server = await serve();
const b = await launch();
const file = path.join(RESULTS, `${MODE}_x${CPU}.jsonl`);
try {
  await b.send('Network.enable');
  await b.send('Network.setCacheDisabled', { cacheDisabled: true });
  if (CPU > 1) await b.send('Emulation.setCPUThrottlingRate', { rate: CPU });
  const session = new Date().toISOString();
  console.log(`${MODE}: ${ARMS.join(' ')}, ${ROUNDS} rounds, CPU x${CPU}, ${b.version}, ${process.env.CHROME_FLAGS || 'no extra flags'}`);
  if (MODE === 'count') {
    const out = { session, mode: 'count', chrome: b.version, arms: {} };
    for (const arm of ARMS) {
      out.arms[arm] = { port: (await portLoad(b, arm, true)).counts };
      if (DEFAULT_ARMS.keyed.split(',').includes(arm)) out.arms[arm].keyed = (await keyedLoad(b, arm, true)).counts;
      if (DEFAULT_ARMS.micro.split(',').includes(arm)) out.arms[arm].micro = await microLoad(b, arm, true);
      console.log(arm, JSON.stringify(out.arms[arm].port));
      await b.goto('about:blank');
    }
    fs.appendFileSync(file, JSON.stringify(out) + '\n');
  } else {
    for (let r = 0; r < ROUNDS; r++) {
      for (let k = 0; k < ARMS.length; k++) {
        const arm = ARMS[(k + r) % ARMS.length];
        const g = await gate();
        await b.goto('about:blank'); await sleep(300); // tear down the last load before the next
        let res;
        if (MODE === 'port') res = await portLoad(b, arm, false);
        else if (MODE === 'keyed') res = await keyedLoad(b, arm, false);
        else res = { micro: await microLoad(b, arm, false) };
        const row = { session, mode: MODE, cpu: CPU, round: r, arm, ...g, loadAfter: +os.loadavg()[0].toFixed(2), flags: process.env.CHROME_FLAGS || '', chrome: b.version, ...res };
        fs.appendFileSync(file, JSON.stringify(row) + '\n');
        const brief = res.windows ? Object.entries(res.windows).map(([n, w]) => `${n} ${w.script.toFixed(2)}/${w.total.toFixed(1)}`).join('  ') : JSON.stringify(res.micro);
        console.log(`r${r} ${arm.padEnd(6)} load ${g.load}${g.over ? ' OVER' : ''}  ${brief}`);
      }
    }
  }
} finally {
  await b.close();
  server.close();
}
