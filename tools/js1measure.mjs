// The measurements of docs/js1-interp-design.md, section 9, in headless Chrome (indicative only: the repo manager
// measures the real browsers). LEGACY=frames node tools/js1measure.mjs, after tests/build.sh; prints one line a
// measurement and asserts nothing. Its hooks go through Reflect: the frames profile takes Object.defineProperty and
// Function.prototype.apply away. Timings are the median of several runs, from Event.timeStamp (performance.now is gone
// under LEGACY=frames, as on IE 9).
//   P1 sizes, gzip -9      P2 the module as a digit string against statements: bytes and load time
//   P3 tests/frames.c: boot (the factory, cd_start and the first draw) and a click, interpreter against the IE 9 build
//   P4 tests/js1cpu.c: loop(1e5) and rec(1000), interpreter against the IE 9 build, and instructions a second
//   P5 the deepest recursion on each      P6 interpreter instructions against wasm instructions (from the profile)
//   P7 the dispatch shapes on loop(1e5)   P8 binaryen's i64 lowering, in instructions
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import zlib from 'node:zlib';
import { execFileSync } from 'node:child_process';
import { launch, sleep, TIER } from './cdp.mjs';
import * as L from './legacy/js1lower.mjs';

if (TIER !== 5) { console.log('FAIL tools/js1measure.mjs runs under LEGACY=frames'); process.exit(1); }
const root = path.resolve(path.dirname(new URL(import.meta.url).pathname), '../tests/out');
const gz = (f) => zlib.gzipSync(fs.readFileSync(path.join(root, f)), { level: 9 }).length;
const size = (f) => fs.statSync(path.join(root, f)).size;
const median = (a) => { const s = a.slice().sort((x, y) => x - y); return s[s.length >> 1]; };
const r2 = (x) => Math.round(x * 1000) / 1000;
const extra = new Map();
const srv = http.createServer((q, r) => {
  const u = q.url.split(/[?#]/)[0];
  if (extra.has(u)) { r.writeHead(200, { 'content-type': u.endsWith('.html') ? 'text/html' : 'text/javascript' }); r.end(extra.get(u)); return; }
  const f = path.join(root, u);
  fs.readFile(f, (e, b) => { if (e) { r.writeHead(404); r.end(); return; } r.writeHead(200, { 'content-type': f.endsWith('.html') ? 'text/html' : 'text/javascript' }); r.end(b); });
}).listen(0);
const port = srv.address().port, base = `http://127.0.0.1:${port}`;
extra.set('/blank.html', '<!doctype html><title>blank</title>');
// The boot's clock: the factory's start (the app file's assignment of window.cdomFactory, wrapped by a setter) and the end
// of the first draw (the shim's cf_draw, wrapped when the factory is called, by when cdom.frames.js has run).
const HOOK = `(function () {
  var now = function () { return new Event('x').timeStamp; };
  window.__now = now;
  if (window !== window.top) return;
  window.__m = {};
  var f = null;
  Reflect.defineProperty(window, 'cdomFactory', { configurable: true, get: function () { return f; },
    set: function (v) { f = v && function (imp) {
      var d = window.cf_draw;
      window.cf_draw = function () { var r = Reflect.apply(d, this, arguments); if (!__m.d) __m.d = now(); return r; };
      __m.f0 = now(); var r = v(imp); __m.f1 = now(); return r; }; } });
})();`;;
const b = await launch();
await b.send('Page.addScriptToEvaluateOnNewDocument', { source: HOOK });
const E = b.evaluate;
async function go(url) {
  await b.send('Page.navigate', { url });
  for (let i = 0; i < 300 && !(await E('document.readyState === "complete"').catch(() => false)); i++) await sleep(10);
}
// A page of inline scripts, each a file of tests/out or a text, served at /name.html (the frames profile has no
// XMLHttpRequest, and inline scripts keep the network out of the timings).
const text = (x) => (x.startsWith('@') ? fs.readFileSync(path.join(root, x.slice(1)), 'utf8') : x);
function inline(name, scripts) {
  const body = scripts.map((x) => { const t = text(x); if (/<\/script/i.test(t)) throw new Error(`${x} has </script`); return `<script>\n${t}\n</script>`; }).join('\n');
  extra.set(`/${name}.html`, `<!doctype html><title>${name}</title>\n${body}`);
  return `${base}/${name}.html`;
}

// P1.
const ie9 = 'frames.wasm.ie9.js';
console.log(`P1 cdom.js1.js ${size('cdom.js1.js')} bytes, ${gz('cdom.js1.js')} gzipped; frames.wasm.js1.js ${size('frames.wasm.js1.js')} bytes, ${gz('frames.wasm.js1.js')} gzipped;`
  + ` together ${gz('cdom.js1.js') + gz('frames.wasm.js1.js')} gzipped against ${ie9} ${size(ie9)} bytes, ${gz(ie9)} gzipped (${r2((gz('cdom.js1.js') + gz('frames.wasm.js1.js')) / gz(ie9))} times);`
  + ` cdom.frames.js ${size('cdom.frames.js')} bytes, ${gz('cdom.frames.js')} gzipped`);

// P2: frames.wasm's module in both forms, each loaded (parsed, run, decoded) into a fresh page after the interpreter.
{
  const m = L.parseWasm(fs.readFileSync(path.join(root, 'frames.wasm'))), low = L.lower(m), order = L.opOrder();
  const forms = {};
  for (const form of ['digits', 'statements']) {
    const src = L.moduleFile(m, low, order, `frames.wasm.js1.${form}.js`, form);
    const url = inline(`p2-${form}`, ['@cdom.js1.js', 'var t0 = __now();', src, 'var o = new Object(); o.cdom = new Object(); cdomFactory(o); var t1 = __now();']);
    const times = [];
    for (let i = 0; i < 9; i++) {
      await go(url);
      times.push(await E('t1 - t0'));
    }
    forms[form] = { bytes: src.length, gzipped: zlib.gzipSync(src, { level: 9 }).length, ms: r2(median(times)) };
  }
  console.log(`P2 digits ${JSON.stringify(forms.digits)}, statements ${JSON.stringify(forms.statements)}: the digit string is ${r2(forms.digits.bytes / forms.statements.bytes)} of the bytes and takes ${r2(forms.digits.ms / forms.statements.ms)} times as long`);
}

// P3: tests/frames.c's boot and a click, on the interpreter (frames.html) and the IE 9 build (frames-ie9.html).
{
  const res = {};
  // cdomF.k(0) is the button's onclick ("parent.cdomF.k(0)"): the click's whole handling, its call into C and its draw.
  for (const [name, page] of [['interpreter', 'frames.html'], ['ie9', 'frames-ie9.html']]) {
    const boot = [], fac = [], click = [], inC = [];
    for (let i = 0; i < 7; i++) {
      await go(`${base}/${page}`);
      for (let k = 0; k < 200 && !(await E('!!(window.cdomF && cdomF.draws > 0)')); k++) await sleep(10);
      const m = await E('JSON.stringify(__m)').then(JSON.parse);
      boot.push(m.d - m.f0); fac.push(m.f1 - m.f0);
      // 20 clicks in a row, and 5000 calls of the export touch (one text changed, as the click does) straight into C with
      // no draw: the clock's grain is 0.1 ms.
      click.push(await E('(function () { var t = __now(), i; for (i = 0; i < 20; i++) cdomF.k(0); return (__now() - t) / 20; })()'));
      inC.push(await E('(function () { var t = __now(), i; for (i = 0; i < 5000; i++) cf_raw.touch(); return (__now() - t) / 5000; })()'));
    }
    res[name] = { boot: r2(median(boot)), factory: r2(median(fac)), click: r2(median(click)), touchUs: r2(median(inC) * 1000),
      count: await E('X.stat(0)') };
  }
  console.log(`P3 interpreter ${JSON.stringify(res.interpreter)}, ie9 ${JSON.stringify(res.ie9)} (ms): boot ${r2(res.interpreter.boot / res.ie9.boot)} times,`
    + ` click ${r2(res.interpreter.click / res.ie9.click)} times, the call into C alone (touch, us) ${r2(res.interpreter.touchUs / res.ie9.touchUs)} times`);
}

// The instructions each CPU run takes on the interpreter, from its profiling build in node.
const cpu = fs.readFileSync(path.join(root, 'js1cpu.wasm'));
function counted(call) {
  const m = L.parseWasm(cpu), low = L.lower(m), order = L.opOrder(), ctx = vm.createContext({});
  ctx.window = ctx;
  vm.runInContext(L.interpreter(order, { profile: true }), ctx);
  vm.runInContext(L.moduleFile(m, low, order, 'x.js1.js'), ctx);
  call(vm.runInContext('cdomFactory(new Object())', ctx));
  let reg = 0, wasm = 0;
  for (const k of Object.keys(ctx.wi_P)) { reg += ctx.wi_P[k]; wasm += ctx.wi_P[k] * low.weight[+k]; }
  return [reg, wasm];
}
// P4, P5: each run in a fresh page; the first (cold) run and the median of 5 after 3 to warm up.
// [export, argument, calls a sample]: a sample long against the clock's 0.1 ms grain.
const RUNS = [['loop', 100000, 1], ['rec', 1000, 100]];
async function cpuRuns(url) {
  const out = {};
  for (const [f, n, k] of RUNS) {
    await go(url);
    const t = [];
    for (let i = 0; i < 9; i++) t.push(await E(`(function () { var t = __now(), i; for (i = 0; i < ${k}; i++) X.${f}(${n}); return (__now() - t) / ${k}; })()`));
    out[`${f}(${n})`] = { cold: r2(t[0]), warm: r2(median(t.slice(4))) };
  }
  return out;
}
{
  // Both after cdom.frames.js, which supplies the Math.imul, clz32 and trunc the profile takes and the IE 9 build calls.
  const js1 = inline('cpu-js1', ['@cdom.frames.js', '@cdom.js1.js', '@js1cpu.wasm.js1.js', 'var X = cdomFactory(new Object());']);
  const w9 = inline('cpu-ie9', ['@cdom.frames.js', '@js1cpu.wasm.ie9.js', 'var X = cdomFactory({});']);
  const a = await cpuRuns(js1), c = await cpuRuns(w9);
  const counts = RUNS.map(([f, n]) => counted((x) => x[f](n)));
  RUNS.forEach(([f, n], i) => {
    const k = `${f}(${n})`, [reg, wasm] = counts[i];
    console.log(`P4 ${k}: interpreter ${JSON.stringify(a[k])}, ie9 ${JSON.stringify(c[k])} ms; ${r2(a[k].warm / c[k].warm)} times warm, ${r2(a[k].cold / c[k].cold)} cold;`
      + ` ${reg} instructions, ${Math.round(reg / a[k].warm / 1000)} million a second warm, ${Math.round(reg / a[k].cold / 1000)} cold`);
    console.log(`P6 ${k}: ${reg} interpreter instructions for ${wasm} wasm instructions: ${r2(reg / wasm)}`);
  });
  // P5: the deepest recursion each reaches, and whether the next call works.
  await go(js1);
  const deep = await E(`(function () { var lo = 1000, hi = 4000000, mid; while (lo < hi) { mid = Math.ceil((lo + hi) / 2); X.rec(mid); if (wi_trapped()) hi = mid - 1; else lo = mid; }
    X.rec(lo + 1); var t = wi_trapped(); X.rec(10); return [lo, t, wi_trapped()]; })()`);
  await go(w9);
  const deep9 = await E(`(function () { var lo = 100, hi = 4000000, mid, ok; while (lo < hi) { mid = Math.ceil((lo + hi) / 2); ok = true; try { X.rec(mid); } catch (e) { ok = false; } if (ok) lo = mid; else hi = mid - 1; }
    return lo; })()`);
  console.log(`P5 the interpreter reaches ${deep[0]} frames of rec, one more traps (${deep[1]}), and the next call ${deep[2] ? 'traps' : 'works'}; the IE 9 build in this Chrome reaches ${deep9}`);
}

// P7: each dispatch shape's interpreter (unminified, all alike) on loop(1e5), in fresh pages.
{
  const order = L.opOrder(), res = {};
  for (const shape of L.SHAPES) {
    const url = inline(`shape-${shape}`, [L.interpreter(order, { shape }), '@js1cpu.wasm.js1.js', 'var X = cdomFactory(new Object());']);
    const t = [];
    for (let i = 0; i < 3; i++) {
      await go(url);
      for (let k = 0; k < 5; k++) t.push(await E('(function () { var t = __now(); X.loop(100000); return __now() - t; })()'));
    }
    res[shape] = r2(median(t));
  }
  console.log(`P7 loop(1e5) ms by dispatch shape: ${JSON.stringify(res)}`);
}

// P8: binaryen's i64 lowering on frames.wasm and on tests/memforms.c (which is mostly i64), in instructions.
for (const f of ['frames.wasm', 'memforms.wasm']) {
  const count = (args) => +execFileSync('wasm-opt', ['-all', ...args, '--metrics', path.join(root, f), '-o', '/dev/null'], { encoding: 'utf8' })
    .match(/\[total\]\s*:\s*(\d+)/)[1];
  const a = count([]), c = count(['--flatten', '--i64-to-i32-lowering']), d = count(['--flatten', '--i64-to-i32-lowering', '-O2']);
  console.log(`P8 ${f}: ${a} instructions; ${c} after --flatten --i64-to-i32-lowering (${r2(c / a)} times); ${d} with -O2 after (${r2(d / a)} times)`);
}
await b.close();
srv.close();
