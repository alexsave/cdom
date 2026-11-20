// Checks the frames tier's WebAssembly interpreter (tools/legacy/interp.js, built with tools/legacy/js1lower.mjs;
// docs/js1-interp-design.md) against the engine's own WebAssembly, in node, op by op and on whole programs; exit 1 on
// any failure. node tools/js1interp.mjs [section ...]; tests/build.sh must have run (tests/out/*.wasm).
//   ops       a module of every op the lowering takes (written here as text, assembled by binaryen's wasm-as), each
//             export called on edge values of its types, results and traps compared
//   memforms  tests/memforms.c: every load and store form, as tools/words.mjs runs it against wasm2js
//   cpu       tests/js1cpu.c: the integer loop and the recursion, and the depth at which the interpreter's own stack
//             limit traps, after which a call works again
//   frames    tests/frames.c's module loads, its imports bound by name, and cd_layout and cd_lits answer as on wasm
//   profile   prints each op's dynamic count on tests/frames.c's boot and tests/js1cpu.c (js1ops.txt's order), the
//             comparisons an instruction pays in each dispatch shape, and the wasm instructions each run stands for
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import vm from 'node:vm';
import { execFileSync } from 'node:child_process';
import * as L from './legacy/js1lower.mjs';

const top = path.resolve(path.dirname(new URL(import.meta.url).pathname), '..');
const out = path.join(top, 'tests/out');
const want = process.argv.slice(2);
const on = (s) => !want.length || want.includes(s);
let fails = 0;
const ok = (c, m) => { if (!c) { fails++; console.log('FAIL', m); } else console.log('ok  ', m); };

// A fresh interpreter with module m in it: { ctx, exports }. profile: the counting build.
export function instantiate(buf, imports = {}, { shape = 'weighted', profile = false, form = 'digits', fold = true } = {}) {
  const m = L.parseWasm(buf), low = L.lower(m, { fold }), order = L.opOrder();
  const ctx = vm.createContext({});
  ctx.window = ctx;
  vm.runInContext(L.interpreter(order, { shape, profile }), ctx);
  vm.runInContext(L.moduleFile(m, low, order, 'test.wasm.js1.js', form), ctx);
  ctx.imports = imports;
  const exports = vm.runInContext('cdomFactory(imports)', ctx);
  return { ctx, exports, m, low, order };
}

// --- ops: one export an op, on edge values. ----------------------------------------------------------------------
const I32 = [0, 1, -1, 2, -2, 3, 7, 31, 32, 33, 63, 64, 255, 256, 65535, 65536, -65536, 0x7fffffff, -0x80000000, 0x7ffffffe,
  0x12345678, 0xdeadbeef | 0, 1000000007, -1000000007, 0x55555555, 0xaaaaaaaa | 0];
const HI = [0, 1, -1, 0x7fffffff, -0x80000000, 0x12345678, 2];
const LO = [0, 1, -1, 2, 0x7fffffff, -0x80000000, 0x87654321 | 0, 63, 64];
const I64 = [];
for (const hi of HI) for (const lo of LO) I64.push([lo, hi]);
const F64 = [0, -0, 1, -1, 0.5, -0.5, 1.5, 2.5, -2.5, 3.7, -3.7, 1e300, -1e300, 2 ** 31, -(2 ** 31), 2 ** 31 - 1, 2 ** 32, 2 ** 53,
  2 ** 63, -(2 ** 63), 2 ** 64, Infinity, -Infinity, NaN, 5e-324, 1.1754943508222875e-38, 3.4028234663852886e38, 4294967295.5,
  1 / 3, -2147483648.9, -2147483649, 0.49999999999999994, 1e-310, 4294967296.5, -0.75];
