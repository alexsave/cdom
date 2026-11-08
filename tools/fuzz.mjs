// Runs tests/out/listfuzz.html in headless Chrome for a few seeds; exit 1 on any mismatch.
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { launch, sleep, LEGACY, FETCHED } from './cdp.mjs';
// LEGACY=1 runs the same seeds as an old browser, on cdom.legacy.js and the wasm2js build (see cdp.mjs).
const root = path.resolve(path.dirname(new URL(import.meta.url).pathname), '../tests/out');
const types = { '.html': 'text/html', '.wasm': 'application/wasm', '.js': 'text/javascript' };
const srv = http.createServer((q, r) => { const f = path.join(root, q.url.split(/[?#]/)[0]);
  fs.readFile(f, (e, b) => { if (e) { r.writeHead(404); r.end(); return; } r.writeHead(200, { 'content-type': types[path.extname(f)] }); r.end(b); }); }).listen(0);
const b = await launch();
let bad = 0;
try {
  for (const seed of [1, 2, 3, 4, 5]) {
    await b.send('Page.navigate', { url: `http://127.0.0.1:${srv.address().port}/listfuzz.html?seed=${seed}` });
    let r;
    for (let i = 0; i < 600 && !r; i++) { await sleep(100); r = await b.evaluate('window.RESULT'); }
    console.log(`seed ${seed}: ${r.rounds} rounds, ${r.errors.length} errors`, r.errors.slice(0, 3).join(' | '));
    if (!r || r.errors.length || r.rounds !== 20000) bad++;
    // The wasm path never fetches the fallback, and the fallback never fetches the wasm.
    const got = await b.evaluate(FETCHED), want = LEGACY ? ['cdom.legacy.js', 'listfuzz.wasm.js'] : ['listfuzz.wasm'];
    const other = LEGACY ? ['listfuzz.wasm'] : ['cdom.legacy.js', 'listfuzz.wasm.js'];
    if (!want.every(f => got.includes(f)) || other.some(f => got.includes(f))) { bad++; console.log('wrong path, fetched', got.join(' ')); }
  }
} finally { await b.close(); srv.close(); }
process.exit(bad ? 1 : 0);
