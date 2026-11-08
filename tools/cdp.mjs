// Minimal CDP client: one headless Chrome per process, killed by its own PID. CHROME names the
// binary (default: the macOS app's).
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawn } from 'node:child_process';
export const sleep = ms => new Promise(r => setTimeout(r, ms));
// LEGACY=1 makes every page an old browser before its first script runs: no WebAssembly, and none of the
// APIs tools/legacy/polyfills.js supplies, so those polyfills and PEP are what the page runs on; crypto is
// only msCrypto, as in IE 11 (__noCrypto records that it was gone). Chrome still fires its own pointer
// events, so trusted ones are stopped at the window and only PEP's, made from mouse events, get through.
// addEventListener reads its third argument as useCapture, as before options objects; __adds logs what
// each call passed (type, typeof, capture) for tools/legacy.mjs.
// LEGACY=ie9 is all of that and then IE 9: IE9_GAPS below deletes what IE 9 lacks that IE 11 has.
// LEGACY=ie9-gaps is IE 9 with its two walls put back (typed arrays and msCrypto, see IE9_WALLS), so the
// rest of IE 9's gaps can be tested on a page that boots.
export const LEVEL = process.env.LEGACY || '';
if (LEVEL && !['1', 'ie9', 'ie9-gaps'].includes(LEVEL)) throw new Error(`cdp: LEGACY=${LEVEL}: want 1, ie9 or ie9-gaps`);
export const LEGACY = !!LEVEL;
export const IE9 = LEVEL === 'ie9'; // below cdom's floor: the boot must refuse it (see shim/boot.js)
// What IE 9 lacks that IE 11 has, each with the IE version it arrived in, from mdn/browser-compat-data
// (main, read 2026-11-08); "never" is a feature BCD lists with no IE entry, which is how BCD records an IE
// that never had it. ES5 itself (Object.create, defineProperty, bind, forEach, getPrototypeOf) is IE 9's.
// Kept on purpose: performance (IE 9) and performance.getEntriesByType (IE 10), which the harness reads to
// see what a page fetched (FETCHED); the app never calls it.
const IE9_WALLS = `
  // Typed arrays, IE 10: ArrayBuffer, DataView and every view, Uint8ClampedArray included.
  for (const k of ['ArrayBuffer', 'DataView', 'Int8Array', 'Uint8Array', 'Uint8ClampedArray', 'Int16Array',
    'Uint16Array', 'Int32Array', 'Uint32Array', 'Float32Array', 'Float64Array', 'BigInt64Array', 'BigUint64Array',
    'Float16Array', 'SharedArrayBuffer']) delete window[k];
  // crypto.getRandomValues, IE 11 (as msCrypto): IE 9 and 10 have no cryptographic generator at all.
  delete window.msCrypto;`;
const IE9_GAPS = `
  // requestAnimationFrame and cancelAnimationFrame, IE 10 (and no prefixed ones to fall back on).
  for (const k of ['requestAnimationFrame', 'cancelAnimationFrame', 'webkitRequestAnimationFrame',
    'webkitCancelAnimationFrame', 'webkitCancelRequestAnimationFrame']) delete window[k];
  // MutationObserver, IE 11.
  delete window.MutationObserver; delete window.WebKitMutationObserver;
  // classList and DOMTokenList, IE 10; dataset (DOMStringMap), IE 11.
  delete Element.prototype.classList; delete window.DOMTokenList;
  delete HTMLElement.prototype.dataset; delete window.DOMStringMap;
  // history.pushState and replaceState, IE 10.
  delete History.prototype.pushState; delete History.prototype.replaceState;
  // WebSocket, Blob, File, FormData, URL (with createObjectURL), IE 10; URLSearchParams, never.
  for (const k of ['WebSocket', 'Blob', 'File', 'FormData', 'URL', 'URLSearchParams']) delete window[k];
  // indexedDB, atob and btoa, IE 10.
  for (const k of ['indexedDB', 'IDBFactory', 'IDBDatabase', 'IDBKeyRange', 'IDBRequest', 'IDBTransaction',
    'IDBObjectStore']) delete window[k];
  delete window.atob; delete window.btoa;
  // performance.now, IE 10 (performance itself is IE 9's).
  delete Performance.prototype.now;
  // Map, Set and WeakMap, IE 11; WeakSet and Symbol, never; Object.setPrototypeOf, IE 11.
  for (const k of ['Map', 'Set', 'WeakMap', 'WeakSet', 'Symbol']) delete window[k];
  delete Object.setPrototypeOf;
  // matchMedia, document.hidden and String.prototype.trim, IE 10; devicePixelRatio, IE 11.
  delete window.matchMedia; delete Document.prototype.hidden; delete String.prototype.trim;
  delete window.devicePixelRatio;
  // Element.matches, never: IE 9 to 11 have it only as msMatchesSelector.
  Element.prototype.msMatchesSelector = Element.prototype.matches; delete Element.prototype.matches;`;