const i64of = ([lo, hi]) => (BigInt(hi) << 32n) | BigInt(lo >>> 0);
const MK = '(func $mk (param i32 i32) (result i64) (i64.or (i64.extend_i32_u (local.get 0)) (i64.shl (i64.extend_i32_u (local.get 1)) (i64.const 32))))';
// [export, wat body, argument kinds, result kind]: i i32, p an i64 as two i32 (lo, hi), d f64, f f32; a result p is
// stored at address 0 and read from memory.
function opsModule() {
  const fns = [];
  const add = (name, params, result, body, kinds) => fns.push({ name, params, result, body, kinds });
  const arg = (k, i) => (k === 'p' ? `(call $mk (local.get ${i}) (local.get ${i + 1}))` : `(local.get ${i})`);
  const sig = (kinds) => { const ps = []; for (const k of kinds) { if (k === 'p') ps.push('i32', 'i32'); else ps.push({ i: 'i32', d: 'f64', f: 'f32' }[k]); } return ps; };
  const args = (kinds) => { let i = 0; return [...kinds].map((k) => { const a = arg(k, i); i += k === 'p' ? 2 : 1; return a; }); };
  const op = (name, kinds, result) => {
    const body = `(${name} ${args(kinds).join(' ')})`;
    if (result === 'p') add(name, sig(kinds), null, `(i64.store (i32.const 0) ${body})`, kinds);
    else add(name, sig(kinds), { i: 'i32', d: 'f64', f: 'f32' }[result], body, kinds);
  };
  for (const o of ['add', 'sub', 'mul', 'div_s', 'div_u', 'rem_s', 'rem_u', 'and', 'or', 'xor', 'shl', 'shr_s', 'shr_u', 'rotl', 'rotr',
    'eq', 'ne', 'lt_s', 'lt_u', 'gt_s', 'gt_u', 'le_s', 'le_u', 'ge_s', 'ge_u']) {
    op(`i32.${o}`, 'ii', 'i');
    op(`i64.${o}`, 'pp', /^(eq|ne|lt|gt|le|ge)/.test(o) ? 'i' : 'p');
  }
  for (const o of ['eqz', 'clz', 'ctz', 'popcnt', 'extend8_s', 'extend16_s']) op(`i32.${o}`, 'i', 'i');
  for (const o of ['clz', 'ctz', 'popcnt', 'extend8_s', 'extend16_s', 'extend32_s']) op(`i64.${o}`, 'p', 'p');
  op('i64.eqz', 'p', 'i');
  op('i64.extend_i32_s', 'i', 'p'); op('i64.extend_i32_u', 'i', 'p'); op('i32.wrap_i64', 'p', 'i');
  for (const [t, k] of [['f64', 'd'], ['f32', 'f']]) {
    for (const o of ['add', 'sub', 'mul', 'div', 'min', 'max', 'copysign']) op(`${t}.${o}`, k + k, k);
    for (const o of ['eq', 'ne', 'lt', 'gt', 'le', 'ge']) op(`${t}.${o}`, k + k, 'i');
    for (const o of ['abs', 'neg', 'sqrt', 'ceil', 'floor', 'trunc', 'nearest']) op(`${t}.${o}`, k, k);
    for (const s of ['s', 'u']) {
      op(`i32.trunc_${t}_${s}`, k, 'i'); op(`i32.trunc_sat_${t}_${s}`, k, 'i');
      op(`i64.trunc_${t}_${s}`, k, 'p'); op(`i64.trunc_sat_${t}_${s}`, k, 'p');
      op(`${t}.convert_i32_${s}`, 'i', k); op(`${t}.convert_i64_${s}`, 'p', k);
    }
  }
  op('f32.demote_f64', 'd', 'f'); op('f64.promote_f32', 'f', 'd');
  op('i32.reinterpret_f32', 'f', 'i'); op('f32.reinterpret_i32', 'i', 'f');
  op('i64.reinterpret_f64', 'd', 'p'); op('f64.reinterpret_i64', 'p', 'd');
  // Memory: a store of each width at an address, then every load of the word around it (aligned and not).
  for (const [st, k] of [['i32.store', 'i'], ['i32.store8', 'i'], ['i32.store16', 'i'], ['i64.store', 'p'], ['i64.store8', 'p'],
    ['i64.store16', 'p'], ['i64.store32', 'p'], ['f32.store', 'f'], ['f64.store', 'd']]) {
    add(st, sig('i' + k), null, `(${st} offset=16 (local.get 0) ${arg(k, 1)})`, 'a' + k);
  }
  for (const [ld, r] of [['i32.load', 'i'], ['i32.load8_s', 'i'], ['i32.load8_u', 'i'], ['i32.load16_s', 'i'], ['i32.load16_u', 'i'],
    ['i64.load', 'p'], ['i64.load8_s', 'p'], ['i64.load8_u', 'p'], ['i64.load16_s', 'p'], ['i64.load16_u', 'p'], ['i64.load32_s', 'p'],
    ['i64.load32_u', 'p'], ['f32.load', 'f'], ['f64.load', 'd']]) {
    const body = `(${ld} offset=16 (local.get 0))`;
    if (r === 'p') add(ld, ['i32'], null, `(i64.store (i32.const 0) ${body})`, 'L');
    else add(ld, ['i32'], { i: 'i32', d: 'f64', f: 'f32' }[r], body, 'L');
  }
  // Control flow and the rest: br_table with values, a loop, select, globals, call_indirect, memory.size, grow, copy, fill.
  add('br_table', ['i32'], 'i32', '(block $d (result i32) (block $c (result i32) (block $b (result i32) (block $a (result i32) (i32.const 100) (local.get 0) (br_table $a $b $c $d))'
    + ' (i32.const 1) (i32.add)) (i32.const 2) (i32.add)) (i32.const 3) (i32.add))');
  add('loop_sum', ['i32'], 'i32', '(local $s i32) (block $out (loop $l (br_if $out (i32.eqz (local.get 0))) (local.set $s (i32.add (local.get $s) (local.get 0)))'
    + ' (local.set 0 (i32.sub (local.get 0) (i32.const 1))) (br $l))) (local.get $s)');
  add('select', ['i32', 'i32', 'i32'], 'i32', '(select (local.get 0) (local.get 1) (local.get 2))');
  add('select64', ['i32', 'i32', 'i32', 'i32', 'i32'], null, '(i64.store (i32.const 0) (select (call $mk (local.get 0) (local.get 1)) (call $mk (local.get 2) (local.get 3)) (local.get 4)))');
  add('globals', ['i32'], 'i32', '(global.set $g (i32.add (global.get $g) (local.get 0))) (global.set $g64 (i64.add (global.get $g64) (i64.extend_i32_s (local.get 0)))) (i32.xor (global.get $g) (i32.wrap_i64 (i64.shr_u (global.get $g64) (i64.const 7))))');
  add('call_indirect', ['i32', 'i32'], 'i32', '(call_indirect (type $ii) (local.get 0) (local.get 1))');
  add('call_indirect_wrong', ['i32'], 'i32', '(call_indirect (type $v) (local.get 0)) (i32.const 0)');
  add('mem', ['i32'], 'i32', '(i32.add (i32.mul (memory.size) (i32.const 1000)) (memory.grow (local.get 0)))');
  add('copy', ['i32', 'i32', 'i32'], null, '(memory.copy (local.get 0) (local.get 1) (local.get 2))');
  add('fill', ['i32', 'i32', 'i32'], null, '(memory.fill (local.get 0) (local.get 1) (local.get 2))');
  add('unreachable', [], 'i32', '(unreachable)');
  add('if_else', ['i32', 'i32'], 'i32', '(if (result i32) (i32.lt_s (local.get 0) (local.get 1)) (then (i32.sub (local.get 1) (local.get 0))) (else (if (result i32) (i32.eqz (local.get 0)) (then (i32.const 77)) (else (i32.mul (local.get 0) (i32.const 3))))))');
  add('br_if_value', ['i32'], 'i32', '(block $b (result i32) (drop (br_if $b (i32.const 5) (local.get 0))) (i32.const 9))');
  add('tee', ['i32'], 'i32', '(local $t i32) (i32.add (local.tee $t (i32.mul (local.get 0) (i32.const 3))) (local.get $t))');
  add('swap', ['i32', 'i32'], 'i32', '(local $t i32) (local.get 0) (local.get 1) (local.set 0) (local.set 1) (i32.sub (local.get 0) (local.get 1))');
  add('fact', ['i32'], 'i32', '(if (result i32) (i32.le_s (local.get 0) (i32.const 1)) (then (i32.const 1)) (else (i32.mul (local.get 0) (call $fact (i32.sub (local.get 0) (i32.const 1))))))');
  add('ret_early', ['i32'], 'i32', '(if (local.get 0) (then (return (i32.const 11)))) (i32.const 22)');
  add('dead_after_br', ['i32'], 'i32', '(block $b (result i32) (br $b (local.get 0)) (i32.const 1) (i32.add))');
  const wat = ['(module', ' (type $ii (func (param i32) (result i32)))', ' (type $v (func))', ' (memory (export "memory") 1 4)',
    ' (global $g (mut i32) (i32.const 5))', ' (global $g64 (mut i64) (i64.const 0x123456789))', ' (table 4 funcref)',
    ' (elem (i32.const 1) $sq $neg)', ' (data (i32.const 100) "\\01\\02\\03\\04\\05\\06\\07\\08\\09\\0a\\0b\\0c\\0d\\0e\\0f\\10")',
    ' ' + MK, ' (func $sq (param i32) (result i32) (i32.mul (local.get 0) (local.get 0)))',
    ' (func $neg (param i32) (result i32) (i32.sub (i32.const 0) (local.get 0)))'];
  for (const f of fns) {
    const id = f.name === 'fact' ? ' $fact' : '';
    wat.push(` (func${id} (export "${f.name}") ${f.params.map((p) => `(param ${p})`).join(' ')} ${f.result ? `(result ${f.result})` : ''} ${f.body})`);
  }
  wat.push(')');
  return { wat: wat.join('\n'), fns };
}
function assemble(wat) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'js1ops-'));
  try {
    fs.writeFileSync(path.join(dir, 'ops.wat'), wat);
    execFileSync('wasm-as', ['-all', path.join(dir, 'ops.wat'), '-o', path.join(dir, 'ops.wasm')]);
    return fs.readFileSync(path.join(dir, 'ops.wasm'));
  } finally { fs.rmSync(dir, { recursive: true, force: true }); }
}
const same = (a, b) => (typeof a === 'number' && typeof b === 'number' ? (a !== a && b !== b) || Object.is(a, b) : a === b);
const show = (x) => (Object.is(x, -0) ? '-0' : String(x));

