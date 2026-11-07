// Runs tests/out/canvas.html in headless Chrome; exit 1 on any mismatch.
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { launch, sleep } from './cdp.mjs';
const root = path.resolve(path.dirname(new URL(import.meta.url).pathname), '../tests/out');
const types = { '.html': 'text/html', '.wasm': 'application/wasm', '.js': 'text/javascript' };
const srv = http.createServer((q, r) => { const f = path.join(root, q.url.split(/[?#]/)[0]);
  fs.readFile(f, (e, b) => { if (e) { r.writeHead(404); r.end(); return; } r.writeHead(200, { 'content-type': types[path.extname(f)] }); r.end(b); }); }).listen(0);
const b = await launch();
let r;
try {
  await b.send('Page.navigate', { url: `http://127.0.0.1:${srv.address().port}/canvas.html` });
  for (let i = 0; i < 100 && !r; i++) { await sleep(100); r = await b.evaluate('window.RESULT'); }
} finally { await b.close(); srv.close(); }
console.log(r ? (r.errors.length ? r.errors.join('\n') : 'canvas: all ok') : 'canvas: no result');
process.exit(r && !r.errors.length ? 0 : 1);
