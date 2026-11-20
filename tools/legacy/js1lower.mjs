// The build-time half of the frames tier's WebAssembly interpreter (docs/js1-interp-design.md): it reads an app's
// .wasm, lowers each function from wasm's stack machine to a register code for tools/legacy/interp.js, and writes the
// module file (app.wasm.js1.js) in JavaScript 1.0; it also assembles the interpreter itself (cdom.js1.js) from
// interp.js's hand-written handlers, numbered in the frequency order of js1ops.txt. tools/legacy/build.mjs calls it
// (app-js1, shim-js1); tools/js1interp.mjs tests it against the engine's own WebAssembly.
import fs from 'node:fs';
import path from 'node:path';

const here = path.dirname(new URL(import.meta.url).pathname);

// ---------------------------------------------------------------------------------------------------------------
// The register code's ops: name -> operand kinds. r a register of the frame, k an integer, f a float (patched into
// the code at load), t a position in the code, n a count of targets that follow (br_table).
const I32_RR = ['add', 'sub', 'mul', 'divs', 'divu', 'rems', 'remu', 'and', 'or', 'xor', 'shl', 'shrs', 'shru', 'rotl', 'rotr',
  'eq', 'ne', 'lts', 'ltu', 'gts', 'gtu', 'les', 'leu', 'ges', 'geu'];
const I32_RI = ['addi', 'muli', 'andi', 'ori', 'xori', 'shli', 'shrsi', 'shrui', 'eqi', 'nei', 'ltsi', 'ltui', 'gtsi', 'gtui',
  'lesi', 'leui', 'gesi', 'geui'];
const CMPS = ['eq', 'ne', 'lts', 'ltu', 'gts', 'gtu', 'les', 'leu', 'ges', 'geu'];
const I64_RR = ['add64', 'sub64', 'mul64', 'divs64', 'divu64', 'rems64', 'remu64', 'and64', 'or64', 'xor64', 'shl64', 'shrs64',
  'shru64', 'rotl64', 'rotr64', 'eq64', 'ne64', 'lts64', 'ltu64', 'gts64', 'gtu64', 'les64', 'leu64', 'ges64', 'geu64'];
const F_RR = ['fadd', 'fsub', 'fmul', 'fdiv', 'fadd32', 'fsub32', 'fmul32', 'fdiv32', 'fmin', 'fmax', 'fcopysign',
  'feq', 'fne', 'flt', 'fgt', 'fle', 'fge'];
const UNARY = ['eqz', 'clz', 'ctz', 'popcnt', 'ext8', 'ext16', 'eqz64', 'clz64', 'ctz64', 'popcnt64', 'ext8_64', 'ext16_64',
  'ext32_64', 'extends', 'extendu', 'fabs', 'fneg', 'fsqrt', 'fsqrt32', 'fceil', 'ffloor', 'ftrunc', 'fnearest', 'fdemote',
  'truncs', 'truncu', 'truncsats', 'truncsatu', 'trunc64s', 'trunc64u', 'truncsat64s', 'truncsat64u', 'cvtu', 'cvt32s',
  'cvt32u', 'cvt64s', 'cvt64u', 'cvt64s32', 'cvt64u32', 'bitsf32', 'f32bits', 'bitsf64', 'f64bits'];
const LOADS = ['ld', 'ld8u', 'ld8s', 'ld16u', 'ld16s', 'ldf32', 'ldf64', 'ld64', 'ld64_8s', 'ld64_8u', 'ld64_16s', 'ld64_16u',
  'ld64_32s', 'ld64_32u'];
const STORES = ['st', 'st8', 'st16', 'stf32', 'stf64', 'st64'];
export const OPS = {
  mov: 'rr', mov64: 'rr', const: 'rk', constf: 'rf', const64: 'rkk', gget: 'rk', gget64: 'rk', gset: 'kr', gset64: 'kr',
  select: 'rrrr', select64: 'rrrr', jmp: 't', jnz: 'rt', jz: 'rt', brtab: 'rn', call: 'tk', callimp: 'kkk',
  callind: 'rkkk', ret: 'r', ret64: 'r', ret0: '', enter: 'kkk', trap: 'k', memsize: 'r', memgrow: 'rr', memcopy: 'rrr',
  memfill: 'rrr',
};
for (const o of I32_RR) OPS[o] = 'rrr';
for (const o of I32_RI) OPS[o] = 'rrk';
for (const o of CMPS) { OPS['j' + o] = 'rrt'; OPS['j' + o + 'i'] = 'rkt'; }
for (const o of I64_RR) OPS[o] = 'rrr';
for (const o of F_RR) OPS[o] = 'rrr';
for (const o of UNARY) OPS[o] = 'rr';
for (const o of LOADS) OPS[o] = 'rrk';
for (const o of STORES) OPS[o] = 'rrk';