if (on('ops')) {
  const { wat, fns } = opsModule();
  const buf = assemble(wat);
  const fresh = () => {
    const w = new WebAssembly.Instance(new WebAssembly.Module(buf), {}).exports;
    const it = instantiate(buf);
    return { w, it };
  };
  let { w, it } = fresh();
  const words = (n) => { const a = new Int32Array(w.memory.buffer, 0, n); return Array.from(a); };
  const iwords = (n) => { const a = []; for (let i = 0; i < n; i++) a.push(it.ctx.wi_M[i] | 0); return a; };
  const valuesOf = (k) => (k === 'i32' ? I32 : k === 'f64' || k === 'f32' ? F64 : I32);
  const fnByName = new Map(fns.map((f) => [f.name, f]));
  // The argument lists for an op of these kinds: every value of one operand, every pair of two; a store takes an
  // address (a) and a value, a load (L) an address around the stored bytes.
  const values = (k) => (k === 'i' ? I32.map((x) => [x]) : k === 'p' ? I64 : F64.map((x) => [x]));
  const cases = (f) => {
    const k = f.kinds;
    if (!k) return null;
    if (k === 'L') return [[84], [85], [86], [87], [88], [89], [90], [91], [92]];
    if (k[0] === 'a') { const o = []; for (const at of [0, 1, 2, 3, 5, 6, 7]) for (const v of values(k[1]).slice(0, 30)) o.push([at, ...v]); return o; }
    if (k.length === 1) return values(k).map((v) => [...v]);
    const o = []; for (const a of values(k[0])) for (const b of values(k[1])) o.push([...a, ...b]); return o;
  };
  const bad = new Map();
  let n = 0;
  for (const f of fns) {
    const list = cases(f);
    if (!list) continue;
    for (const a of list) {
      if (f.kinds[0] === 'a') { const W = new Int32Array(w.memory.buffer); for (let i = 0; i < 16; i++) { W[i] = 0; it.ctx.wi_M[i] = 0; } }
      let wr, ir, wt = '', itp = '';
      try { wr = w[f.name](...a); } catch (e) { wt = String(e.message); }
      ir = it.exports[f.name](...a);
      itp = it.ctx.wi_trapped();
      let agree;
      if (wt || itp) agree = !!wt === !!itp;
      else if (f.result === 'i32') agree = (wr | 0) === (ir | 0);
      else if (f.result) agree = same(wr, ir);
      else agree = words(8).join() === iwords(8).join();
      if (!agree && !(bad.get(f.name) || []).length) bad.set(f.name, []);
      if (!agree && bad.get(f.name).length < 3) bad.get(f.name).push(`${f.name}(${a.map(show).join(', ')}): wasm ${wt ? 'trap ' + wt : f.result ? show(wr) : words(8).join(' ')}, interpreter ${itp ? 'trap ' + itp : f.result ? show(ir) : iwords(8).join(' ')}`);
      n++;
    }
  }
  for (const [name, m] of bad) ok(false, `${name}: ${m.join('; ')}`);
  ok(bad.size === 0, `ops: ${n} calls of ${fns.length} exports agree with the engine's WebAssembly, traps included (${bad.size} ops disagree)`);
  // Calls in sequence, on one instance: globals carry, memory.grow grows the words, copy and fill move bytes.
  ({ w, it } = fresh());
  const seq = [['globals', [3]], ['globals', [-100]], ['mem', [1]], ['mem', [2]], ['mem', [0]], ['mem', [9]], ['fill', [10, 0xab, 13]],
    ['copy', [3, 9, 12]], ['copy', [104, 100, 9]], ['copy', [100, 103, 9]], ['fill', [65536 * 3 + 5, 7, 6]]];
  const got = [];
  for (const [name, a] of seq) {
    const x = w[name](...a), y = it.exports[name](...a);
    got.push(x === undefined ? 'void' : x, y === undefined ? 'void' : y);
  }
  const all = (k) => { const W = new Int32Array(w.memory.buffer), o = []; for (let i = 0; i < 64; i++) o.push(k ? it.ctx.wi_M[i] | 0 : W[i]); return o.join(); };
  ok(got.every((x, i) => i % 2 || x === got[i + 1]) && all(0) === all(1), `globals, memory.size, grow (past the maximum too), copy (both directions, overlapping) and fill agree: ${got.join(' ')}`);
  ok(it.ctx.wi_pages === 4 && it.ctx.wi_M[(65536 * 3 + 8) >> 2] === 0x070707 && w.memory.buffer.byteLength === 4 * 65536, `the grown memory is there, words written: ${it.ctx.wi_pages} pages`);
  ok(it.exports.call_indirect(5, 1) === 25 && it.exports.call_indirect(5, 2) === -5, 'call_indirect reaches the table\'s functions');
  for (const [name, a, trap] of [['call_indirect', [5, 0], 'indirect call to a null'], ['call_indirect', [5, 9], 'indirect call to a null'],
    ['call_indirect_wrong', [1], 'signature mismatch'], ['unreachable', [], 'unreachable'], ['i32.div_s', [1, 0], 'divide by zero'],
    ['i32.div_s', [-0x80000000, -1], 'integer overflow'], ['i32.trunc_f64_s', [NaN], 'invalid conversion'], ['i64.div_s', [1, 0, 0, 0], 'divide by zero']]) {
    it.exports[name](...a);
    ok(it.ctx.wi_trapped().includes(trap), `${name}(${a.join(', ')}) traps: ${it.ctx.wi_trapped()}`);
  }
  ok(it.exports.fact(10) === 3628800 && it.ctx.wi_trapped() === '', 'and the next call works, with no trap left over');
}

