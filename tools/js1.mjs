// Checks the JS 1 transform (tools/legacy/js1.mjs, docs/js1-transform-design.md) in node; exit 1 on any failure.
// node tools/js1.mjs, after tests/build.sh:
// - the lowering, on planted code shaped as wasm2js writes it (labels, labeled blocks, switch with fallthrough and a
//   default in the middle, do-while, a continue out of a switch, jumps three loops out, traps, Math.imul and its kin):
//   each function gives what the original gives, run in a context with no Math.imul, clz32, trunc or fround, and the
//   transform's output passes the 1.1 and 1.0 gates;
// - tests/js1cpu.c's exports (loop, rec, recs) in js1cpu.wasm.js1.js give what WebAssembly gives;
// - the call depth: every top-level function of the file wrapped to count, a C recursion d deep is d + k JavaScript
//   frames, k at most 8 (T5). Timings are tools/frames.mjs's (FRAMES_MEASURE=1), in Chrome: a vm context's globals,
//   which every name of the transformed file is, cost tens of times what a page's do.
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';

const here = path.dirname(new URL(import.meta.url).pathname), out = path.resolve(here, '../tests/out');
const { js1 } = await import('./legacy/js1.mjs');
const { lacks } = await import('./legacy/js11.mjs');
const { lacks10 } = await import('./legacy/js10.mjs');
let fails = 0;
const ok = (c, m) => { if (!c) { fails++; console.log('FAIL', m); } else console.log('ok  ', m); };
// What the frames tier's engines lack and wasm2js output would call, gone from a context.
const OLD = `delete Math.imul; delete Math.clz32; delete Math.trunc; delete Math.fround; delete String.prototype.charCodeAt;
  delete String.fromCharCode; delete Array.prototype.push; delete Function.prototype.call; delete Function.prototype.apply;
  delete globalThis.Int32Array; delete globalThis.Uint8Array; delete globalThis.ArrayBuffer; delete globalThis.Float64Array;`;
const run = (src, old) => {
  const ctx = vm.createContext({ window: {} });
  if (old) vm.runInContext(OLD, ctx);
  vm.runInContext(src, ctx);
  return ctx;
};
const words1 = fs.readFileSync(path.join(here, 'legacy/words1.js'), 'utf8');

