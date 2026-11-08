// Checks tools/legacy/words.js, the word-Array memory app.wasm.ie9.js runs on, against real typed arrays: every
// load and store at every byte and halfword position, sign edges, i64 halves, the f32 and f64 packing on special and
// ordinary values, Math.fround on halfway cases through the subnormals, and copy, fill, grow and the base64
// segments. Exit 1 on any difference. The inputs come from one fixed-seed generator (SEED), so a run is repeatable.
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';

const SEED = 0x9e3779b9; // the one seed: change it here to try other inputs
const src = fs.readFileSync(path.resolve(path.dirname(new URL(import.meta.url).pathname), 'legacy/words.js'), 'utf8');
const w = vm.createContext({});
vm.runInContext(src, w);
const W = () => w.W;

let fails = 0, checks = 0;
const ok = (c, m) => { checks++; if (!c && fails++ < 20) console.log('FAIL', m); };
const section = (name, f) => { const before = fails, n0 = checks; f(); console.log(`${fails === before ? 'ok  ' : 'FAIL'} ${name} (${checks - n0} checks)`); };

let state = SEED >>> 0; // xorshift32
const next = () => { state ^= state << 13; state >>>= 0; state ^= state >>> 17; state ^= state << 5; state >>>= 0; return state; };

const BYTES = 64;
const buf = new ArrayBuffer(BYTES), u8 = new Uint8Array(buf);
const views = { i8: new Int8Array(buf), u8, i16: new Int16Array(buf), u16: new Uint16Array(buf), i32: new Int32Array(buf),
  u32: new Uint32Array(buf), f32: new Float32Array(buf), f64: new Float64Array(buf) };
// Both memories hold the same bytes, chosen by fill(i) for byte i.
function both(fill) {
  w.words_init(BYTES);
  for (let i = 0; i < BYTES; i++) { u8[i] = fill(i); w.store8(i, u8[i]); }
}
const same = (what) => {
  for (let i = 0; i < BYTES; i++) if (w.load8_u(i) !== u8[i]) return ok(false, `${what}: byte ${i} is ${w.load8_u(i)}, typed ${u8[i]}`);
  for (let i = 0; i < BYTES / 4; i++) if (W()[i] !== views.i32[i]) return ok(false, `${what}: word ${i} is ${W()[i]}, typed ${views.i32[i]}`);
  ok(true);
};
const EDGES = [0, 1, 0x7f, 0x80, 0xff, 0x100, 0x7fff, 0x8000, 0xffff, 0x10000, 0x7fffffff, 0x80000000, 0xffffffff,
  -1, -0x80, -0x81, -0x8000, -0x8001, -0x80000000, 0x12345678, 0x9abcdef0, 2 ** 32 + 5, -(2 ** 32) - 3];

section('loads at every position: i8, u8, i16, u16, i32, u32, against the typed views', () => {
  for (let round = 0; round < 200; round++) {
    both(round < 2 ? (i) => (round ? 0xff : 0x80) ^ (i & 1) : () => next() & 255);
    for (let a = 0; a < BYTES; a++) {
      ok(w.load8_s(a) === views.i8[a], `load8_s(${a})`);
      ok(w.load8_u(a) === views.u8[a], `load8_u(${a})`);
    }
    for (let h = 0; h < BYTES / 2; h++) {
      ok(w.load16_s(h) === views.i16[h], `load16_s(${h})`);
      ok(w.load16_u(h) === views.u16[h], `load16_u(${h})`);
    }
    for (let i = 0; i < BYTES / 4; i++) {
      ok(W()[i] === views.i32[i], `W[${i}] as HEAP32`);
      ok(W()[i] >>> 0 === views.u32[i], `W[${i}] >>> 0 as HEAPU32`);
    }
  }
});

