// Drives tests/out/legacy.html with real input through CDP; exit 1 on any failure.
// LEGACY=1 node tools/legacy.mjs runs it as a browser without WebAssembly (see cdp.mjs): the page must then
// take the wasm2js path, on cdom.legacy.js's polyfills and PEP. Without it, the same checks run on wasm.
// LEGACY=ie9 runs them on the third tier (cdom.legacy-ie9.js and the words build: no typed arrays, no crypto),
// LEGACY=ie9-gaps on IE 9's other gaps on the second tier, LEGACY=ie6 on the fourth (cdom.legacy-ie6.js, ES3 on the old
// DOM: no ES5, attachEvent, no textContent, SVG as VML), and LEGACY=below checks that the boot refuses a browser under its floor.
// VML_TIME=1 also times C building 1,000 paths (T2 and T3 in docs/vml-design.md).
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { launch, sleep, LEGACY, LEVEL, TIER, BELOW, GAPS, FETCHED, TIER_FILES, refused, tierFetched } from './cdp.mjs';

const root = path.resolve(path.dirname(new URL(import.meta.url).pathname), '../tests/out');
const types = { '.html': 'text/html', '.wasm': 'application/wasm', '.js': 'text/javascript' };
// slow.png is answered (404) after 1.5 s: the ?nomount page stays interactive that long.
// The network cases (tests/legacy.c, net_ask): what each route answers, by index there.
const NET_TEXT = Buffer.from('hello \u00e9 \u20ac net\n'), NET_MISSING = Buffer.from('no such thing\n');
const NET_BIN = Buffer.alloc(512, 0);
for (let i = 0; i < 512; i++) NET_BIN[i] = i < 256 ? i : 511 - i;
const netHits = [];
const net = (q, r) => {
  const route = q.url.split(/[?#]/)[0];
  netHits.push(`${q.method} ${route}`);
  if (route === '/net/text') { r.writeHead(200, { 'content-type': 'text/plain; charset=utf-8' }); r.end(NET_TEXT); }
  // The label is what lets IE 5 to 9, which can read only responseText, have the bytes (polyfills.js, the network).
  else if (route === '/net/bin') { r.writeHead(200, { 'content-type': 'application/octet-stream; charset=windows-1252' }); r.end(NET_BIN); }
  else if (route === '/net/missing') { r.writeHead(404, { 'content-type': 'text/plain; charset=utf-8' }); r.end(NET_MISSING); }
  else if (route === '/net/echo') {
    const got = [];
    q.on('data', (c) => got.push(c));
    q.on('end', () => { r.writeHead(200, { 'content-type': 'text/plain; charset=utf-8' });
      r.end(Buffer.concat([Buffer.from(`${q.method} ${q.headers['x-cdom']}|`), ...got])); });
  } else if (route === '/net/drop') q.socket.destroy();
  else if (route !== '/net/hang') { r.writeHead(404); r.end(); } // /net/hang is never answered
};
const srv = http.createServer((q, r) => { if (q.url.startsWith('/net/')) return net(q, r);
  if (q.url.startsWith('/slow.png')) { setTimeout(() => { r.writeHead(404); r.end(); }, 1500); return; }
  const f = path.join(root, q.url.split(/[?#]/)[0]);
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
// A rect's edges, not its width and height, which IE 6 to 8 (LEGACY=ie6) leave out.
const centre = async (sel) => E(`(function () { var r = document.querySelector(${JSON.stringify(sel)}).getBoundingClientRect();
  return [Math.round((r.left + r.right) / 2), Math.round((r.top + r.bottom) / 2)]; })()`);
// A node's text through innerText, every IE's: LEGACY=ie6 has no textContent.
const textOf = (id) => E(`document.getElementById(${JSON.stringify(id)}).innerText`);
async function click(sel) {
  const [x, y] = await centre(sel);
  await mouse('mouseMoved', x, y); await mouse('mousePressed', x, y, 1, 'left'); await mouse('mouseReleased', x, y, 0, 'left');
  await sleep(50);
}
const since = async (n) => (await E('LOG')).slice(n);
// Where cd_seed came from: crypto.getRandomValues for the one 32-bit word seed() asks of it answers SEED_STUB (the
// draws the page asks for bytes are the browser's own), and every Math.random draw is counted.
const SEED_STUB = 0x5eed1e55;
await b.send('Page.addScriptToEvaluateOnNewDocument', { source: `(() => {
  const draws = window.__seedDraws = { crypto: 0, random: 0 };
  const C = window.Crypto && Crypto.prototype, get = C && C.getRandomValues;
  if (get) C.getRandomValues = function (a) {
    if (a && a.length === 1 && a.BYTES_PER_ELEMENT === 4) { draws.crypto++; a[0] = ${SEED_STUB}; return a; }
    return get.call(this, a);
  };
  const random = Math.random;
  Math.random = function () { draws.random++; return random(); };
})();` });
run: try {
  await b.send('Page.navigate', { url: `http://127.0.0.1:${srv.address().port}/legacy.html` });
  // LEGACY=below is below the floor: the boot refuses it, and that refusal is all there is to check.
  if (BELOW) {
    for (const [c, m] of await refused(b, '#m')) ok(c, m);
    ok(!(await E('!!window.READY')), 'never ready');
    const errs = await E('ERRORS');
    ok(errs.length === 1 && /^Error: cdom: this browser cannot run cdom: it has no /.test(errs[0]),
      `the page's error listener heard the boot's error: ${JSON.stringify(errs)}`);
    break run;
  }
  for (let i = 0; i < 100 && !(await E('!!window.READY')); i++) await sleep(50);
  ok(await E('!!window.READY'), 'booted');
  // Inputs typed inside a parent (IE 5 to 8 refuse that once the input has had one): each keeps its type, IE's being
  // text for one it does not know, and the boot threw nothing.
  const types = await E('[(document.getElementById("tcheck") || {}).type, (document.getElementById("tsearch") || {}).type, ERRORS.length]');
  ok(JSON.stringify(types) === JSON.stringify(['checkbox', TIER === 4 ? 'text' : 'search', 0]), `nested inputs typed: ${JSON.stringify(types)} ${JSON.stringify(await E('ERRORS'))}`);
  ok(await E('X.sj() === 1'), "the image module's js way out of a libjpeg error: 500 throws from 20 frames down, the stack pointer back, then a body that returns");
  const id = {}; for (const [k, i] of Object.entries({ PAD: 0, TEXT: 1, BROKEN: 7 })) id[k] = await E(`X.id(${i})`);
  // An event that does not bubble, from inside the mount: the image that does not load, its error heard once (the
  // capture phase, or on the fourth tier the image's own listener).
  for (let i = 0; i < 40 && !(await E('LOG')).some(e => e[1] === id.BROKEN); i++) await sleep(50);
  const broken = (await E('LOG')).filter(e => e[1] === id.BROKEN).map(e => e[0]);
  ok(JSON.stringify(broken) === JSON.stringify([EV.ERROR]), `the broken image's error, which does not bubble, reached C once: ${JSON.stringify(broken)}`);

  // The path: each tier's own files, and no other tier's. The page starts the pair [legacy.wasm, legacy.simd.wasm], and
  // this Chrome has wasm SIMD, so the first tier fetches only the twin; the fallbacks take the scalar module's builds.
  ok(...tierFetched(await E(FETCHED), 'legacy', 'legacy.simd.wasm'));
  if (LEGACY) {
    ok(await E('typeof WebAssembly === "undefined" && typeof X.memory.grow === "function"'), 'no WebAssembly; the exports are wasm2js');
    ok(await E('!/native code/.test(String(TextDecoder)) && !/native code/.test(String(TextEncoder))'), 'TextDecoder and TextEncoder are the polyfills');
    if (TIER === 4) {
      // No PEP, no ChildNode.remove: dom-old.js makes the pointer events from mouse events and removes nodes itself.
      ok(await E('typeof PointerEvent === "undefined" && typeof cdomOld === "object" && !("remove" in Element.prototype)'),
        'no PointerEvent and no ChildNode.remove: the fourth tier is on dom-old.js');
      ok(await E('typeof document.addEventListener === "undefined" && typeof Object.defineProperty === "undefined"'),
        'no addEventListener and no defineProperty, as in IE 6 and 7');
    } else {
      ok(await E('!/native code/.test(String(PointerEvent))'), 'PointerEvent is PEP');
      ok(await E('!/native code/.test(String(Element.prototype.remove)) && !/native code/.test(String(CharacterData.prototype.remove))'),
        'ChildNode.remove is the polyfill, as in every IE');
    }
    if (TIER === 2) ok(await E('__noCrypto && !!window.msCrypto && window.crypto === window.msCrypto'), 'no crypto but msCrypto, as in IE 11; the polyfill made it crypto');
    else {
      ok(await E('typeof Uint8Array === "undefined" && typeof ArrayBuffer === "undefined" && typeof crypto === "undefined" && typeof msCrypto === "undefined"'),
        'no typed arrays and no crypto, as in IE 9');
      ok(await E('Array.isArray(X.memory.words) && X.memory.words.length * 4 === X.memory.grow(0) * 65536 && !("buffer" in X.memory)'),
        'the exports keep C\'s memory in one plain Array of words');
    }
    // The two fallback files are fetched in parallel: the app's starts before the shim's has arrived.
    const [shimFile, appFile] = TIER_FILES('legacy')[TIER];
    const [shim, app] = await E(`${JSON.stringify([shimFile, appFile])}.map(f => performance.getEntriesByType('resource')
      .find(r => r.name.endsWith('/' + f))).map(r => r && [r.startTime, r.responseEnd])`);
    ok(shim && app && app[0] < shim[1], `${appFile} requested before ${shimFile} had arrived: ${JSON.stringify({ shim, app })}`);
  }
  // cd_seed: from crypto.getRandomValues wherever there is one (msCrypto on the second tier), and the weak
  // fallback (shim seed(): Math.random and the clock) only on the third, which has no crypto at all.
  const seed = (await E('X.seed()')) >>> 0, draws = await E('__seedDraws');
  if (TIER >= 3) ok(draws.crypto === 0 && draws.random === 1 && Number.isInteger(seed), `cd_seed from the weak fallback, one Math.random draw, as IE 9 has no crypto: ${seed} ${JSON.stringify(draws)}`);
  else ok(seed === SEED_STUB && draws.crypto === 1 && draws.random === 0, `cd_seed from crypto.getRandomValues, never Math.random, where crypto exists: ${seed.toString(16)} ${JSON.stringify(draws)}`);

  // The memory grows and a new string is read right after it: the views are stale, on wasm and on wasm2js (on
  // words nothing goes stale: the grow pushes onto the same Array).
  const size = 'X.memory.buffer ? X.memory.buffer.byteLength : X.memory.words.length * 4';
  const before = await E(size);
  await E('X.grow()');
  ok(await E(size) > before, 'the memory grew');
  ok(await textOf('echo') === 'read after the memory grew \u00e9', `a string read after the memory grew: ${await textOf('echo')}`);

  // A literal the macro cannot see is its own address: each read from memory once, then kept (a Map, or a plain
  // object where there is none), and the same on a page with no Map.
  const roRun = async (what) => {
    const seen = [];
    for (const i of [0, 1, 0, 1]) { await E(`X.ro(${i})`); seen.push(await textOf('echo')); }
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
  ok(await textOf('echo') === word, `echo of ${word}: ${await textOf('echo')}`);
  // The value is 21 bytes: h é l l o (6), space (7), ж (9), space (10), 日 本 (16), space (17), 😀 (21).
  const cuts = {}; for (const c of [6, 7, 8, 9, 10, 11, 12, 13, 16, 17, 18, 20, 21, 40]) cuts[c] = await E(`X.cut(${c})`);
  ok(JSON.stringify(cuts) === JSON.stringify({ 6: 6, 7: 7, 8: 7, 9: 9, 10: 10, 11: 10, 12: 10, 13: 13, 16: 16, 17: 17, 18: 17, 20: 17, 21: 21, 40: 21 }),
    `a read never splits a character: ${JSON.stringify(cuts)}`);
  const wholes = []; for (const c of [0, 6, 8, 20, 21, 40]) wholes.push(await E(`X.whole(${c})`));
  const W = 1 << 16;
  ok(wholes.join() === [21, 21, 21, 21, W | 21, W | 21].join(), `cd_input_whole says the whole length, cut or not, and whether it fitted: ${wholes.join()}`);

  // A click on an element after a text node and an element: the paragraph's listener hears it with the path down to
  // the <i>, its index among the elements before it (1: the <b>), not among the text nodes too, at depth 1.
  await click('#pathi');
  const cp = await E('X.click_path()');
  ok(cp === (1 << 16 | 1), `a click on the <i> reaches the paragraph with depth 1, path 1: depth ${cp >> 16}, path ${cp & 0xffff}`);

  // A failing call is reported and the batch goes on.
  await E('ERRORS.length = 0');
  await click('#err'); await sleep(50);
  const errs = await E('ERRORS');
  // A TypeError, whose wording differs: Babel calls the missing method through .apply.
  ok(errs.length === 1 && /^TypeError: /.test(errs[0]), `the failed call reported once: ${JSON.stringify(errs)}`);
  ok(await E('document.getElementById("err").getAttribute("data-after")') === 'yes', 'the op after it applied');

  // Every other way the shim reads and writes C memory, which on the third tier is words, not views: a wheel
  // event's two floats, a rect's four doubles, the viewport's four words, a number set on the page through a
  // cd_val's double, and pixels blitted onto a canvas.
  // The fourth tier hears IE's mousewheel: no deltaX, and deltaY from wheelDelta (dom-old.js: 120 a notch is 100).
  await E(TIER === 4 ? 'window.__wheel = null; document.getElementById("pad").attachEvent("onmousewheel", function () { __wheel = [0, -event.wheelDelta * 5 / 6]; })'
    : 'window.__wheel = null; document.getElementById("pad").addEventListener("wheel", function (e) { __wheel = [e.deltaX, e.deltaY]; })');
  await b.send('Input.dispatchMouseEvent', { type: 'mouseWheel', x: 60, y: 60, deltaX: 3.3, deltaY: -125.7 }); await sleep(100);
  const wheel = await E('__wheel'), cw = [await E('X.wheel(0)'), await E('X.wheel(1)')];
  ok(wheel && cw[0] === Math.fround(wheel[0]) && cw[1] === Math.fround(wheel[1]) && cw[1] !== 0, `the wheel's deltas as floats: C ${cw}, page ${wheel}`);
  const box = [0, 1, 2, 3].map(k => E(`X.rect(${k})`)), want = await E('(function (r) { return [r.left, r.top, r.right - r.left, r.bottom - r.top]; })(document.getElementById("pad").getBoundingClientRect())');
  ok(JSON.stringify(await Promise.all(box)) === JSON.stringify(want), `cd_rect's doubles: ${JSON.stringify(await Promise.all(box))} against ${JSON.stringify(want)}`);
  // IE 9 and 10 have no devicePixelRatio (the polyfill reads screen.deviceXDPI, which Chrome has not: 1).
  const vp = [];
  for (let k = 0; k < 4; k++) vp.push(await E(`X.viewport(${k})`));
  // The fourth tier has no innerWidth: the root's client size (dom-old.js), the same here with no scroll bar.
  ok(JSON.stringify(vp) === JSON.stringify(await E(TIER === 4 ? '[document.documentElement.clientWidth, document.documentElement.clientHeight, 0, 100]'
    : '[innerWidth, innerHeight, 0, Math.round(devicePixelRatio * 100)]')) && vp[3] === 100,
    `cd_viewport's width, height, hidden and ratio: ${vp}`);
  if (GAPS && TIER !== 4) ok(await E('!Object.getOwnPropertyDescriptor(window, "devicePixelRatio").get.toString().includes("native code")'), 'devicePixelRatio is the polyfill');
  await E('X.setnum()');
  ok(await E('window.cdomNum') === -1234.5678, `a number through a cd_val: ${await E('window.cdomNum')}`);
  await E('X.blit()');
  const px = await E('Array.prototype.slice.call(document.getElementById("cv").getContext("2d").getImageData(0, 0, 2, 2).data)');
  // the half-transparent pixel comes back premultiplied and unpremultiplied, so its white may round
  ok(JSON.stringify(px.slice(0, 12)) === JSON.stringify([255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255]) && px[15] === 128 && px[12] >= 254,
    `cd_blit's pixels: ${px}`);

  // SVG (docs/vml-design.md): real SVG on every tier but the fourth; there (IE 5 to 8, no SVG) VML, made by
  // tools/legacy/vml.js as the shim builds the subtree, read here from Chrome's VML stand-in (tools/cdp.mjs).
  await E('ERRORS.length = 0');
  await E('X.svg()'); await sleep(100);
  const SVGNS = 'http://www.w3.org/2000/svg';
  const svgIn = await E(`(function () { var n = 0, all = document.getElementById('m').getElementsByTagName('*'), i;
    for (i = 0; i < all.length; i++) if (all[i].namespaceURI === ${JSON.stringify(SVGNS)}) n++; return n; })()`);
  if (TIER === 4) {
    const vmlOf = (id) => E(`(function (el) {
      if (!el) return null;
      var f = el.firstChild, s = f && f.nextSibling, st = el.style;
      return { tag: el.tagName.toLowerCase(), path: el.path, coordsize: el.coordsize, coordorigin: el.coordorigin,
        box: [st.position, st.left, st.top, st.width, st.height], display: st.display, overflow: st.overflow,
        filled: el.filled, fillcolor: el.fillcolor, stroked: el.stroked, strokecolor: el.strokecolor, strokeweight: el.strokeweight,
        fill: f ? [f.tagName.toLowerCase(), f.opacity] : null, stroke: s ? [s.tagName.toLowerCase(), s.opacity] : null,
        join: s ? [s.joinstyle, s.miterlimit, s.endcap] : null };
    })(document.getElementById(${JSON.stringify(id)}))`);
    ok(svgIn === 0, `no element in the SVG namespace: ${svgIn}`);
    // As real IE 6 draws it (docs/vml-design.md, section 9): the namespace with two arguments, a behavior rule a tag, and
    // nothing the stand-in knows IE 6 not to draw (tools/cdp.mjs: a v: node before the namespace, or with no rule, a v:group).
    const setup = await E('__vml.ns.concat(__vml.rules)');
    ok(JSON.stringify(setup) === JSON.stringify([['v', 'urn:schemas-microsoft-com:vml', null],
      ...['shape', 'fill', 'stroke'].map((t) => ['v\\:' + t, 'behavior:url(#default#VML)'])]),
      `the VML namespace added and the behavior rules made, once: ${JSON.stringify(setup)}`);
    const bad = await E('__vml.bad.slice(0)');
    ok(bad.length === 0, `no VML node made that IE 6 would not draw: ${JSON.stringify(bad)}`);
    const root = await vmlOf('svg');
    const rootRect = await E('(function (e) { return [e.offsetWidth, e.offsetHeight]; })(document.getElementById("svg"))');
    ok(root && root.tag === 'span' && root.display === 'inline-block' && root.overflow === 'hidden' &&
      JSON.stringify(root.box.slice(0, 1).concat(root.box.slice(3))) === '["relative","200px","100px"]' && JSON.stringify(rootRect) === '[200,100]',
      `the svg is a span of its own size that clips, 200 by 100: ${JSON.stringify(root)} ${JSON.stringify(rootRect)}`);
    const g = await vmlOf('sg');
    ok(g && g.tag === 'span' && JSON.stringify(g.box) === '["absolute","0px","0px","",""]',
      `the g is a span at the svg's top left: ${JSON.stringify(g)}`);
    // The viewBox 0 0 100 50 in 200 by 100 px is a scale of 2: every point times 2 pixels times K (100).
    const SHAPE = { tag: 'v:shape', coordsize: '100,100', coordorigin: '0,0', box: ['absolute', '0px', '0px', '1px', '1px'], display: '',
      join: ['miter', '4', 'flat'] };
    const op = (f, s) => ({ fill: ['v:fill', f], stroke: ['v:stroke', s] });
    const want = {
      sp: { path: 'm2000,2000 l6000,2000 l6000,4000 x e', filled: true, fillcolor: '#ff0000', stroked: true, strokecolor: 'blue', strokeweight: '4px', ...op('1', '1') },
      // the rounded rect and the circles as arcs: the same strings tools/vml.mjs checks the arcs of
      // x 40 to 60, y 5 to 15, rx and ry 2: 8000 to 12000 by 1000 to 3000, corners of 400 with controls 0.5523 of that out
      sr: { path: 'm8400,1000 l11600,1000 c11821,1000,12000,1179,12000,1400 l12000,2600 c12000,2821,11821,3000,11600,3000 l8400,3000' +
        ' c8179,3000,8000,2821,8000,2600 l8000,1400 c8000,1179,8179,1000,8400,1000 x e', filled: true, fillcolor: '#0080ff', stroked: false, ...op('0.5', '1') },
      sc: { path: 'm15000,2000 c15000,2552,15448,3000,16000,3000 c16552,3000,17000,2552,17000,2000 c17000,1448,16552,1000,16000,1000' +
        ' c15448,1000,15000,1448,15000,2000 x e', filled: false, stroked: true, strokecolor: '#00ff00', strokeweight: '2px', ...op('1', '1') },
      se: { path: 'm400,8000 c400,8442,1116,8800,2000,8800 c2884,8800,3600,8442,3600,8000 c3600,7558,2884,7200,2000,7200' +
        ' c1116,7200,400,7558,400,8000 x e', filled: true, fillcolor: 'black', stroked: false, ...op('1', '1') },
      sl: { path: 'm4000,6000 l8000,9000 e', filled: false, stroked: true, strokecolor: 'black', strokeweight: '2px', ...op('1', '0.25') },
      spl: { path: 'm10000,6000 l12000,9000 l14000,6000 e', filled: false, stroked: true, strokecolor: 'purple', ...op('1', '1') },
      spg: { path: 'm16000,6000 l18000,9000 l15000,9000 x e', filled: true, fillcolor: '#aabbcc', stroked: false, ...op('0.25', '0.5') },
      // inside the g: translate(5,6) scale(2) after the viewBox's 2, so (4 x + 10, 4 y + 12); the g's fill and stroke-width
      sgp: { path: 'm1000,1200 l3000,1200 l3000,3200 e', filled: true, fillcolor: '#123456', stroked: true, strokecolor: 'red', strokeweight: '12px', ...op('1', '1') },
    };
    for (const [id, w] of Object.entries(want)) {
      const got = await vmlOf(id), exp = { ...SHAPE, ...w };
      const bad = got ? Object.keys(exp).filter((k) => JSON.stringify(got[k]) !== JSON.stringify(exp[k])) : ['the node'];
      ok(!bad.length, `<${{ sp: 'path', sr: 'rect', sc: 'circle', se: 'ellipse', sl: 'line', spl: 'polyline', spg: 'polygon', sgp: 'path in a g' }[id]}> as a v:shape: ${
        bad.map((k) => `${k} ${JSON.stringify(got && got[k])}, want ${JSON.stringify(exp[k])}`).join('; ') || got.path}`);
    }
    // What a LaTeX formula is made of. Text: a span at x times 2, in its font size times 2, its baseline (found here by an
    // image on it, not the way vml.js finds it) at y times 2, its colour the CSS color it inherits (currentColor).
    const text = await E(`(function (e, svg) {
      var img = document.createElement('img'), s = e.style, r;
      img.width = 1; img.height = 1; img.style.verticalAlign = 'baseline'; e.appendChild(img);
      r = img.getBoundingClientRect().bottom - svg.getBoundingClientRect().top; e.removeChild(img);
      return [e.tagName.toLowerCase(), s.position, s.left, s.fontSize, s.fontStyle, s.fontFamily, s.color, getComputedStyle(e).color, Math.round(r), e.innerText];
    })(document.getElementById('st'), document.getElementById('svg'))`);
    ok(JSON.stringify(text) === JSON.stringify(['span', 'absolute', '2px', '20px', 'italic', 'KaTeX_Math, serif', '', 'rgb(18, 49, 35)', 90, 'x']),
      `<text> as a span in its font, its baseline at y: ${JSON.stringify(text)}`);
    ok((await vmlOf('scc')).fillcolor === '#123123', `a currentColor fill is the CSS color: ${(await vmlOf('scc')).fillcolor}`);
    // The svg in the svg: a span at x and y, its width and height, clipping; its viewBox sliced to fill it (scale 1 inside).
    const sn = await vmlOf('sn'), snr = await vmlOf('snr');
    ok(sn && sn.tag === 'span' && sn.overflow === 'hidden' && JSON.stringify(sn.box) === '["absolute","100px","40px","40px","20px"]',
      `the svg in the svg is a span at its x and y that clips: ${JSON.stringify(sn)}`);
    ok(snr.path === 'm0,0 l8000,0 l8000,2000 l0,2000 x e' && snr.fillcolor === '#ff00ff', `its rect drawn from its corner, sliced: ${snr.path}`);
    // The svg sized in em: as CSS lays it out, 40 by 20, so a viewBox of 20 by 10 is a scale of 2.
    const em = await E('(function (e) { return [e.offsetWidth, e.offsetHeight, e.style.width, e.style.height]; })(document.getElementById("svgem"))');
    ok(JSON.stringify(em) === '[40,20,"2em","1em"]', `the svg in em is laid out by CSS: ${JSON.stringify(em)}`);
    ok((await vmlOf('sem')).path === 'm200,200 l600,200 l600,800 l200,800 x e', `and drawn at its scale: ${(await vmlOf('sem')).path}`);
    // What VML does not draw: hidden, and each kind reported once through window.reportError.
    const hid = await E(`['lg', 'srot'].map(function (id) { var e = document.getElementById(id); return e ? e.tagName.toLowerCase() + ' ' + e.style.display : 'none'; })`);
    ok(JSON.stringify(hid) === '["span none","v:shape none"]', `the gradient is a hidden span, the rotated rect is hidden: ${JSON.stringify(hid)}`);
    ok((await vmlOf('sgrad')).filled === false, 'a gradient paint fills nothing');
    const kinds = ['<defs>', '<linearGradient>', '<stop>', 'transform rotate', 'paint url'];
    const lost = await E('cdomVml.lost.slice(0)'), errs = await E('ERRORS');
    ok(JSON.stringify(lost) === JSON.stringify(kinds), `cdomVml.lost names each kind once: ${JSON.stringify(lost)}`);
    ok(errs.length === kinds.length && kinds.every((k, i) => errs[i] === `Error: cdom: SVG ${k} is not drawn in VML (IE 5 to 8: docs/vml-design.md)`),
      `each reported once through window.onerror: ${JSON.stringify(errs)}`);
    // Later changes (cd_set_attr) reach the VML: a path's d, a rect's fill; and the g's transform, set while the path in it is
    // detached, then the path attached again (the shim's place, which on this tier draws what it put back).
    await E('X.svg_set()');
    const [sp2, sr2, sgp2] = [await vmlOf('sp'), await vmlOf('sr'), await vmlOf('sgp')];
    ok(sp2.path === 'm0,0 l2000,0 e', `cd_set_attr of d reached the v:shape: ${sp2.path}`);
    ok(sr2.fillcolor === '#0000ff' && sr2.fill[1] === '0.5', `cd_set_attr of fill reached it, its fill-opacity kept: ${sr2.fillcolor} ${sr2.fill}`);
    ok(sgp2.path === 'm0,0 l1000,0 l1000,1000 e' && sgp2.strokeweight === '6px', `the g's transform changed while its path was detached: put back, the path is drawn under it: ${sgp2.path} ${sgp2.strokeweight}`);
    // Removed, then built again: nothing left, then the same VML, and nothing reported twice.
    await E('X.svg_drop()');
    const left = await E(`(function () { var all = document.getElementById('m').getElementsByTagName('*'), n = 0, i;
      for (i = 0; i < all.length; i++) if (/^v:/i.test(all[i].tagName)) n++; return n; })()`);
    ok(left === 0 && (await E('document.getElementById("svg") === null')), `cd_remove took every VML node: ${left} left`);
    await E('X.svg()'); await sleep(100);
    ok((await vmlOf('sp')).path === 'm2000,2000 l6000,2000 l6000,4000 x e' && (await E('ERRORS.length')) === kinds.length,
      `built again, the path is drawn again and nothing is reported twice: ${(await vmlOf('sp')).path}, ${await E('ERRORS.length')} errors`);
  } else {
    ok(svgIn === 21, `the svg is real SVG, all 21 of its elements in the SVG namespace: ${svgIn}`);
    ok(await E('document.getElementById("sp").getAttribute("d")') === 'M10 10 h20 v10 z', 'the path has its d');
    await E('X.svg_set()');
    ok(await E('document.getElementById("sp").getAttribute("d") + " " + document.getElementById("sr").getAttribute("fill")') === 'M0 0 L10 0 #00f',
      'cd_set_attr reached the SVG');
    ok(await E('typeof cdomVml === "undefined" && !window.__vml && ERRORS.length === 0'), 'no VML, nothing reported');
    await E('X.svg_drop()');
    ok(await E('document.getElementById("svg") === null'), 'cd_remove took the svg');
    await E('X.svg()');
  }
  await E('X.svg_drop()');
  // T2 and T3: C building 1,000 paths (each d, fill and stroke) in an svg of their own (the one before removed), 20 builds
  // a run (the clock the fourth tier has is Date's, to the ms), the median of 5 runs after one warm-up.
  if (process.env.VML_TIME) {
    const runs = [];
    for (let r = 0; r < 6; r++) runs.push(await E('(function () { var t = new Date().getTime(), i; for (i = 0; i < 20; i++) X.svg_many(1000); return (new Date().getTime() - t) / 20; })()'));
    const ms = runs.slice(1).sort((a, b) => a - b)[2];
    console.log(`T2/T3 (LEGACY=${LEVEL || 'none'}): 1,000 paths in ${ms} ms, ${(1000 / ms).toFixed(1)} paths per ms (median of 5 runs of 20 builds after one warm-up: ${runs.join(', ')})`);
    if (TIER === 4) ok(await E('__vml.made.length') >= 3000, 'and each path made a v:shape, a v:fill and a v:stroke');
    await E('X.svg_many(0)');
  }

  // The polyfills against the built-ins: the app cannot tell some of them apart from a wrong one (a wrong
  // imul still gives a consistent intern hash, a short fill leaves bytes that were zero anyway), so each is
  // checked on its own, the same expressions in the page and in node.
  if (LEGACY) {
    const U = (f) => `(function () { var a = new Uint8Array([1, 2, 3, 4, 5, 6, 7, 8]); a.${f}; return Array.prototype.slice.call(a); })()`;
    // Without typed arrays (the third tier) there is no fround polyfill (the app rounds in software, words.js)
    // and no typed array to give copyWithin and fill.
    const typed = TIER === 2;
    const exprs = [
      ...[[3, 4], [-5, 12], [0xffffffff, 5], [0x7fffffff, 0x7fffffff], [0x12345678, 0x9abcdef0], [65536, 65536], [-1, -1]]
        .map(([a, b]) => `Math.imul(${a}, ${b})`),
      // MDN's clz32 goes through Math.log, so every power of two and its neighbours, where rounding would show.
      ...[0, -1, 0.5, 4294967297, ...Array.from({ length: 32 }, (_, k) => [2 ** k - 1, 2 ** k, 2 ** k + 1]).flat()]
        .map(v => `Math.clz32(${v})`),
      ...(typed ? ['0', '1.1', '-1.1', '1e39', '5e-46', 'NaN', '16777217', '-0'].map(v => `Math.fround(${v})`) : []),
      ...['1.7', '-1.7', '0.2', '-0.2', '1e21', '-0', 'NaN', 'Infinity'].map(v => `Math.trunc(${v})`),
      ...(typed ? [U('copyWithin(2, 0, 5)'), U('copyWithin(0, 3, 8)'), U('copyWithin(5, 1)'), U('copyWithin(-2, 0)'), U('copyWithin(1, -3, -1)'),
        U('fill(7, 2, 5)'), U('fill(9)'), U('fill(0, 6)'), U('fill(3, -2)'), U('fill(4, 1, 99)')] : []),
      'Object.entries({ a: 1, b: "x", c: [2] })', 'Object.entries({})', 'Object.entries([7, 8])',
      '"--x".startsWith("--")', '"a--".startsWith("--", 1)', '"x--".startsWith("--")', '"pan-y".includes("pan-")', '"abc".includes("d")', '"abcabc".includes("a", 4)',
      // The fourth tier's ES5 (polyfills-es3.js), sparse arrays and the DontEnum case included.
      ...(TIER === 4 ? ['[3, 1, 2, 1].indexOf(1)', '[3, 1, 2, 1].indexOf(1, 2)', '[3, 1, 2, 1].indexOf(1, -1)', '[3, 1, 2, 1].indexOf(9)',
        '[3, 1, 2, 1].lastIndexOf(1)', '[3, 1, 2, 1].lastIndexOf(1, 2)', '[3, 1, 2, 1].lastIndexOf(1, -2)', '[, 1].indexOf(undefined)',
        '[1, 2, 3].map(function (v, i) { return v * 10 + i; })', '[1, , 3].map(function (v) { return v; }).length',
        '[1, 2, 3, 4].filter(function (v) { return v % 2; })', '[1, 2].some(function (v) { return v > 1; })', '[].some(function () { return true; })',
        '[1, 2].every(function (v) { return v > 1; })', '[].every(function () { return false; })',
        '[1, 2, 3].reduce(function (a, v) { return a + v; })', '[1, 2, 3].reduce(function (a, v) { return a + v; }, 10)', '[, 5].reduce(function (a, v) { return a + v; })',
        '(function () { var n = 0; [1, , 3].forEach(function (v) { n += v; }); return n; })()',
        'Object.keys({ a: 1, b: 2 })', 'Object.keys([7, 8])', 'Array.isArray([])', 'Array.isArray({ length: 0 })', '" x\\t".trim()',
        '(function (a, b) { return [this.v, a, b]; }).bind({ v: 7 }, 1)(2)', 'typeof Date.now()',
        '(function () { var a = [1, 2, 3, 4]; return [a.splice(1), a]; })()', 'Object.create({ p: 3 }).p'] : []),
    ];
    const show = (v) => Object.is(v, -0) ? '-0' : JSON.stringify(v) ?? String(v);
    const page = await E(`[${exprs.join(', ')}].map(function (v) { return 1 / v === -Infinity ? '-0' : JSON.stringify(v) || String(v); })`);
    const bad = exprs.filter((x, i) => page[i] !== show(new Function('return ' + x)()));
    ok(!bad.length, `polyfills agree with the built-ins on ${exprs.length} cases: ${bad.map(x => `${x} gave ${page[exprs.indexOf(x)]}`).join('; ')}`);
    ok(await E('globalThis === window'), 'globalThis is the window');
    // holds() takes the binary in where the browser has one: its for-in fallback, JScript 5.0's alone, is a pass over the
    // array per slot, which over 10,000 slots holding undefined takes seconds.
    if (TIER === 4) {
      const [calls, ms] = await E('(function () { var a = [], n = 0, i, t; for (i = 0; i < 10000; i++) a[i] = undefined; t = Date.now();' +
        ' a.forEach(function () { n++; }); return [n, Date.now() - t]; })()');
      ok(calls === 10000 && ms < 500, `forEach over 10,000 undefined slots: ${calls} calls in ${ms} ms (under 500: holds takes the in operator)`);
    }
    ok(await E(`!/native code/.test(String(Object.entries)) && !/native code/.test(String(Math.imul) + String(Math.clz32) + String(Math.trunc)${typed
      ? ' + String(Math.fround) + String(Uint8Array.prototype.fill) + String(Uint8Array.prototype.copyWithin)' : ''}${TIER === 4
      ? ' && !/native code/.test(String(Array.prototype.indexOf) + String(Array.prototype.map) + String(Object.keys) + String(Function.prototype.bind) + String(Array.isArray))' : ''})`), 'those were the polyfills');
    // TextEncoder.encode with no Uint8Array to make: a plain Array of the bytes.
    if (!typed) ok(await E('JSON.stringify(new TextEncoder().encode("h\u00e9\u65e5\ud83d\ude00"))') === JSON.stringify([...Buffer.from('h\u00e9\u65e5\u{1F600}')]),
      `TextEncoder.encode without typed arrays: ${await E('JSON.stringify(new TextEncoder().encode("h\u00e9\u65e5\ud83d\ude00"))')}`);
  }

  // IE 9's gaps above its walls (LEGACY=ie9-gaps): no MutationObserver and no requestAnimationFrame, prefixed
  // or not, so queueMicrotask and requestAnimationFrame are the polyfills' timeout forms. The focus check
  // above ran on that queueMicrotask; requestAnimationFrame must call back once with a time.
  if (GAPS) {
    ok(await E('typeof MutationObserver === "undefined" && typeof WebKitMutationObserver === "undefined" && !/native code/.test(String(queueMicrotask))'),
      'queueMicrotask is the polyfill, on a timeout');
    await E('window.__raf = []; requestAnimationFrame(function (t) { __raf.push(typeof t, t > 0); })');
    for (let i = 0; i < 20 && !(await E('__raf.length')); i++) await sleep(20);
    await sleep(100);
    ok(!/native code/.test(await E('String(requestAnimationFrame)')) && JSON.stringify(await E('__raf')) === '["number",true]',
      `requestAnimationFrame is the polyfill and called back once with a time: ${JSON.stringify(await E('__raf'))}`);
    // performance.now, IE 10's: the polyfill counts from the navigation's start, as the real one does.
    const [p0, real, p1] = await E('(function () { var a = performance.now(); var t = Date.now() - performance.timing.navigationStart; var b = performance.now(); return [a, t, b]; })()');
    ok(!/native code/.test(await E('String(performance.now)')) && p0 > 0 && p1 >= p0 && Math.abs(real - p0) < 50,
      `performance.now is the polyfill, on the navigation's clock: ${p0}, ${real}, ${p1}`);
  }

  // Without options objects, every listener the shim adds must get a plain capture flag: bubbling types
  // false, the non-bubbling ones (focus) true. An object would mean capture for all of them.
  if (TIER === 4) {
    // No addEventListener: every listener went through attachEvent, focus as the focusin that bubbles in IE, the
    // pointer events as mouse events, and the field's input as its propertychange.
    const on = await E('__attached.map(function (a) { return a[1]; })');
    for (const t of ['onfocusin', 'onmousedown', 'onmousemove', 'onmouseup', 'onclick', 'onmousewheel', 'onpropertychange'])
      ok(on.includes(t), `attachEvent("${t}"): ${on.join(' ')}`);
    ok(!on.some(t => /^on(focus|pointer|wheel|input)/.test(t) && t !== 'onfocusin'), `no type IE lacks: ${on.join(' ')}`);
  } else if (LEGACY) {
    const adds = (await E('__adds')).filter(a => a[0] !== 'cdom-probe');
    const objs = adds.filter(a => a[1] === 'object');
    ok(!objs.length, `no options object reached addEventListener: ${JSON.stringify(objs)}`);
    const phase = (t) => adds.filter(a => a[0] === t).map(a => a[2]);
    ok(phase('click').includes(false) && !phase('click').includes(true), `click in the bubble phase: ${phase('click')}`);
    ok(phase('focus').includes(true), `focus in the capture phase: ${phase('focus')}`);
  }
  // Picked files on the old tiers: a file set on the input (a DataTransfer, as a drop gives), then read as IE 10
  // and 11 have it (FileReader): the bytes; then as IE 9 has it (no FileReader): UNSUPPORTED (4). A call that
  // throws is a failure of its own case, not the end of the run. Under LEGACY=ie9 and ie9-gaps the page is IE 9
  // already (no File, no FileReader: tools/cdp.mjs), so only the second read is made, with no file set.
  const fileAsk = async () => {
    try { await E('X.file_ask()'); } catch (e) { return 'threw ' + String(e).slice(0, 120); }
    for (let i = 0; i < 100 && (await E('X.file_seen(0) === -1')); i++) await sleep(20);
    return E('[0, 1, 2].map(function (k) { return X.file_seen(k); }).join()');
  };
  if (!GAPS) {
    await E(`(function () { var d = new DataTransfer(), b = new Uint8Array(50); for (var i = 0; i < 50; i++) b[i] = i + 1;
      d.items.add(new File([b], 'f.bin')); document.getElementById('file').files = d.files; })()`);
    const ie10 = await fileAsk();
    ok(ie10 === '0,50,1', `IE 10 and 11 (FileReader): cd_file_read gives the bytes: ${ie10}`);
    await E('delete window.FileReader');
  } else ok(await E('typeof FileReader === "undefined"'), 'no FileReader, as in IE 9');
  const ie9 = await fileAsk();
  ok(ie9 === '4,0,0', `IE 9 (no FileReader): cd_file_read answers UNSUPPORTED: ${ie9}`);
  // A job (cd_job_cb) on the old tiers has no WebAssembly to run: UNSUPPORTED (3), from the event loop, whatever else the page has.
  await E('delete window.WebAssembly'); // already gone under LEGACY=1; here the page started on it, and an old tier has none
  let jobSync = 'threw';
  try { jobSync = await E('X.job_ask(); X.job_seen(0)'); } catch (e) { jobSync = 'threw ' + String(e).slice(0, 120); }
  for (let i = 0; i < 100 && (await E('X.job_seen(0) === -1')); i++) await sleep(20);
  const jobSeen = await E('[0, 1].map(function (k) { return X.job_seen(k); }).join()');
  ok(jobSync === -1 && jobSeen === '3,0', `the old tiers (no WebAssembly): cd_job_cb answers UNSUPPORTED from the event loop: ${jobSync} then ${jobSeen}`);
  // The network on every tier (cdom_net.h): a text GET, a binary GET, a 404, a POST, a request that never arrived and a
  // deadline, each answered once and as the server sent it. IE 10 and 11 have no fetch (cdp.mjs deletes it on every legacy
  // level), so the polyfill's XMLHttpRequest answers there; IE 9 has no responseType, and IE 5 and 6 no XMLHttpRequest, only
  // ActiveX's (LEGACY=ie6). The same cases again on the pages ?mime (a browser with overrideMimeType, Firefox 1.0 on: tier 3) and
  // ?xhr (IE 7 and 8, which have XMLHttpRequest: tier 4), also with ?mime.
  async function netRun(label) {
    netHits.length = 0;
    const errors = (await E('ERRORS')).length; // the page's earlier cases throw on purpose
    await E('(function () { for (var k = 0; k < 7; k++) X.net_ask(k); })()');
    for (let i = 0; i < 200 && !(await E('(function () { for (var k = 0; k < 7; k++) if (X.net_seen(k, 3) !== 1) return false; return true; })()')); i++) await sleep(50);
    await sleep(300); // an answer that comes twice has come by now
    const seen = await E('(function () { var o = []; for (var k = 0; k < 7; k++) o.push([X.net_seen(k, 0), X.net_seen(k, 1), X.net_seen(k, 2), X.net_seen(k, 3)].join()); return o; })()');
    const want = [`200,${NET_TEXT.length},1,1`, `200,512,1,1`, `404,${NET_MISSING.length},1,1`, `200,${Buffer.byteLength('POST yes|ping \u00e9')},1,1`, '0,0,1,1', '1,0,1,1',
      // not UTF-8: sent as it is where typed arrays are, and refused where only text can be sent (IE 9 and below)
      TIER >= 3 ? '0,0,0,1' : '200,11,1,1'];
    ok(seen.join(' ') === want.join(' '), `${label}: network: a text GET, a binary GET (all 256 bytes), a 404 and its body, a POST with a header, a dropped request (0), a deadline (1), a body that is not UTF-8, each answered once: ${seen.join(' ')}`);
    ok(new Set(netHits).size === 6 && (await E('ERRORS')).length === errors, `${label}: the server saw all six routes (a browser may retry a dropped GET), and the page threw nothing: ${netHits.join(', ')} ${JSON.stringify((await E('ERRORS')).slice(errors))}`);
  }
  await netRun('on this page');
  if (TIER >= 2) {
    const how = await E('[typeof ArrayBuffer, typeof XMLHttpRequest, window.__activeX ? __activeX.length : -1, typeof fetch].join()');
    // Where each tier's request went: typed arrays (IE 10, 11) the ArrayBuffer way; the third tier no typed arrays; the fourth
    // ActiveX alone, one request object each (7).
    ok(TIER === 4 ? how === 'undefined,undefined,7,function' : TIER === 3 ? how.startsWith('undefined,function,-1,') : how.startsWith('function,function,-1,'),
      `the polyfilled fetch went through ${TIER === 4 ? 'ActiveXObject' : 'XMLHttpRequest'}: ${how}`);
  }
  for (const q of TIER === 3 ? ['mime'] : TIER === 4 ? ['xhr', 'xhr&mime'] : []) {
    await b.send('Page.navigate', { url: `http://127.0.0.1:${srv.address().port}/legacy.html?${q}` });
    await sleep(300);
    for (let i = 0; i < 100 && !(await E(`location.search === "?${q}" && !!window.READY`)); i++) await sleep(50);
    ok(await E('!!window.READY'), `the ?${q} page booted`);
    if (/xhr/.test(q)) ok(await E('typeof XMLHttpRequest !== "undefined" && !window.__activeX.length'), `?${q}: XMLHttpRequest is there (IE 7 and 8)`);
    if (/mime/.test(q)) ok(await E('!!window.__mime'), `?${q}: overrideMimeType is kept`);
    await netRun(`?${q}`);
    if (/xhr/.test(q)) ok(await E('window.__activeX.length') === 0, `?${q}: no ActiveX made`);
  }
  // The same page with no Map: the read-only strings are kept in a plain object.
  await b.send('Page.navigate', { url: `http://127.0.0.1:${srv.address().port}/legacy.html?nomap` });
  await sleep(300);
  for (let i = 0; i < 100 && !(await E('!!window.READY')); i++) await sleep(50);
  ok(await E('typeof Map === "undefined" && !!window.READY'), 'the no-Map page booted with no Map');
  await roRun('with no Map');
  // A start that fails reaches window.onerror, on every tier: the boot throws its rejection again. The waits ask for
  // window.ERRORS on the page navigated to: right after a navigation the evaluate may still reach the page before (whose
  // ERRORS may hold its own error), or the new one before its scripts have run (where ERRORS alone would throw).
  await b.send('Page.navigate', { url: `http://127.0.0.1:${srv.address().port}/legacy.html?nolits` });
  for (let i = 0; i < 100 && !(await E('location.search === "?nolits" && window.ERRORS && ERRORS.length')); i++) await sleep(50);
  const startErrs = await E('ERRORS');
  ok(!(await E('!!window.READY')) && startErrs.length === 1 && /no string table/.test(startErrs[0]), `a failed start is thrown again: ${JSON.stringify(startErrs)}`);
  // A mount id no element has, found while the page is still interactive: an error once the page has loaded, not a wait.
  await b.send('Page.navigate', { url: `http://127.0.0.1:${srv.address().port}/legacy.html?nomount` });
  for (let i = 0; i < 100 && !(await E('location.search === "?nomount" && window.ERRORS && ERRORS.length')); i++) await sleep(50);
  const mountErrs = await E('ERRORS');
  ok(mountErrs.length === 1 && /no element has the id no-such-mount/.test(mountErrs[0]), `a missing mount is an error once the page has loaded: ${JSON.stringify(mountErrs)}`);
} finally { await b.close(); srv.close(); }
console.log(fails ? `${fails} FAILED` : 'all ok');
process.exit(fails ? 1 : 0);
