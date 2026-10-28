// Firefox (159, WebDriver BiDi through puppeteer-core's firefox product; the copy under
// jfb/webdriver-ts/node_modules, nothing installed) for the proof of why direct calls win and
// for first paint. One Firefox per process, killed by its own PID.
//   micro                 micro/direct/direct.html (micro_direct.mjs's page): ns per call per arm
//   ops <entries> <S>     S paired sessions of 01_run1k, 07_create10k, 09_clear1k with the
//                         harness's steps (fresh page, 5x run+clear warm-up, clear: one more run),
//                         COUNT measured clicks per (session, benchmark, entry), entries
//                         interleaved click by click, every page load gated on the 1-minute load
//                         < LOADMAX (3). Timing (the harness's own needs Chrome tracing): from the
//                         click event's timeStamp to the end of the click's dispatch (script, a
//                         bubbling listener on window, after the app's own on #main) and to the
//                         first task after the next frame (total: rAF then a 0 ms timeout, the
//                         after-frame method). Differences from the harness: no CPU throttling
//                         (Firefox has none) and no forced GC before the click (content cannot
//                         call one); a 300 ms settle instead.
//   fp <entries> <R>      first paint as the harness's 40_ size run takes it: compression on
//                         (GET /enableCompression: the server brotli-compresses every file of
//                         1 KB or more), a fresh page, the paint timing entries ('first-paint'
//                         when Firefox reports one, else 'first-contentful-paint', recorded as
//                         which), the server's /sizeInfo bytes, compression off again; R
//                         interleaved rounds, every load gated.
// Results as JSONL to OUT; a summary on stdout.
// Usage: node tools/ffbench.mjs micro | ops "pf-list pf-direct" 6 | fp "vanillajs react-hooks" 11
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import http from 'node:http';
import { execFileSync } from 'node:child_process';
const top = path.resolve(path.dirname(new URL(import.meta.url).pathname), '..');
const { default: puppeteer } = await import(path.join(top, 'jfb/webdriver-ts/node_modules/puppeteer-core/lib/puppeteer/puppeteer-core.js'));
const sleep = ms => new Promise(r => setTimeout(r, ms));
const [mode, entriesArg, nArg] = process.argv.slice(2);
const PORT = process.env.PORT || 8703, COUNT = +(process.env.COUNT || 15), LOADMAX = +(process.env.LOADMAX || 3);
const load1 = () => +execFileSync('sysctl', ['-n', 'vm.loadavg']).toString().trim().split(/\s+/)[1];
async function calm() { let l = load1(); for (let i = 0; i < 240 && l >= LOADMAX; i++) { await sleep(5000); l = load1(); } return l; }
const out = [];
const rec = (r) => { out.push(r); if (process.env.OUT) fs.appendFileSync(process.env.OUT, JSON.stringify(r) + '\n'); };
const prof = fs.mkdtempSync(path.join(os.tmpdir(), 'cdom-firefox-'));
const browser = await puppeteer.launch({ browser: 'firefox', executablePath: '/Applications/Firefox.app/Contents/MacOS/firefox', headless: true,
  userDataDir: prof, defaultViewport: { width: 1280, height: 800 }, extraPrefsFirefox: { 'browser.cache.disk.enable': false, 'browser.cache.memory.enable': false } });
