// The image module in wasm, in headless Chrome (tests/image/run.sh, step 4): tests/image/wasm.c built by cdom.mk each
// way out of a libjpeg error (CDOM_IMAGE_SJLJ js and eh), every case of the oracle's cases.tsv run on the page:
// the status it lists, and an image equal to its .rgba to the byte. Then the error path itself: a fatal libjpeg
// error (a bad DQT), 300 times over, answers CORRUPT each time, leaves the stack pointer where it was and the heap
// as it was, and the next good file still decodes exactly. Then a heap capped at CAP pages (CDOM_HEAP_PAGES_MAX):
// the 12 MP progressive JPEG is NO_ROOM, never a trap, and a small file decodes after it; and the same with the
// module's own memory limited (wasm-ld --max-memory, both ways), where memory.grow itself refuses.
// And the chunked read (docs/chunked-file-read-design.md, tests 11 and 13), in the builds without a cap: every case's
// file picked from disk (CDP DOM.setFileInputFiles) and decoded by cd_image_file_cb, its status and its bytes as above;
// 300 corrupt files in a row, the memory still from the second on; the tier without Blob.stream (slices); the same
// files in a job (cd_job_pieces_cb, tests/image/job.c), with the job's memory and the app's; and cd_job_stop and
// cd_image_file_stop: no answer, the worker ended, no slot or memory kept.
// Usage: node tools/image.mjs <dir with cases.tsv>   Exit 1 on any failure. Kills the Chrome it starts.
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { launch, sleep } from './cdp.mjs';

const top = path.resolve(path.dirname(new URL(import.meta.url).pathname), '..');
const dir = path.resolve(process.argv[2] || path.join(top, 'tests/out/image'));
const CAP = 400; // pages (64 KiB each): 26 MB, under the 12 MP progressive JPEG's 36 MB of coefficients
const S = ['OK', 'NOT_IMAGE', 'TOO_BIG', 'UNSUPPORTED', 'CORRUPT', 'NO_ROOM', 'BAD_BOX', 'UNREADABLE'];
const MAXMEM = CAP * 65536; // the same room, as the module's own limit: memory.grow itself refuses past it
// Every build twice: scalar (CDOM_IMAGE_SIMD=0, the default) and with the hand-written wasm SIMD loops (CDOM_IMAGE_SIMD=1),
// so both pass the whole set. IMAGE_EH=0 leaves out the eh builds, for a clang whose wasm EH lowering is not clang 23's
// (the js way and SIMD are plain wasm).
const builds = [['js', '', ''], ['eh', '', ''], ['js', CAP, ''], ['js', '', MAXMEM], ['eh', '', MAXMEM]]
  .flatMap((x) => [[...x, 0], [...x, 1]])
  .filter((x) => process.env.IMAGE_EH !== '0' || x[0] !== 'eh');
for (const [sjlj, cap, max, simd] of builds)
  execFileSync('make', ['-s', '-f', path.join(top, 'tests/image/wasm.mk'), `OUT=${dir}`, `SJLJ=${sjlj}`, ...(cap ? [`CAP=${cap}`] : []), ...(max ? [`MAXMEM=${max}`] : []), ...(simd ? ['SIMD=1'] : []), 'all'], { stdio: 'inherit' });