section('stores at every position, on edge and random values: 8, 16 and 32 bits leave every other byte alone', () => {
  for (let round = 0; round < 40; round++) {
    both(() => next() & 255);
    const values = [...EDGES, next() | 0, next(), -(next() >>> 1)];
    for (const v of values) {
      for (let a = 0; a < BYTES; a++) { w.store8(a, v); views.u8[a] = v; }
      same(`store8 of ${v}`);
      for (let h = 0; h < BYTES / 2; h++) { w.store16(h, v); views.i16[h] = v; }
      same(`store16 of ${v}`);
      for (let i = 0; i < BYTES / 4; i++) { W()[i] = v | 0; views.i32[i] = v; } // HEAP32[i] = v becomes W[i] = v | 0
      same(`store32 of ${v}`);
      const a = next() % BYTES; w.store8(a, ~v); views.u8[a] = ~v; same(`store8 of ~${v} at ${a}`);
      const h = next() % (BYTES / 2); w.store16(h, v ^ 0x5a5a); views.u16[h] = v ^ 0x5a5a; same(`store16 at ${h}`);
    }
  }
});

section('i64 as wasm2js lowers it: two words, low first, and bytes in place for an unaligned one', () => {
  const big = new BigInt64Array(buf);
  for (let round = 0; round < 2000; round++) {
    both(() => 0);
    const lo = next() | 0, hi = round < 4 ? [0, -1, 0x7fffffff, -0x80000000][round] : next() | 0;
    const k = next() % (BYTES / 8);
    W()[2 * k] = lo; W()[2 * k + 1] = hi; big[k] = (BigInt(hi) << 32n) | BigInt(lo >>> 0);
    same(`i64 ${hi}:${lo} at ${k}`);
    // an unaligned i64 (a packed struct): eight HEAP8 stores of the halves shifted, read back as HEAPU8
    const a = 1 + next() % (BYTES - 9);
    for (let b = 0; b < 4; b++) { w.store8(a + b, lo >>> (8 * b)); w.store8(a + 4 + b, hi >>> (8 * b)); }
    new DataView(buf).setBigInt64(a, (BigInt(hi) << 32n) | BigInt(lo >>> 0), true);
    same(`unaligned i64 at ${a}`);
    const back = (o) => w.load8_u(o) | w.load8_u(o + 1) << 8 | (w.load8_u(o + 2) << 16 | w.load8_u(o + 3) << 24);
    ok(back(a) === lo && back(a + 4) === hi, `unaligned i64 read back at ${a}`);
  }
});

// Doubles that matter to the float paths.
const F32 = new Float32Array(1), F32U = new Uint32Array(F32.buffer), F64 = new Float64Array(1), F64U = new Uint32Array(F64.buffer);
const fbits = (x) => { F32[0] = x; return F32U[0] | 0; };
const fromfbits = (b) => { F32U[0] = b; return F32[0]; };
const fnext = (x, d) => fromfbits(fbits(x) + d); // the float d steps from float x (x > 0)
const SPECIAL = [0, -0, Infinity, -Infinity, NaN, 1, -1, 0.1, 1 / 3, -2.5, 1e-45, 1.401298464324817e-45, 1.1754942106924411e-38,
  1.1754943508222875e-38, 3.4028234663852886e38, -3.4028234663852886e38, 3.4028235677973366e38, 3.4028235677973362e38,
  3.4028235677973370e38, 1e39, -1e39, 5e-324, 2.2250738585072014e-308, 2.225073858507201e-308, 1.7976931348623157e308,
  -1.7976931348623157e308, 16777216, 16777217, 16777219, 2 ** 24 + 1, 2 ** -149, 2 ** -150, 2 ** -150 * 1.0000001, 2 ** -151,
  3 * 2 ** -150, 2 ** -126 - 2 ** -150, 0.5, 1.5, 2.5, 123456789, -987654321.123, Math.PI, Math.E];
function doubles(n) {
  const out = [...SPECIAL];
  for (let e = -150; e <= 128; e++) { // every float binade, through the subnormals: each float, its neighbour, halfway, and off it
    for (let k = 0; k < 4; k++) {
      const f = Math.abs(fromfbits(Math.min(0x7f7fffff, Math.max(1, ((e + 127) << 23) + (next() & 0x7fffff)))));
      const g = fnext(f, 1);
      if (!Number.isFinite(g)) continue;
      const mid = (f + g) / 2; // exact in a double: a tie
      out.push(f, g, mid, -mid, mid * (1 + 2 ** -52), mid * (1 - 2 ** -53), -f);
    }
  }
  for (let i = 0; i < n; i++) { // random doubles from random bits
    F64U[0] = next(); F64U[1] = next(); out.push(F64[0]);
  }
  return out;
}
const VALUES = doubles(20000);
const same64 = (a, b) => Object.is(a, b) || (Number.isNaN(a) && Number.isNaN(b));

