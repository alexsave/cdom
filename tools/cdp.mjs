// Minimal CDP client: one headless Chrome per process, killed by its own PID. CHROME names the
// binary (default: the macOS app's).
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawn } from 'node:child_process';
export const sleep = ms => new Promise(r => setTimeout(r, ms));
// LEGACY=1 makes every page an old browser before its first script runs: no WebAssembly, and none of the
// APIs tools/legacy/polyfills.js supplies, so those polyfills and PEP are what the page runs on. Chrome
// still fires its own pointer events, so trusted ones are stopped at the window and only PEP's, made from
// mouse events, get through. addEventListener reads its third argument as useCapture, as before options
// objects; __adds logs what each call passed (type, typeof, capture) for tools/legacy.mjs.
export const LEGACY = process.env.LEGACY === '1';
const OLD = `(() => {
  for (const k of ['WebAssembly', 'TextDecoder', 'TextEncoder', 'queueMicrotask', 'reportError', 'Promise', 'fetch',
    'PointerEvent', 'globalThis']) delete window[k];
  for (const k of ['imul', 'clz32', 'fround', 'trunc']) delete Math[k];
  delete Object.entries; delete String.prototype.startsWith; delete String.prototype.includes;
  const ta = Object.getPrototypeOf(Uint8Array.prototype); delete ta.copyWithin; delete ta.fill;
  for (const t of ['pointerdown', 'pointerup', 'pointermove', 'pointerover', 'pointerout', 'pointercancel',
    'pointerenter', 'pointerleave', 'gotpointercapture', 'lostpointercapture'])
    window.addEventListener(t, (e) => { if (e.isTrusted) e.stopImmediatePropagation(); }, true);
  const P = EventTarget.prototype, add = P.addEventListener, rem = P.removeEventListener;
  const adds = window.__adds = [];
  P.addEventListener = function (t, f, c) { adds.push([t, typeof c, !!c]); return add.call(this, t, f, !!c); };
  P.removeEventListener = function (t, f, c) { return rem.call(this, t, f, !!c); };
})();`;
// The scripts a page fetched, by file name, to prove which path it took.
export const FETCHED = "performance.getEntriesByType('resource').map(r => r.name.split('/').pop().split('?')[0])";

export async function launch(args = []) { // args: Chrome flags of the caller's own
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'cdom-chrome-'));
  const dbg = +(process.env.CDP_PORT || 8730 + Math.floor(Math.random() * 10)); // 8730-8739 unless CDP_PORT
  const proc = spawn(process.env.CHROME || '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    ['--headless=new', `--remote-debugging-port=${dbg}`, `--user-data-dir=${dir}`, '--no-first-run',
     '--no-default-browser-check', '--disable-extensions', '--mute-audio', '--window-size=1280,800', ...args, ...(process.env.CHROME_ARGS ? process.env.CHROME_ARGS.split(' ') : []), 'about:blank'],
    { stdio: process.env.CHROME_LOG ? ['ignore', fs.openSync(process.env.CHROME_LOG, 'w'), fs.openSync(process.env.CHROME_LOG + '.err', 'w')] : 'ignore' });
  let ver;
  for (let i = 0; i < 50 && !ver; i++) { try { ver = await (await fetch(`http://127.0.0.1:${dbg}/json/version`)).json(); } catch { await sleep(200); } }
  const ws = new WebSocket(ver.webSocketDebuggerUrl);
  await new Promise(r => ws.onopen = r);
  let id = 0; const pend = new Map();
  const api = {};
  ws.onmessage = m => { const d = JSON.parse(m.data); if (d.id && pend.has(d.id)) { pend.get(d.id)(d); pend.delete(d.id); } else if (d.method && api.onEvent) api.onEvent(d); };
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
  return Object.assign(api, { version: ver.Browser, send: s, sendBrowser: send, targetId, evaluate, close });
}
