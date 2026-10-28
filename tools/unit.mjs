// Runs tests/out/unit.html in headless Chrome (tests/build.sh first): the in-page cases, then the
// two that need the outside: a resize (CDP) and the network (this server's WebSocket echo and a
// POST answered reversed). Exit 1 on any failure.
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { launch, sleep } from './cdp.mjs';
const root = path.resolve(path.dirname(new URL(import.meta.url).pathname), '../tests/out');
const PORT = +(process.env.UNIT_PORT || 8723);
const types = { '.html': 'text/html', '.wasm': 'application/wasm', '.js': 'text/javascript' };
const srv = http.createServer((q, r) => {
  if (q.url === '/echo' && q.method === 'POST') {
    const parts = [];
    q.on('data', (d) => parts.push(d));
    q.on('end', () => { r.writeHead(200, { 'content-type': 'application/octet-stream' }); r.end(Buffer.concat(parts).reverse()); });
    return;
  }
  const f = path.join(root, q.url.split(/[?#]/)[0]);
  fs.readFile(f, (e, b) => { if (e) { r.writeHead(404); r.end(); return; } r.writeHead(200, { 'content-type': types[path.extname(f)] }); r.end(b); });
});
srv.on('upgrade', (req, sock) => { // a minimal RFC 6455 echo for binary frames under 126 bytes
  const accept = crypto.createHash('sha1').update(req.headers['sec-websocket-key'] + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');
  sock.write(`HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ${accept}\r\n\r\n`);
  let buf = Buffer.alloc(0);
  sock.on('error', () => {});
  sock.on('data', (d) => {
    buf = Buffer.concat([buf, d]);
    while (buf.length >= 6) {
      const op = buf[0] & 15, len = buf[1] & 127;
      if (buf.length < 6 + len) return;
      const mask = buf.subarray(2, 6), body = Buffer.from(buf.subarray(6, 6 + len));
      for (let i = 0; i < len; i++) body[i] ^= mask[i & 3];
      buf = buf.subarray(6 + len);
      if (op === 8) { sock.end(Buffer.from([0x88, 0])); return; }
      if (op === 2) sock.write(Buffer.concat([Buffer.from([0x82, len]), body]));
    }
  });
});
await new Promise((r) => srv.listen(PORT, '127.0.0.1', r));
const b = await launch();
const errors = [];
let passed = [];
try {
  await b.send('Emulation.setDeviceMetricsOverride', { width: 1000, height: 700, deviceScaleFactor: 1, mobile: false });
  await b.send('Page.navigate', { url: `http://127.0.0.1:${PORT}/unit.html` });
  let r;
  for (let i = 0; i < 300 && !r; i++) { await sleep(50); r = await b.evaluate('window.RESULT'); }
  if (!r) errors.push('the page never finished');
  else { errors.push(...r.errors); passed = r.passed; }
  // A resize arrives as CD_EV_RESIZE with the new size, and the app's patch lands without a cd_flush.
  await b.send('Emulation.setDeviceMetricsOverride', { width: 640, height: 480, deviceScaleFactor: 1, mobile: false });
  let ok = false;
  for (let i = 0; i < 100 && !ok; i++) { await sleep(20); ok = await b.evaluate(`__x.t_viewport(3) === 640 && __x.t_viewport(4) === 480 && document.querySelector('#window i').textContent === '640'`); }
  const walks = () => b.evaluate(`trace.filter((k) => k === 'walk').length`);
  ok = ok && await walks() === 1;
  if (ok) passed.push('a resize arrives through the event path, with the viewport size, and the export flushes'); else errors.push('resize: ' + await b.evaluate(`__x.t_viewport(3) + 'x' + __x.t_viewport(4) + ' ' + document.querySelector('#window i').textContent`));
  await b.evaluate(`__x.t_net(${PORT})`);
  let v = '';
  for (let i = 0; i < 200; i++) { await sleep(20); v = await b.evaluate(`document.querySelector('#net i').textContent`); if (v !== 'waiting' && v !== 'ws ok') break; }
  if (v === 'ws ok, http ok' && await walks() === 3) passed.push('WebSocket echo and POST, bytes into C memory, answered without a cd_flush'); else errors.push('net: ' + v + ', walks flushed ' + await walks());
} finally { await b.close(); srv.close(); }
for (const p of passed) console.log('ok  ', p);
for (const e of errors) console.log('FAIL', e);
console.log(`${passed.length} passed, ${errors.length} failed`);
process.exit(errors.length ? 1 : 0);
