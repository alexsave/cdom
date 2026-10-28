// Runs micro/direct/direct.html in headless Chrome and prints ns per call for each arm.
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { launch, sleep } from './cdp.mjs';
const root = path.resolve(path.dirname(new URL(import.meta.url).pathname), '../micro/direct');
const types = { '.html': 'text/html', '.wasm': 'application/wasm' };
const srv = http.createServer((q, r) => { const f = path.join(root, q.url.split(/[?#]/)[0]);
  fs.readFile(f, (e, b) => { if (e) { r.writeHead(404); r.end(); return; } r.writeHead(200, { 'content-type': types[path.extname(f)] }); r.end(b); }); }).listen(0);
const b = await launch();
try {
  for (let rep = 0; rep < (+process.argv[2] || 3); rep++) {
    await b.send('Page.navigate', { url: `http://127.0.0.1:${srv.address().port}/direct.html` });
    let r; for (let i = 0; i < 600 && !r; i++) { await sleep(100); r = await b.evaluate('window.RESULT'); }
    console.log(JSON.stringify(r));
  }
} finally { await b.close(); srv.close(); }
