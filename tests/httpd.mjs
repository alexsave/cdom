// Drives src/httpd.c over raw sockets: static files, 404, traversal, routes, POST
// echo, keep-alive and pipelining, a body split across writes, size limits, HEAD.
// Usage: node tests/httpd.mjs (builds tests/out/httpd_test first; port 8702).
import { spawn, execFileSync } from 'node:child_process';
import fs from 'node:fs';
import net from 'node:net';
import path from 'node:path';
const here = path.dirname(new URL(import.meta.url).pathname);
const out = path.join(here, 'out'), root = path.join(out, 'httpd_root'), PORT = 8702;
const sleep = ms => new Promise(r => setTimeout(r, ms));
fs.mkdirSync(path.join(root, 'sub'), { recursive: true });
execFileSync('nice', ['-n', '10', 'cc', '-O2', '-Wall', '-Wextra', '-Werror', '-o', path.join(out, 'httpd_test'),
  path.join(here, 'httpd_test.c'), path.join(here, '../src/httpd.c')], { stdio: 'inherit' });
const files = { 'index.html': '<!doctype html><p>root</p>', 'a.js': 'var a = 1;', 'c.css': 'p{color:red}',
  'sub/index.html': '<p>sub</p>', 'x.unknown': 'raw' };
for (const [f, b] of Object.entries(files)) fs.writeFileSync(path.join(root, f), b);
const wasm = Buffer.from([0, 0x61, 0x73, 0x6d, 1, 0, 0, 0, 0xff, 0x00, 0x80]);
fs.writeFileSync(path.join(root, 'b.wasm'), wasm);
fs.writeFileSync(path.join(out, 'secret.txt'), 'secret');

// Parses every complete response in buf: [{status, headers, body}], and the bytes left.
function parse(buf, headOnly = false) {
  const res = [];
  for (;;) {
    const e = buf.indexOf('\r\n\r\n');
    if (e < 0) break;
    const lines = buf.subarray(0, e).toString('latin1').split('\r\n');
    const status = +lines[0].split(' ')[1], headers = {};
    for (const l of lines.slice(1)) { const i = l.indexOf(':'); headers[l.slice(0, i).toLowerCase()] = l.slice(i + 1).trim(); }
    const len = headOnly ? 0 : +headers['content-length'];
    if (buf.length < e + 4 + len) break;
    res.push({ status, headers, body: buf.subarray(e + 4, e + 4 + len) });
    buf = buf.subarray(e + 4 + len);
  }
  return res;
}
// Sends the chunks (with a pause between) and reads until want responses or the close.
function exchange(chunks, want = 1, headOnly = false) {
  return new Promise((resolve, reject) => {
    const s = net.connect(PORT, '127.0.0.1');
    let buf = Buffer.alloc(0), closed = false, done = false;
    const finish = () => { if (done) return; done = true; s.destroy(); resolve({ res: parse(buf, headOnly), closed }); };
    s.on('data', d => { buf = Buffer.concat([buf, d]); if (parse(buf, headOnly).length >= want) setTimeout(finish, 50); });
    s.on('close', () => { closed = true; finish(); });
    s.on('error', e => { closed = true; finish(); });
    s.on('connect', async () => { for (const c of chunks) { s.write(c); await sleep(30); } });
    setTimeout(() => done || finish(), 3000);
  });
}
const get = (p, extra = '') => `GET ${p} HTTP/1.1\r\nHost: x\r\n${extra}\r\n`;

