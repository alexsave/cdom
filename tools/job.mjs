// Runs tests/out/job.html (tests/job.c, with the job tests/shrink_job.c) in headless Chrome: cd_job_cb and cd_job_file_cb.
// OK with every byte checked, a job that runs out of room (the capped build), a job that traps, a module that is not
// there, two jobs at once, a picked file read straight into the job's memory, and the tiers without Worker or
// WebAssembly (UNSUPPORTED). The Worker is wrapped to see what the page does with it: a blob: URL, the input
// transferred, the module fetched once, every worker ended. Exit 1 on any failure.
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { launch, sleep } from './cdp.mjs';
const top = path.resolve(path.dirname(new URL(import.meta.url).pathname), '..');
const root = path.join(top, 'tests/out');
const types = { '.html': 'text/html', '.wasm': 'application/wasm', '.js': 'text/javascript' };
const srv = http.createServer((q, r) => { const f = path.join(root, q.url.split(/[?#]/)[0]);
    fs.readFile(f, (e, b) => { if (e) { r.writeHead(404); r.end(); return; } r.writeHead(200, { 'content-type': types[path.extname(f)] }); r.end(b); }); }).listen(0);
const S = { OK: 0, NO_ROOM: 1, FAILED: 2, UNSUPPORTED: 3, NO_FILE: 4 };
// The files by index: a small one, and a 20 MB one the capped job (6.4 MB of heap) cannot hold.
const SMALL = 3000000, BIG = 20000000;
const fill = (n) => { const b = Buffer.alloc(n); for (let i = 0; i < n; i++) b[i] = (i * 29 + 11) & 255; return b; };
const fnv = (b) => { let h = 2166136261; for (const x of b) h = Math.imul(h ^ x, 16777619) >>> 0; return h; };
const files = [path.join(root, 'job-small.bin'), path.join(root, 'job-big.bin')];
fs.writeFileSync(files[0], fill(SMALL));
fs.writeFileSync(files[1], fill(BIG));
// What the job sums when passes is 0: the bytes (i * 131 + seed) & 255 over mb megabytes.
const sumOf = (mb, seed) => { let s = 0; for (let i = 0; i < mb << 20; i++) s = (s + ((i * 131 + seed) & 255)) >>> 0; return s; };

const b = await launch();
let fails = 0;
const ok = (c, m, why = '') => { if (!c) { fails++; console.log('FAIL', m + (why ? ': ' + why : '')); } else console.log('ok  ', m); };
const E = b.evaluate;
const F = ['status', 'calls', 'len', 'in_len', 'hash', 'mb', 'seed', 'sum', 'tail'];
const rec = async (k) => { const v = await E(`[0, 1, 2, 3, 4, 5, 6, 7, 8].map((f) => X.rec(${k}, f))`); const r = Object.fromEntries(F.map((n, i) => [n, v[i]])); r.status |= 0; for (const n of ['hash', 'sum', 'len']) r[n] >>>= 0; return r; };
const answered = async (ks, ms = 15000) => {
    for (let t = 0; t < ms; t += 25) { if ((await E(`[${ks}].every((k) => X.rec(k, 0) !== -1)`))) return true; await sleep(25); }
    return false;
};
// Every Worker the page makes: its URL, the largest input buffer it was sent and whether that went away (was transferred), how many are alive.
const WRAP = `(() => {
  window.W = { made: [], live: 0, max: 0, ended: 0, urls: [], detached: [], sent: [] };
  const Real = window.Worker, post = Real.prototype.postMessage, end = Real.prototype.terminate;
  window.Worker = function (url) { const w = new Real(url); W.made.push(w); W.urls.push(String(url)); W.live++; W.max = Math.max(W.max, W.live); return w; };
  Real.prototype.postMessage = function (m, t) { const input = m[1]; post.call(this, m, t);
    if (input instanceof ArrayBuffer) { W.sent.push(t.length); W.detached.push(input.byteLength === 0); } else W.sent.push(-1); };
  Real.prototype.terminate = function () { W.ended++; W.live--; return end.call(this); };
  window.READS = 0;
  for (const [o, n] of [[Blob.prototype, 'arrayBuffer'], [Blob.prototype, 'stream'], [FileReader.prototype, 'readAsArrayBuffer']]) { const f = o[n]; o[n] = function (...a) { READS++; return f.apply(this, a); }; }
})()`;
try {
    await b.send('Page.navigate', { url: `http://127.0.0.1:${srv.address().port}/job.html` });
    for (let i = 0; i < 100 && !(await E('!!window.READY')); i++) await sleep(50);
    ok(await E('!!window.READY'), 'the page started');
    await E(WRAP);
    await E('X.reset()');

    // OK: the input and args read during the call (the app overwrites and frees them at once), the answer from the event loop.
    const inBytes = Buffer.alloc(1000); for (let i = 0; i < 1000; i++) inBytes[i] = (i * 31 + 3) & 255;
    const sync = await E('X.ask(0, 0, 4, 0, 0, 5, 1000); X.rec(0, 0)');
    ok(sync === -1, 'no answer inside the call', String(sync));
    ok(await answered([0]), 'the job answered');
    const r0 = await rec(0);
    ok(r0.status === S.OK && r0.calls === 1 && r0.len === 65536 && r0.tail === 1, 'OK: 64 KB, every byte as the job wrote it, one answer', JSON.stringify(r0));
    ok(r0.in_len === 1000 && r0.hash === fnv(inBytes), 'OK: the input reached the job whole (length and hash), though C had freed and overwritten it', JSON.stringify(r0));
    ok(r0.mb === 4 && r0.seed === 5 && r0.sum === sumOf(4, 5), 'OK: the args reached the job, and its 4 MB were touched (the sum of its bytes)', JSON.stringify(r0));
    const w0 = await E('JSON.stringify({ made: W.made.length, live: W.live, ended: W.ended, urls: W.urls, detached: W.detached, sent: W.sent })');
    const w = JSON.parse(w0);
    ok(w.made === 1 && w.ended === 1 && w.live === 0, 'one worker, ended when it answered', w0);
    ok(w.urls.every((u) => u.startsWith('blob:')), 'the worker comes from a blob: URL', w0);
    ok(w.detached[0] === true && w.sent[0] === 2, 'the input and args are transferred, not copied (the buffer is detached after the post)', w0);
    const fetches = () => E(`performance.getEntriesByType('resource').filter((r) => r.name.endsWith('/shrink_job.wasm')).length`);
    await E('X.ask(1, 0, 1, 0, 0, 6, 10)');
    ok(await answered([1]) && (await rec(1)).status === S.OK && await fetches() === 1, 'the module is fetched and compiled once for two jobs', String(await fetches()));

    // Out of room: 20 MB asked of a job whose heap is capped at 6.4 MB; and a trap; and a module that is not there.
    await E('X.ask(2, 1, 20, 0, 0, 1, 10)');
    ok(await answered([2]) && (await rec(2)).status === S.NO_ROOM && (await rec(2)).len === 0, 'NO_ROOM: a job that cannot get its memory (cd_alloc_try answers 0) says so', JSON.stringify(await rec(2)));
    await E('X.ask(3, 0, 1, 0, 1, 1, 10)');
    ok(await answered([3]) && (await rec(3)).status === S.FAILED && (await rec(3)).len === 0, 'FAILED: a job that traps (the page lives on)', JSON.stringify(await rec(3)));
    const live = await E('W.live');
    ok(live === 0, 'a trapped job\'s worker is ended too', String(live));
    await E('X.ask(4, 0, 1, 0, 0, 7, 10)');
    ok(await answered([4]) && (await rec(4)).status === S.OK && (await rec(4)).seed === 7, 'a job after a trap runs normally', JSON.stringify(await rec(4)));

    // Two at once: two workers alive together, each with its own answer.
    const before = await E('W.made.length');
    await E('X.ask(5, 0, 8, 2, 0, 11, 100); X.ask(6, 0, 12, 2, 0, 12, 200)');
    ok(await answered([5, 6]), 'both jobs answered');
    const [r5, r6] = await Promise.all([rec(5), rec(6)]);
    ok(r5.status === 0 && r6.status === 0 && r5.mb === 8 && r6.mb === 12 && r5.seed === 11 && r6.seed === 12 && r5.in_len === 100 && r6.in_len === 200 && r5.tail && r6.tail,
      'two at once: each answered its own job', JSON.stringify([r5, r6]));
    ok(await E('W.max') >= 2 && await E('W.made.length') === before + 2 && await E('W.live') === 0, 'two workers alive together, both ended', await E('JSON.stringify([W.max, W.made.length - ' + before + ', W.live])'));

    // A picked file, read in the worker straight into the job's memory: never into the app's.
    const { result: { result: { objectId } } } = await b.send('Runtime.evaluate', { expression: 'document.getElementById("pick")' });
    await b.send('DOM.setFileInputFiles', { files, objectId });
    const pages0 = await E('X.pages()'), reads0 = await E('READS');
    await E('X.ask_file(7, 0, 0, 1, 9)');
    ok(await answered([7]), 'the file job answered');
    const r7 = await rec(7);
    ok(r7.status === S.OK && r7.in_len === SMALL && r7.hash === fnv(fs.readFileSync(files[0])) && r7.tail === 1, 'OK: the 3 MB file reached the job whole (length and hash)', JSON.stringify(r7));
    const pages1 = await E('X.pages()');
    ok(pages1 - pages0 <= 2 && await E('READS') === reads0, 'the file never entered the app: no read on the page, its memory grew by at most 2 pages', `pages ${pages0} -> ${pages1}, reads ${await E('READS')}`);
    await E('X.ask_file(8, 0, 5, 1, 9)');
    ok(await answered([8]) && (await rec(8)).status === S.NO_FILE, 'NO_FILE: nothing at that index', JSON.stringify(await rec(8)));
    await E('X.ask_file(9, 1, 1, 1, 9)');
    ok(await answered([9]) && (await rec(9)).status === S.NO_ROOM, 'NO_ROOM: a 20 MB file the capped job cannot hold', JSON.stringify(await rec(9)));
    ok(await E('W.live') === 0, 'every worker ended');

    // The answer's room goes back to the heap after the callback: ten more jobs of 64 KB answers (68 KB blocks) leave the app's memory where one left it.
    await E('X.ask(13, 0, 1, 0, 0, 1, 10)');
    await answered([13]);
    const pagesA = await E('X.pages()');
    for (let i = 1; i <= 10; i++) { await E('X.ask(14, 0, 1, 0, 0, 1, 10)'); for (let t = 0; t < 600 && await E('X.rec(14, 1)') < i; t++) await sleep(25); }
    const pagesB = await E('X.pages()');
    ok(await E('X.rec(14, 1)') === 10 && pagesB - pagesA <= 1, 'the answer\'s bytes are freed after the callback: ten more answers leave the app\'s memory (almost) where it was', `pages ${pagesA} -> ${pagesB}`);

    // The tiers without Worker, and without WebAssembly: UNSUPPORTED, from the event loop, no worker made.
    const made = await E('W.made.length');
    const un = await E(`(() => { const R = window.Worker; delete window.Worker; X.ask(10, 0, 1, 0, 0, 1, 10); const s = X.rec(10, 0); window.Worker = R; return s; })()`);
    ok(un === -1, 'UNSUPPORTED is answered from the event loop too', String(un));
    ok(await answered([10]) && (await rec(10)).status === S.UNSUPPORTED, 'UNSUPPORTED: no Worker', JSON.stringify(await rec(10)));
    await E(`(() => { window.WA = WebAssembly; delete window.WebAssembly; X.ask(11, 0, 1, 0, 0, 1, 10); X.ask_file(12, 0, 0, 1, 1); })()`);
    await sleep(300);
    await E('window.WebAssembly = WA');
    ok(await answered([11, 12]) && (await rec(11)).status === S.UNSUPPORTED && (await rec(12)).status === S.UNSUPPORTED, 'UNSUPPORTED: no WebAssembly (bytes and file forms)', JSON.stringify([await rec(11), await rec(12)]));
    ok(await E('W.made.length') === made, 'no worker was made for any of them');
} finally {
    await b.close();
    srv.close();
}
console.log(fails ? `${fails} failed` : 'job: all passed');
process.exit(fails ? 1 : 0);