// ---------------------------------------------------------------------------------------------------------------
// The .wasm, read.
const T = { 0x7f: 'i32', 0x7e: 'i64', 0x7d: 'f32', 0x7c: 'f64' };
class Reader {
  constructor(buf, at = 0, end = buf.length) { this.b = buf; this.p = at; this.end = end; }
  u8() { if (this.p >= this.end) throw new Error('wasm: read past the end'); return this.b[this.p++]; }
  u32() { let r = 0, s = 0, x; do { x = this.u8(); r += (x & 127) * 2 ** s; s += 7; } while (x & 128); return r; }
  s32() { let r = 0n, s = 0n, x; do { x = this.u8(); r |= BigInt(x & 127) << s; s += 7n; } while (x & 128);
    if (s < 64n && (x & 64)) r -= 1n << s; return Number(BigInt.asIntN(32, r)); }
  s64() { let r = 0n, s = 0n, x; do { x = this.u8(); r |= BigInt(x & 127) << s; s += 7n; } while (x & 128);
    if (s < 64n && (x & 64)) r -= 1n << s; return BigInt.asIntN(64, r); }
  f32() { const v = this.b.readFloatLE(this.p); const bits = this.b.readInt32LE(this.p); this.p += 4; return [v, bits]; }
  f64() { const v = this.b.readDoubleLE(this.p); const lo = this.b.readInt32LE(this.p), hi = this.b.readInt32LE(this.p + 4); this.p += 8; return [v, lo, hi]; }
  name() { const n = this.u32(); const s = this.b.toString('utf8', this.p, this.p + n); this.p += n; return s; }
  type() { const t = T[this.u8()]; if (!t) throw new Error(`wasm: value type 0x${this.b[this.p - 1].toString(16)} is not one of i32, i64, f32, f64`); return t; }
}
// A constant expression's value: [type, lo, hi] for integers, [type, number] for floats.
function constExpr(r, globals) {
  const op = r.u8();
  let v;
  if (op === 0x41) v = { t: 'i32', lo: r.s32(), hi: 0 };
  else if (op === 0x42) { const x = r.s64(); v = { t: 'i64', lo: Number(BigInt.asIntN(32, x)), hi: Number(BigInt.asIntN(32, x >> 32n)) }; }
  else if (op === 0x43) { const [f, bits] = r.f32(); v = { t: 'f32', f, lo: bits, hi: 0 }; }
  else if (op === 0x44) { const [f, lo, hi] = r.f64(); v = { t: 'f64', f, lo, hi }; }
  else if (op === 0x23) { const g = globals[r.u32()]; if (!g || g.mut) throw new Error('wasm: a constant expression reads a global that is not an immutable one before it'); v = g.init; }
  else throw new Error(`wasm: constant expression op 0x${op.toString(16)}`);
  if (r.u8() !== 0x0b) throw new Error('wasm: a constant expression longer than one op');
  return v;
}
export function parseWasm(buf) {
  if (buf.readUInt32LE(0) !== 0x6d736100 || buf.readUInt32LE(4) !== 1) throw new Error('wasm: not a version 1 module');
  const m = { types: [], imports: [], funcs: [], table: null, elems: [], memory: null, globals: [], exports: [], start: -1, data: [] };
  const r = new Reader(buf, 8);
  let fnTypes = [];
  while (r.p < buf.length) {
    const id = r.u8(), size = r.u32(), end = r.p + size, s = new Reader(buf, r.p, end);
    if (id === 1) {
      for (let n = s.u32(); n--;) {
        if (s.u8() !== 0x60) throw new Error('wasm: a type that is not a function');
        const params = []; for (let k = s.u32(); k--;) params.push(s.type());
        const results = []; for (let k = s.u32(); k--;) results.push(s.type());
        if (results.length > 1) throw new Error('wasm: multiple results');
        m.types.push({ params, results });
      }
    } else if (id === 2) {
      for (let n = s.u32(); n--;) {
        const mod = s.name(), name = s.name(), kind = s.u8();
        if (kind !== 0) throw new Error(`wasm: import ${mod}.${name} is not a function (kind ${kind})`);
        m.imports.push({ mod, name, type: s.u32() });
      }
    } else if (id === 3) { for (let n = s.u32(); n--;) fnTypes.push(s.u32()); }
    else if (id === 4) {
      if (s.u32() !== 1 || s.u8() !== 0x70) throw new Error('wasm: one funcref table only');
      const f = s.u8(); m.table = { min: s.u32(), max: f & 1 ? s.u32() : -1 };
    } else if (id === 5) {
      if (s.u32() !== 1) throw new Error('wasm: one memory only');
      const f = s.u8(); if (f & ~1) throw new Error('wasm: shared or 64-bit memory');
      m.memory = { min: s.u32(), max: f & 1 ? s.u32() : -1 };
    } else if (id === 6) {
      for (let n = s.u32(); n--;) { const t = s.type(), mut = s.u8(); m.globals.push({ t, mut, init: constExpr(s, m.globals) }); }
    } else if (id === 7) {
      for (let n = s.u32(); n--;) { const name = s.name(), kind = s.u8(), index = s.u32(); m.exports.push({ name, kind, index }); }
    } else if (id === 8) m.start = s.u32();
    else if (id === 9) {
      for (let n = s.u32(); n--;) {
        const flags = s.u32();
        if (flags !== 0) throw new Error(`wasm: element segment flags ${flags} (only active segments of function indexes)`);
        const off = constExpr(s, m.globals), fs_ = []; for (let k = s.u32(); k--;) fs_.push(s.u32());
        m.elems.push({ offset: off.lo, funcs: fs_ });
      }
    } else if (id === 10) {
      const n = s.u32();
      if (n !== fnTypes.length) throw new Error('wasm: code and function sections differ');
      for (let i = 0; i < n; i++) {
        const sz = s.u32(), fend = s.p + sz, locals = [];
        for (let k = s.u32(); k--;) { const c = s.u32(), t = s.type(); for (let j = 0; j < c; j++) locals.push(t); }
        m.funcs.push({ type: fnTypes[i], locals, body: [s.p, fend] });
        s.p = fend;
      }
    } else if (id === 11) {
      for (let n = s.u32(); n--;) {
        const flags = s.u32();
        if (flags !== 0) throw new Error(`wasm: data segment flags ${flags} (only active segments of memory 0)`);
        const off = constExpr(s, m.globals), len = s.u32();
        m.data.push({ offset: off.lo, bytes: buf.subarray(s.p, s.p + len) }); s.p += len;
      }
    } else if (id !== 0 && id !== 12) throw new Error(`wasm: section ${id}`);
    r.p = end;
  }
  m.buf = buf;
  return m;
}

// ---------------------------------------------------------------------------------------------------------------
// The lowering. Each function's frame: its locals at registers 0 to L-1 (the parameters first), the value stack's
// slot h at register L + h. A stack entry is a slot, a local not read yet (local.get emits nothing), or a constant;
// it becomes a slot (is materialized) where a later op needs it there.
const BIN32 = { 0x46: 'eq', 0x47: 'ne', 0x48: 'lts', 0x49: 'ltu', 0x4a: 'gts', 0x4b: 'gtu', 0x4c: 'les', 0x4d: 'leu', 0x4e: 'ges', 0x4f: 'geu',
  0x6a: 'add', 0x6b: 'sub', 0x6c: 'mul', 0x6d: 'divs', 0x6e: 'divu', 0x6f: 'rems', 0x70: 'remu', 0x71: 'and', 0x72: 'or', 0x73: 'xor',
  0x74: 'shl', 0x75: 'shrs', 0x76: 'shru', 0x77: 'rotl', 0x78: 'rotr' };
const BIN64 = { 0x51: 'eq64', 0x52: 'ne64', 0x53: 'lts64', 0x54: 'ltu64', 0x55: 'gts64', 0x56: 'gtu64', 0x57: 'les64', 0x58: 'leu64',
  0x59: 'ges64', 0x5a: 'geu64', 0x7c: 'add64', 0x7d: 'sub64', 0x7e: 'mul64', 0x7f: 'divs64', 0x80: 'divu64', 0x81: 'rems64',
  0x82: 'remu64', 0x83: 'and64', 0x84: 'or64', 0x85: 'xor64', 0x86: 'shl64', 0x87: 'shrs64', 0x88: 'shru64', 0x89: 'rotl64', 0x8a: 'rotr64' };