section('Math.fround in software: the same as the built-in on ties, subnormals, infinities and NaN', () => {
  for (const x of VALUES) ok(same64(w.fround(x), Math.fround(x)), `fround(${x}) is ${w.fround(x)}, built-in ${Math.fround(x)}`);
});

section('f32 stores: the bits of a Float32Array store of the same value; a NaN stores as 0x7fc00000', () => {
  for (const x of VALUES) {
    w.words_init(BYTES); w.store_f32(3, x);
    if (Number.isNaN(x)) ok(W()[3] === 0x7fc00000, `NaN stores canonical: ${W()[3].toString(16)}`);
    else ok(W()[3] === fbits(x), `store_f32(${x}): ${W()[3]} against ${fbits(x)}`);
  }
});

section('f32 loads: every exponent with edge and random mantissas, both signs, against Float32Array', () => {
  for (let e = 0; e < 256; e++) {
    for (const m of [0, 1, 2, 0x3fffff, 0x400000, 0x400001, 0x7ffffe, 0x7fffff, next() & 0x7fffff, next() & 0x7fffff]) {
      for (const s of [0, 0x80000000]) {
        const b = (s | e << 23 | m) | 0;
        w.words_init(BYTES); W()[5] = b;
        const v = w.load_f32(5);
        ok(same64(v, fromfbits(b)), `load_f32 of ${(b >>> 0).toString(16)}: ${v} against ${fromfbits(b)}`);
        if (e !== 255 || !m) ok(w.f32_bits(v) === b, `f32 bits round trip ${(b >>> 0).toString(16)}`);
      }
    }
  }
});

section('f64 stores and loads: the bits of Float64Array on the same values; a NaN stores as 0x7ff8000000000000', () => {
  for (const x of VALUES) {
    w.words_init(BYTES); w.store_f64(2, x);
    if (Number.isNaN(x)) { ok(W()[4] === 0 && W()[5] === 0x7ff80000, 'NaN stores canonical'); continue; }
    F64[0] = x;
    ok(W()[4] === (F64U[0] | 0) && W()[5] === (F64U[1] | 0), `store_f64(${x}): ${W()[5]}:${W()[4]} against ${F64U[1] | 0}:${F64U[0] | 0}`);
    ok(Object.is(w.load_f64(2), x), `load_f64 gives back ${x}: ${w.load_f64(2)}`);
  }
  for (let e = 0; e < 2048; e++) {
    for (const s of [0, 0x80000000]) {
      const hi = (s | e << 20 | (e & 1 ? 0xfffff : next() & 0xfffff)) | 0, lo = e & 2 ? next() | 0 : e & 4 ? 0 : 1;
      F64U[0] = lo; F64U[1] = hi;
      w.words_init(BYTES); W()[6] = lo; W()[7] = hi;
      ok(same64(w.load_f64(3), F64[0]), `load_f64 of ${(hi >>> 0).toString(16)}:${(lo >>> 0).toString(16)}: ${w.load_f64(3)} against ${F64[0]}`);
    }
  }
});

section('the reinterpret scratch: an f64 or f32 stored reads back as its bits through the i32 slots, and the reverse', () => {
  for (const x of VALUES.slice(0, 5000)) {
    w.scratch_store_f64(x);
    F64[0] = x;
    if (!Number.isNaN(x)) ok(w.scratch_load_i32(0) === (F64U[0] | 0) && w.scratch_load_i32(1) === (F64U[1] | 0), `scratch f64 ${x}`);
    w.scratch_store_i32(0, F64U[0]); w.scratch_store_i32(1, F64U[1]);
    ok(same64(w.scratch_load_f64(), F64[0]), `scratch f64 from bits of ${x}`);
    w.scratch_store_f32(x);
    if (!Number.isNaN(x)) ok(w.scratch_load_i32(2) === fbits(x), `scratch f32 ${x}`);
    w.scratch_store_i32(2, fbits(x));
    ok(same64(w.scratch_load_f32(), Math.fround(x)), `scratch f32 from bits of ${x}`);
  }
});

