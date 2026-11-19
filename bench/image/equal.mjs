// The SIMD build's output against the scalar build's, byte for byte (docs/image-simd-hand.md): for each input and each box,
// cd_image_resize in headless Chrome on build-js (scalar) and build-simd (CDOM_IMAGE_SIMD=1), the status, the size and a
// SHA-256 of the RGBA block. Prints one line per (input, box) and exits 1 on any difference.
// Usage: node bench/image/equal.mjs [boxes=900,300,1500,2500,4000] [inputs=p12.jpg,p12p.jpg,p48.jpg,p12.png]
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { launch, sleep } from '../../tools/cdp.mjs';

const here = path.dirname(new URL(import.meta.url).pathname);
const boxes = (process.argv[2] || '900,300,1500,2500,4000').split(',').map(Number);
const inputs = (process.argv[3] || 'p12.jpg,p12p.jpg,p48.jpg,p12.png').split(',');
const srv = http.createServer((q, r) => {
  const f = path.join(here, decodeURIComponent(q.url.split(/[?#]/)[0]));
  fs.readFile(f, (e, b) => { if (e) { r.writeHead(404); r.end(); return; } r.writeHead(200, { 'content-type': f.endsWith('.html') ? 'text/html' : 'application/octet-stream' }); r.end(b); });
}).listen(0);
const base = `http://127.0.0.1:${srv.address().port}`;
const b = await launch();
const run = async (build, input, box) => {
  await b.send('Page.navigate', { url: `${base}/${build}/index.html` });
  for (let i = 0; i < 200 && !(await b.evaluate('!!window.READY')); i++) await sleep(25);
  return b.evaluate(`(async () => {
    const data = new Uint8Array(await (await fetch(${JSON.stringify(`${base}/inputs/${input}`)})).arrayBuffer());
    const p = X.room(data.length);
    new Uint8Array(X.memory.buffer, p, data.length).set(data);
    const status = X.resize(p, data.length, ${box}, ${box});
    const o = new Uint32Array(X.memory.buffer, X.out(), 3);
    let hash = '';
    if (status === 0) {
      const d = await crypto.subtle.digest('SHA-256', new Uint8Array(X.memory.buffer, o[2], o[0] * o[1] * 4));
      hash = [...new Uint8Array(d)].map((v) => v.toString(16).padStart(2, '0')).join('');
      X.drop(o[2]);
    }
    X.drop(p);
    return { status, w: o[0], h: o[1], hash };
  })()`);
};
let bad = 0;
try {
  for (const input of inputs) for (const box of boxes) {
    const a = await run('build-js', input, box), c = await run('build-simd', input, box);
    const same = a.status === c.status && a.w === c.w && a.h === c.h && a.hash === c.hash;
    if (!same) bad++;
    console.log(`${same ? 'equal' : 'DIFFER'}  ${input} box ${box}: status ${a.status}, ${a.w} x ${a.h}, sha256 ${a.hash.slice(0, 16)}${same ? '' : ` vs ${c.hash.slice(0, 16)} (status ${c.status}, ${c.w} x ${c.h})`}`);
  }
} finally { await b.close(); srv.close(); }
process.exit(bad ? 1 : 0);