if (on('memforms')) {
  // tests/memforms.c, as tools/words.mjs runs it: every load, store, float, bits, copy, fill and grow path.
  const buf = fs.readFileSync(path.join(out, 'memforms.wasm'));
  const w = new WebAssembly.Instance(new WebAssembly.Module(buf), {}).exports;
  const it = instantiate(buf);
  const names = it.m.exports.filter((e) => e.kind === 0).map((e) => e.name);
  ok(names.length >= 4, `memforms exports: ${names.join(' ')}`);
  // The same arguments into both: addresses inside a scratch area, each kind of each switch, values of each type. An i64
  // goes to the interpreter as a number (so within 2^53), and its i64 result's high word is wi_hi.
  const base = 4096;
  let n = 0;
  const bad = [];
  const cmpMem = () => { const W = new Int32Array(w.memory.buffer); for (let i = 0; i < W.length; i++) if (W[i] !== (it.ctx.wi_M[i] | 0)) return i; return -1; };
  const call = (name, a) => {
    const t = it.m.types[it.m.funcs[it.m.exports.find((e) => e.name === name).index - it.m.imports.length].type];
    let wr, wt = '';
    try { wr = w[name](...a.map((x, i) => (t.params[i] === 'i64' ? BigInt(x) : x))); } catch (e) { wt = e.message; }
    const ir = it.exports[name](...a), trap = it.ctx.wi_trapped();
    n++;
    let agree = !!wt === !!trap;
    if (agree && !wt && typeof wr === 'bigint') agree = BigInt.asIntN(64, wr) === BigInt.asIntN(64, i64of([ir | 0, it.ctx.wi_hi | 0]));
    else if (agree && !wt && t.results.length) agree = t.results[0] === 'i32' ? (wr | 0) === (ir | 0) : same(wr, ir);
    const at = cmpMem();
    if (!agree || at >= 0) bad.push(`${name}(${a.map(show).join(', ')}): wasm ${wt || show(wr)}, interpreter ${trap || show(ir)}${at >= 0 ? ` (word ${at} differs)` : ''}`);
  };
  for (const off of [0, 1, 2, 3, 5]) {
    for (let k = 0; k < 10; k++) for (const v of [0x11223344556677, -2, 255, -129, 0x7fff, -(2 ** 52) + 3]) {
      call('st', [base + off, k, v]);
      call('ld', [base + off, k]);
    }
    for (let k = 0; k < 4; k++) for (const v of [1.5, -0, 3.4e38, 1e-40, NaN, 1 / 3, -1e300, 5e-324]) {
      call('stf', [base + off, k, v]);
      call('ldf', [base + off, k]);
    }
  }
  for (const name of names) {
    if (['st', 'ld', 'stf', 'ldf', 'grow'].includes(name)) continue;
    const t = it.m.types[it.m.funcs[it.m.exports.find((e) => e.name === name).index - it.m.imports.length].type];
    for (const a of [[base, 1, 7, 2], [base + 3, 2, 300, 1], [base + 1, 0, 1, 9], [base + 2, 5, 17, 3], [base + 7, 3, 2, 0]]) call(name, a.slice(0, t.params.length));
  }
  if (names.includes('grow')) for (const p of [1, 2, 0]) call('grow', [p]);
  ok(!bad.length, `memforms: ${n} calls agree on results and every word of memory${bad.length ? ': ' + bad.slice(0, 4).join('; ') : ''}`);
}