const pid = browser.process()?.pid;
let srv;
try {
  console.log('firefox', await browser.version(), 'pid', pid);
  const page = await browser.newPage();
  if (mode === 'micro') {
    const root = path.join(top, 'micro/direct'), types = { '.html': 'text/html', '.wasm': 'application/wasm' };
    srv = http.createServer((q, r) => { const f = path.join(root, q.url.split(/[?#]/)[0]); fs.readFile(f, (e, b) => { if (e) { r.writeHead(404); r.end(); return; } r.writeHead(200, { 'content-type': types[path.extname(f)] }); r.end(b); }); }).listen(0);
    for (let rep = 0; rep < (+entriesArg || 3); rep++) {
      await calm();
      await page.goto(`http://127.0.0.1:${srv.address().port}/direct.html`);
      let r; for (let i = 0; i < 600 && !r; i++) { await sleep(100); r = await page.evaluate('window.RESULT'); }
      console.log(JSON.stringify(r)); rec({ mode, rep, r });
    }
  } else if (mode === 'ops') {
    const entries = entriesArg.split(' '), S = +nArg || 6, first = +(process.env.FIRST || 1);
    const td1 = (k) => `tbody>tr:nth-of-type(${k})>td:nth-of-type(1)`;
    const waitFor = async (fn, ...a) => { for (let i = 0; i < 400; i++) { if (await page.evaluate(fn, ...a)) return; await sleep(i < 3 ? 10 : 25); } throw new Error('check failed'); };
    const hasText = (sel, t) => document.querySelector(sel)?.textContent.includes(t);
    const gone = (sel) => !document.querySelector(sel);
    for (let s = first; s < first + S; s++) for (const bench of ['run1k', 'create10k', 'clear']) for (let i = 0; i < COUNT; i++) {
      const order = (s + i) % 2 ? entries : [...entries].reverse();
      for (const e of order) {
        const load = await calm();
        await page.goto('about:blank');
        await page.goto(`http://localhost:${PORT}/frameworks/keyed/${e}/index.html`, { waitUntil: 'load' });
        await waitFor(() => !!document.getElementById('run'));
        for (let w = 0; w < 5; w++) {
          await page.click('#run'); await waitFor(hasText, td1(1), String(w * 1000 + 1));
          await page.click('#clear'); await waitFor(gone, td1(1000));
        }
        if (bench === 'clear') { await page.click('#run'); await waitFor(hasText, td1(1), '5001'); }
        await sleep(300);
        await page.evaluate(() => { window.__m = {};
          addEventListener('click', (ev) => { window.__m.t0 = ev.timeStamp; requestAnimationFrame(() => setTimeout(() => { window.__m.t2 = performance.now(); }, 0)); }, { capture: true, once: true });
          addEventListener('click', () => { window.__m.t1 = performance.now(); }, { once: true }); });
        const id = bench === 'run1k' ? '#run' : bench === 'create10k' ? '#runlots' : '#clear';
        await page.click(id);
        await waitFor(() => window.__m.t2 !== undefined);
        if (bench === 'run1k') await waitFor(hasText, td1(1000), '6000');
        else if (bench === 'create10k') await waitFor(() => !!document.querySelector('tbody>tr:nth-of-type(10000)>td:nth-of-type(2)>a'));
        else await waitFor(gone, td1(1000));
        const m = await page.evaluate(() => window.__m);
        rec({ mode, session: s, bench, entry: e, i, script: m.t1 - m.t0, total: m.t2 - m.t0, load });
      }
    }
  } else if (mode === 'fp') {
    const entries = entriesArg.split(' '), R = +nArg || 11;
    for (let r = 0; r < R; r++) for (const e of (r % 2 ? [...entries].reverse() : entries)) {
      const load = await calm();
      const p = await browser.newPage();
      try {
        const base = `http://localhost:${PORT}`;
        if (await (await fetch(`${base}/enableCompression`)).text() !== 'OK') throw new Error('could not enable compression');
        await p.goto(`${base}/frameworks/keyed/${e}/index.html`, { waitUntil: 'load' });
        let paint = []; for (let i = 0; i < 200; i++) { paint = await p.evaluate(() => performance.getEntriesByType('paint').map(x => [x.name, x.startTime])); if (paint.some(x => x[0] === 'first-contentful-paint')) break; await sleep(25); }
        const size = await (await fetch(`${base}/sizeInfo`)).json();
        await fetch(`${base}/disableCompression`);
        const fpE = paint.find(x => x[0] === 'first-paint') || paint.find(x => x[0] === 'first-contentful-paint');
        const ok = await p.evaluate(() => !!document.getElementById('run'));
        rec({ mode, round: r, entry: e, fp: fpE ? fpE[1] : null, which: fpE ? fpE[0] : null, paint, size, ok, load });
      } finally { await p.close(); }
    }
  }
} finally {
  if (mode === 'fp') { try { await fetch(`http://localhost:${PORT}/disableCompression`); } catch {} }
  try { await browser.close(); } catch {}
  if (pid) { try { process.kill(pid, 'SIGKILL'); } catch {} }
  if (srv) srv.close();
  try { fs.rmSync(prof, { recursive: true, force: true }); } catch {}
}
