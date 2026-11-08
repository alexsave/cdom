// Minimal CDP client: one headless Chrome per process, killed by its own PID. CHROME names the
// binary (default: the macOS app's).
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawn } from 'node:child_process';
export const sleep = ms => new Promise(r => setTimeout(r, ms));
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
  ws.onmessage = m => { const d = JSON.parse(m.data); if (d.id && pend.has(d.id)) { pend.get(d.id)(d); pend.delete(d.id); } else if (d.method && api.onEvent) api.onEvent(d); };
  const send = (method, params = {}, sessionId) => new Promise(r => { const i = ++id; pend.set(i, r); ws.send(JSON.stringify({ id: i, method, params, sessionId })); });
  const { result: { targetId } } = await send('Target.createTarget', { url: 'about:blank', newWindow: true });
  const { result: { sessionId } } = await send('Target.attachToTarget', { targetId, flatten: true });
  const s = (m, p) => send(m, p, sessionId);
  await s('Page.enable'); await s('Runtime.enable');
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
