// Median ms of 20000 rows (40000 opens) built and cleared, for the two arms, alternating fresh loads.
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { launch, sleep } from '../../tools/cdp.mjs';
const root = path.resolve(path.dirname(new URL(import.meta.url).pathname), 'out');
const srv = http.createServer((q, r) => { const f = path.join(root, q.url.split('?')[0]);
  fs.readFile(f, (e, b) => { if (e) { r.writeHead(404); r.end(); return; } r.writeHead(200, { 'content-type': f.endsWith('.wasm') ? 'application/wasm' : f.endsWith('.js') ? 'text/javascript' : 'text/html' }); r.end(b); }); }).listen(8724);
const b = await launch();
const res = { c: [], js: [] };
try {
  for (let i = 0; i < +(process.argv[2] || 15); i++) for (const arm of i % 2 ? ['js', 'c'] : ['c', 'js']) {
    await b.send('Page.navigate', { url: `http://127.0.0.1:8724/${arm}/page.html` });
    let v; for (let k = 0; k < 200 && v === undefined; k++) { await sleep(50); v = await b.evaluate('window.RESULT'); }
    res[arm].push(v);
  }
} finally { await b.close(); srv.close(); }
const med = (a) => [...a].sort((x, y) => x - y)[a.length >> 1];
console.log(`C tracks the namespace: ${med(res.c).toFixed(3)} ms; JS compares the tag: ${med(res.js).toFixed(3)} ms (median of ${res.c.length} loads, 40000 opens each)`);