// Planted functions, each called with each of ARGS: the original (wasm2js-shaped JS, run as it is) and the transformed.
const PLANTED = {
  // A labeled block left from two loops in, and a loop's labeled continue from inside a block.
  blocks: `function blocks(n) {
    n = n | 0; var i = 0, j = 0, s = 0;
    out: {
      while (1) {
        j = 0;
        inner: while (1) {
          j = j + 1 | 0; s = s + j | 0;
          if ((s | 0) > (n | 0)) break out;
          if ((j | 0) > 3) break inner;
          if (j & 1) continue inner;
          s = s + 100 | 0;
        }
        i = i + 1 | 0;
        if ((i | 0) > 5) break;
      }
      s = s + 7 | 0;
    }
    return s | 0;
  }`,
  // br_table as wasm2js writes it: a switch whose cases leave blocks, a default in the middle, and fallthrough.
  table: `function table(x) {
    x = x | 0; var r = 0;
    b3: {
      b2: {
        b1: {
          switch (x | 0) {
          case 0: case 3: r = 10;
          case 1: r = r + 1 | 0; break b1;
          default: r = 50; break b2;
          case 2: break b3;
          case 5: r = 5;
          }
          r = r + 1000 | 0;
        }
        r = r + 200 | 0;
      }
      r = r + 30 | 0;
    }
    return r | 0;
  }`,
  // A continue of the loop from inside a switch (which the transform makes a loop of its own), and a plain break of it.
  loopswitch: `function loopswitch(n) {
    n = n | 0; var i = 0, s = 0;
    while (1) {
      i = i + 1 | 0;
      if ((i | 0) > (n | 0)) break;
      switch (i & 3) {
      case 0: s = s + 1 | 0; continue;
      case 1: s = s + 10 | 0; break;
      default: s = s + 100 | 0;
      }
      s = s + 1000 | 0;
    }
    return s | 0;
  }`,
  // do-while with a continue, which must go to the test.
  dowhile: `function dowhile(n) {
    n = n | 0; var i = 0, s = 0;
    do { i = i + 1 | 0; if (i & 1) continue; s = s + i | 0; } while ((i | 0) < (n | 0));
    return s | 0;
  }`,
  // Three loops out, by continue and by break.
  deep: `function deep(n) {
    n = n | 0; var a = 0, b = 0, c = 0, s = 0;
    l1: while (1) {
      a = a + 1 | 0; if ((a | 0) > 4) break;
      b = 0;
      l2: while (1) {
        b = b + 1 | 0; if ((b | 0) > 4) break;
        c = 0;
        while (1) {
          c = c + 1 | 0; s = s + 1 | 0;
          if ((c | 0) == (n | 0)) continue l1;
          if ((c + b | 0) == 6) continue l2;
          if ((s | 0) > 40) break l1;
          if ((c | 0) > 3) break;
        }
        s = s + 1000 | 0;
      }
    }
    return s | 0;
  }`,
  // Math.imul by a small constant (written x * c | 0) and by a variable, clz32, trunc, fround, === and Infinity.
  maths: `function maths(x) {
    x = x | 0;
    return Math_imul(x, 2654) + Math_imul(x, x) + Math_imul(-1048576, x) + Math_clz32(x) + Math_trunc(x / 7) + (x === 3 ? 1 : 0)
      + (Math_fround(x / 3) * 3 | 0) + (1 / 0 == Infinity ? 0 : 1) | 0;
  }`,
  // A trap two calls in: nothing after it runs, in any caller, and the flag is up.
  traps: `function trap0(x) { if ((x | 0) == 3) wasm2js_trap(); seen = seen + 1 | 0; return x + 1 | 0; }
  function trap1(x) { var r = 0; r = trap0(x) | 0; seen = seen + 10 | 0; return r + 1 | 0; }
  function traps(x) { x = x | 0; var r = 0; seen = 0; r = trap1(x) | 0; seen = seen + 100 | 0; if (trap1(x + 1 | 0)) seen = seen + 1000 | 0; return (r + (seen << 8)) | 0; }
  function seenNow() { return seen | 0; }`,
};
const ARGS = [0, 1, 2, 3, 4, 5, 6, 7, 9, 12, 20, 40, 1000, -1, -7];
// maths alone takes the edges too: the others loop that many times.
const EDGE = [65535, 65536, 2147483647, -2147483648, 1 << 20, -(1 << 20) - 1];
const NAMES = Object.keys(PLANTED);
// The planted source in the shape js1() takes: words1.js, wasm2js's trap, and asmFunc with the functions in it.
const decls = Object.values(PLANTED).join('\n');
const shaped = `${words1}
function wasm2js_trap() { throw new Error('abort'); }
function asmFunc(imports) {
  var buffer = 0; words_init(65536);
  var Math_imul = Math.imul; var Math_clz32 = Math.clz32; var Math_trunc = Math.trunc; var Math_fround = fround;
  var seen = 0;
  ${decls}
  return { "memory": { grow: words_grow, words: W, load8_u: load8_u, store8: store8, load_f32: load_f32, store_f32: store_f32,
    load_f64: load_f64, store_f64: store_f64 }, ${[...NAMES, 'seenNow'].map((n) => `"${n}": ${n}`).join(', ')} };
}`;
const { code: lowered } = js1(shaped);
ok(!lacks(lowered).length, `the planted functions, transformed, pass the 1.1 gate: ${[...new Set(lacks(lowered))].slice(0, 5)}`);
ok(!lacks10(lowered).length, `and the 1.0 gate: ${[...new Set(lacks10(lowered))].slice(0, 5)}`);
ok(!/\b(switch|do|throw|try)\b|\w+\s*:\s*(while|for|\{)|===|!==|Infinity|Math\.(imul|clz32|trunc|fround)/.test(lowered),
  'no switch, do, throw, try, label, ===, !==, Infinity or Math.imul, clz32, trunc or fround is left');
const orig = run(`var window = {}; var seen = 0; function wasm2js_trap() { throw new Error('abort'); }
  var Math_imul = Math.imul, Math_clz32 = Math.clz32, Math_trunc = Math.trunc, Math_fround = Math.fround;
  ${decls}`, false);
const low = run(lowered, true).window.cdomFactory({});
for (const n of NAMES) {
  if (n === 'traps') continue;
  const bad = (n === 'maths' ? [...ARGS, ...EDGE] : ARGS).filter((a) => orig[n](a) !== low[n](a)).map((a) => `${n}(${a}) ${low[n](a)}, want ${orig[n](a)}`);
  ok(!bad.length, `${n} gives what the original gives on ${ARGS.length} arguments: ${bad.slice(0, 3).join('; ')}`);
}
// Traps: the original throws out of traps(3) with seen at 0 (trap0 threw before any add); the transformed returns, its
// flag up, with seen 0 too, and a call without a trap afterwards is as the original's.
let threw = false;
try { orig.traps(3); } catch { threw = true; }
const r3 = low.traps(3), seen3 = low.seenNow(), flag = low.cdom_trapped(), flag2 = low.cdom_trapped();
ok(threw && seen3 === 0 && flag === 1 && flag2 === 0,
  `a trap two calls in: nothing after it runs (seen ${seen3}, want 0), the flag is up once (${flag}, then ${flag2}), the call answers ${r3}`);
low.traps(2);
const seen2 = low.seenNow(), flagged2 = low.cdom_trapped();
let threw2 = false;
try { orig.traps(2); } catch { threw2 = true; }
// traps(2): trap1(2) runs whole (seen 11), then trap1(3) traps inside the if's test: the 100 before it is added, the
// 1000 after it is not.
ok(threw2 && seen2 === 111 && flagged2 === 1, `a trap in an if's test: what came before it stays, nothing after runs (seen ${seen2}, want 111), flagged ${flagged2}`);
const r1 = low.traps(1), o1 = orig.traps(1);
ok(r1 === o1 && low.cdom_trapped() === 0, `traps(1), with no trap, gives what the original gives: ${r1}, want ${o1}, and no flag`);

// js1cpu against WebAssembly.
const stubs = new Proxy({}, { get: () => () => 0 });
const cpuWasm = new WebAssembly.Instance(new WebAssembly.Module(fs.readFileSync(path.join(out, 'js1cpu.wasm'))), { cdom: stubs }).exports;
const cpuCtx = run(fs.readFileSync(path.join(out, 'js1cpu.wasm.js1.js'), 'utf8'), true);
const cpu = cpuCtx.window.cdomFactory({ cdom: stubs });
for (const [name, args] of [['loop', [0]], ['loop', [1]], ['loop', [1000]], ['loop', [100000]], ['rec', [0]], ['rec', [20]], ['rec', [90]],
  ['recs', [10, 20]]]) {
  const a = cpu[name](...args) >>> 0, b = cpuWasm[name](...args) >>> 0;
  ok(a === b, `js1cpu ${name}(${args}) gives what WebAssembly gives: ${a}, want ${b}`);
}

// The call depth: each top-level function of the file counted on the way in and out. The exports object holds the
// functions themselves, so rec is called through its wrapped global, found by identity.
let depth = 0, max = 0, recName = null;
for (const k of Object.keys(cpuCtx)) {
  const f = cpuCtx[k];
  if (!/^c_/.test(k) || typeof f !== 'function') continue;
  if (f === cpu.rec) recName = k;
  cpuCtx[k] = function () { if (++depth > max) max = depth; try { return Reflect.apply(f, this, arguments); } finally { depth--; } };
}
const r20 = recName ? cpuCtx[recName](20) >>> 0 : -1;
const k20 = recName && r20 === (cpuWasm.rec(20) >>> 0) ? max - 22 : -1;
ok(k20 >= 0 && k20 <= 8, `the export rec(20), 21 C frames of rec under it, is ${max} JavaScript frames: ${k20} others, at most 8 (T5)`);

console.log(fails ? `${fails} FAILED` : 'all ok');
process.exit(fails ? 1 : 0);
