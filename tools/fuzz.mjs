// Runs tests/out/listfuzz.html in headless Chrome for a few seeds; exit 1 on any mismatch.
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { launch, sleep, BELOW, FETCHED, refused, tierFetched } from './cdp.mjs';
// LEGACY=1 runs the same seeds as an old browser, on cdom.legacy.js and the wasm2js build (see cdp.mjs), and
// LEGACY=ie9 on cdom.legacy-ie9.js and the words build; LEGACY=ie8 checks instead that the boot refuses IE 8
// (refused, in cdp.mjs).
const root = path.resolve(path.dirname(new URL(import.meta.url).pathname), '../tests/out');
const types = { '.html': 'text/html', '.wasm': 'application/wasm', '.js': 'text/javascript' };
const srv = http.createServer((q, r) => { const f = path.join(root, q.url.split(/[?#]/)[0]);
  fs.readFile(f, (e, b) => { if (e) { r.writeHead(404); r.end(); return; } r.writeHead(200, { 'content-type': types[path.extname(f)] }); r.end(b); }); }).listen(+(process.env.FUZZ_PORT || 0)); // 0: a free port, so two worktrees' runs never collide
const b = await launch();
let bad = 0;
try {
  if (BELOW) {
    await b.send('Page.navigate', { url: `http://127.0.0.1:${srv.address().port}/listfuzz.html?seed=1` });
    for (const [c, m] of await refused(b, '#m')) { console.log(c ? 'ok  ' : 'FAIL', m); if (!c) bad++; }
  }
  for (const seed of BELOW ? [] : [1, 2, 3, 4, 5]) {
    await b.send('Page.navigate', { url: `http://127.0.0.1:${srv.address().port}/listfuzz.html?seed=${seed}` });
    let r;
    for (let i = 0; i < 600 && !r; i++) { await sleep(100); r = await b.evaluate('window.RESULT'); }
    console.log(`seed ${seed}: ${r.rounds} rounds, ${r.errors.length} errors`, r.errors.slice(0, 3).join(' | '));
    if (!r || r.errors.length || r.rounds !== 40000) bad++;
    // Each tier fetches its own files and no other's.
    const [right, what] = tierFetched(await b.evaluate(FETCHED), 'listfuzz');
    if (!right) { bad++; console.log('wrong path:', what); }
  }
} finally { await b.close(); srv.close(); }
process.exit(bad ? 1 : 0);