let fails = 0;
const ok = (c, m) => { if (!c) { fails++; console.log('FAIL', m); } else console.log('ok  ', m); };
const srv = spawn(path.join(out, 'httpd_test'), [String(PORT), root], { stdio: ['ignore', 'inherit', 'inherit'] });
try {
  for (let i = 0; i < 50 && !(await exchange([get('/')])).res.length; i++) await sleep(100);
  let r = (await exchange([get('/index.html')])).res[0];
  ok(r?.status === 200 && r.headers['content-type'].startsWith('text/html') && r.body.toString() === files['index.html'], 'static html');
  r = (await exchange([get('/')])).res[0];
  ok(r?.status === 200 && r.body.toString() === files['index.html'], '/ is index.html');
  r = (await exchange([get('/sub/')])).res[0];
  ok(r?.status === 200 && r.body.toString() === files['sub/index.html'], 'directory index');
  r = (await exchange([get('/b.wasm')])).res[0];
  ok(r?.status === 200 && r.headers['content-type'] === 'application/wasm' && r.body.equals(wasm), 'wasm bytes and type');
  r = (await exchange([get('/a.js')])).res[0];
  ok(r?.headers['content-type'].startsWith('text/javascript'), 'js type');
  r = (await exchange([get('/c.css')])).res[0];
  ok(r?.headers['content-type'].startsWith('text/css'), 'css type');
  r = (await exchange([get('/x.unknown')])).res[0];
  ok(r?.headers['content-type'] === 'application/octet-stream', 'unknown type');
  r = (await exchange([get('/missing.html')])).res[0];
  ok(r?.status === 404, 'missing is 404');
  r = (await exchange([get('/sub')])).res[0];
  ok(r?.status === 404, 'a directory without the slash is 404');
  for (const t of ['/../secret.txt', '/%2e%2e/secret.txt', '/sub/../../secret.txt', '/.%2E/secret.txt']) {
    r = (await exchange([get(t)])).res[0];
    ok(r?.status === 404, `traversal ${t} is 404`);
  }
  r = (await exchange([get('/a%00.js')])).res[0];
  ok(r?.status === 400, 'encoded NUL is 400');
  r = (await exchange([get('/hello?x=1&n=42&y')])).res[0];
  ok(r?.status === 200 && r.body.toString() === 'hello 42', 'route with query');
  r = (await exchange([`POST /hello HTTP/1.1\r\nContent-Length: 0\r\n\r\n`])).res[0];
  ok(r?.status === 405, 'wrong method is 405');
  const big = Buffer.alloc(300000); for (let i = 0; i < big.length; i++) big[i] = (i * 7) & 255;
  r = (await exchange([Buffer.concat([Buffer.from(`POST /echo HTTP/1.1\r\nContent-Length: ${big.length}\r\nX-Echo-Type: application/x-test\r\n\r\n`), big])])).res[0];
  ok(r?.status === 200 && r.headers['content-type'] === 'application/x-test' && r.body.equals(big), 'POST body echo (300000 bytes)');
  r = (await exchange([`POST /echo HTTP/1.1\r\nContent-Length: 11\r\n\r\nhello`, ` world`])).res[0];
  ok(r?.status === 200 && r.body.toString() === 'hello world', 'body split across writes');
  let x = await exchange([get('/hello?n=1') + get('/hello?n=2') + `POST /echo HTTP/1.1\r\nContent-Length: 3\r\n\r\nabc` + get('/hello?n=3')], 4);
  ok(x.res.map(r => r.body.toString()).join('|') === 'hello 1|hello 2|abc|hello 3', 'pipelined keep-alive in order');
  x = await exchange([get('/hello?n=1'), get('/hello?n=2')], 2);
  ok(x.res.length === 2 && x.res[1].headers.connection === 'keep-alive', 'keep-alive across writes');
  x = await exchange([get('/hello?n=1', 'Connection: close\r\n')], 1);
  ok(x.res[0]?.headers.connection === 'close' && x.closed, 'Connection: close closes');
  x = await exchange([`GET /hello HTTP/1.0\r\n\r\n`], 1);
  ok(x.res[0]?.status === 200 && x.closed, 'HTTP/1.0 closes by default');
  x = await exchange([`GET /hello HTTP/1.1\r\nX-Big: ${'a'.repeat(20000)}\r\n\r\n`], 1);
  ok(x.res[0]?.status === 431 && x.closed, 'oversized header is 431 and a close');
  x = await exchange([`GET /hello HTTP/1.1\r\nX-Big: ${'a'.repeat(20000)}`], 1);
  ok(x.res[0]?.status === 431 && x.closed, 'oversized unfinished header is 431 and a close');
  x = await exchange([`GET /hello HTTP/1.1\r\n${'X-A: b\r\n'.repeat(65)}\r\n`], 1);
  ok(x.res[0]?.status === 431 && x.closed, 'too many headers is 431');
  x = await exchange([`POST /echo HTTP/1.1\r\nContent-Length: ${(64 << 20) + 1}\r\n\r\n`], 1);
  ok(x.res[0]?.status === 413 && x.closed, 'Content-Length over the limit is 413');
  x = await exchange([`POST /echo HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n`], 1);
  ok(x.res[0]?.status === 501 && x.closed, 'chunked request body is 501');
  x = await exchange([`GARBAGE\r\n\r\n`], 1);
  ok(x.res[0]?.status === 400 && x.closed, 'bad request line is 400');
  x = await exchange([`GET /hello HTTP/1.1\r\nBad Header: x\r\n\r\n`], 1);
  ok(x.res[0]?.status === 400 && x.closed, 'space in header name is 400');
  x = await exchange([`HEAD /index.html HTTP/1.1\r\n\r\n` + get('/hello?n=9')], 2, true);
  ok(x.res[0]?.status === 200 && +x.res[0].headers['content-length'] === files['index.html'].length, 'HEAD has the length');
  {
    const s = net.connect(PORT, '127.0.0.1'); let b = Buffer.alloc(0);
    s.on('error', () => {});
    await new Promise(res => { s.on('connect', () => s.write(`HEAD /index.html HTTP/1.1\r\n\r\n` + get('/hello?n=9'))); s.on('data', d => { b = Buffer.concat([b, d]); if (b.toString().endsWith('hello 9')) res(); }); setTimeout(res, 2000); });
    s.destroy();
    ok(/^HTTP\/1\.1 200[^]*\r\n\r\nHTTP\/1\.1 200[^]*\r\n\r\nhello 9$/.test(b.toString()), 'HEAD sends no body (the next response follows the blank line)');
  }
  r = (await exchange([get('/hello?n=after')])).res[0];
  ok(r?.body.toString() === 'hello after', 'still serving after the bad clients');
} finally { srv.kill('SIGTERM'); }
console.log(fails ? `${fails} FAILED` : 'all ok');
process.exit(fails ? 1 : 0);
