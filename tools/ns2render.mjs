// node tools/ns2render.mjs: the Mac's tests of Netscape 2's self-painting page (demo/ns2render, docs/descent.md "Netscape 2
// paints itself"):
//  1. the differential test: the same C, native (demo/ns2render/out/ns2render html) and as JavaScript 1.0 (tools/js1c's
//     out/paint.wasm.js1.js driven by host.js in node, as the cdomApp frame runs it), gives byte-identical markup, for every
//     page and state, batches of 1, 8 and 20 rows, both write modes, and draws made partly from the host's cache of batches;
//  2. the pixel test against headless Chrome (tools/ns2render_check.py): the picture's non-text pixels, its rounded corners and
//     its overlaps within tolerance of the design's Chrome render.
// NS2RENDER_TIME=1 also prints the renderer's time in node (the median of 9 draws a page) and natively.
// Needs the build's tools (make -C demo/ns2render: Homebrew llvm, binaryen) and, for 2, tests/out/paint/src (tools/paintshots.sh).
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { execFileSync } from 'node:child_process';

const top = path.resolve(path.dirname(new URL(import.meta.url).pathname), '..');
const dir = path.join(top, 'demo/ns2render');
const out = path.join(dir, 'out');
execFileSync('make', ['-s', '-C', dir, 'out/ns2render', 'out/paint.wasm.js1.js'], { stdio: 'inherit' });

// The host and the module in this realm, as a browser runs its scripts; setTimeout takes a string, as Netscape 2's does.
globalThis.window = globalThis;
const timers = [];
const realTimeout = globalThis.setTimeout;
globalThis.setTimeout = (code) => { timers.push(code); return 1; };
for (const f of [path.join(out, 'paint.wasm.js1.js'), path.join(dir, 'host.js'), path.join(dir, 'status.js')]) vm.runInThisContext(fs.readFileSync(f, 'utf8'), { filename: f });
let written = '';
const view = { open() { written = ''; }, write(s) { written += s; }, close() {} };
globalThis.rsView = view;
globalThis.location = { href: 'http://10.0.2.2:8130/ns2render/paint.app.html' };
globalThis.rsTables();
globalThis.rsX = globalThis.cdomFactory({});
globalThis.rsM = globalThis.rsX.memory.words;
globalThis.rsBase = 'http://10.0.2.2:8130/ns2render/';
const drain = () => { while (timers.length) vm.runInThisContext(timers.shift()); };
function draw(page, state, batch, mode, keep) {
  if (!keep) globalThis.rsC = {};   // no batch kept from an earlier draw: every row computed
  globalThis.rsBatch = batch;
  globalThis.rsMode = mode;
  globalThis.rsJoin = mode === 1 && batch === 1 ? 3 : 0;
  globalThis.rsDraw(page, state);
  drain();
  const head = globalThis.rsHead();
  if (!written.startsWith(head)) throw new Error('no head');
  return written.slice(head.length, written.lastIndexOf('<IMG SRC="end.gif'));
}

let fails = 0;
const check = (ok, what) => { console.log((ok ? 'ok   ' : 'FAIL ') + what); if (!ok) fails++; };
const nativeHtml = (page, state, batch) => execFileSync(path.join(out, 'ns2render'), ['html', String(page), String(state), String(batch)], { maxBuffer: 1 << 26 }).toString('latin1');
for (const [page, state] of [[0, 1], [0, 2], [1, 1]]) {
  for (const [batch, mode] of [[8, 1], [1, 0], [20, 1]]) {
    const native = nativeHtml(page, state, batch);
    const js = draw(page, state, batch, mode);
    check(js === native, `js1 page ${page} state ${state} batch ${batch} mode ${mode}: the same ${native.length} bytes as native` + (js === native ? '' : ` (got ${js.length}; first difference at ${[...js].findIndex((c, i) => c !== native[i])})`));
  }
}
// the host's cache of batches: the hover state from the normal state's batches outside rs_dirty's rows, then details, then
// still again from the cache alone
{
  draw(0, 1, 8, 1);
  for (const [page, state] of [[0, 2], [1, 1], [0, 1], [0, 2]]) {
    const js = draw(page, state, 8, 1, true);
    check(js === nativeHtml(page, state, 8), `cached draw of page ${page} state ${state}: the same markup as native`);
  }
}
if (process.env.NS2RENDER_TIME) {
  for (const [page, state] of [[0, 1], [0, 2], [1, 1]]) {
    const t = [];
    for (let i = 0; i < 9; i++) {
      draw(page, state, 8, 0);
      t.push([globalThis.rsTc, globalThis.rsTm, globalThis.rsTe - globalThis.rsT0]);
    }
    const med = (k) => t.map((x) => x[k]).sort((a, b) => a - b)[4];
    const nat = execFileSync(path.join(out, 'ns2render'), ['time', String(page), String(state), '200']).toString().trim();
    console.log(`time page ${page} state ${state}: node compute ${med(0)} ms, markup ${med(1)} ms, whole draw ${med(2)} ms; native ${nat}`);
  }
}
globalThis.setTimeout = realTimeout;
try {
  execFileSync('python3', [path.join(top, 'tools/ns2render_check.py')], { stdio: 'inherit' });
} catch { fails++; }
console.log(fails ? `${fails} failed` : 'all passed');
process.exit(fails ? 1 : 0);
