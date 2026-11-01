// The bridge bench driver: the arms (micro/bridge/build.sh's out/) in one headless Chrome per command,
// each load fresh in a target of its own, the arms in a random order per round, over CDP.
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
//   count  the counting builds through the port's flows, the keyed operations and the micro, untimed:
//          import calls, export calls and strings decoded per flow, and the DOM each flow leaves
//          (a hash of the mount's HTML, and one with the movers' style text left out). Arm a runs
//          twice: a flow whose DOM differs between its two loads is unstable and only its own checks
//          apply to it. Every other arm's DOM must equal arm a's after every flow (exit 1 if not);
//          e_t and e_css may differ only in the movers' style text, and only while a drag is up.
//          The hashes are the reference the timed modes check every load against.
//   same   micro/bridge/same.html per arm: every arm's DOM after each step and C's reads equal to
//          arm a's, or exit 1 naming the first difference.
// Every load checks the app did the work: each window ends in a check (in a window of its own, not
// measured) of the DOM it left against count mode's reference hash for that arm and the flow's own
// assertions (row counts and the like); a load that fails one is kept but marked invalid, and
// summary.mjs leaves it out.
// The gate: before Chrome starts, the 1-minute load average under LOADMAX (default 4, waited for at
// most 120 s); per load, the CPU of every process but this one and its Chrome (ps), recorded, and the
// load marked busy above OTHERS_MAX (default 150, per cent of one core). Each command stops starting
// loads past BUDGET_S (default 780 s) of wall time, and says so.
// Results append as JSON lines to micro/bridge/results/<mode>_x<cpu>[_TAG].jsonl; summary.mjs reads them.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import http from 'node:http';
import { spawn, execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const T0 = Date.now();
const HERE = path.dirname(fileURLToPath(import.meta.url));
const OUT = path.join(HERE, 'out');
const RESULTS = path.join(HERE, 'results');
const PORT_POC = process.env.PORT_POC || '/Users/alex/Dev/foolish-cdom-port/foolish/c/cdom_poc';
const FOOLISH = path.resolve(PORT_POC, '../..');
const MODE = process.argv[2];
const ROUNDS = Number(process.argv[3] || 1), CPU = Number(process.argv[4] || 1);
const DEFAULT_ARMS = { port: 'a,list,b,c,d,e,f,e_t,e_css', keyed: 'a,list,b,c,d,e', micro: 'a,list,b,c,d,e,f',
  count: 'a,list,b,c,d,e,f,e_t,e_css', same: 'a,list,b,c,d,e,f' };
if (!DEFAULT_ARMS[MODE]) { console.error('modes: port keyed micro count same'); process.exit(2); }
const ARMS = (process.argv[5] || DEFAULT_ARMS[MODE]).split(',').filter(Boolean);
const KEYED_ARMS = DEFAULT_ARMS.keyed.split(','), MICRO_ARMS = DEFAULT_ARMS.micro.split(',');
const LOADMAX = Number(process.env.LOADMAX || 4), OTHERS_MAX = Number(process.env.OTHERS_MAX || 150);
const BUDGET_MS = Number(process.env.BUDGET_S || 780) * 1000;
const PER_LOAD_MS = { port: 45000, keyed: 25000, micro: 60000, count: 120000, same: 5000 }[MODE];
const ROUND0 = Number(process.env.ROUND || 0); // bench.sh runs one round per command; it seeds the order
const HTTP_PORT = 8741, CDP_PORT = 8742;
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// ---- cleanup: our Chrome and our server go however the process ends
let chromeProc = null, server = null, chromeDir = null;
function cleanup() {
  if (chromeProc && chromeProc.exitCode === null && chromeProc.signalCode === null) { try { chromeProc.kill('SIGKILL'); } catch { /* gone */ } }
  if (server) { try { server.close(); } catch { /* closed */ } }
  if (chromeDir) { try { fs.rmSync(chromeDir, { recursive: true, force: true }); } catch { /* busy */ } }
}
for (const sig of ['SIGINT', 'SIGTERM', 'SIGHUP']) process.on(sig, () => { cleanup(); process.exit(130); });
process.on('uncaughtException', (e) => { console.error(e); cleanup(); process.exit(1); });
process.on('unhandledRejection', (e) => { console.error(e); cleanup(); process.exit(1); });

// ---- the server: out/ first, then the foolish tree (the port's /src/styles), cross-origin isolated
const TYPES = { '.html': 'text/html', '.css': 'text/css', '.js': 'text/javascript', '.wasm': 'application/wasm',
  '.png': 'image/png', '.svg': 'image/svg+xml', '.woff2': 'font/woff2', '.jpg': 'image/jpeg', '.json': 'application/json' };
function serve() {
  const s = http.createServer((req, res) => {
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
  return new Promise((r, j) => { s.on('error', j); s.listen(HTTP_PORT, '127.0.0.1', () => r(s)); });
}
const ORIGIN = `http://127.0.0.1:${HTTP_PORT}`;

// Installed before any script of every page: Math.random made a fixed sequence, so cdom.start's seed
// and so the keyed labels and the port's deal are the same in every arm and every load.
const SEEDED = `(() => { let s = 0x9e3779b9; Math.random = () => { s = (s + 0x6d2b79f5) | 0; let t = Math.imul(s ^ (s >>> 15), 1 | s);
  t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t; return ((t ^ (t >>> 14)) >>> 0) / 4294967296; }; })();`;

// ---- headless Chrome over CDP: one per command; every load in a target of its own
async function launch() {
  const busy = await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`).then(() => true, () => false);
  if (busy) throw new Error(`a Chrome already listens on ${CDP_PORT}: stop it by its PID first`);
  chromeDir = fs.mkdtempSync(path.join(os.tmpdir(), 'cdom-bridge-chrome-'));
  const flags = (process.env.CHROME_FLAGS || '').split(' ').filter(Boolean);
  chromeProc = spawn('/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    ['--headless=new', `--remote-debugging-port=${CDP_PORT}`, `--user-data-dir=${chromeDir}`, '--no-first-run',
      '--no-default-browser-check', '--disable-extensions', '--hide-scrollbars', '--window-size=1024,768', ...flags, 'about:blank'],
    { stdio: 'ignore' });
  let ver;
  for (let i = 0; i < 60 && !ver; i++) {
    try { ver = await (await fetch(`http://127.0.0.1:${CDP_PORT}/json/version`)).json(); } catch { await sleep(200); }
  }
  if (!ver) throw new Error('chrome did not start');
  const ws = new WebSocket(ver.webSocketDebuggerUrl);
  await new Promise((r, j) => { ws.onopen = r; ws.onerror = j; });
  let id = 0, targetId = null, sessionId = null;
  const pend = new Map(), listeners = [];
  ws.onmessage = (m) => {
    const d = JSON.parse(m.data);
    if (d.id && pend.has(d.id)) { pend.get(d.id)(d); pend.delete(d.id); return; }
    if (d.sessionId && d.sessionId !== sessionId) return; // a closed target's late events
    for (const l of [...listeners]) if (l.method === d.method) l.fn(d.params);
  };
  const raw = (method, params = {}, sid) => new Promise((r) => {
    const i = ++id; pend.set(i, r); ws.send(JSON.stringify({ id: i, method, params, sessionId: sid }));
  });
  const send = async (m, p) => { const r = await raw(m, p, sessionId); if (r.error) throw new Error(`${m}: ${r.error.message}`); return r.result; };
  const listen = (method, fn) => { const l = { method, fn }; listeners.push(l); return () => { const k = listeners.indexOf(l); if (k >= 0) listeners.splice(k, 1); }; };
  // A new target for the next load (the last one closed): nothing of the last page's renderer,
  // heap or JIT carries over. Cache off, the CPU throttle and the viewport are per target.
  const fresh = async () => {
    if (targetId) await raw('Target.closeTarget', { targetId });
    ({ result: { targetId } } = await raw('Target.createTarget', { url: 'about:blank' }));
    ({ result: { sessionId } } = await raw('Target.attachToTarget', { targetId, flatten: true }));
    await send('Page.enable'); await send('Runtime.enable'); await send('Network.enable');
    await send('Network.setCacheDisabled', { cacheDisabled: true });
    await send('Emulation.setDeviceMetricsOverride', { width: 1024, height: 768, deviceScaleFactor: 1, mobile: false });
    if (CPU > 1) await send('Emulation.setCPUThrottlingRate', { rate: CPU });
    await send('Page.addScriptToEvaluateOnNewDocument', { source: SEEDED });
  };
  const evaluate = async (expr) => {
    const r = await send('Runtime.evaluate', { expression: expr, returnByValue: true, awaitPromise: true });
    if (r.exceptionDetails) throw new Error(r.exceptionDetails.exception?.description || JSON.stringify(r.exceptionDetails));
    return r.result.value;
  };
  const goto = async (url) => {
    const loaded = new Promise((r) => { const off = listen('Page.loadEventFired', () => { off(); r(); }); });
    await send('Page.navigate', { url }); await loaded;
    await send('Page.bringToFront'); // headless renders a target brought to the front (the port's README); no window is shown
  };
  const until = async (expr, ms = 10000) => {
    for (let n = 0; n < ms / 50; n++) { if (await evaluate(expr)) return; await sleep(50); }
    throw new Error(`timed out waiting for ${expr}`);
  };
  const close = async () => {
    try { if (targetId) await raw('Target.closeTarget', { targetId }); } catch { /* gone */ }
    try { ws.close(); } catch { /* gone */ }
    chromeProc.kill('SIGTERM');
    for (let n = 0; n < 20 && chromeProc.exitCode === null && chromeProc.signalCode === null; n++) await sleep(100);
    cleanup();
  };
  return { send, evaluate, listen, goto, until, fresh, close, version: ver.Browser };
}

// ---- the gate
async function gateBeforeLaunch() {
  const t0 = Date.now();
  while (os.loadavg()[0] >= LOADMAX && Date.now() - t0 < 120000) await sleep(5000);
  return { load: +os.loadavg()[0].toFixed(2), waited: Math.round((Date.now() - t0) / 1000) };
}
// The CPU (per cent of one core, ps's decaying average) of every process but this node and its
// Chrome's whole tree: what else the machine is doing while a load runs.
function othersCpu() {
  const rows = execFileSync('ps', ['-A', '-o', 'pid=,ppid=,pcpu=']).toString().trim().split('\n')
    .map((l) => l.trim().split(/\s+/).map(Number));
  const kids = new Map();
  for (const [pid, ppid] of rows) { if (!kids.has(ppid)) kids.set(ppid, []); kids.get(ppid).push(pid); }
  const mine = new Set([process.pid]);
  const stack = [process.pid];
  while (stack.length) for (const k of kids.get(stack.pop()) || []) if (!mine.has(k)) { mine.add(k); stack.push(k); }
  return +rows.filter(([pid]) => !mine.has(pid)).reduce((a, [, , c]) => a + (c || 0), 0).toFixed(1);
}
// A shuffle seeded by the round (mulberry32): the arms' order differs from round to round, and no arm
// always follows the same one.
function shuffled(list, seed) {
  let s = seed >>> 0;
  const rnd = () => { s = (s + 0x6d2b79f5) >>> 0; let t = Math.imul(s ^ (s >>> 15), 1 | s); t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t; return ((t ^ (t >>> 14)) >>> 0) / 4294967296; };
  const a = [...list];
  for (let i = a.length - 1; i > 0; i--) { const j = Math.floor(rnd() * (i + 1)); [a[i], a[j]] = [a[j], a[i]]; }
  return a;
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
// Per window between consecutive marks (every mark named seg:*): tasks summed, script being what is
// left of a task after the rest (framepace.mjs's rule), frames (tasks that painted), long tasks (over
// 50 ms). Nested events count once, the outermost.
function windows(events) {
  const marks = events.filter((e) => e.cat && e.cat.includes('blink.user_timing') && e.name.startsWith('seg:'));
  if (!marks.length) throw new Error('no marks in the trace');
  const { pid, tid } = marks[0];
  const mine = events.filter((e) => e.pid === pid && e.tid === tid && e.ph === 'X');
  const tasks = mine.filter((e) => e.name === 'RunTask').sort((x, y) => x.ts - y.ts);
  const kids = mine.filter((e) => KIND[e.name]).sort((x, y) => x.ts - y.ts);
  const order = marks.sort((x, y) => x.ts - y.ts);
  const out = {};
  for (let k = 0; k < order.length; k++) {
    const name = order[k].name.slice(4);
    if (name.startsWith('chk_') || name.startsWith('prep')) continue;
    const from = order[k].ts, to = k + 1 < order.length ? order[k + 1].ts : Infinity;
    const w = { total: 0, script: 0, style: 0, layout: 0, paint: 0, gc: 0, other: 0, frames: 0, long: 0, max: 0, tasks: 0 };
    for (const t of tasks) {
      if (t.ts < from || t.ts >= to) continue;
      const ms = t.dur / 1000;
      const parts = { style: 0, layout: 0, paint: 0, gc: 0, other: 0 };
      let painted = false, edge = -1;
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
    for (const k2 of Object.keys(w)) if (!Number.isInteger(w[k2])) w[k2] = +w[k2].toFixed(3);
    out[name] = w;
  }
  return out;
}

// ---- what a flow left: the counters, and the mount's HTML hashed (FNV-1a) as is and with the
// movers' style text left out (the ghosts' and the drag shadow's: e_t and e_css write it differently)
const SNAP = (mount) => `(() => { const m = document.getElementById(${JSON.stringify(mount)});
  const fnv = (s) => { let h = 2166136261; for (let i = 0; i < s.length; i++) h = Math.imul(h ^ s.charCodeAt(i), 16777619) >>> 0; return h; };
  const full = fnv(m.outerHTML);
  const c = m.cloneNode(true);
  for (const e of c.querySelectorAll('[data-role-flight], .drag-shadow')) e.removeAttribute('style');
  return { ...(globalThis.__cdomCross || {}), $$hash: full, $$hashMovers: fnv(c.outerHTML) }; })()`;
function delta(a, b) {
  const d = { hash: b.$$hash, hashMovers: b.$$hashMovers, exports: 0 };
  let calls = 0;
  for (const k of Object.keys(b)) {
    if (k.startsWith('$$')) continue;
    const v = (b[k] | 0) - (a[k] | 0);
    if (!v) continue;
    if (k.startsWith('$export_')) d.exports += v;
    else if (k[0] === '$') d[k.slice(1)] = v;
    else { d[k] = v; calls += v; }
  }
  d.calls = calls;
  return d;
}
// The reference: count mode's last session, each arm's DOM per flow (and which flows are unstable).
function reference() {
  const f = path.join(RESULTS, 'count_x1.jsonl');
  if (!fs.existsSync(f)) throw new Error('no reference DOM: run `run.mjs count` first');
  return JSON.parse(fs.readFileSync(f, 'utf8').trim().split('\n').at(-1));
}

// ---- input
const centre = (b, sel) => b.evaluate(`(() => { const e = document.querySelector(${JSON.stringify(sel)}); if (!e) return null;
  const r = e.getBoundingClientRect(); return { x: r.left + r.width / 2, y: r.top + r.height / 2 }; })()`);
const mouse = (b, type, at, buttons) => b.send('Input.dispatchMouseEvent',
  { type, x: at.x, y: at.y, button: type === 'mouseMoved' && !buttons ? 'none' : 'left', buttons, clickCount: type === 'mouseMoved' ? 0 : 1 });
async function click(b, at) { await mouse(b, 'mousePressed', at, 1); await mouse(b, 'mouseReleased', at, 0); }
const mark = (b, name) => b.evaluate(`performance.mark(${JSON.stringify('seg:' + name)}), 0`);

// One load's windows: seg(name, act, check) marks the window, acts, then (in a window of its own,
// chk_<name>) snapshots what it left and runs its check (a string naming what failed, or '').
function recorder(b, mount, count, ref, arm, page) {
  const counts = {}, invalid = [];
  let before = null;
  const snap = () => b.evaluate(SNAP(mount));
  return {
    counts, invalid,
    async start() { before = await snap(); },
    async seg(name, act, check) {
      await mark(b, name);
      await act();
      await mark(b, 'chk_' + name);
      const now = await snap();
      const d = delta(before, now);
      before = now;
      if (count) counts[name] = d;
      const why = check ? await check() : '';
      if (why) invalid.push(`${name}: ${why}`);
      const want = ref && ref.arms[arm] && ref.arms[arm][page] && ref.arms[arm][page][name];
      if (!count && want && !(ref.unstable[page] || []).includes(name) && want.hash !== d.hash) invalid.push(`${name}: DOM differs from the reference`);
    },
    async resync() { before = await snap(); }, // after setup between windows
  };
}

// ---- the port's flows
const HAND_6S = '[data-hand-plane] [data-card="0-5"]';
const PILL = (name) => `[...document.querySelectorAll('[data-touch-interactive] > [style*="z-index: 999"] button')].find((e) => e.textContent.trim() === ${JSON.stringify(name)})`;
const STATE = 'window.app.then((x) => { const p = x.app_debug_state(); return new TextDecoder().decode(new Uint8Array(x.memory.buffer, p, x.app_debug_len())); })';
const busy = (b) => b.evaluate('window.app.then((x) => x.app_anim_busy())');
async function settle(b, ms, limit = 15000) { // a fixed wait, then until the animations are done (bounded)
  await sleep(ms);
  const t0 = Date.now();
  while (await busy(b)) { if (Date.now() - t0 > limit) throw new Error('animations never ended'); await sleep(200); }
}
const battles = (b) => b.evaluate(`document.querySelectorAll('[data-table-container] [data-battle-index]').length`);
async function portLoad(b, arm, count, ref) {
  await b.send('Storage.clearDataForOrigin', { origin: ORIGIN, storageTypes: 'indexeddb' });
  await b.goto(`${ORIGIN}/port/${arm}${count ? '_count' : ''}/index.html?notex&debug`);
  await b.until('window.appReady');
  await b.until('window.app.then((x) => x.app_game_store(1) > 0)');
  if (await b.evaluate('window.app.then((x) => x.app_game_store(2))')) throw new Error('a kept game was adopted');
  await b.evaluate('window.app.then((x) => x.app_debug_hold(1))');
  await sleep(500);
  const rec = recorder(b, 'root', count, ref, arm, 'port');
  const stop = count ? null : await traceStart(b);
  await sleep(200);
  await rec.start();
  // the deal: a fixed seed, every card's flight (one per card, 350 ms apart)
  await mark(b, 'prep0');
  await b.evaluate('window.app.then((x) => x.app_debug_reseed(12345))');
  await rec.resync();
  await rec.seg('deal', async () => { await b.evaluate('window.app.then((x) => { setTimeout(() => x.app_debug_deal(), 0); })'); await settle(b, 5500); },
    async () => ((await b.evaluate(`document.querySelectorAll('[data-hand-plane] [data-card]').length`)) >= 6 ? '' : 'no hand dealt'));
  // the golden board for the play, the bot and the drag, set in a window of its own
  await mark(b, 'prep1');
  await b.evaluate('window.app.then((x) => x.app_debug_board(0))'); await settle(b, 600);
  const six = await centre(b, HAND_6S);
  if (!six) throw new Error('no 6s in the hand');
  const battles0 = await battles(b);
  await rec.resync();
  await rec.seg('select', async () => { await click(b, six); await sleep(400); },
    async () => ((await b.evaluate(`!!${PILL('Attack')}`)) ? '' : 'the tap offered no Attack'));
  await mark(b, 'prep2');
  const pill = await b.evaluate(`(() => { const e = ${PILL('Attack')}; if (!e) return null; const r = e.getBoundingClientRect(); return { x: r.left + r.width / 2, y: r.top + r.height / 2 }; })()`);
  if (!pill) throw new Error('no Attack pill');
  await rec.resync();
  await rec.seg('play', async () => { await click(b, pill); await settle(b, 1800); },
    async () => ((await battles(b)) === battles0 + 1 ? '' : 'the attack did not land'));
  await mark(b, 'prep3');
  await b.until('window.app.then((x) => x.app_debug_due())');
  const state0 = await b.evaluate(STATE);
  await rec.resync();
  await rec.seg('bot', async () => { await b.evaluate('window.app.then((x) => x.app_debug_release())'); await settle(b, 2200); },
    async () => ((await b.evaluate(STATE)) !== state0 ? '' : 'the bot did not move'));
  await mark(b, 'prep4');
  await b.evaluate('window.app.then((x) => x.app_debug_board(0))'); await settle(b, 600);
  const from = await centre(b, HAND_6S);
  const onto = await centre(b, '[data-table-container] [data-battle-index][data-card="3-5"]');
  if (!from || !onto) throw new Error('drag: no 6s or no 6d');
  const battles1 = await battles(b);
  await rec.resync();
  await rec.seg('drag', async () => {
    await mouse(b, 'mousePressed', from, 1);
    const route = [];
    for (let k = 1; k <= 20; k++) route.push({ x: from.x + (512 - from.x) * k / 20, y: from.y + (300 - from.y) * k / 20 });
    for (let k = 1; k <= 10; k++) route.push({ x: 512 + (onto.x - 512) * k / 10, y: 300 + (onto.y - 300) * k / 10 });
    for (let k = 1; k <= 10; k++) route.push({ x: onto.x + (512 - onto.x) * k / 10, y: onto.y + (300 - onto.y) * k / 10 });
    for (const at of route) { await mouse(b, 'mouseMoved', at, 1); await sleep(16); }
    await sleep(100);
  }, async () => ((await b.evaluate(`!!document.querySelector('.drag-shadow')`)) ? '' : 'no drag shadow up'));
  await rec.seg('drop', async () => { await mouse(b, 'mouseReleased', { x: 512, y: 300 }, 0); await settle(b, 1800); },
    async () => ((await battles(b)) === battles1 + 1 ? '' : 'the drop did not attack'));
  await mark(b, 'prep5');
  await b.evaluate('window.app.then((x) => x.app_debug_transfer())'); await settle(b, 600);
  await b.until('window.app.then((x) => x.app_debug_due())');
  await b.evaluate(`window.__ghosts = 0; new MutationObserver((l) => { for (const r of l) for (const n of r.addedNodes) if (n.querySelector && n.querySelector('[data-role-flight]')) window.__ghosts++; })
    .observe(document.getElementById('root'), { subtree: true, childList: true }), 0`);
  await rec.resync();
  await rec.seg('transfer', async () => { await b.evaluate('window.app.then((x) => x.app_debug_release())'); await settle(b, 2200); },
    async () => ((await b.evaluate('window.__ghosts')) > 0 ? '' : 'no ghost flew'));
  await mark(b, 'end');
  if (count) return { counts: rec.counts, invalid: rec.invalid };
  const w = windows(await stop());
  delete w.end;
  return { windows: w, invalid: rec.invalid };
}

// ---- the keyed page's operations (js-framework-benchmark's), each checked
async function keyedLoad(b, arm, count, ref) {
  await b.goto(`${ORIGIN}/keyed/${arm}${count ? '_count' : ''}/index.html`);
  await b.until('!!document.getElementById("run") && !!window.cdom');
  await sleep(300);
  const rec = recorder(b, 'main', count, ref, arm, 'keyed');
  const stop = count ? null : await traceStart(b);
  await sleep(200);
  await rec.start();
  const rows = () => b.evaluate(`document.querySelectorAll('tbody tr').length`);
  const cell = (k, c) => b.evaluate(`document.querySelector('tbody tr:nth-child(${k}) td:nth-child(${c})')?.textContent ?? null`);
  const op = async (name, sel, wait, check) => {
    await mark(b, 'prep_' + name);
    const at = await centre(b, sel);
    if (!at) throw new Error(`no ${sel}`);
    const ctx = { id1: await cell(1, 1), id999: await cell(999, 1) };
    await rec.resync();
    await rec.seg(name, async () => { await click(b, at); await sleep(wait); }, () => check(ctx));
  };
  const n = (want) => async () => { const got = await rows(); return got === want ? '' : `${got} rows, not ${want}`; };
  await op('create1k', '#run', 600, n(1000));
  await op('replace1k', '#run', 600, async (c) => ((await n(1000)()) || ((await cell(1, 1)) !== c.id1 ? '' : 'the rows were not replaced')));
  await op('update', '#update', 400, async () => (((await cell(1, 2)) || '').endsWith(' !!!') ? '' : 'row 1 not updated'));
  await op('select', 'tbody tr:nth-child(2) td:nth-child(2) a', 400,
    async () => ((await b.evaluate(`document.querySelectorAll('tbody tr.danger').length === 1 && document.querySelector('tbody tr:nth-child(2)').className === 'danger'`)) ? '' : 'row 2 not selected'));
  await op('swap', '#swaprows', 400, async (c) => ((await cell(2, 1)) === c.id999 ? '' : 'rows 2 and 999 not swapped'));
  await op('remove', 'tbody tr:nth-child(4) td:nth-child(3) a span', 400, n(999));
  await op('clear1k', '#clear', 400, n(0));
  await op('create10k', '#runlots', 3000, n(10000));
  await op('clear10k', '#clear', 1200, n(0));
  await op('run_for_append', '#run', 600, n(1000));
  await op('append', '#add', 800, n(2000));
  await mark(b, 'end');
  if (count) { delete rec.counts.run_for_append; return { counts: rec.counts, invalid: rec.invalid }; }
  const w = windows(await stop());
  delete w.run_for_append; delete w.end;
  return { windows: w, invalid: rec.invalid };
}

// ---- the correctness test and the micro
async function poll(b, what, ms) {
  for (let n = 0; n < ms / 100; n++) {
    const r = await b.evaluate('window.RESULT');
    if (r) { if (r.error) throw new Error(`${what}: ${r.error}`); return r; }
    await sleep(100);
  }
  throw new Error(`${what}: no result in ${ms} ms`);
}
async function sameLoad(b, arm) {
  await b.goto(`${ORIGIN}/same/${arm}/index.html`);
  return poll(b, `same ${arm}`, 10000);
}
function firstDifference(x = '', y = '') {
  let i = 0;
  while (i < x.length && x[i] === y[i]) i++;
  return `at ${i}: ${JSON.stringify(x.slice(Math.max(0, i - 40), i + 60))} vs ${JSON.stringify(y.slice(Math.max(0, i - 40), i + 60))}`;
}
async function microLoad(b, arm, count) {
  await b.goto(`${ORIGIN}/micro/${arm}${count ? '_count' : ''}/index.html?round=${ROUND0}${count ? '&count=1' : ''}`);
  return poll(b, `micro ${arm}`, 60000);
}

// ---- main
fs.mkdirSync(RESULTS, { recursive: true });
const file = path.join(RESULTS, `${MODE}_x${CPU}${process.env.TAG ? '_' + process.env.TAG : ''}.jsonl`);
const g0 = await gateBeforeLaunch();
server = await serve();
const b = await launch();
try {
  const session = new Date().toISOString();
  console.log(`${MODE}: ${ARMS.join(' ')}, ${ROUNDS} round(s) from ${ROUND0}, CPU x${CPU}, ${b.version}, ${process.env.CHROME_FLAGS || 'no extra flags'}, load ${g0.load} after ${g0.waited} s`);
  if (MODE === 'same') {
    const got = {};
    for (const arm of ARMS) { await b.fresh(); got[arm] = await sameLoad(b, arm); }
    let bad = 0;
    for (const arm of ARMS.filter((x) => x !== 'a')) {
      const a = got.a, x = got[arm];
      const diffs = [];
      if (x.html.length !== a.html.length) diffs.push(`${x.html.length} DOM snapshots, not ${a.html.length}`);
      a.html.forEach((h, k) => { if (x.html[k] !== h) diffs.push(`DOM after step ${k - 1} ${firstDifference(h, x.html[k])}`); });
      for (const key of Object.keys(a).filter((k) => k !== 'html')) {
        if (JSON.stringify(a[key]) !== JSON.stringify(x[key])) diffs.push(`${key}: ${JSON.stringify(a[key])} vs ${JSON.stringify(x[key])}`);
      }
      console.log(`${arm}: ${diffs.length ? 'DIFFERS\n  ' + diffs.join('\n  ') : 'same as a'}`);
      bad += diffs.length;
    }
    fs.appendFileSync(path.join(RESULTS, 'same.jsonl'), JSON.stringify({ session, chrome: b.version, got }) + '\n');
    process.exitCode = bad ? 1 : 0;
  } else if (MODE === 'count') {
    const out = { session, mode: 'count', chrome: b.version, arms: {}, unstable: {} };
    const runs = ['a', ...ARMS.filter((x) => x !== 'a'), 'a#2'];
    const got = {};
    for (const run of runs) {
      if (Date.now() - T0 > BUDGET_MS - PER_LOAD_MS) { console.log(`CUT: the budget ran out before ${run}`); process.exitCode = 1; break; }
      const arm = run.split('#')[0];
      got[run] = {};
      await b.fresh(); got[run].port = (await portLoad(b, arm, true, null)).counts;
      if (KEYED_ARMS.includes(arm)) { await b.fresh(); got[run].keyed = (await keyedLoad(b, arm, true, null)).counts; }
      if (MICRO_ARMS.includes(arm) && !run.includes('#')) { await b.fresh(); got[run].micro = await microLoad(b, arm, true); }
      console.log(run, JSON.stringify(Object.fromEntries(Object.entries(got[run].port).map(([k, v]) => [k, v.calls]))));
    }
    let bad = 0;
    for (const page of ['port', 'keyed']) {
      if (!got['a#2']) break;
      out.unstable[page] = Object.keys(got.a[page]).filter((s) => got.a[page][s].hash !== got['a#2'][page][s].hash);
      if (out.unstable[page].length) console.log(`${page}: arm a's DOM differs between two loads after ${out.unstable[page].join(', ')} (unstable: checked by their own assertions only)`);
      for (const arm of ARMS.filter((x) => x !== 'a')) {
        const mine = got[arm] && got[arm][page];
        if (!mine) continue;
        const movers = arm === 'e_t' || arm === 'e_css';
        const differ = Object.keys(got.a[page]).filter((s) => !out.unstable[page].includes(s)).filter((s) => {
          const x = mine[s], a = got.a[page][s];
          if (!x) return true;
          if (movers) return x.hashMovers !== a.hashMovers || (s !== 'drag' && x.hash !== a.hash);
          return x.hash !== a.hash;
        });
        if (differ.length) bad++;
        console.log(`${arm} ${page}: DOM ${differ.length ? 'DIFFERS from a after ' + differ.join(', ') : 'the same as a after every flow' + (movers ? ' (the movers\' style text aside, during the drag)' : '')}`);
      }
    }
    for (const [run, v] of Object.entries(got)) if (!run.includes('#')) out.arms[run] = v;
    fs.appendFileSync(file, JSON.stringify(out) + '\n');
    if (bad) process.exitCode = 1;
  } else {
    const ref = MODE === 'micro' ? null : reference();
    for (let r = 0; r < ROUNDS; r++) {
      for (const arm of shuffled(ARMS, (ROUND0 + r) * 7919 + CPU)) {
        if (Date.now() - T0 > BUDGET_MS - PER_LOAD_MS) {
          console.log(`CUT: the ${Math.round(BUDGET_MS / 1000)} s budget ran out before round ${ROUND0 + r}'s ${arm}`);
          fs.appendFileSync(file, JSON.stringify({ session, mode: MODE, cpu: CPU, round: ROUND0 + r, arm, cut: true }) + '\n');
          continue;
        }
        await b.fresh();
        const others0 = othersCpu();
        let res;
        if (MODE === 'port') res = await portLoad(b, arm, false, ref);
        else if (MODE === 'keyed') res = await keyedLoad(b, arm, false, ref);
        else res = { micro: await microLoad(b, arm, false), invalid: [] };
        const others = Math.max(others0, othersCpu());
        const row = { session, mode: MODE, cpu: CPU, round: ROUND0 + r, arm, load: +os.loadavg()[0].toFixed(2), others,
          busy: others > OTHERS_MAX, flags: process.env.CHROME_FLAGS || '', chrome: b.version, ...res };
        fs.appendFileSync(file, JSON.stringify(row) + '\n');
        const brief = res.windows ? Object.entries(res.windows).map(([n, w]) => `${n} ${w.script.toFixed(2)}/${w.total.toFixed(1)}`).join('  ') : JSON.stringify(res.micro);
        console.log(`r${ROUND0 + r} ${arm.padEnd(6)} others ${others}%${row.busy ? ' BUSY' : ''}${res.invalid.length ? ' INVALID ' + res.invalid.join('; ') : ''}  ${brief}`);
      }
    }
  }
} finally {
  await b.close();
}