const OLD = `(() => {
  for (const k of ['WebAssembly', 'TextDecoder', 'TextEncoder', 'queueMicrotask', 'reportError', 'Promise', 'fetch',
    'PointerEvent', 'globalThis']) delete window[k];
  window.msCrypto = window.crypto; delete window.crypto; // an own accessor on the window in Chrome
  window.__noCrypto = typeof crypto === 'undefined';
  for (const k of ['imul', 'clz32', 'fround', 'trunc']) delete Math[k];
  delete Object.entries; delete String.prototype.startsWith; delete String.prototype.includes;
  const ta = Object.getPrototypeOf(Uint8Array.prototype); delete ta.copyWithin; delete ta.fill;
  for (const t of ['pointerdown', 'pointerup', 'pointermove', 'pointerover', 'pointerout', 'pointercancel',
    'pointerenter', 'pointerleave', 'gotpointercapture', 'lostpointercapture'])
    window.addEventListener(t, (e) => { if (e.isTrusted) e.stopImmediatePropagation(); }, true);
  const P = EventTarget.prototype, add = P.addEventListener, rem = P.removeEventListener;
  const adds = window.__adds = [];
  P.addEventListener = function (t, f, c) { adds.push([t, typeof c, !!c]); return add.call(this, t, f, !!c); };
  P.removeEventListener = function (t, f, c) { return rem.call(this, t, f, !!c); };${LEVEL.startsWith('ie9') ? IE9_GAPS : ''}${IE9 ? IE9_WALLS : ''}
})();`;
// The scripts a page fetched, by file name, to prove which path it took.
export const FETCHED = "performance.getEntriesByType('resource').map(r => r.name.split('/').pop().split('?')[0])";

