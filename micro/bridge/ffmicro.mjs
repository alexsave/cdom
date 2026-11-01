// The bridge micro in Firefox (headless, WebDriver BiDi through the puppeteer-core copy under a jfb
// checkout's webdriver-ts/node_modules, as tools/ffbench.mjs on direct-calls does; nothing installed).
//   node micro/bridge/ffmicro.mjs [rounds] [arms]
// One Firefox, killed by its own PID; fresh loads, the arms interleaved per round, load-gated.
// Arm d needs CSS Typed OM (attributeStyleMap, CSS.px), which Firefox does not have: it is not run.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import http from 'node:http';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const OUT = path.join(HERE, 'out');
const JFB = process.env.JFB || '/Users/alex/Dev/cdom-direct/jfb';
const { default: puppeteer } = await import(path.join(JFB, 'webdriver-ts/node_modules/puppeteer-core/lib/puppeteer/puppeteer-core.js'));
const ROUNDS = Number(process.argv[2] || 3);
const ARMS = (process.argv[3] || 'a,list,b,c,e,f').split(',');
const LOADMAX = Number(process.env.LOADMAX || 4);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const types = { '.html': 'text/html', '.wasm': 'application/wasm' };
const srv = http.createServer((q, r) => {
  const f = path.join(OUT, decodeURIComponent(q.url.split(/[?#]/)[0]));
  if (!f.startsWith(OUT)) { r.writeHead(404); r.end(); return; }
  fs.readFile(f, (e, b) => {
    if (e) { r.writeHead(404); r.end(); return; }
    r.writeHead(200, { 'content-type': types[path.extname(f)] || 'application/octet-stream', 'cache-control': 'no-store',
      'cross-origin-opener-policy': 'same-origin', 'cross-origin-embedder-policy': 'require-corp' });
    r.end(b);
  });
}).listen(0, '127.0.0.1');
const prof = fs.mkdtempSync(path.join(os.tmpdir(), 'cdom-bridge-firefox-'));
const browser = await puppeteer.launch({ browser: 'firefox', executablePath: '/Applications/Firefox.app/Contents/MacOS/firefox',
  headless: true, userDataDir: prof, extraPrefsFirefox: { 'browser.cache.disk.enable': false, 'browser.cache.memory.enable': false } });
const file = path.join(HERE, 'results', 'micro_firefox.jsonl');
fs.mkdirSync(path.dirname(file), { recursive: true });
try {
  const version = await browser.version();
  console.log('firefox', version, 'pid', browser.process()?.pid);
  const page = await browser.newPage();
  for (let r = 0; r < ROUNDS; r++) {
    for (let k = 0; k < ARMS.length; k++) {
      const arm = ARMS[(k + r) % ARMS.length];
      const t0 = Date.now();
      while (os.loadavg()[0] >= LOADMAX && Date.now() - t0 < 180000) await sleep(5000);
      const load = +os.loadavg()[0].toFixed(2);
      await page.goto('about:blank');
      await page.goto(`http://127.0.0.1:${srv.address().port}/micro/${arm}/index.html`);
      let res;
      for (let i = 0; i < 1200 && !res; i++) { await sleep(100); res = await page.evaluate('window.RESULT'); }
      if (!res || res.error) throw new Error(`${arm}: ${res ? res.error : 'no result'}`);
      fs.appendFileSync(file, JSON.stringify({ mode: 'micro', browser: 'firefox', chrome: version, cpu: 1, round: r, arm, load, over: load >= LOADMAX, flags: '', micro: res }) + '\n');
      console.log(`r${r} ${arm} load ${load} ${JSON.stringify(res)}`);
    }
  }
} finally {
  await browser.close();
  srv.close();
  fs.rmSync(prof, { recursive: true, force: true });
}
