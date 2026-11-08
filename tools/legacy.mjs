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
const EV = { CLICK: 0, INPUT: 2, POINTERDOWN: 9, POINTERUP: 10, POINTERMOVE: 11, FOCUS: 19 };
const b = await launch();
let fails = 0;
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
  const id = {}; for (const [k, i] of Object.entries({ PAD: 0, TEXT: 1 })) id[k] = await E(`X.id(${i})`);

  // The path: the fallback files only without WebAssembly, the wasm only with it.
  const got = await E(FETCHED), mine = ['legacy.wasm.js', 'cdom.legacy.js'];
  ok(LEGACY ? mine.every(f => got.includes(f)) && !got.includes('legacy.wasm') : !got.some(f => mine.includes(f)) && got.includes('legacy.wasm'),
    `${LEGACY ? 'wasm2js' : 'wasm'} path only: ${got.filter(f => /\.(js|wasm)$/.test(f)).join(' ')}`);
  if (LEGACY) {
    ok(await E('typeof WebAssembly === "undefined" && typeof X.memory.grow === "function"'), 'no WebAssembly; the exports are wasm2js');
    ok(await E('!/native code/.test(String(TextDecoder)) && !/native code/.test(String(TextEncoder))'), 'TextDecoder and TextEncoder are the polyfills');
    ok(await E('!/native code/.test(String(PointerEvent))'), 'PointerEvent is PEP');
  }

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
  const word = 'héllo 日本 \u{1F600}';
  await b.send('Input.insertText', { text: word }); await sleep(50);
  ok(await E('document.getElementById("echo").textContent') === word, `echo of ${word}: ${await E('document.getElementById("echo").textContent')}`);
  // The value is 18 bytes: h é l l o (6), space (7), 日 本 (13), space (14), 😀 (18).
  const cuts = {}; for (const c of [6, 7, 8, 9, 10, 13, 14, 15, 17, 18, 40]) cuts[c] = await E(`X.cut(${c})`);
  ok(JSON.stringify(cuts) === JSON.stringify({ 6: 6, 7: 7, 8: 7, 9: 7, 10: 10, 13: 13, 14: 14, 15: 14, 17: 14, 18: 18, 40: 18 }),
    `a read never splits a character: ${JSON.stringify(cuts)}`);

  // A failing call is reported and the batch goes on.
  await E('ERRORS.length = 0');
  await click('#err'); await sleep(50);
  const errs = await E('ERRORS');
  // A TypeError, whose wording differs: Babel calls the missing method through .apply.
  ok(errs.length === 1 && /^TypeError: /.test(errs[0]), `the failed call reported once: ${JSON.stringify(errs)}`);
  ok(await E('document.getElementById("err").getAttribute("data-after")') === 'yes', 'the op after it applied');

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
} finally { await b.close(); srv.close(); }
console.log(fails ? `${fails} FAILED` : 'all ok');
process.exit(fails ? 1 : 0);
