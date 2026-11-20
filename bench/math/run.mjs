// The math bench's runner for the wasm arms (docs/math-design.md): the same rounds (glue.js) in headless Chrome
// (tools/cdp.mjs; CHROME names the binary), in Safari (tools/safari.mjs) or in node itself.
// Usage: node bench/math/run.mjs chrome|safari|node [rounds=9] [ms=10] [arms=musl,msun,core,ours,js1,js1w,jsn]
// Prints TSV (env round entry domain arm ns reps), then each arm's sum and the browser's version as # lines.
// Needs make -C bench/math first. Every run waits for the 1-minute load to be under LOADMAX (default 4, at most
// 20 minutes), as the image bench does, and logs it.
import http from 'node:http';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { launch as chrome, sleep } from '../../tools/cdp.mjs';
import { launch as safari } from '../../tools/safari.mjs';

const here = path.dirname(new URL(import.meta.url).pathname);
const env = process.argv[2] || 'chrome';
const rounds = +(process.argv[3] || 9), ms = +(process.argv[4] || 10);
const arms = process.argv[5] || 'musl,msun,core,ours,js1,js1w,jsn';
const LOADMAX = +(process.env.LOADMAX || 4);
if (!['chrome', 'safari', 'node'].includes(env)) { console.error('run.mjs: chrome, safari or node'); process.exit(2); }
for (const a of arms.split(',')) if (!fs.existsSync(path.join(here, 'build', `${a === 'js1w' ? 'js1' : a}.wasm`))) { console.error(`no build/${a}.wasm: make -C bench/math`); process.exit(1); }
for (let w = 0; os.loadavg()[0] >= LOADMAX && w < 1200; w += 10) await sleep(10000);
console.log(`# load ${os.loadavg()[0].toFixed(2)} cpu ${os.cpus()[0].model} node ${process.version}`);
console.log('env\tround\tentry\tdomain\tarm\tns\treps');

if (env === 'node') {
  await import('./glue.js');
  const mods = arms.split(',').map((arm) => ({ arm, bytes: fs.readFileSync(path.join(here, 'build', `${arm === 'js1w' ? 'js1' : arm}.wasm`)) }));
  const info = await globalThis.mathBench.setup(mods, ms);
  console.log(`# node ${process.version} v8 ${process.versions.v8}, ${info.cells} cells`);
  for (let r = 0; r < rounds; r++) for (const row of globalThis.mathBench.round(r)) console.log(['node', ...row].join('\t'));
  for (const [a, s] of Object.entries(globalThis.mathBench.sums())) console.log(`# sum ${a} ${s}`);
  process.exit(0);
}

const srv = http.createServer((q, r) => {
  const f = path.join(here, decodeURIComponent(q.url.split(/[?#]/)[0]));
  if (!f.startsWith(here)) { r.writeHead(403); r.end(); return; }
  fs.readFile(f, (e, b) => {
    if (e) { r.writeHead(404); r.end(); return; }
    const type = f.endsWith('.html') ? 'text/html' : f.endsWith('.js') ? 'text/javascript' : f.endsWith('.wasm') ? 'application/wasm' : 'application/octet-stream';
    r.writeHead(200, { 'content-type': type, 'cross-origin-opener-policy': 'same-origin', 'cross-origin-embedder-policy': 'require-corp' });
    r.end(b);
  });
}).listen(0);
const b = env === 'safari' ? await safari() : await chrome();
try {
  const at = `http://127.0.0.1:${srv.address().port}/page.html`;
  await (env === 'safari' ? b.navigate(at) : b.send('Page.navigate', { url: at }));
  for (let i = 0; i < 400 && !(await b.evaluate('!!window.READY')); i++) await sleep(25);
  console.log(`# ${b.version}: ${await b.evaluate(`mbSetup(${JSON.stringify(arms)}, ${ms})`)}`);
  for (let r = 0; r < rounds; r++) {
    const text = await b.evaluate(`mbRound(${r})`);
    for (const line of text.split('\n')) console.log(`${env}\t${line}`);
  }
  console.log(await b.evaluate('mbSums()'));
} finally {
  await b.close();
  srv.close();
}
