// The inflate candidates in wasm, in headless Chrome (docs/image-png-speed.md, P2): each arm on each input, rounds
// interleaved and the arm order rotated each round, each run on a freshly loaded page.
// Usage: node bench/image/inflate/run.mjs <dir with the wasm builds> [rounds=9] [arms=base:0,base:1,simd:1,zlib:2,zng:3] [inputs=p12.png]
// An arm is <build>:<arm number of wasm.c's run>. Prints every run and then each arm's median, min and max.
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { launch, sleep } from '../../../tools/cdp.mjs';

const here = path.dirname(new URL(import.meta.url).pathname);
const wdir = path.resolve(process.argv[2]);
const rounds = +(process.argv[3] || 9);
const arms = (process.argv[4] || 'base:0,base:1,simd:1,zlib:2,zng:3').split(',');
const inputs = (process.argv[5] || 'p12.png').split(',');
const names = { '0': 'tinfl', '1': 'cdom', '2': 'zlib', '3': 'zlib-ng', '9': 'adler32' };
const srv = http.createServer((q, r) => {
  const u = decodeURIComponent(q.url.split(/[?#]/)[0]);
  const f = u.startsWith('/w/') ? path.join(wdir, u.slice(3)) : u.startsWith('/in/') ? path.join(here, '..', 'inputs', u.slice(4)) : path.join(here, u);
  fs.readFile(f, (e, b) => {
    if (e) { r.writeHead(404); r.end(); return; }
    r.writeHead(200, { 'content-type': f.endsWith('.html') ? 'text/html' : f.endsWith('.wasm') ? 'application/wasm' : 'application/octet-stream' });
    r.end(b);
  });
}).listen(0);
const base = () => `http://127.0.0.1:${srv.address().port}`;
const med = (v) => { const s = [...v].sort((a, b) => a - b); return s.length % 2 ? s[s.length >> 1] : (s[s.length / 2 - 1] + s[s.length / 2]) / 2; };
const b = await launch();
const all = {};
try {
  for (let r = 0; r < rounds; r++) for (const input of inputs) for (let k = 0; k < arms.length; k++) {
    const arm = arms[(k + r) % arms.length];
    const [build, n] = arm.split(':');
    await b.send('Page.navigate', { url: `${base()}/page.html` });
    for (let i = 0; i < 200 && !(await b.evaluate('!!window.go')); i++) await sleep(25);
    const res = await b.evaluate(`go('/w/${build}.wasm', ${n}, '/in/${input}')`);
    if (!res.made) throw new Error(`${arm} on ${input}: failed`);
    (all[`${input}\t${build}:${names[n]}`] ||= []).push(res);
    console.log(`round ${r}\t${input}\t${build}:${names[n]}\t${res.ms.toFixed(2)}\t${res.made}\t${res.bytes}`);
  }
} finally { await b.close(); srv.close(); }
console.log('input\tarm\tmedian ms\tmin\tmax\twasm bytes');
for (const [k, v] of Object.entries(all)) { const ms = v.map((x) => x.ms); console.log(`${k}\t${med(ms).toFixed(1)}\t${Math.min(...ms).toFixed(1)}\t${Math.max(...ms).toFixed(1)}\t${v[0].bytes}`); }
