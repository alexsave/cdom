// Runs tests/out/unit.html in headless Chrome (tests/build.sh first): the in-page cases, then the
// ones that need the outside: a resize and the display preferences (CDP) and the network (this
// server's WebSocket echo, a POST answered reversed, request headers, a deadline, a port nobody
// listens on, sockets opened and closed, a large send draining), the click that unlocks sound
// when the page asks for one, and the click demo tapped. Chrome runs with its autoplay policy
// enforced (sound only after a real gesture) and gc() exposed (the page sees a freed clip's buffer
// go). Exit 1 on any failure.
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
  if (q.url === '/slow-font') { setTimeout(() => { r.writeHead(200, { 'content-type': 'font/woff2' }); r.end('no font'); }, 800); return; } // a font that loads slowly, then fails
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
const b = await launch(['--autoplay-policy=document-user-activation-required', '--js-flags=--expose-gc']);
// A real click (CDP input, so a user gesture to Chrome) at a point where the page listens for nothing.
const click = async (x, y) => {
  for (const type of ['mousePressed', 'mouseReleased']) await b.send('Input.dispatchMouseEvent', { type, x, y, button: 'left', clickCount: 1 });
};
const errors = [];
let passed = [];
try {
  await b.send('Emulation.setDeviceMetricsOverride', { width: 1000, height: 700, deviceScaleFactor: 1, mobile: false });
  await b.send('Page.navigate', { url: `http://127.0.0.1:${PORT}/unit.html` });
  let r;
  let clicked = false;
  for (let i = 0; i < 400 && !r; i++) {
    await sleep(50);
    if (!clicked && await b.evaluate('window.WANT_GESTURE === true')) { await click(990, 690); clicked = true; }
    r = await b.evaluate('window.RESULT');
  }
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
  // A real key (CDP): the keyCode C reads, no character for an arrow, and the default stopped
  // (read by a window listener, which runs after the document's).
  await b.evaluate(`window.__kd = []; addEventListener('keydown', (e) => __kd.push(e.defaultPrevented)); __x.t_prevent(1)`);
  await b.send('Input.dispatchKeyEvent', { type: 'rawKeyDown', key: 'ArrowLeft', code: 'ArrowLeft', windowsVirtualKeyCode: 37 });
  await b.send('Input.dispatchKeyEvent', { type: 'keyUp', key: 'ArrowLeft', code: 'ArrowLeft', windowsVirtualKeyCode: 37 });
  const key = await b.evaluate(`[__x.t_ev(0), __x.t_ev(5), __x.t_ev(6), __kd[0]].join(' ')`);
  await b.evaluate(`__x.t_prevent(0); __x.t_listen_win(4, 0)`);
  if (key === '4 37 0 true') passed.push('a real ArrowLeft: keyCode 37, no character, the default stopped'); else errors.push('real key: ' + key);
  // The display preferences change (CDP emulates the media queries): dark, then dark with reduced
  // motion, then neither; cd_on_prefs hears each with what cd_prefs reads (heard: prefs + 1 a digit).
  const prefsAfter = async (features, last) => {
    await b.send('Emulation.setEmulatedMedia', { features });
    for (let i = 0; i < 100; i++) { await sleep(20); if (await b.evaluate(`__x.t_prefs_heard() % 10 === ${last} && __x.t_prefs() === ${last - 1}`)) return true; }
    return false;
  };
  const heardFrom = await b.evaluate('__x.t_prefs_heard()');
  const darkHeard = await prefsAfter([{ name: 'prefers-color-scheme', value: 'dark' }], 2);
  const bothHeard = await prefsAfter([{ name: 'prefers-color-scheme', value: 'dark' }, { name: 'prefers-reduced-motion', value: 'reduce' }], 4);
  const noneHeard = await prefsAfter([], 1);
  const prefsHeard = await b.evaluate('__x.t_prefs_heard()');
  if (heardFrom === 0 && darkHeard && bothHeard && noneHeard && await stale() === 0) passed.push('cd_on_prefs: dark, then reduced motion too, then neither, each heard with what cd_prefs reads, and the export flushes');
  else errors.push(`cd_on_prefs: heard ${prefsHeard} (dark ${darkHeard}, both ${bothHeard}, none ${noneHeard}), stale ${await stale()}`);
  // The real clipboard: denied by CDP (the browser refuses as it does outside a gesture) answers
  // FAILED; granted, the text C wrote (scribbled over in C as the call returns) is on it.
  const origin = `http://127.0.0.1:${PORT}`;
  const clipboardAsk = async (token, text) => {
    await b.evaluate(`(() => {
      const bytes = new TextEncoder().encode(${JSON.stringify(text)});
      new Uint8Array(__x.memory.buffer, __x.t_share_text(), bytes.length).set(bytes);
      __x.t_clipboard(${token}, bytes.length);
      new Uint8Array(__x.memory.buffer, __x.t_share_text(), 256).fill(42);
    })()`);
    for (let i = 0; i < 100 && await b.evaluate(`__x.t_clipboard_status(${token})`) === -1; i++) await sleep(20);
    return b.evaluate(`[__x.t_clipboard_status(${token}), __x.t_clipboard_answers(${token})].join()`);
  };
  await b.sendBrowser('Browser.setPermission', { permission: { name: 'clipboard-write' }, setting: 'denied', origin });
  const denied = await clipboardAsk(2, 'refused');
  await b.sendBrowser('Browser.grantPermissions', { permissions: ['clipboardReadWrite', 'clipboardSanitizedWrite'], origin });
  const written = await clipboardAsk(3, 'Ünit: cartes à jouer');
  const pasted = await b.evaluate('navigator.clipboard.readText()');
  if (denied === '0,1' && written === '1,1' && pasted === 'Ünit: cartes à jouer' && await stale() === 0) passed.push('cd_clipboard_write: denied, FAILED; granted, DONE and the text on the clipboard, read during the call, and the export flushes');
  else errors.push(`cd_clipboard_write: denied ${denied}, granted ${written}, pasted ${pasted}, stale ${await stale()}`);
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
  // The click demo: the hint until a tap, then the tap heard (the voice played out) and the hint gone.
  await b.send('Page.navigate', { url: `http://127.0.0.1:${PORT}/click.html` });
  const page = () => b.evaluate(`[...document.querySelectorAll('#m p')].map((p) => p.textContent).join('|')`);
  let before = '';
  for (let i = 0; i < 100 && !before.includes('clicks'); i++) { await sleep(20); before = await page(); }
  const at = await b.evaluate(`(() => { const r = document.querySelector('#m button').getBoundingClientRect(); return [r.left + r.width / 2, r.top + r.height / 2]; })()`);
  await click(at[0], at[1]);
  let tapped = '';
  for (let i = 0; i < 200 && tapped !== 'sound on|clicks played: 1'; i++) { await sleep(20); tapped = await page(); }
  if (before === 'tap for sound|clicks played: 0' && tapped === 'sound on|clicks played: 1') passed.push('the click demo: "tap for sound" until a tap, then the tap\'s click played'); else errors.push(`demo: ${before} then ${tapped}`);
} catch (e) { errors.push('threw: ' + String(e.message || e).slice(0, 300)); } finally { await b.close(); srv.close(); }
for (const p of passed) console.log('ok  ', p);
for (const e of errors) console.log('FAIL', e);
console.log(`${passed.length} passed, ${errors.length} failed`);
process.exit(errors.length ? 1 : 0);