if (on('cpu')) {
  const buf = fs.readFileSync(path.join(out, 'js1cpu.wasm'));
  const w = new WebAssembly.Instance(new WebAssembly.Module(buf), {}).exports;
  const it = instantiate(buf);
  for (const n of [0, 1, 7, 1000, 100000]) ok((w.loop(n) | 0) === (it.exports.loop(n) | 0), `loop(${n}): ${w.loop(n) >>> 0} on both`);
  for (const n of [0, 1, 300, 1000, 5000]) ok((w.rec(n) | 0) === (it.exports.rec(n) | 0), `rec(${n}), a recursion ${n} deep: ${w.rec(n) >>> 0} on both`);
  // The depth the interpreter's own limit allows: the call traps cleanly past it, and the next call works.
  let lo = 1000, hi = 4000000;
  while (lo < hi) { const mid = Math.ceil((lo + hi) / 2); it.exports.rec(mid); if (it.ctx.wi_trapped()) hi = mid - 1; else lo = mid; }
  it.exports.rec(lo + 1);
  const trapped = it.ctx.wi_trapped();
  ok(lo >= 100000 && trapped === 'call stack exhausted', `the deepest recursion is ${lo} (wi_LIMIT ${it.ctx.wi_LIMIT} registers), and one deeper traps: ${trapped}`);
  ok((it.exports.rec(1000) | 0) === (w.rec(1000) | 0) && it.ctx.wi_trapped() === '', 'after that trap the next call works');
  console.log('measure depth', lo);
}