// Float binary ops: [op, result type]; f32 and f64 compares are the same op on numbers.
const BINF = { 0x5b: ['feq', 'i32'], 0x5c: ['fne', 'i32'], 0x5d: ['flt', 'i32'], 0x5e: ['fgt', 'i32'], 0x5f: ['fle', 'i32'], 0x60: ['fge', 'i32'],
  0x61: ['feq', 'i32'], 0x62: ['fne', 'i32'], 0x63: ['flt', 'i32'], 0x64: ['fgt', 'i32'], 0x65: ['fle', 'i32'], 0x66: ['fge', 'i32'],
  0x92: ['fadd32', 'f32'], 0x93: ['fsub32', 'f32'], 0x94: ['fmul32', 'f32'], 0x95: ['fdiv32', 'f32'], 0x96: ['fmin', 'f32'], 0x97: ['fmax', 'f32'],
  0x98: ['fcopysign', 'f32'], 0xa0: ['fadd', 'f64'], 0xa1: ['fsub', 'f64'], 0xa2: ['fmul', 'f64'], 0xa3: ['fdiv', 'f64'], 0xa4: ['fmin', 'f64'],
  0xa5: ['fmax', 'f64'], 0xa6: ['fcopysign', 'f64'] };
// Unary ops: [op, result type]; null op is a move (the value is the same number).
const UN = { 0x45: ['eqz', 'i32'], 0x67: ['clz', 'i32'], 0x68: ['ctz', 'i32'], 0x69: ['popcnt', 'i32'], 0x50: ['eqz64', 'i32'],
  0x79: ['clz64', 'i64'], 0x7a: ['ctz64', 'i64'], 0x7b: ['popcnt64', 'i64'],
  0x8b: ['fabs', 'f32'], 0x8c: ['fneg', 'f32'], 0x8d: ['fceil', 'f32'], 0x8e: ['ffloor', 'f32'], 0x8f: ['ftrunc', 'f32'], 0x90: ['fnearest', 'f32'], 0x91: ['fsqrt32', 'f32'],
  0x99: ['fabs', 'f64'], 0x9a: ['fneg', 'f64'], 0x9b: ['fceil', 'f64'], 0x9c: ['ffloor', 'f64'], 0x9d: ['ftrunc', 'f64'], 0x9e: ['fnearest', 'f64'], 0x9f: ['fsqrt', 'f64'],
  0xa7: [null, 'i32'], 0xa8: ['truncs', 'i32'], 0xa9: ['truncu', 'i32'], 0xaa: ['truncs', 'i32'], 0xab: ['truncu', 'i32'],
  0xac: ['extends', 'i64'], 0xad: ['extendu', 'i64'], 0xae: ['trunc64s', 'i64'], 0xaf: ['trunc64u', 'i64'], 0xb0: ['trunc64s', 'i64'], 0xb1: ['trunc64u', 'i64'],
  0xb2: ['cvt32s', 'f32'], 0xb3: ['cvt32u', 'f32'], 0xb4: ['cvt64s32', 'f32'], 0xb5: ['cvt64u32', 'f32'], 0xb6: ['fdemote', 'f32'],
  0xb7: [null, 'f64'], 0xb8: ['cvtu', 'f64'], 0xb9: ['cvt64s', 'f64'], 0xba: ['cvt64u', 'f64'], 0xbb: [null, 'f64'],
  0xbc: ['f32bits', 'i32'], 0xbd: ['f64bits', 'i64'], 0xbe: ['bitsf32', 'f32'], 0xbf: ['bitsf64', 'f64'],
  0xc0: ['ext8', 'i32'], 0xc1: ['ext16', 'i32'], 0xc2: ['ext8_64', 'i64'], 0xc3: ['ext16_64', 'i64'], 0xc4: ['ext32_64', 'i64'] };
const SAT = { 0: ['truncsats', 'i32'], 1: ['truncsatu', 'i32'], 2: ['truncsats', 'i32'], 3: ['truncsatu', 'i32'],
  4: ['truncsat64s', 'i64'], 5: ['truncsat64u', 'i64'], 6: ['truncsat64s', 'i64'], 7: ['truncsat64u', 'i64'] };
const LOAD = { 0x28: ['ld', 'i32'], 0x29: ['ld64', 'i64'], 0x2a: ['ldf32', 'f32'], 0x2b: ['ldf64', 'f64'], 0x2c: ['ld8s', 'i32'], 0x2d: ['ld8u', 'i32'],
  0x2e: ['ld16s', 'i32'], 0x2f: ['ld16u', 'i32'], 0x30: ['ld64_8s', 'i64'], 0x31: ['ld64_8u', 'i64'], 0x32: ['ld64_16s', 'i64'], 0x33: ['ld64_16u', 'i64'],
  0x34: ['ld64_32s', 'i64'], 0x35: ['ld64_32u', 'i64'] };
const STORE = { 0x36: 'st', 0x37: 'st64', 0x38: 'stf32', 0x39: 'stf64', 0x3a: 'st8', 0x3b: 'st16', 0x3c: 'st8', 0x3d: 'st16', 0x3e: 'st' };
// Ops whose first register operand is read, not written.
const NODEST = new Set([...STORES, 'ret', 'ret64', 'memcopy', 'memfill', 'callind', 'brtab']);
const COMMUTES = new Set(['add', 'mul', 'and', 'or', 'xor', 'eq', 'ne']);
const MIRROR = { lts: 'gts', ltu: 'gtu', gts: 'lts', gtu: 'ltu', les: 'ges', leu: 'geu', ges: 'les', geu: 'leu', eq: 'eq', ne: 'ne' };
const INVERT = { eq: 'ne', ne: 'eq', lts: 'ges', ltu: 'geu', gts: 'les', gtu: 'leu', les: 'gts', leu: 'gtu', ges: 'lts', geu: 'ltu' };
export const TRAPS = ['unreachable', 'integer divide by zero', 'integer overflow', 'invalid conversion to integer',
  'indirect call to a null or out of range entry', 'indirect call signature mismatch', 'call stack exhausted'];
const sigKey = (t) => t.params.join(',') + '>' + t.results.join(',');