section('memory.copy (overlapping both ways, every alignment) and memory.fill, against copyWithin and fill', () => {
  for (let round = 0; round < 3000; round++) {
    both(() => next() & 255);
    const dest = next() % BYTES, src = next() % BYTES, n = next() % (BYTES + 8);
    w.words_copy(dest, src, n); u8.copyWithin(dest, src, src + n);
    same(`copy ${n} from ${src} to ${dest}`);
    const at = next() % BYTES, m = next() % (BYTES - at + 1), v = next() | 0;
    w.words_fill(at, v, m); u8.fill(v, at, at + m);
    same(`fill ${m} of ${v} at ${at}`);
  }
});

section('grow adds zero pages at the end and keeps what was there', () => {
  w.words_init(65536); w.store8(65535, 0xab); W()[100] = -5;
  w.words_grow(2);
  ok(w.WBYTES === 3 * 65536 && W().length === 3 * 16384, `size after grow: ${w.WBYTES} bytes, ${W().length} words`);
  ok(w.load8_u(65535) === 0xab && W()[100] === -5 && W().slice(16384).every((x) => x === 0), 'old bytes kept, new ones zero');
});

section('data segments: base64 decoded into the words, every length and offset, the bytes around untouched', () => {
  for (let round = 0; round < 500; round++) {
    both(() => next() & 255);
    const n = next() % 40, at = next() % (BYTES - n);
    const data = Buffer.from(Array.from({ length: n }, () => next() & 255));
    w.words_base64(at, data.toString('base64')); u8.set(data, at);
    same(`segment of ${n} at ${at}`);
  }
});