if (on('frames')) {
  // tests/frames.c's module with every import a stub that records its calls: it loads, binds the imports by name,
  // and its exports answer as on wasm.
  const buf = fs.readFileSync(path.join(out, 'frames.wasm'));
  const mk = (log) => { const cdom = new Proxy({}, { get: (_, k) => (...a) => { log.push(k + ':' + a.join(',')); return 0; } }); return { cdom }; };
  const wl = [], il = [];
  const w = new WebAssembly.Instance(new WebAssembly.Module(buf), mk(wl)).exports;
  const it = instantiate(buf, mk(il));
  ok(it.ctx.wi_missing === '', `every import bound by name: ${it.ctx.wi_missing || 'none missing'}`);
  ok(w.cd_layout() === it.exports.cd_layout() && w.cd_lits() === it.exports.cd_lits(), `cd_layout and cd_lits agree: ${w.cd_layout()} ${w.cd_lits()}`);
  w.cd_start(5); it.exports.cd_start(5);
  ok(wl.join('|') === il.join('|') && wl.length > 20, `cd_start makes the same ${wl.length} import calls with the same arguments`);
  const W = new Int32Array(w.memory.buffer);
  let diff = -1; for (let i = 0; i < W.length; i++) if (W[i] !== (it.ctx.wi_M[i] | 0)) { diff = i; break; }
  ok(diff < 0, `and leaves the same memory${diff < 0 ? '' : `: word ${diff}`}`);
  wl.length = il.length = 0;
  w.grow(3); it.exports.grow(3);
  let threw = false;
  try { w.trap(); } catch { threw = true; }
  it.exports.trap();
  ok(threw && wl.join('|') === il.join('|') && it.ctx.wi_trapped() === 'unreachable', `grow(3) and trap() make the same import calls, and trap() traps on both: ${it.ctx.wi_trapped()}`);
}