// Lowers module m: { code (numbers and floats), funcs (per function: entry, params, result), sigs, weight (per code
// position: the wasm instructions it stands for, for the counts of P6), stats }.
export function lower(m, opts = {}) {
  const fold = opts.fold !== false;
  const code = [], weight = [], floats = [];
  const sigIds = new Map(), sigOf = (ti) => { const k = sigKey(m.types[ti]); if (!sigIds.has(k)) sigIds.set(k, sigIds.size); return sigIds.get(k); };
  const nimp = m.imports.length;
  const ftype = (f) => (f < nimp ? m.types[m.imports[f].type] : m.types[m.funcs[f - nimp].type]);
  const entries = [], callPatches = [];
  const stats = { wasmOps: 0, regOps: 0, folded: 0, fused: 0, retargeted: 0 };
  let pendingWeight = 0;
  let last = null; // the last instruction emitted: { at, op, args, dest (position of its destination operand) }
  const emit = (op, ...args) => {
    const kinds = OPS[op];
    if (kinds === undefined) throw new Error(`js1lower: no op ${op}`);
    if (kinds !== 'rn' && kinds.length !== args.length) throw new Error(`js1lower: ${op} takes ${kinds.length} operands, not ${args.length}`);
    const at = code.length;
    code.push(op); weight.push(pendingWeight); pendingWeight = 0;
    for (const a of args) { code.push(a); weight.push(0); }
    stats.regOps++;
    last = { at, op, args, dest: kinds[0] === 'r' && !NODEST.has(op) && !op.startsWith('j') ? at + 1 : -1 };
    return at;
  };
  const unemit = () => { // the last instruction taken back, its weight kept for the next
    pendingWeight += weight[last.at];
    code.length = last.at; weight.length = last.at; stats.regOps--;
    const l = last; last = null; return l;
  };
  for (let fi = 0; fi < m.funcs.length; fi++) {
    const fn = m.funcs[fi], ty = m.types[fn.type];
    const ltypes = [...ty.params, ...fn.locals], L = ltypes.length;
    const stack = [];
    let maxH = 0;
    const ctrl = [];
    const r = new Reader(m.buf, fn.body[0], fn.body[1]);
    const entry = code.length;
    entries.push(entry);
    const enterAt = emit('enter', ty.params.length, fn.locals.length, 0);
    const is64 = (t) => t === 'i64';
    const slot = (i) => L + i;
    const push = (e) => { stack.push(e); if (stack.length > maxH) maxH = stack.length; };
    const pushSlot = (t) => push({ k: 's', t });
    // Entry i made a slot; returns its register.
    const materialize = (i) => {
      const e = stack[i];
      if (e.k === 's') return slot(i);
      if (e.k === 'l') emit(is64(e.t) ? 'mov64' : 'mov', slot(i), e.v);
      else constInto(slot(i), e);
      stack[i] = { k: 's', t: e.t };
      return slot(i);
    };
    const constInto = (reg, e) => {
      if (e.t === 'i64') emit('const64', reg, e.lo, e.hi);
      else if (e.t === 'i32') emit('const', reg, e.lo);
      else { const at = emit('constf', reg, 0); code[at + 2] = { f: e.f }; floats.push(at + 2); }
    };
    const materializeAll = () => { for (let i = 0; i < stack.length; i++) materialize(i); };
    // The register of entry i, as an operand: a local's own, a slot's, a constant's slot once put there.
    const reg = (i) => (stack[i].k === 'l' ? stack[i].v : materialize(i));
    const top = () => stack.length - 1;
    const pop = () => stack.pop();
    // Branch target positions: a label's patches are filled at its end; a loop's target is its start.
    const bind = (patches, at) => { for (const p of patches) code[p] = at; last = null; };
    const label = (depth) => ctrl[ctrl.length - 1 - depth];
    // Moves the top value into the label's result slot when it carries one; returns whether it did anything.
    const branchValue = (lab) => {
      if (lab.kind === 'loop' || !lab.result) return false;
      const want = slot(lab.h), i = top();
      if (stack[i].k === 's' && i === lab.h) return false;
      if (stack[i].k === 'c') constInto(want, stack[i]);
      else emit(is64(lab.result) ? 'mov64' : 'mov', want, reg(i));
      return true;
    };
    const jumpTo = (lab, op, ...args) => {
      const at = emit(op, ...args, -1);
      if (lab.kind === 'loop') code[at + args.length + 1] = lab.start;
      else lab.patches.push(at + args.length + 1);
    };
    const fuseCond = () => {
      // The condition on top: a compare we can fold into the jump, an eqz, or a plain register.
      const i = top(), e = stack[i];
      if (fold && last && e.k === 's' && last.args[0] === slot(i) && last.dest === last.at + 1) {
        const op = last.op;
        const base = op.endsWith('i') && I32_RI.includes(op) ? op.slice(0, -1) : op;
        if (CMPS.includes(base) || op === 'eqz') {
          const l = unemit(); pop(); stats.fused++;
          return { cmp: op === 'eqz' ? 'eqz' : base, imm: op !== base && op !== 'eqz', a: l.args[1], b: l.args[2] };
        }
      }
      const a = reg(i); pop();
      return { cmp: null, a };
    };
    // Emits a jump to lab taken when cond holds (sense true) or fails (false).
    const condJump = (c, sense, lab) => {
      if (!c.cmp) return jumpTo(lab, sense ? 'jnz' : 'jz', c.a);
      if (c.cmp === 'eqz') return jumpTo(lab, sense ? 'jz' : 'jnz', c.a);
      const k = sense ? c.cmp : INVERT[c.cmp];
      return jumpTo(lab, 'j' + k + (c.imm ? 'i' : ''), c.a, c.b);
    };
    // Code after an unconditional branch, until the end or else of the block it is in, is never run.
    let dead = 0; // nesting depth inside dead code; 0 is live
    const toDead = () => { dead = 1; };
    const blockType = () => {
      const b = m.buf[r.p];
      if (b === 0x40) { r.p++; return null; }
      if (T[b]) { r.p++; return T[b]; }
      const ti = r.s32(), t = m.types[ti];
      if (t.params.length) throw new Error('wasm: a block with parameters');
      return t.results[0] || null;
    };
    ctrl.push({ kind: 'fn', h: 0, result: ty.results[0] || null, patches: [] });
    const fnEnd = (lab) => {
      // The function's end: its result into the return.
      if (lab.result) {
        const i = top();
        emit(is64(lab.result) ? 'ret64' : 'ret', reg(i));
      } else emit('ret0');
    };
    while (r.p < r.end) {
      const op = r.u8();
      stats.wasmOps++;
      if (dead) {
        // Skip to the matching end or else, reading the immediates.
        if (op === 0x02 || op === 0x03 || op === 0x04) { blockType(); dead++; continue; }
        if (op === 0x0b || (op === 0x05 && dead === 1)) {
          if (op === 0x0b && dead > 1) { dead--; continue; }
          dead = 0;
          // fall through to handle end/else live, with the stack as at the block's entry
          const lab = ctrl[ctrl.length - 1];
          stack.length = lab.h;
          lab.deadEnd = true;
        } else { skipImm(r, op); continue; }
      }
      pendingWeight++;
      if (op === 0x00) { emit('trap', 0); toDead(); }
      else if (op === 0x01) { /* nop */ }
      else if (op === 0x02 || op === 0x03) {
        const result = blockType();
        materializeAll();
        const lab = { kind: op === 0x02 ? 'block' : 'loop', h: stack.length, result, patches: [], start: code.length };
        last = null;
        ctrl.push(lab);
      } else if (op === 0x04) {
        const result = blockType();
        const c = fuseCond();
        materializeAll();
        const lab = { kind: 'if', h: stack.length, result, patches: [], elsePatch: [] };
        ctrl.push(lab);
        // Jump to the else (or end) when the condition fails.
        const tmp = { kind: 'block', patches: [] };
        condJump(c, false, tmp);
        lab.elsePatch = tmp.patches;
        last = null;
      } else if (op === 0x05) {
        const lab = ctrl[ctrl.length - 1];
        if (!lab.deadEnd) {
          if (lab.result) materialize(lab.h);
          jumpTo(lab, 'jmp');
        }
        lab.deadEnd = false;
        stack.length = lab.h;
        bind(lab.elsePatch, code.length);
        lab.elsePatch = [];
      } else if (op === 0x0b) {
        const lab = ctrl.pop();
        if (lab.kind === 'fn') {
          if (!lab.deadEnd) fnEnd(lab);
          bind(lab.patches, code.length);
          if (lab.patches.length) { stack.length = 0; if (lab.result) pushSlot(lab.result); fnEnd(lab); }
          break;
        }
        if (!lab.deadEnd && lab.result) materialize(lab.h);
        stack.length = lab.h;
        if (lab.result) pushSlot(lab.result);
        bind(lab.patches, code.length);
        if (lab.elsePatch) bind(lab.elsePatch, code.length);
      } else if (op === 0x0c) {
        const lab = label(r.u32());
        if (lab.kind === 'fn') { if (lab.result) emit(is64(lab.result) ? 'ret64' : 'ret', reg(top())); else emit('ret0'); }
        else { branchValue(lab); jumpTo(lab, 'jmp'); }
        toDead();
      } else if (op === 0x0d) {
        const lab = label(r.u32());
        const v = top() - 1;
        const moves = lab.kind === 'fn' || (lab.kind !== 'loop' && lab.result && !(stack[v].k === 's' && v === lab.h));
        if (moves) {
          // A taken branch with a value (or a return): jump over the move when the condition fails.
          const c = fuseCond();
          const skip = { kind: 'block', patches: [] };
          condJump(c, false, skip);
          if (lab.kind === 'fn') { if (lab.result) emit(is64(lab.result) ? 'ret64' : 'ret', reg(top())); else emit('ret0'); }
          else { branchValue(lab); jumpTo(lab, 'jmp'); }
          bind(skip.patches, code.length);
        } else condJump(fuseCond(), true, lab);
      } else if (op === 0x0e) {
        const n = r.u32(), depths = [];
        for (let k = 0; k <= n; k++) depths.push(r.u32());
        const idx = reg(top()); pop();
        const at = emit('brtab', idx, n);
        for (let k = 0; k <= n; k++) { code.push(-1); weight.push(0); }
        last = null;
        // Each target: a label's position, or a stub that moves the value (or returns) first.
        const stubs = [];
        depths.forEach((d, k) => {
          const lab = label(d);
          if (lab.kind === 'loop') code[at + 3 + k] = lab.start;
          else if (lab.kind !== 'fn' && !lab.result) lab.patches.push(at + 3 + k);
          else stubs.push([k, lab]);
        });
        for (const [k, lab] of stubs) {
          code[at + 3 + k] = code.length;
          if (lab.kind === 'fn') { if (lab.result) emit(is64(lab.result) ? 'ret64' : 'ret', reg(top())); else emit('ret0'); }
          else { branchValue(lab); jumpTo(lab, 'jmp'); }
        }
        toDead();
      } else if (op === 0x0f) {
        const lab = ctrl[0];
        if (lab.result) emit(is64(lab.result) ? 'ret64' : 'ret', reg(top())); else emit('ret0');
        toDead();
      } else if (op === 0x10 || op === 0x11) {
        let f = -1, sig = 0, t;
        if (op === 0x10) { f = r.u32(); t = ftype(f); }
        else { const ti = r.u32(); if (r.u8() !== 0) throw new Error('wasm: call_indirect on a table other than 0'); t = m.types[ti]; sig = sigOf(ti); }
        let tableReg = -1;
        if (op === 0x11) { tableReg = reg(top()); pop(); }
        const n = t.params.length, base = stack.length - n;
        for (let i = base; i < stack.length; i++) materialize(i);
        // The callee's frame starts at the first argument's slot; for an import, the frame size of this function
        // (patched at the end, when it is known) places a call back into C above it.
        if (op === 0x11) { const at = emit('callind', tableReg, sig, slot(base), 0); callPatches.push(['fs', at + 4]); }
        else if (f < nimp) {
          const at = emit('callimp', f, slot(base), 0); callPatches.push(['fs', at + 3]);
        } else { const at = emit('call', -1, slot(base)); callPatches.push(['fn', at + 1, f - nimp]); }
        stack.length = base;
        if (t.results.length) pushSlot(t.results[0]);
        last = null;
      } else if (op === 0x1a) { pop(); }
      else if (op === 0x1b || op === 0x1c) {
        if (op === 0x1c) { const k = r.u32(); for (let j = 0; j < k; j++) r.type(); }
        const c = reg(top()); const b = reg(top() - 1); const a = reg(top() - 2);
        const t = stack[top() - 1].t;
        stack.length -= 3; pushSlot(t);
        emit(is64(t) ? 'select64' : 'select', slot(top()), a, b, c);
      } else if (op === 0x20) { const x = r.u32(); push({ k: 'l', v: x, t: ltypes[x] }); }
      else if (op === 0x21 || op === 0x22) {
        const x = r.u32(), i = top(), e = stack[i];
        // Readers of local x still on the stack read its old value: they become slots first.
        let hazard = false;
        for (let j = 0; j < i; j++) if (stack[j].k === 'l' && stack[j].v === x) { materialize(j); hazard = true; }
        if (fold && !hazard && e.k === 's' && last && last.dest === last.at + 1 && last.args[0] === slot(i) && code.length === last.at + 1 + last.args.length) {
          code[last.dest] = x; last.args[0] = x; stats.retargeted++;
        } else if (e.k === 'c') constInto(x, e);
        else if (!(e.k === 'l' && e.v === x)) emit(is64(e.t) ? 'mov64' : 'mov', x, reg(i));
        pop();
        if (op === 0x22) push({ k: 'l', v: x, t: ltypes[x] });
        if (op === 0x21) last = null;
      } else if (op === 0x23) { const g = r.u32(); pushSlot(m.globals[g].t); emit(is64(m.globals[g].t) ? 'gget64' : 'gget', slot(top()), g); }
      else if (op === 0x24) { const g = r.u32(); emit(is64(m.globals[g].t) ? 'gset64' : 'gset', g, reg(top())); pop(); last = null; }
      else if (LOAD[op]) {
        r.u32(); const off = r.u32();
        const a = reg(top()); pop(); pushSlot(LOAD[op][1]);
        emit(LOAD[op][0], slot(top()), a, off);
      } else if (STORE[op]) {
        r.u32(); const off = r.u32();
        const v = reg(top()), a = reg(top() - 1);
        stack.length -= 2;
        emit(STORE[op], a, v, off);
        last = null;
      } else if (op === 0x3f) { r.u8(); pushSlot('i32'); emit('memsize', slot(top())); }
      else if (op === 0x40) { r.u8(); const a = reg(top()); pop(); pushSlot('i32'); emit('memgrow', slot(top()), a); last = null; }
      else if (op === 0x41) push({ k: 'c', t: 'i32', lo: r.s32(), hi: 0 });
      else if (op === 0x42) { const x = r.s64(); push({ k: 'c', t: 'i64', lo: Number(BigInt.asIntN(32, x)), hi: Number(BigInt.asIntN(32, x >> 32n)) }); }
      else if (op === 0x43) { const [f] = r.f32(); push({ k: 'c', t: 'f32', f }); }
      else if (op === 0x44) { const [f] = r.f64(); push({ k: 'c', t: 'f64', f }); }
      else if (BIN32[op]) {
        let name = BIN32[op];
        const i = top() - 1;
        let ea = stack[i], eb = stack[i + 1];
        if (ea.k === 'c' && eb.k !== 'c' && (COMMUTES.has(name) || MIRROR[name])) { [ea, eb] = [eb, ea]; stack[i] = ea; stack[i + 1] = eb; if (!COMMUTES.has(name)) name = MIRROR[name]; }
        let imm = null;
        if (eb.k === 'c') {
          if (name === 'sub') { name = 'add'; imm = -eb.lo | 0; }
          else if (OPS[name + 'i']) imm = eb.lo;
        }
        if (imm !== null) {
          const a = reg(i);
          stack.length = i; pushSlot('i32');
          emit(name + 'i', slot(i), a, imm);
        } else {
          const b = reg(i + 1), a = reg(i);
          stack.length = i; pushSlot('i32');
          emit(name, slot(i), a, b);
        }
      } else if (BIN64[op] || BINF[op]) {
        const [name, rt] = BIN64[op] ? [BIN64[op], BIN64[op].match(/^(eq|ne|lt|gt|le|ge)/) ? 'i32' : 'i64'] : BINF[op];
        const i = top() - 1;
        const b = reg(i + 1), a = reg(i);
        stack.length = i; pushSlot(rt);
        emit(name, slot(i), a, b);
      } else if (UN[op] || op === 0xfc) {
        let name, rt;
        if (op === 0xfc) {
          const sub = r.u32();
          if (SAT[sub]) [name, rt] = SAT[sub];
          else if (sub === 10) { r.u8(); r.u8(); const c = reg(top()), b = reg(top() - 1), a = reg(top() - 2); stack.length -= 3; emit('memcopy', a, b, c); last = null; continue; }
          else if (sub === 11) { r.u8(); const c = reg(top()), b = reg(top() - 1), a = reg(top() - 2); stack.length -= 3; emit('memfill', a, b, c); last = null; continue; }
          else throw new Error(`wasm: 0xfc ${sub} (memory.init and data.drop are not taken)`);
        } else [name, rt] = UN[op];
        const i = top();
        if (name === null) {
          // A move: the same number (i32.wrap_i64 keeps the low word, the promote and the i32 to f64 conversion
          // are exact).
          const e = stack[i];
          if (e.k === 'c') { const v = e.t === 'i64' ? { k: 'c', t: rt, lo: e.lo, hi: 0, f: e.lo } : { k: 'c', t: rt, lo: e.lo, f: e.t === 'i32' ? e.lo : e.f }; stack[i] = v; }
          else { stack[i] = { ...e, t: rt }; if (e.k === 's' && is64(e.t)) stack[i].t = rt; }
          continue;
        }
        const a = reg(i); pop(); pushSlot(rt);
        emit(name, slot(i), a);
      } else throw new Error(`wasm: op 0x${op.toString(16)} at ${r.p - 1}`);
    }
    const size = Math.max(1, L + maxH + 1);
    code[enterAt + 3] = size;
    fn.frame = size;
    for (const p of callPatches.filter((x) => x[0] === 'fs' && x[3] === undefined)) { code[p[1]] = size; p[3] = 1; }
  }
  for (const p of callPatches) if (p[0] === 'fn') code[p[1]] = entries[p[2]];
  const sigs = [];
  for (let f = 0; f < nimp + m.funcs.length; f++) sigs.push(sigOf(f < nimp ? m.imports[f].type : m.funcs[f - nimp].type));
  return { code, weight, floats, entries, sigs, stats };
}