const types = { '.html': 'text/html' };
const srv = http.createServer((q, r) => {
  const f = path.join(dir, decodeURIComponent(q.url.split(/[?#]/)[0]));
  if (!f.startsWith(dir)) { r.writeHead(403); r.end(); return; }
  fs.readFile(f, (e, b) => { if (e) { r.writeHead(404); r.end(); return; } r.writeHead(200, { 'content-type': types[path.extname(f)] || 'application/octet-stream' }); r.end(b); });
}).listen(0);
const url = (f) => `http://127.0.0.1:${srv.address().port}/${path.relative(dir, f)}`;
const cases = fs.readFileSync(path.join(dir, 'cases.tsv'), 'utf8').trim().split('\n').map((l) => l.split('\t'));

let fails = 0;
const ok = (c, m, why = '') => { if (!c) { fails++; console.log('FAIL', m + (why ? ': ' + why : '')); } else console.log('ok  ', m); };
const b = await launch();
// The chunked read on the page loaded now (see the header).
const picked = [...new Set(cases.map((c) => c[1]))].concat([path.join(dir, 'inputs/dqt-bad.jpg'), path.join(dir, 'inputs/dqt-bad-progressive.jpg')]);
const WRAP = `(() => {
  window.W = { made: 0, live: 0, ended: 0 };
  const Real = window.Worker, end = Real.prototype.terminate;
  window.Worker = function (url) { const w = new Real(url); W.made++; W.live++; return w; };
  Real.prototype.terminate = function () { W.ended++; W.live--; return end.call(this); };
})()`;
async function chunked(name) {
  const { result: { result: { objectId } } } = await b.send('Runtime.evaluate', { expression: 'document.getElementById("pick")' });
  await b.send('DOM.setFileInputFiles', { files: picked, objectId });
  await b.evaluate(WRAP);
  const runFile = (i, bw, bh, exp, job, report) => b.evaluate(`runFile(${i}, ${bw}, ${bh}, ${exp ? JSON.stringify(url(exp)) : 'null'}, ${job === undefined ? 'undefined' : job}, ${report ? 1 : 0})`);
  const each = async (job, what, filter = () => true) => {
    let bad = 0, n = 0;
    for (const [cname, file, bw, bh, want, ew, eh] of cases) {
      if (!filter(cname, file)) continue;
      const exact = !want.startsWith('STATUS');
      const r = await runFile(picked.indexOf(file), +bw, +bh, exact ? want : null, job);
      const status = exact ? 'OK' : want.split(':')[1];
      const good = S[r.status] === status && r.calls >= 1 && (!exact || (r.len === +ew * +eh * 4 && r.diff === -1 && (job !== undefined || (r.w === +ew && r.h === +eh))));
      if (!good) { bad++; console.log(`FAIL ${name}: ${what}: ${cname}: ${S[r.status] ?? r.status} ${r.w}x${r.h} (${r.len} bytes), first difference at ${r.diff}; wanted ${status}${exact ? ` ${ew}x${eh}, exact` : ''}`); }
      n++;
    }
    fails += bad;
    ok(bad === 0, `${name}: ${what}: every case of cases.tsv (${n}) picked from disk has its status, and each image equals the exact one to the byte`);
  };
  await each(undefined, 'cd_image_file_cb');
  // 300 corrupt files in a row: CORRUPT each time, the memory still from the second on
  const bad1 = picked.length - 2, bad2 = picked.length - 1;
  const r = await b.evaluate(`(async () => {
    const statuses = new Set(), pages = new Set();
    for (let i = 0; i < 300; i++) { const r = await runFile(i % 2 ? ${bad2} : ${bad1}, 300, 300, null); statuses.add(r.status); if (i >= 2) pages.add(r.pages); }
    return { statuses: [...statuses], pages: [...pages] };
  })()`);
  ok(r.statuses.length === 1 && r.statuses[0] === 4 && r.pages.length === 1, `${name}: cd_image_file_cb on 300 corrupt files in a row: CORRUPT every time, the memory grows 0 pages after the first`, JSON.stringify(r));
  // the tier without Blob.stream: FileReader on slices
  await b.evaluate('window.STREAM = Blob.prototype.stream; delete Blob.prototype.stream');
  await each(undefined, 'cd_image_file_cb without Blob.stream (slices)', (c) => ['jpeg-420', 'jpeg-progressive', 'png-rgb', 'png-rgba', 'jpeg-exif6', 'jpeg-p12-progressive', 'png-p12'].includes(c));
  await b.evaluate('Blob.prototype.stream = STREAM');
  // in a job, the file in pieces; and the job's memory and the app's
  await each(1, 'cd_job_pieces_cb');
  const p12png = picked.findIndex((f) => f.endsWith('/p12.png'));
  if (p12png >= 0) {
    await runFile(p12png, 900, 900, null, 1);
    const a0 = await b.evaluate('X.memory.buffer.byteLength >> 16');
    const j = await runFile(p12png, 900, 900, null, 1, 1);
    const j0 = await runFile(p12png, 900, 900, null, 0, 1);
    await runFile(p12png, 900, 900, null, 1);
    const a1 = await b.evaluate('X.memory.buffer.byteLength >> 16');
    ok(j.status === 0 && j.jobPages > 0 && j.jobPages * 65536 < j0.jobPages * 65536 - 12e6 && j.len === j.w * j.h * 4 && a1 - a0 <= 2,
      `${name}: p12.png (${fs.statSync(picked[p12png]).size} bytes) in a job: the pieces job's memory at its end ${(j.jobPages / 16).toFixed(2)} MiB (the whole-file job's ${(j0.jobPages / 16).toFixed(2)}); the app's memory grows ${a1 - a0} pages after the first answer`, JSON.stringify({ j, j0, a0, a1 }));
  } else ok(false, `${name}: no p12.png case (IMAGE_BIG=0?)`);
  // cd_job_stop and cd_image_file_stop: no answer, the worker ended, the slot and the memory as before
  const big = p12png >= 0 ? p12png : 0;
  for (const delay of [0, 30, 120]) {
    const slot = await b.evaluate('X.slot_probe()'), calls = await b.evaluate('X.file_rec(5)'), live = await b.evaluate('W.live');
    await b.evaluate(`X.job_start(${big}, 900, 900, 1, 0)`);
    await sleep(delay);
    await b.evaluate('X.job_stop()');
    await sleep(800);
    const after = { slot: await b.evaluate('X.slot_probe()'), calls: await b.evaluate('X.file_rec(5)'), live: await b.evaluate('W.live') };
    ok(after.slot === slot && after.calls === calls && after.live === live && live === 0, `${name}: cd_job_stop ${delay} ms after the start: no callback, the worker ended, the callback slot freed`, JSON.stringify({ slot, calls, live, after }));
  }
  const pages0 = await b.evaluate('X.memory.buffer.byteLength >> 16');
  for (const delay of [0, 10, 40]) {
    const slot = await b.evaluate('X.slot_probe()'), calls = await b.evaluate('X.file_rec(5)');
    await b.evaluate(`X.file_start(${big}, 900, 900)`);
    await sleep(delay);
    await b.evaluate('X.file_stop()');
    await sleep(300);
    const after = { slot: await b.evaluate('X.slot_probe()'), calls: await b.evaluate('X.file_rec(5)') };
    ok(after.slot === slot && after.calls === calls, `${name}: cd_image_file_stop ${delay} ms after the start: no callback, the callback slots freed`, JSON.stringify({ slot, calls, after }));
  }
  const pages1 = await b.evaluate('X.memory.buffer.byteLength >> 16');
  ok(pages1 === pages0, `${name}: three stopped decodes leave the memory as it was`, `${pages0} -> ${pages1}`);
}
try {
  for (const [sjlj, cap, max, simd] of builds) {
    const name = `wasm_${sjlj}${cap ? '_cap' : ''}${max ? '_max' : ''}${simd ? '_simd' : ''}`;
    await b.send('Page.navigate', { url: url(path.join(dir, name + '.html')) });
    for (let i = 0; i < 200 && !(await b.evaluate('!!window.READY')); i++) await sleep(50);
    ok(await b.evaluate('!!window.READY'), `${name}: the page started`);
    const run = (file, bw, bh, exp) => b.evaluate(`run(${JSON.stringify(url(file))}, ${bw}, ${bh}, ${exp ? JSON.stringify(url(exp)) : 'null'})`);
    if (!cap && !max) {
      let bad = 0, n = 0, sp0 = null;
      for (const [cname, file, bw, bh, want, ew, eh] of cases) {
        const exact = !want.startsWith('STATUS');
        const r = await run(file, +bw, +bh, exact ? want : null);
        const status = want.startsWith('STATUS') ? want.split(':')[1] : 'OK';
        const good = S[r.status] === status && (!exact || (r.w === +ew && r.h === +eh && r.diff === -1));
        if (sp0 === null) sp0 = r.sp;
        if (!good) { bad++; console.log(`FAIL ${name}: ${cname}: ${S[r.status] ?? r.status} ${r.w}x${r.h}, first difference at ${r.diff}; wanted ${status}${exact ? ` ${ew}x${eh}, exact` : ''}`); }
        if (r.sp !== sp0) { bad++; console.log(`FAIL ${name}: ${cname}: the stack pointer moved from ${sp0} to ${r.sp}`); }
        n++;
      }
      fails += bad;
      ok(bad === 0, `${name}: every case of cases.tsv (${n}) has its status, and each image equals the exact one to the byte`);
      const bad1 = path.join(dir, 'inputs/dqt-bad.jpg'), bad2 = path.join(dir, 'inputs/dqt-bad-progressive.jpg');
      const r = await b.evaluate(`(async () => {
        const first = await run(${JSON.stringify(url(bad1))}, 300, 300, null);
        const statuses = new Set(), sps = new Set(), pages = new Set();
        for (let i = 0; i < 300; i++) {
          const r = await run(i % 2 ? ${JSON.stringify(url(bad2))} : ${JSON.stringify(url(bad1))}, 300, 300, null);
          statuses.add(r.status); sps.add(r.sp); if (i >= 2) pages.add(r.pages);
        }
        return { first, statuses: [...statuses], sps: [...sps], pages: [...pages] };
      })()`);
      ok(r.statuses.length === 1 && r.statuses[0] === 4, `${name}: a fatal libjpeg error, 300 times, baseline and progressive, is CORRUPT every time`, JSON.stringify(r.statuses));
      ok(r.sps.length === 1 && r.sps[0] === sp0, `${name}: the stack pointer is where it was after every one (the guard puts it back)`, JSON.stringify(r.sps) + ' vs ' + sp0);
      ok(r.pages.length === 1, `${name}: the memory does not grow from the third error on (nothing is left allocated)`, JSON.stringify(r.pages));
      const g = cases.find((c) => c[0] === 'jpeg-420');
      const after = await run(g[1], +g[2], +g[3], g[4]);
      ok(after.status === 0 && after.diff === -1, `${name}: after the errors, a good JPEG decodes exactly`, JSON.stringify(after));
      await chunked(name);
    } else {
      const p12p = cases.find((c) => c[0] === 'jpeg-p12-progressive'), small = cases.find((c) => c[0] === 'jpeg-420');
      if (p12p) {
        const r = await run(p12p[1], 900, 900, null);
        ok(S[r.status] === 'NO_ROOM', `${name}: with ${cap ? `the heap capped at ${cap} pages` : `the module's memory at most ${max} bytes (memory.grow refuses)`}, the 12 MP progressive JPEG is NO_ROOM, not a trap`, JSON.stringify(r));
      } else ok(false, `${name}: no jpeg-p12-progressive case (IMAGE_BIG=0?)`);
      const r = await run(small[1], +small[2], +small[3], small[4]);
      ok(r.status === 0 && r.diff === -1, `${name}: then a small JPEG decodes exactly`, JSON.stringify(r));
    }
  }
  // The example (demo/photo, README.md "image"): a photo picked in its file input, as a user picks one, comes back
  // resized, encoded and shown, and a file that is not an image is said in words.
  execFileSync('make', ['-s', '-C', path.join(top, 'demo/photo')], { stdio: 'inherit' });
  const demo = path.join(top, 'demo/photo/build/index.html');
  const pickFile = async (file) => {
    const { result: { result: { objectId } } } = await b.send('Runtime.evaluate', { expression: 'document.querySelector("input[type=file]")' });
    await b.send('DOM.setFileInputFiles', { files: [file], objectId });
  };
  const note = () => b.evaluate('document.querySelector("p").textContent');
  const waitNote = async (want) => { for (let i = 0; i < 400; i++) { const n = await note(); if (n === want) return n; await sleep(25); } return note(); };
  await b.send('Page.navigate', { url: 'file://' + demo });
  for (let i = 0; i < 200 && !(await b.evaluate('!!document.querySelector("input[type=file]")')); i++) await sleep(25);
  const exif = cases.find((c) => c[0] === 'jpeg-exif6');
  await pickFile(exif[1]);
  const done = await waitNote('Resized in C.');
  const bg = await b.evaluate('getComputedStyle(document.querySelector("#app div")).backgroundImage');
  ok(done === 'Resized in C.' && bg.startsWith('url("blob:'), 'demo/photo: a picked JPEG (EXIF 6) is resized in C, encoded and shown', `${done}, ${bg.slice(0, 40)}`);
  await pickFile(path.join(dir, 'cases.tsv'));
  ok(await waitNote('That file is not a JPEG or a PNG.') === 'That file is not a JPEG or a PNG.', 'demo/photo: a file that is not an image is said in words');
} finally {
  await b.close();
  srv.close();
}
process.exit(fails ? 1 : 0);
