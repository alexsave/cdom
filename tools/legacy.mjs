// Drives tests/out/legacy.html with real input through CDP; exit 1 on any failure.
// LEGACY=1 node tools/legacy.mjs runs it as a browser without WebAssembly (see cdp.mjs): the page must then
// take the wasm2js path, on cdom.legacy.js's polyfills and PEP. Without it, the same checks run on wasm.
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { launch, sleep, LEGACY, FETCHED } from './cdp.mjs';

const root = path.resolve(path.dirname(new URL(import.meta.url).pathname), '../tests/out');
const types = { '.html': 'text/html', '.wasm': 'application/wasm', '.js': 'text/javascript' };
const srv = http.createServer((q, r) => { const f = path.join(root, q.url.split(/[?#]/)[0]);
  fs.readFile(f, (e, b) => { if (e) { r.writeHead(404); r.end(); return; } r.writeHead(200, { 'content-type': types[path.extname(f)] }); r.end(b); }); }).listen(0);
// Event types by name, read from cdom.h's enum (its numbers are cdom's to change).
const header = fs.readFileSync(path.resolve(root, '../../src/cdom.h'), 'utf8');
const EV = Object.fromEntries(header.match(/enum \{ (CD_EV_CLICK[^}]*)\}/)[1].split(',')
  .map((n, i) => [n.trim().replace(/^CD_EV_/, ''), i]));
const b = await launch();
let fails = 0;
// A hang is a failure: a broken polyfill can leave the C looping on bytes it never cleared.
setTimeout(() => { console.log('FAIL no result within 90 s'); process.exit(1); }, 90000).unref();
const ok = (c, m) => { if (!c) { fails++; console.log('FAIL', m); } else console.log('ok  ', m); };
const E = b.evaluate;
const mouse = (type, x, y, buttons = 0, button = 'none') =>
  b.send('Input.dispatchMouseEvent', { type, x, y, buttons, button, clickCount: type === 'mouseMoved' ? 0 : 1 });
const centre = async (sel) => E(`(function () { var r = document.querySelector(${JSON.stringify(sel)}).getBoundingClientRect();
  return [Math.round(r.left + r.width / 2), Math.round(r.top + r.height / 2)]; })()`);
async function click(sel) {
  const [x, y] = await centre(sel);
  await mouse('mouseMoved', x, y); await mouse('mousePressed', x, y, 1, 'left'); await mouse('mouseReleased', x, y, 0, 'left');
  await sleep(50);
}
const since = async (n) => (await E('LOG')).slice(n);
try {
  await b.send('Page.navigate', { url: `http://127.0.0.1:${srv.address().port}/legacy.html` });
  for (let i = 0; i < 100 && !(await E('!!window.READY')); i++) await sleep(50);
  ok(await E('!!window.READY'), 'booted');
  ok(await E('X.sj() === 1'), "the image module's js way out of a libjpeg error: 500 throws from 20 frames down, the stack pointer back, then a body that returns");
  const id = {}; for (const [k, i] of Object.entries({ PAD: 0, TEXT: 1 })) id[k] = await E(`X.id(${i})`);

  // The path: the fallback files only without WebAssembly (the scalar module's wasm2js build), the wasm only with it: the
  // page starts the pair [legacy.wasm, legacy.simd.wasm], and this Chrome has wasm SIMD, so only the twin.
  const got = await E(FETCHED), mine = ['legacy.wasm.js', 'cdom.legacy.js'], wasms = got.filter(f => f.endsWith('.wasm'));
  ok(LEGACY ? mine.every(f => got.includes(f)) && !wasms.length : !got.some(f => mine.includes(f)) && wasms.join() === 'legacy.simd.wasm',
    `${LEGACY ? 'wasm2js' : 'wasm'} path only: ${got.filter(f => /\.(js|wasm)$/.test(f)).join(' ')}`);
  if (LEGACY) {
    ok(await E('typeof WebAssembly === "undefined" && typeof X.memory.grow === "function"'), 'no WebAssembly; the exports are wasm2js');
    ok(await E('!/native code/.test(String(TextDecoder)) && !/native code/.test(String(TextEncoder))'), 'TextDecoder and TextEncoder are the polyfills');
    ok(await E('!/native code/.test(String(PointerEvent))'), 'PointerEvent is PEP');
    ok(await E('__noCrypto && !!window.msCrypto && window.crypto === window.msCrypto'), 'no crypto but msCrypto, as in IE 11; the polyfill made it crypto');
    // The two fallback files are fetched in parallel: the app's starts before the shim's has arrived.
    const [shim, app] = await E(`['cdom.legacy.js', 'legacy.wasm.js'].map(f => performance.getEntriesByType('resource')
      .find(r => r.name.endsWith('/' + f))).map(r => r && [r.startTime, r.responseEnd])`);
    ok(shim && app && app[0] < shim[1], `legacy.wasm.js requested before cdom.legacy.js had arrived: ${JSON.stringify({ shim, app })}`);
  }

  // The memory grows and a new string is read right after it: the views are stale, on wasm and on wasm2js.
  const before = await E('X.memory.buffer.byteLength');
  await E('X.grow()');
  ok(await E('X.memory.buffer.byteLength') > before, 'the memory grew');
  ok(await E('document.getElementById("echo").textContent') === 'read after the memory grew \u00e9',
    `a string read after the memory grew: ${await E('document.getElementById("echo").textContent')}`);

  // A literal the macro cannot see is its own address: each read from memory once, then kept (a Map, or a plain
  // object where there is none), and the same on a page with no Map.
  const roRun = async (what) => {
    const seen = [];
    for (const i of [0, 1, 0, 1]) { await E(`X.ro(${i})`); seen.push(await E('document.getElementById("echo").textContent')); }
    ok(seen.join('|') === 'ro-zero|ro-one \u00e9|ro-zero|ro-one \u00e9', `read-only strings ${what}: ${seen.join('|')}`);
  };
  await roRun('with a Map');

  // Pointer events: one press, a drag and a release on the pad, each delivered exactly once.
  let n = (await E('LOG')).length;
  await mouse('mouseMoved', 50, 50); await mouse('mousePressed', 50, 50, 1, 'left');
  await mouse('mouseMoved', 60, 55, 1); await mouse('mouseReleased', 60, 55, 0, 'left'); await sleep(50);
  const log = (await since(n)).filter(e => e[1] === id.PAD);
  const of = (t) => log.filter(e => e[0] === t);
  ok(of(EV.POINTERDOWN).length === 1 && of(EV.POINTERUP).length === 1 && of(EV.CLICK).length === 1,
    `one pointerdown, one pointerup, one click: ${JSON.stringify(log)}`);
  const down = of(EV.POINTERDOWN)[0] || [], up = of(EV.POINTERUP)[0] || [];
  ok(down[2] === 50 && down[3] === 50 && down[4] === 1 && down[5] === 1, `pointerdown at 50,50, pointer 1, buttons 1: ${down}`);
  ok(of(EV.POINTERMOVE).some(e => e[2] === 60 && e[3] === 55 && e[5] === 1), 'pointermove to 60,55 with the button held');
  ok(up[2] === 60 && up[3] === 55 && up[5] === 0, `pointerup at 60,55, no buttons: ${up}`);
  const order = log.map(e => e[0]).filter(t => t !== EV.POINTERMOVE);
  ok(order.join() === [EV.POINTERDOWN, EV.POINTERUP, EV.CLICK].join(), `down, up, click in order: ${order}`);

  // A focus() inside a batch: its focus event comes after the click that caused it, once.
  n = (await E('LOG')).length;
  await click('#focus');
  const f = await since(n);
  ok(f.filter(e => e[0] === EV.FOCUS && e[1] === id.TEXT).length === 1 && f[f.length - 1][0] === EV.FOCUS,
    `focus delivered once, after the click: ${JSON.stringify(f)}`);

  // Text: typed into the input, read into C (encodeInto), sent back as the echo (the decoder).
  // Latin, Cyrillic (a lead byte whose fourth bit is set), CJK and an astral character.
  const word = 'héllo ж 日本 \u{1F600}';
  await b.send('Input.insertText', { text: word }); await sleep(50);
  ok(await E('document.getElementById("echo").textContent') === word, `echo of ${word}: ${await E('document.getElementById("echo").textContent')}`);
  // The value is 21 bytes: h é l l o (6), space (7), ж (9), space (10), 日 本 (16), space (17), 😀 (21).
  const cuts = {}; for (const c of [6, 7, 8, 9, 10, 11, 12, 13, 16, 17, 18, 20, 21, 40]) cuts[c] = await E(`X.cut(${c})`);
  ok(JSON.stringify(cuts) === JSON.stringify({ 6: 6, 7: 7, 8: 7, 9: 9, 10: 10, 11: 10, 12: 10, 13: 13, 16: 16, 17: 17, 18: 17, 20: 17, 21: 21, 40: 21 }),
    `a read never splits a character: ${JSON.stringify(cuts)}`);
  const wholes = []; for (const c of [0, 6, 8, 20, 21, 40]) wholes.push(await E(`X.whole(${c})`));
  const W = 1 << 16;
  ok(wholes.join() === [21, 21, 21, 21, W | 21, W | 21].join(), `cd_input_whole says the whole length, cut or not, and whether it fitted: ${wholes.join()}`);

  // A failing call is reported and the batch goes on.
  await E('ERRORS.length = 0');
  await click('#err'); await sleep(50);
  const errs = await E('ERRORS');
  // A TypeError, whose wording differs: Babel calls the missing method through .apply.
  ok(errs.length === 1 && /^TypeError: /.test(errs[0]), `the failed call reported once: ${JSON.stringify(errs)}`);
  ok(await E('document.getElementById("err").getAttribute("data-after")') === 'yes', 'the op after it applied');

  // The polyfills against the built-ins: the app cannot tell some of them apart from a wrong one (a wrong
  // imul still gives a consistent intern hash, a short fill leaves bytes that were zero anyway), so each is
  // checked on its own, the same expressions in the page and in node.
  if (LEGACY) {
    const U = (f) => `(function () { var a = new Uint8Array([1, 2, 3, 4, 5, 6, 7, 8]); a.${f}; return Array.prototype.slice.call(a); })()`;
    const exprs = [
      ...[[3, 4], [-5, 12], [0xffffffff, 5], [0x7fffffff, 0x7fffffff], [0x12345678, 0x9abcdef0], [65536, 65536], [-1, -1]]
        .map(([a, b]) => `Math.imul(${a}, ${b})`),
      // MDN's clz32 goes through Math.log, so every power of two and its neighbours, where rounding would show.
      ...[0, -1, 0.5, 4294967297, ...Array.from({ length: 32 }, (_, k) => [2 ** k - 1, 2 ** k, 2 ** k + 1]).flat()]
        .map(v => `Math.clz32(${v})`),
      ...['0', '1.1', '-1.1', '1e39', '5e-46', 'NaN', '16777217', '-0'].map(v => `Math.fround(${v})`),
      ...['1.7', '-1.7', '0.2', '-0.2', '1e21', '-0', 'NaN', 'Infinity'].map(v => `Math.trunc(${v})`),
      U('copyWithin(2, 0, 5)'), U('copyWithin(0, 3, 8)'), U('copyWithin(5, 1)'), U('copyWithin(-2, 0)'), U('copyWithin(1, -3, -1)'),
      U('fill(7, 2, 5)'), U('fill(9)'), U('fill(0, 6)'), U('fill(3, -2)'), U('fill(4, 1, 99)'),
      'Object.entries({ a: 1, b: "x", c: [2] })', 'Object.entries({})', 'Object.entries([7, 8])',
      '"--x".startsWith("--")', '"a--".startsWith("--", 1)', '"x--".startsWith("--")', '"pan-y".includes("pan-")', '"abc".includes("d")', '"abcabc".includes("a", 4)',
    ];
    const show = (v) => Object.is(v, -0) ? '-0' : JSON.stringify(v) ?? String(v);
    const page = await E(`[${exprs.join(', ')}].map(function (v) { return 1 / v === -Infinity ? '-0' : JSON.stringify(v) || String(v); })`);
    const bad = exprs.filter((x, i) => page[i] !== show(new Function('return ' + x)()));
    ok(!bad.length, `polyfills agree with the built-ins on ${exprs.length} cases: ${bad.map(x => `${x} gave ${page[exprs.indexOf(x)]}`).join('; ')}`);
    ok(await E('globalThis === window'), 'globalThis is the window');
    ok(await E('!/native code/.test(String(Object.entries)) && !/native code/.test(String(Math.imul) + String(Math.clz32) + String(Math.fround) + String(Math.trunc) + String(Uint8Array.prototype.fill) + String(Uint8Array.prototype.copyWithin))'),
      'those were the polyfills');
  }

  // Without options objects, every listener the shim adds must get a plain capture flag: bubbling types
  // false, the non-bubbling ones (focus) true. An object would mean capture for all of them.
  if (LEGACY) {
    const adds = (await E('__adds')).filter(a => a[0] !== 'cdom-probe');
    const objs = adds.filter(a => a[1] === 'object');
    ok(!objs.length, `no options object reached addEventListener: ${JSON.stringify(objs)}`);
    const phase = (t) => adds.filter(a => a[0] === t).map(a => a[2]);
    ok(phase('click').includes(false) && !phase('click').includes(true), `click in the bubble phase: ${phase('click')}`);
    ok(phase('focus').includes(true), `focus in the capture phase: ${phase('focus')}`);
  }
  // Picked files on the old tiers: a file set on the input (a DataTransfer, as a drop gives), then read as IE 10
  // and 11 have it (FileReader): the bytes; then as IE 9 has it (no FileReader): UNSUPPORTED (4). A call that
  // throws is a failure of its own case, not the end of the run.
  const fileAsk = async () => {
    try { await E('X.file_ask()'); } catch (e) { return 'threw ' + String(e).slice(0, 120); }
    for (let i = 0; i < 100 && (await E('X.file_seen(0) === -1')); i++) await sleep(20);
    return E('[0, 1, 2].map(function (k) { return X.file_seen(k); }).join()');
  };
  await E(`(function () { var d = new DataTransfer(), b = new Uint8Array(50); for (var i = 0; i < 50; i++) b[i] = i + 1;
    d.items.add(new File([b], 'f.bin')); document.getElementById('file').files = d.files; })()`);
  const ie10 = await fileAsk();
  ok(ie10 === '0,50,1', `IE 10 and 11 (FileReader): cd_file_read gives the bytes: ${ie10}`);
  await E('delete window.FileReader');
  const ie9 = await fileAsk();
  ok(ie9 === '4,0,0', `IE 9 (no FileReader): cd_file_read answers UNSUPPORTED: ${ie9}`);
  // The same page with no Map: the read-only strings are kept in a plain object.
  await b.send('Page.navigate', { url: `http://127.0.0.1:${srv.address().port}/legacy.html?nomap` });
  await sleep(300);
  for (let i = 0; i < 100 && !(await E('!!window.READY')); i++) await sleep(50);
  ok(await E('typeof Map === "undefined" && !!window.READY'), 'the no-Map page booted with no Map');
  await roRun('with no Map');
} finally { await b.close(); srv.close(); }
console.log(fails ? `${fails} FAILED` : 'all ok');
process.exit(fails ? 1 : 0);
