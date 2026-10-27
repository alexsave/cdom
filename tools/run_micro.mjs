// Serves micro/, launches one headless Chrome, runs micro.html over CDP, prints JSON.
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import { spawn } from 'node:child_process';

const root = path.resolve(path.dirname(new URL(import.meta.url).pathname), '../micro');
const types = { '.html': 'text/html', '.wasm': 'application/wasm', '.js': 'text/javascript' };
const srv = http.createServer((q, r) => {
  const f = path.join(root, decodeURIComponent(q.url.split('?')[0]));
  fs.readFile(f, (e, b) => { if (e) { r.writeHead(404); r.end(); return; }
    r.writeHead(200, { 'content-type': types[path.extname(f)] || 'application/octet-stream' }); r.end(b); });
}).listen(0);
const port = srv.address().port;
const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'cdom-chrome-'));
const dbg = 9300 + Math.floor(Math.random() * 500);
const chrome = spawn('/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
  ['--headless=new', `--remote-debugging-port=${dbg}`, `--user-data-dir=${dir}`, '--no-first-run',
   '--no-default-browser-check', '--disable-extensions', 'about:blank'], { stdio: 'ignore' });
const sleep = ms => new Promise(r => setTimeout(r, ms));
try {
  let ver;
  for (let i = 0; i < 50 && !ver; i++) { try { ver = await (await fetch(`http://127.0.0.1:${dbg}/json/version`)).json(); } catch { await sleep(200); } }
  const ws = new WebSocket(ver.webSocketDebuggerUrl);
  await new Promise(r => ws.onopen = r);
  let id = 0; const pend = new Map();
  ws.onmessage = m => { const d = JSON.parse(m.data); if (d.id && pend.has(d.id)) { pend.get(d.id)(d); pend.delete(d.id); } };
  const send = (method, params = {}, sessionId) => new Promise(r => { const i = ++id; pend.set(i, r); ws.send(JSON.stringify({ id: i, method, params, sessionId })); });
  const { result: { targetId } } = await send('Target.createTarget', { url: 'about:blank', newWindow: true });
  const { result: { sessionId } } = await send('Target.attachToTarget', { targetId, flatten: true });
  await send('Page.enable', {}, sessionId);
  await send('Page.bringToFront', {}, sessionId);
  await send('Page.navigate', { url: `http://127.0.0.1:${port}/micro.html` }, sessionId);
  let res;
  for (let i = 0; i < 600 && !res; i++) {
    await sleep(500);
    const r = await send('Runtime.evaluate', { expression: 'window.RESULT && JSON.stringify(window.RESULT)', returnByValue: true }, sessionId);
    if (r.result?.result?.value) res = JSON.parse(r.result.result.value);
  }
  console.log(JSON.stringify({ browser: ver.Browser, result: res }, null, 1));
  await send('Target.closeTarget', { targetId });
  ws.close();
} finally {
  chrome.kill('SIGTERM');
  srv.close();
  setTimeout(() => { try { fs.rmSync(dir, { recursive: true, force: true }); } catch {} process.exit(0); }, 500);
}