if (want.includes('profile')) {
  // Dynamic counts: tests/frames.c's cd_start with stub imports, and tests/js1cpu.c's loop(1e5) and rec(1000).
  const runs = [['frames.wasm', (x) => { x.cd_start(5); x.grow(5); }], ['js1cpu.wasm', (x) => { x.loop(100000); x.rec(1000); }]];
  const total = new Map();
  for (const [file, run] of runs) {
    const buf = fs.readFileSync(path.join(out, file));
    const imports = { cdom: new Proxy({}, { get: () => () => 0 }) };
    const it = instantiate(buf, imports, { profile: true });
    run(it.exports);
    const P = it.ctx.wi_P, code = L.numbered(it.low.code, it.order);
    let reg = 0, wasm = 0;
    const mine = new Map();
    for (const k of Object.keys(P)) {
      const pc = +k, c = P[k], op = it.order[code[pc]];
      reg += c; wasm += c * it.low.weight[pc];
      mine.set(op, (mine.get(op) || 0) + c);
    }
    // Each program weighs the same in the order: its counts as fractions of its own run.
    for (const [op, c] of mine) total.set(op, (total.get(op) || 0) + c / reg);
    console.log(`profile ${file}: ${reg} instructions run, standing for ${wasm} wasm instructions (${(reg / wasm).toFixed(3)})`);
  }
  const sorted = [...total].sort((a, b) => b[1] - a[1]);
  console.log('profile order', sorted.map(([o, c]) => `${o} ${c.toFixed(4)}`).join(', '));
  if (process.env.JS1_ORDER) fs.writeFileSync(process.env.JS1_ORDER, sorted.map(([o, c]) => `${o} ${Math.round(c / 2 * 1e6)}`).join('\n') + '\n');
  // Comparisons an instruction pays: for each op, the tests its dispatch runs before its handler.
  const order = L.opOrder(), sum = [...total.values()].reduce((a, b) => a + b, 0);
  const cost = (shape) => { let c = 0; for (const [o, n] of total) c += n * L.comparisons(order, o, shape); return (c / sum).toFixed(2); };
  console.log(`profile comparisons an instruction: ${L.SHAPES.map((s) => `${s} ${cost(s)}`).join(', ')}`);
}

console.log(fails ? `${fails} FAILED` : 'all ok');
process.exit(fails ? 1 : 0);
