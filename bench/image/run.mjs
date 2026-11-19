// The image bench (docs/image-design.md, "The bench"): each arm on each input, in headless Chrome through
// tools/cdp.mjs, interleaved: a round runs every (input, arm) once, the arms in an order that rotates each round,
// each run on a freshly loaded page, so one run's heap and caches never reach the next. Every run waits for the
// 1-minute load to be under LOADMAX (default 4, bounded at 20 minutes, as tools/pair.sh), and logs the load.
// Arms: C-js and C-eh (cd_image_resize, the two CDOM_IMAGE_SJLJ builds), B1 (createImageBitmap with resizeWidth,
// resizeHeight and resizeQuality "high") and B2 (createImageBitmap, then drawImage scaled, imageSmoothingQuality
// "high"); the browser arms end with getImageData, the pixels in hand as C's are.
// Usage: node bench/image/run.mjs [rounds=9] [box=900] [arms=C-js,C-eh,B1,B2] [inputs=p12.jpg,p12p.jpg,p48.jpg,p12.png]
// Writes bench/image/results/raw/<stamp>.tsv (every run) and prints the medians. Kills the Chrome it starts.
import http from 'node:http';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { launch, sleep } from '../../tools/cdp.mjs';

const here = path.dirname(new URL(import.meta.url).pathname);
const [rounds = 9, box = 900] = process.argv.slice(2, 4).map(Number);
const arms = (process.argv[4] || 'C-js,C-eh,B1,B2').split(',');
const inputs = (process.argv[5] || 'p12.jpg,p12p.jpg,p48.jpg,p12.png').split(',');
const LOADMAX = +(process.env.LOADMAX || 4);
for (const i of inputs) if (!fs.existsSync(path.join(here, 'inputs', i))) { console.error(`no inputs/${i}: make -C bench/image inputs`); process.exit(1); }

const srv = http.createServer((q, r) => {
  const f = path.join(here, decodeURIComponent(q.url.split(/[?#]/)[0]));
  if (!f.startsWith(here)) { r.writeHead(403); r.end(); return; }
  fs.readFile(f, (e, b) => { if (e) { r.writeHead(404); r.end(); return; } r.writeHead(200, { 'content-type': f.endsWith('.html') ? 'text/html' : 'application/octet-stream' }); r.end(b); });
}).listen(0);
const base = () => `http://127.0.0.1:${srv.address().port}`;
const load1 = () => os.loadavg()[0];
async function gate() {
  for (let w = 0; w < 1200; w += 10) { if (load1() < LOADMAX) return w; await sleep(10000); }
  return -1;
}
const med = (v) => { const s = [...v].sort((a, b) => a - b); return s.length % 2 ? s[s.length >> 1] : (s[s.length / 2 - 1] + s[s.length / 2]) / 2; };

fs.mkdirSync(path.join(here, 'results/raw'), { recursive: true });
const stamp = new Date().toISOString().replace(/[:.]/g, '-');   // a file name, not a random draw or a seed
const out = path.join(here, 'results/raw', `${stamp}.tsv`);
fs.writeFileSync(out, 'round\tinput\tarm\tms\tstatus\tw\th\twasm_bytes\tload_before\twaited_s\n');
const b = await launch();
const all = {};
try {
  for (let r = 0; r < rounds; r++) {
    for (const input of inputs) {
      for (let k = 0; k < arms.length; k++) {
        const arm = arms[(k + r) % arms.length];
        const waited = await gate();
        const loadBefore = load1();
        const page = arm === 'C-eh' ? 'build-eh' : 'build-js';
        await b.send('Page.navigate', { url: `${base()}/${page}/index.html` });
        for (let i = 0; i < 200 && !(await b.evaluate('!!window.READY')); i++) await sleep(25);
        const res = await b.evaluate(`arm(${JSON.stringify(arm.startsWith('C') ? 'C' : arm)}, ${JSON.stringify(`${base()}/inputs/${input}`)}, ${box})`);
        fs.appendFileSync(out, [r, input, arm, res.ms.toFixed(2), res.status ?? '', res.w, res.h, res.bytes ?? '', loadBefore.toFixed(2), waited].join('\t') + '\n');
        (all[`${input}\t${arm}`] ||= []).push(res);
        console.log(`round ${r} ${input} ${arm}: ${res.ms.toFixed(1)} ms ${res.w}x${res.h}${res.bytes ? ` wasm ${(res.bytes / 1048576).toFixed(1)} MiB` : ''} (load ${loadBefore.toFixed(2)})`);
      }
    }
  }
} finally {
  await b.close();
  srv.close();
}
console.log(`\ninput\tarm\tmedian ms\tmin\tmax\twasm MiB\t(${rounds} rounds, box ${box}, ${out})`);
for (const [key, v] of Object.entries(all)) {
  const ms = v.map((x) => x.ms);
  console.log(`${key}\t${med(ms).toFixed(1)}\t${Math.min(...ms).toFixed(1)}\t${Math.max(...ms).toFixed(1)}\t${v[0].bytes ? (Math.max(...v.map((x) => x.bytes)) / 1048576).toFixed(1) : ''}`);
}