// The pass itself, end to end: tests/memforms.c (built by tests/build.sh) as wasm2js output on typed arrays
// (app.wasm.js) and as its words build (app.wasm.ie9.js) in a context with no typed array at all, called with the
// same arguments: every result and every byte of memory must agree. wasm2js output is the reference, because the
// pass must change nothing it does: where C's pointer is misaligned for its type, wasm2js truncates the address
// (HEAPF64[p >> 3]) where wasm does not. On aligned pointers, wasm itself must agree too.
section('tests/memforms.c: its words build agrees with wasm2js output on every result and byte, and with wasm where aligned', () => {
  const out = path.resolve(path.dirname(new URL(import.meta.url).pathname), '../tests/out');
  let hi = 0;
  const load = (file, strip) => {
    const ctx = vm.createContext({ window: {} });
    if (strip) vm.runInContext(`for (const k of ${JSON.stringify(['ArrayBuffer', 'SharedArrayBuffer', 'DataView', 'Int8Array', 'Uint8Array',
      'Uint8ClampedArray', 'Int16Array', 'Uint16Array', 'Int32Array', 'Uint32Array', 'Float32Array', 'Float64Array', 'BigInt64Array',
      'BigUint64Array', 'Float16Array', 'Atomics'])}) delete globalThis[k];`, ctx);
    vm.runInContext(fs.readFileSync(path.join(out, file), 'utf8'), ctx);
    return ctx.window.cdomFactory({ env: { setTempRet0: (v) => { hi = v; } } });
  };
  const js = load('memforms.wasm.ie9.js', true), ref = load('memforms.wasm.js', false);
  const wasm = new WebAssembly.Instance(new WebAssembly.Module(fs.readFileSync(path.join(out, 'memforms.wasm'))), {}).exports;
  const i64 = (lo) => (BigInt(hi) << 32n) | BigInt(lo >>> 0);
  const join = (lo, h) => BigInt.asIntN(64, (BigInt(h) << 32n) | BigInt(lo >>> 0));
  // Each call on all three: its result from the words build, wasm2js's and wasm's, as comparable values.
  const call = (name, args, wasmArgs, i64Result) => {
    const r = [js[name](...args)], h1 = hi;
    r.push(ref[name](...args));
    const h2 = hi;
    r.push(wasm[name](...wasmArgs));
    return i64Result ? [join(r[0], h1), join(r[1], h2), r[2]] : r;
  };
  const agree = (what, aligned, from = 0, to = 0) => {
    const words = js.memory.words, typed = new Uint8Array(ref.memory.buffer), real = new Uint8Array(wasm.memory.buffer);
    if (words.length * 4 !== typed.length || typed.length !== real.length) return ok(false, `${what}: memory sizes ${words.length * 4}, ${typed.length}, ${real.length}`);
    for (let a = from; a < (to || typed.length); a++) {
      const b = js.memory.load8_u(a);
      if (b !== typed[a]) return ok(false, `${what}: byte ${a} is ${b}, wasm2js ${typed[a]}`);
      if (aligned && b !== real[a]) return ok(false, `${what}: byte ${a} is ${b}, wasm ${real[a]}`);
    }
    ok(true);
  };
  const results = (what, aligned, [a, b, c]) => ok(same64(a, b) && (!aligned || same64(a, c)), `${what}: ${a}, wasm2js ${b}, wasm ${c}`);
  agree('the data segment and the rest at start', true);
  results('dat', true, call('dat', [], []));
  // Aligned rounds work in one region and the others in another, since after a misaligned store wasm and wasm2js
  // hold different bytes there for good.
  const N = 64;
  for (let round = 0; round < 4000; round++) {
    const aligned = round % 2 === 0, AT = aligned ? 70000 : 71000, p = AT + (aligned ? 8 * (next() % 4) : next() % 32);
    const k = next() % 10, [lo, h] = round < EDGES.length ? [EDGES[round] | 0, EDGES[round] < 0 ? -1 : 0] : [next() | 0, next() | 0];
    call('st', [p, k, lo, h], [p, k, join(lo, h)]);
    agree(`st(${p}, ${k})`, aligned, AT - 8, AT + N);
    results(`ld(${p}, ${k})`, aligned, call('ld', [p, k], [p, k], true));
    const x = VALUES[next() % VALUES.length], y = VALUES[next() % VALUES.length], kf = next() % 4;
    call('stf', [p, kf, x], [p, kf, x]);
    if (!Number.isNaN(x)) agree(`stf(${p}, ${kf}, ${x})`, aligned, AT - 8, AT + N);
    results(`ldf(${p}, ${kf}) after ${x}`, aligned, call('ldf', [p, kf], [p, kf]));
    for (const f of ['fadd', 'fdiv', 'fmul']) results(`${f}(${x}, ${y})`, true, call(f, [x, y], [x, y]));
    results(`tof(${x})`, true, call('tof', [x], [x]));
    if (!Number.isNaN(x)) {
      results(`bits(${x})`, true, call('bits', [x], [x], true).map((v, i) => i === 2 ? BigInt.asIntN(64, v) : v));
      results(`fbits(${x})`, true, call('fbits', [x], [x]));
    }
    results(`frombits(${h}:${lo})`, true, call('frombits', [lo, h], [join(lo, h)]));
    results(`ffrombits(${lo})`, true, call('ffrombits', [lo], [lo]));
    results(`i2d(${h}:${lo})`, true, call('i2d', [lo, h], [join(lo, h)]));
    const d = AT + next() % N, s = AT + next() % N, n = next() % 40;
    call('cpy', [d, s, n], [d, s, n]); agree(`cpy(${d}, ${s}, ${n})`, aligned, AT - 8, AT + 2 * N);
    const c = next() | 0, m = next() % 40;
    call('set', [d, c, m], [d, c, m]); agree(`set(${d}, ${c}, ${m})`, aligned, AT - 8, AT + 2 * N);
  }
  results('grow(3)', true, call('grow', [3], [3]));
  results('size()', true, call('size', [], []));
  agree('all of memory after the rounds and a grow', false);
});

console.log(fails ? `${fails} FAILED of ${checks}` : `all ok: ${checks} checks`);
process.exit(fails ? 1 : 0);