export async function launch(args = []) { // args: Chrome flags of the caller's own
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'cdom-chrome-'));
  // Port 0: the OS gives this Chrome a free port, which it writes to DevToolsActivePort in its own
  // profile, so a run can only ever reach the Chrome it started. A picked port (a range, a guess) lets two
  // parallel runs collide: the second Chrome fails to bind, and its run drives the first one's browser,
  // flags and all (a unit run then saw no window.gc). CDP_PORT still fixes one, for a debugger.
  const want = +(process.env.CDP_PORT || 0);
  const proc = spawn(process.env.CHROME || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    ['--headless=new', `--remote-debugging-port=${want}`, `--user-data-dir=${dir}`, '--no-first-run',
     '--no-default-browser-check', '--disable-extensions', '--mute-audio', '--window-size=1280,800', ...args, ...(process.env.CHROME_ARGS ? process.env.CHROME_ARGS.split(' ') : []), 'about:blank'],
    { stdio: process.env.CHROME_LOG ? ['ignore', fs.openSync(process.env.CHROME_LOG, 'w'), fs.openSync(process.env.CHROME_LOG + '.err', 'w')] : 'ignore' });
  let exited = false;
  proc.on('exit', () => { exited = true; });
  const portFile = path.join(dir, 'DevToolsActivePort');
  let dbg = 0;
  for (let i = 0; i < 100 && !dbg && !exited; i++) {
    try { dbg = +fs.readFileSync(portFile, 'utf8').split('\n')[0]; } catch { await sleep(100); }
  }
  if (!dbg) throw new Error(`cdp: Chrome ${exited ? 'exited' : 'wrote no DevToolsActivePort'} (profile ${dir})`);
  let ver;
  for (let i = 0; i < 50 && !ver; i++) { try { ver = await (await fetch(`http://127.0.0.1:${dbg}/json/version`)).json(); } catch { await sleep(200); } }
  if (!ver) throw new Error(`cdp: no answer from Chrome on port ${dbg}`);
  const ws = new WebSocket(ver.webSocketDebuggerUrl);
  await new Promise(r => ws.onopen = r);
  let id = 0; const pend = new Map();
  const api = {};
  // exceptions: every uncaught page exception since the last navigation, as "file:line message".
  const exceptions = [];
  const thrown = (d) => {
    if (d.method === 'Page.frameNavigated' && !d.params.frame.parentId) exceptions.length = 0;
    if (d.method !== 'Runtime.exceptionThrown') return;
    const x = d.params.exceptionDetails;
    exceptions.push(`${(x.url || '').split('/').pop()}:${x.lineNumber + 1} ${x.exception?.description?.split('\n')[0] || x.text}`);
  };
  ws.onmessage = m => { const d = JSON.parse(m.data); if (d.id && pend.has(d.id)) { pend.get(d.id)(d); pend.delete(d.id); } else if (d.method) { thrown(d); if (api.onEvent) api.onEvent(d); } };
  const send = (method, params = {}, sessionId) => new Promise(r => { const i = ++id; pend.set(i, r); ws.send(JSON.stringify({ id: i, method, params, sessionId })); });
  const { result: { targetId } } = await send('Target.createTarget', { url: 'about:blank', newWindow: true });
  const { result: { sessionId } } = await send('Target.attachToTarget', { targetId, flatten: true });
  const s = (m, p) => send(m, p, sessionId);
  await s('Page.enable'); await s('Runtime.enable');
  if (LEGACY) await s('Page.addScriptToEvaluateOnNewDocument', { source: OLD });
  const evaluate = async (expr) => {
    const r = await s('Runtime.evaluate', { expression: expr, returnByValue: true, awaitPromise: true });
    if (r.result?.exceptionDetails) throw new Error(JSON.stringify(r.result.exceptionDetails));
    return r.result?.result?.value;
  };
  const close = async () => {
    try { await send('Target.closeTarget', { targetId }); ws.close(); } catch {}
    proc.kill('SIGTERM');
    await sleep(300);
    try { fs.rmSync(dir, { recursive: true, force: true }); } catch {}
  };
  return Object.assign(api, { version: ver.Browser, send: s, sendBrowser: send, targetId, evaluate, close, exceptions });
}

// Under LEGACY=ie9 the boot must refuse the browser cleanly (see shim/boot.js): one error naming each missing
// thing, neither fallback file nor the wasm fetched, and nothing drawn in mount (a selector). Returns the
// checks as [pass, message] pairs. The page's own shim script fails first in Chrome, on TextDecoder; a real
// old browser fails that script with a SyntaxError instead, LEGACY=1 sees the same one, and it is not counted.
export const FLOOR = ['ArrayBuffer', 'Int8Array', 'Uint8Array', 'Int16Array', 'Uint16Array', 'Int32Array', 'Uint32Array',
  'Float32Array', 'Float64Array', 'crypto.getRandomValues'];
export async function refused(b, mount) {
  for (let i = 0; i < 100 && (await b.evaluate('document.readyState')) !== 'complete'; i++) await sleep(50);
  await sleep(500); // a fallback file still loading would have run by now
  const errs = b.exceptions.filter(e => !/^(cdom\.js|index\.html):\d+ ReferenceError: TextDecoder is not defined$/.test(e));
  const got = (await b.evaluate(FETCHED)).filter(f => /\.(js|wasm)$/.test(f) && f !== 'cdom.js');
  const want = `cdom: this browser cannot run cdom without WebAssembly: it has no ${FLOOR.join(', ')}`;
  return [
    [errs.length === 1 && errs[0].endsWith(`Error: ${want}`), `one error, the boot's, naming all ${FLOOR.length} missing: ${JSON.stringify(errs)}`],
    [!got.length, `no fallback file or wasm fetched: ${got.join(' ')}`],
    [await b.evaluate(`document.querySelector(${JSON.stringify(mount)}).childNodes.length === 0`), `nothing drawn in ${mount}`],
  ];
}