function skipImm(r, op) {
  if (op === 0x0c || op === 0x0d || (op >= 0x20 && op <= 0x24) || op === 0x10) r.u32();
  else if (op === 0x0e) { const n = r.u32(); for (let k = 0; k <= n; k++) r.u32(); }
  else if (op === 0x11) { r.u32(); r.u8(); }
  else if (op === 0x1c) { const k = r.u32(); for (let j = 0; j < k; j++) r.u8(); }
  else if (op >= 0x28 && op <= 0x3e) { r.u32(); r.u32(); }
  else if (op === 0x3f || op === 0x40) r.u8();
  else if (op === 0x41) r.s32();
  else if (op === 0x42) r.s64();
  else if (op === 0x43) r.p += 4;
  else if (op === 0x44) r.p += 8;
  else if (op === 0xfc) { const s = r.u32(); if (s === 10) { r.u8(); r.u8(); } else if (s === 11) r.u8(); else if (s === 8) { r.u32(); r.u8(); } else if (s === 9) r.u32(); }
}

// ---------------------------------------------------------------------------------------------------------------
// The op numbers: js1ops.txt's order (the most frequent first; each line an op and its measured weight, in parts per
// million of the instructions run), then every other op by name, weight 0.
function opsList() {
  const listed = fs.readFileSync(path.join(here, 'js1ops.txt'), 'utf8').split('\n').map((l) => l.replace(/#.*/, '').trim()).filter(Boolean)
    .map((l) => { const [o, w] = l.split(/\s+/); return [o, +(w || 0)]; });
  for (const [o] of listed) if (!(o in OPS)) throw new Error(`js1ops.txt: no op ${o}`);
  if (new Set(listed.map((x) => x[0])).size !== listed.length) throw new Error('js1ops.txt names an op twice');
  const names = listed.map((x) => x[0]);
  return [...listed, ...Object.keys(OPS).filter((o) => !names.includes(o)).sort().map((o) => [o, 0])];
}
export const opOrder = () => opsList().map((x) => x[0]);
export const opWeights = () => opsList().map((x) => x[1]);

// The code as numbers: each op's name replaced by its number; floats stay numbers, at the positions in floats.
export function numbered(code, order) {
  const num = new Map(order.map((o, i) => [o, i]));
  return code.map((x) => (typeof x === 'string' ? num.get(x) : typeof x === 'object' ? x.f : x));
}

// ---------------------------------------------------------------------------------------------------------------
// The module's numbers, in the order wi_loadD reads them (tools/legacy/interp.js): the code first, at positions 0 on,
// so the decoded numbers are the code with no copy; then the module's shape; last, where the shape starts.
export function moduleNumbers(m, low, order) {
  const nums = numbered(low.code, order);
  const floatAt = new Set(low.floats);
  const out = nums.map((x, i) => (floatAt.has(i) ? 0 : x));
  const head = out.length;
  const nimp = m.imports.length, nf = nimp + m.funcs.length;
  const kindOf = (t) => (t === 'i32' ? 1 : t === 'i64' ? 3 : t === 'f32' ? 4 : 2);
  out.push(1); // the format's version
  out.push(nimp, m.funcs.length);
  // Per import: its parameter count and result kind; per function: its signature id; per defined function: its
  // entry, its parameters' kinds and its result's.
  for (const i of m.imports) { const t = m.types[i.type]; out.push(t.params.length, t.results.length ? kindOf(t.results[0]) : 0); }
  for (let f = 0; f < nf; f++) out.push(low.sigs[f]);
  for (let i = 0; i < m.funcs.length; i++) {
    const t = m.types[m.funcs[i].type];
    out.push(low.entries[i], t.params.length);
    for (const p of t.params) out.push(kindOf(p));
    out.push(t.results.length ? kindOf(t.results[0]) : 0);
  }
  // The table: its size, then runs of function indexes (a count of empty entries, a count of entries, the entries).
  const tsize = m.table ? m.table.min : 0;
  out.push(tsize);
  const table = new Array(tsize).fill(-1);
  for (const e of m.elems) e.funcs.forEach((f, k) => { table[e.offset + k] = f; });
  pushRuns(out, table, -1);
  // The globals: kind, low word, high word (a float as its bits).
  out.push(m.globals.length);
  for (const g of m.globals) out.push(kindOf(g.t), g.init.lo | 0, g.init.hi | 0);
  // The memory: its pages, its maximum (-1 for none), the count of words up to the last that is not 0, and runs of them.
  const pages = m.memory ? m.memory.min : 0;
  out.push(pages, m.memory ? m.memory.max : -1);
  const image = new Uint8Array(pages * 65536);
  for (const d of m.data) image.set(d.bytes, d.offset);
  const words = new Int32Array(image.buffer);
  let n = words.length; while (n && !words[n - 1]) n--;
  out.push(n);
  pushRuns(out, Array.from(words.subarray(0, n)), 0);
  // The float constants: their positions in the code and their bits.
  out.push(low.floats.length);
  for (const p of low.floats) { const b = Buffer.alloc(8); b.writeDoubleLE(low.code[p].f); out.push(p, b.readInt32LE(0), b.readInt32LE(4)); }
  out.push(m.start < 0 ? -1 : m.start);
  out.push(head);
  return out;
}
function pushRuns(out, list, empty) {
  const runs = [];
  let i = 0;
  while (i < list.length) {
    let z = 0; while (i < list.length && list[i] === empty) { z++; i++; }
    // Values up to the next three empty entries in a row (or the end): a shorter gap is cheaper written than skipped.
    const n = list.length, gap = (j) => list[j] === empty && (j + 1 >= n || list[j + 1] === empty) && (j + 2 >= n || list[j + 2] === empty);
    const at = i; while (i < n && !gap(i)) i++;
    runs.push([z, list.slice(at, i)]);
  }
  out.push(runs.length);
  for (const [z, vals] of runs) { out.push(z, vals.length); for (const v of vals) out.push(v); }
}

// The digit string: each number zigzagged (2n for n >= 0, -2n - 1 below), then 5 bits a character, the low bits
// first, a character of 32 or more carrying a continuation. Numbers reach 2^32 and so are done in doubles.
export const ALPHA = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
export function digits(nums) {
  let s = '';
  for (const x of nums) {
    if (!Number.isInteger(x) || x > 4294967295 || x < -4294967296) throw new Error(`js1lower: ${x} is no integer of the module`);
    let v = x >= 0 ? 2 * x : -2 * x - 1;
    do { const d = v % 32; v = (v - d) / 32; s += ALPHA[d + (v ? 32 : 0)]; } while (v);
  }
  return s;
}

// The module file, in JavaScript 1.0: the export wrappers, the factory, and the numbers as a digit string (form
// 'digits', shipped) or as statements (form 'statements', for the measurement of P2).
export function moduleFile(m, low, order, name, form = 'digits') {
  const nums = moduleNumbers(m, low, order);
  const nimp = m.imports.length;
  const lines = [`/* ${name}: generated from the .wasm by tools/legacy/js1lower.mjs for tools/legacy/interp.js (cdom.js1.js), do not edit. */`];
  lines.push('var wm_X = new Object();');
  const fnExports = m.exports.filter((e) => e.kind === 0);
  fnExports.forEach((e, k) => {
    if (e.index < nimp) throw new Error(`js1lower: export ${e.name} is an import`);
    lines.push(`function wm_e${k}(a, b, c, d, e, f, g, h) { return wi_call(${e.index - nimp}, a, b, c, d, e, f, g, h); }`);
  });
  // The import names, each "module.name" ended by a space (wi_load reads them with indexOf).
  const names = m.imports.map((i) => { if (/[ "\\]/.test(i.mod + i.name) || i.mod.includes('.')) throw new Error(`js1lower: import ${i.mod}.${i.name} has a space, a quote or a backslash`); return `${i.mod}.${i.name} `; }).join('');
  lines.push('function wm_factory(imports) {');
  if (form === 'digits') lines.push('  wi_load(wm_S, "' + names + '", imports);');
  else lines.push('  wm_N();', '  wi_loadN(wm_D, "' + names + '", imports);');
  for (const [k, e] of fnExports.entries()) lines.push(`  wm_X[${JSON.stringify(e.name)}] = wm_e${k};`);
  for (const e of m.exports.filter((x) => x.kind === 2)) lines.push(`  wm_X[${JSON.stringify(e.name)}] = wi_mem;`);
  lines.push('  wm_X["$trap"] = wi_trapped;', '  return wm_X;', '}');
  if (form === 'digits') {
    const s = digits(nums);
    // Lines of at most 1000 characters, joined by +, so no line is too long for an old parser.
    const parts = [];
    for (let i = 0; i < s.length; i += 1000) parts.push(s.slice(i, i + 1000));
    lines.push('var wm_S = "' + parts.join('"\n+ "') + '";');
  } else {
    lines.push('var wm_D = new Object();');
    lines.push('function wm_N() {', '  var d = wm_D;');
    for (let i = 0; i < nums.length; i += 16) lines.push('  ' + nums.slice(i, i + 16).map((x, j) => `d[${i + j}]=${x};`).join(''));
    lines.push('}');
  }
  lines.push('window.cdomFactory = wm_factory;');
  return lines.join('\n') + '\n';
}

// ---------------------------------------------------------------------------------------------------------------
// The dispatch's shape, as a tree over the op numbers lo to hi - 1 (in range, so a range of one op needs no test):
// { at, lo, hi, left, right } splits on op < at; { ops } tests each but the last with op ==, the last taking what is left.
// shape: 'chain' (one test an op, in number order), 'tree' (a balanced tree, leaves of up to 3), 'mixed' (a chain over
// the first CHAIN ops, then a balanced tree), 'weighted' (the search tree of least expected tests for js1ops.txt's
// weights, by Knuth's dynamic program).
export const CHAIN = 8;
export const SHAPES = ['weighted', 'mixed', 'tree', 'chain'];
export function dispatchTree(n, shape, weights) {
  const balanced = (lo, hi) => {
    if (hi - lo <= 3) return { ops: range(lo, hi) };
    const at = lo + ((hi - lo) >> 1);
    return { at, left: balanced(lo, at), right: balanced(at, hi) };
  };
  const range = (lo, hi) => { const o = []; for (let i = lo; i < hi; i++) o.push(i); return o; };
  if (shape === 'chain') return { ops: range(0, n) };
  if (shape === 'tree') return balanced(0, n);
  if (shape === 'mixed') return { ops: range(0, CHAIN), rest: balanced(CHAIN, n) };
  if (shape !== 'weighted') throw new Error(`js1lower: no dispatch shape ${shape}`);
  // cost[i][j]: the least expected tests for ops i to j - 1, each a leaf of its own; W the weight of the range.
  const w = weights.map((x) => x + 1e-6), W = (i, j) => { let t = 0; for (let k = i; k < j; k++) t += w[k]; return t; };
  const cost = [], cut = [];
  for (let i = 0; i <= n; i++) { cost.push(new Float64Array(n + 1)); cut.push(new Int32Array(n + 1)); }
  for (let len = 2; len <= n; len++) {
    for (let i = 0; i + len <= n; i++) {
      const j = i + len;
      let best = Infinity, at = -1;
      // Knuth's bound: the best cut lies between those of the two ranges one shorter.
      const from = len > 2 ? cut[i][j - 1] : i + 1, to = len > 2 ? cut[i + 1][j] : j - 1;
      for (let k = from; k <= to; k++) { const c = cost[i][k] + cost[k][j]; if (c < best) { best = c; at = k; } }
      cost[i][j] = best + W(i, j);
      cut[i][j] = at;
    }
  }
  const build = (i, j) => (j - i === 1 ? { ops: [i] } : { at: cut[i][j], left: build(i, cut[i][j]), right: build(cut[i][j], j) });
  return build(0, n);
}
// The tests that dispatch runs before op i's handler.
export function comparisons(order, op, shape = 'weighted', weights = opWeights()) {
  const i = order.indexOf(op);
  const walk = (t) => {
    if (t.at !== undefined) return 1 + walk(i < t.at ? t.left : t.right);
    const k = t.ops.indexOf(i);
    if (k >= 0) return Math.min(k + 1, t.rest ? k + 1 : t.ops.length - 1);
    return t.ops.length + walk(t.rest);
  };
  return walk(dispatchTree(order.length, shape, weights));
}

// The interpreter: interp.js with its dispatch written in. A handler is a section of interp.js that starts with a
// line //@ <op> <operand kinds> and runs to the next such line or //@end; the build checks each op has exactly one,
// with the kinds OPS gives. profile: each instruction counted by position in wi_P.
export function interpreter(order, { shape = 'weighted', profile = false, weights = opWeights() } = {}) {
  const src = fs.readFileSync(path.join(here, 'interp.js'), 'utf8');
  const start = src.indexOf('\n//@ ');
  const end = src.indexOf('\n//@end\n');
  if (start < 0 || end < 0) throw new Error('interp.js: no handlers');
  const handlers = new Map();
  let cur = null;
  for (const line of src.slice(start + 1, end).split('\n')) {
    const h = line.match(/^\/\/@ (\S+)\s*(\S*)$/);
    if (h) {
      if (handlers.has(h[1])) throw new Error(`interp.js: two handlers for ${h[1]}`);
      if (!(h[1] in OPS)) throw new Error(`interp.js: a handler for ${h[1]}, which is no op`);
      if ((h[2] || '') !== OPS[h[1]]) throw new Error(`interp.js: ${h[1]} takes ${OPS[h[1]] || 'nothing'}, its handler says ${h[2] || 'nothing'}`);
      cur = []; handlers.set(h[1], cur);
    } else if (cur) cur.push(line);
  }
  for (const o of order) if (!handlers.has(o)) throw new Error(`interp.js: no handler for ${o}`);
  for (const o of handlers.keys()) if (!order.includes(o)) throw new Error(`interp.js: ${o} has a handler and no number`);
  const body = (i, ind) => `${ind}/* ${order[i]} */\n` + handlers.get(order[i]).filter((l) => l.trim()).map((l) => ind + l).join('\n');
  const gen = (t, ind) => {
    if (t.at !== undefined) return `${ind}if (op < ${t.at}) {\n${gen(t.left, ind + '  ')}\n${ind}} else {\n${gen(t.right, ind + '  ')}\n${ind}}`;
    const last = t.rest ? -1 : t.ops.length - 1;
    let s = '';
    t.ops.forEach((i, k) => {
      if (k === last) s += k ? `${ind}} else {\n${body(i, ind + '  ')}\n${ind}}` : body(i, ind);
      else s += `${ind}${k ? '} else ' : ''}if (op == ${i}) {\n${body(i, ind + '  ')}\n`;
    });
    if (t.rest) s += `${ind}} else {\n${gen(t.rest, ind + '  ')}\n${ind}}`;
    return s;
  };
  let d = gen(dispatchTree(order.length, shape, weights), '    ');
  if (profile) d = `    if (wi_P[pc]) wi_P[pc]++;\n    else wi_P[pc] = 1;\n` + d;
  const out = src.slice(0, start + 1) + src.slice(end + '\n//@end\n'.length);
  if (!out.includes('    /*@dispatch*/\n')) throw new Error('interp.js: no /*@dispatch*/');
  return out.replace('    /*@dispatch*/\n', () => d + '\n') + (profile ? 'var wi_P = new Object();\n' : '');
}

