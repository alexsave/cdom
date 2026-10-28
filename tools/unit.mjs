// Runs tests/out/unit.html in headless Chrome (tests/build.sh first): the in-page cases, then the
// ones that need the outside: a resize (CDP) and the network (this server's WebSocket echo, a POST
// answered reversed, request headers, a deadline, a port nobody listens on, sockets opened and
// closed, a large send draining). Exit 1 on any failure.
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { launch, sleep } from './cdp.mjs';
const root = path.resolve(path.dirname(new URL(import.meta.url).pathname), '../tests/out');
const PORT = +(process.env.UNIT_PORT || 8723);
const types = { '.html': 'text/html', '.wasm': 'application/wasm', '.js': 'text/javascript' };
const srv = http.createServer((q, r) => {
  if (q.url === '/headers') { // the request headers C sent, and a status that is not 200
    q.resume();
    q.on('end', () => { r.writeHead(201); r.end(`${q.headers.authorization || ''}|${q.headers['content-type'] || ''}|${q.headers['x-last'] || ''}`); });
    return;
  }
  if (q.url === '/slow') { setTimeout(() => { r.writeHead(200); r.end('late'); }, 2000); return; }
  if (q.url === '/echo' && q.method === 'POST') {
    const parts = [];
    q.on('data', (d) => parts.push(d));
    q.on('end', () => { r.writeHead(200, { 'content-type': 'application/octet-stream' }); r.end(Buffer.concat(parts).reverse()); });
    return;
  }
  const f = path.join(root, q.url.split(/[?#]/)[0]);
  fs.readFile(f, (e, b) => { if (e) { r.writeHead(404); r.end(); return; } r.writeHead(200, { 'content-type': types[path.extname(f)] }); r.end(b); });
});
srv.on('upgrade', (req, sock) => { // a minimal RFC 6455 echo: binary frames under 126 bytes back, larger ones read and dropped
  const accept = crypto.createHash('sha1').update(req.headers['sec-websocket-key'] + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64');
  sock.write(`HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ${accept}\r\n\r\n`);
  let buf = Buffer.alloc(0);
  sock.on('error', () => {});
  sock.on('data', (d) => {
    buf = Buffer.concat([buf, d]);
    while (buf.length >= 6) {
      const op = buf[0] & 15;
      let len = buf[1] & 127, at = 2;
      if (len === 126) { len = buf.readUInt16BE(2); at = 4; } else if (len === 127) { len = Number(buf.readBigUInt64BE(2)); at = 10; }
      if (buf.length < at + 4 + len) return;
      const mask = buf.subarray(at, at + 4), body = Buffer.from(buf.subarray(at + 4, at + 4 + len));
      buf = buf.subarray(at + 4 + len);
      if (op === 8) { sock.end(Buffer.from([0x88, 0])); return; }
      if (op !== 2 || len >= 126) continue;
      for (let i = 0; i < len; i++) body[i] ^= mask[i & 3];
      sock.write(Buffer.concat([Buffer.from([0x82, len]), body]));
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
  const stale = () => b.evaluate('__x.t_stale()');
  ok = ok && await stale() === 0;
  if (ok) passed.push('a resize arrives through the event path, with the viewport size, and the export flushes'); else errors.push('resize: ' + await b.evaluate(`__x.t_viewport(3) + 'x' + __x.t_viewport(4) + ' ' + document.querySelector('#window i').textContent`));
  // The device pixel ratio, read from C: 2 on a 2x screen.
  await b.send('Emulation.setDeviceMetricsOverride', { width: 640, height: 480, deviceScaleFactor: 2, mobile: false });
  let dpr = 0;
  for (let i = 0; i < 100 && dpr !== 200; i++) { await sleep(20); dpr = await b.evaluate('__x.t_viewport(6)'); }
  if (dpr === 200) passed.push('the device pixel ratio, read from C'); else errors.push('the device pixel ratio: ' + dpr);
  await b.evaluate(`__x.t_net(${PORT})`);
  let v = '';
  for (let i = 0; i < 200; i++) { await sleep(20); v = await b.evaluate(`document.querySelector('#net i').textContent`); if (v !== 'waiting' && v !== 'ws ok') break; }
  if (v === 'ws ok, http ok' && await stale() === 0) passed.push('WebSocket echo and POST, bytes into C memory, answered without a cd_flush'); else errors.push('net: ' + v + ', entries that found work pending ' + await stale());
  // Requests answered by token: request headers and a 201; a deadline that passes; nobody listening.
  const t0 = await b.evaluate('performance.now()');
  await b.evaluate('__x.t_http_cases()');
  const st = (t) => b.evaluate(`__x.t_http_status(${t})`);
  const body = (t) => b.evaluate(`new TextDecoder().decode(new Uint8Array(__x.memory.buffer, __x.t_http_body(${t}), __x.t_http_len(${t})))`);
  for (let i = 0; i < 300; i++) { await sleep(20); if ((await Promise.all([8, 9, 10, 11].map(st))).every((x) => x !== -1)) break; }
  const h8 = await body(8), h11 = await body(11);
  if (await st(8) === 201 && h8 === 'Bearer abc.def|application/octet-stream|' && await st(11) === 201 && h11 === '||no newline') passed.push('cd_http sends request headers from one block of lines, and the status reaches C');
  else errors.push(`headers: ${await st(8)} ${h8} / ${await st(11)} ${h11}`);
  const at9 = await b.evaluate('__x.t_http_at(9)') - t0;
  if (await st(9) === 1 && at9 < 1000) passed.push('a request past its deadline is aborted and answered TIMEOUT (1)'); else errors.push(`deadline: status ${await st(9)} after ${Math.round(at9)} ms`);
  if (await st(10) === 0) passed.push('a request that never arrives is answered FAILED (0), not TIMEOUT'); else errors.push(`never arrived: status ${await st(10)}`);
  // 40 sockets opened and closed one after another, once the echo's socket closed, all get one id:
  // the table does not grow.
  for (let i = 0; i < 200 && !await b.evaluate('__x.t_echo_closed()'); i++) await sleep(20);
  await b.evaluate('__x.t_ws_cycles(40)');
  let cyc = [];
  for (let i = 0; i < 400; i++) { await sleep(20); cyc = await b.evaluate('[0, 1, 2].map((k) => __x.t_ws_cycle(k))'); if (cyc[0] === 40) break; }
  if (cyc[0] === 40 && cyc[1] === cyc[2]) passed.push('sockets opened and closed 40 times reuse their id'); else errors.push(`sockets: ${cyc[0]} cycles, ids ${cyc[1]} to ${cyc[2]}`);
  // bufferedAmount: the 4 MiB just sent, then 0 once it drained.
  await b.evaluate('__x.t_ws_big()');
  let after = 0, buffered = -1;
  for (let i = 0; i < 100 && !after; i++) { await sleep(20); after = await b.evaluate('__x.t_ws_big_after_send()'); }
  for (let i = 0; i < 300; i++) { await sleep(20); buffered = await b.evaluate('__x.t_ws_buffered()'); if (buffered === 0) break; }
  await b.evaluate('__x.t_ws_big_close()');
  if (after >= 4 << 20 && buffered === 0) passed.push('cd_ws_buffered: the bytes just sent, then 0 once drained'); else errors.push(`buffered: ${after} after the send, ${buffered} later`);
} catch (e) { errors.push('threw: ' + String(e.message || e).slice(0, 300)); } finally { await b.close(); srv.close(); }
for (const p of passed) console.log('ok  ', p);
for (const e of errors) console.log('FAIL', e);
console.log(`${passed.length} passed, ${errors.length} failed`);
process.exit(errors.length ? 1 : 0);
