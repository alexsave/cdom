// The memory pass of app.wasm.ie9.js (tools/legacy/build.mjs appWords): wasm2js output, rewritten from its AST to
// keep C's memory in words.js's plain Array of 32-bit words instead of an ArrayBuffer and typed views over it.
//
// wasm2js reaches memory in exactly these ways, and each is rewritten or refused:
// - HEAP8, HEAPU8, HEAP16, HEAPU16, HEAP32, HEAPU32, HEAPF32 and HEAPF64 indexed reads, and plain stores whose
//   value is not used. Unaligned accesses and i64 are already lowered to these by wasm2js (bytes, and pairs of
//   words). 32 bits go to W directly, the rest to words.js's accessors, in the typed array's own index units.
// - The helpers around them: the base64 data segments, the reinterpret scratch, memory.copy, memory.fill,
//   memory.size and memory.grow, the views' declarations, and the exported memory object. Each must be
//   token for token the text wasm2js 130 writes (KNOWN below), or the build fails: a changed helper is a change
//   to review, not to guess at.
// - Math.fround, which becomes words.js's software rounding.
// Anything else (a compound store, a store whose value is used, a view passed around, a helper not listed) fails
// the build, and so does any typed-array name left in the output (noTypedArrays).
import * as acorn from 'acorn';
import { parseSync, traverse, types as t, generate } from '@babel/core';

const VIEWS = ['HEAP8', 'HEAPU8', 'HEAP16', 'HEAPU16', 'HEAP32', 'HEAPU32', 'HEAPF32', 'HEAPF64'];
// Every name that would mean typed-array memory survived, in the minified output too.
export const TYPED = ['ArrayBuffer', 'SharedArrayBuffer', 'DataView', 'Int8Array', 'Uint8Array', 'Uint8ClampedArray',
  'Int16Array', 'Uint16Array', 'Int32Array', 'Uint32Array', 'Float32Array', 'Float64Array', 'BigInt64Array',
  'BigUint64Array', 'Float16Array', 'Atomics'];
// Math.fround becomes the identifier fround (words.js's), so only the member form would be a leftover, and the
// visitor replaces every one.
const LEFTOVERS = [...VIEWS, 'bufferView', 'buffer', 'scratchBuffer', 'i32ScratchView', 'f32ScratchView', 'f64ScratchView',
  'base64ReverseLookup', 'base64DecodeToExistingUint8Array', 'copyWithin', 'subarray', 'byteLength'];

// A statement's tokens, comments and layout dropped: what KNOWN compares.
const tokens = (src) => [...acorn.tokenizer(src, { ecmaVersion: 5 })].map((k) => `${k.type.label}:${k.value ?? ''}`).join(' ');

// wasm2js 130's helpers, each with what it becomes: '' drops it, any other text replaces it.
const KNOWN = [
  ['var bufferView;', ''],
  ["var base64ReverseLookup = new Uint8Array(123/*'z'+1*/);", ''],
  [`for (var i = 25; i >= 0; --i) {
    base64ReverseLookup[48+i] = 52+i; // '0-9'
    base64ReverseLookup[65+i] = i; // 'A-Z'
    base64ReverseLookup[97+i] = 26+i; // 'a-z'
  }`, ''],
  ["base64ReverseLookup[43] = 62; // '+'", ''],
  ["base64ReverseLookup[47] = 63; // '/'", ''],
  // Its calls (in initActiveSegments) become words_base64(offset, b64): see the visitor.
  [`function base64DecodeToExistingUint8Array(uint8Array, offset, b64) {
    var b1, b2, i = 0, j = offset, bLength = b64.length, end = offset + (bLength*3>>2) - (b64[bLength-2] == '=') - (b64[bLength-1] == '=');
    for (; i < bLength; i += 4) {
      b1 = base64ReverseLookup[b64.charCodeAt(i+1)];
      b2 = base64ReverseLookup[b64.charCodeAt(i+2)];
      uint8Array[j++] = base64ReverseLookup[b64.charCodeAt(i)] << 2 | b1 >> 4;
      if (j < end) uint8Array[j++] = b1 << 4 | b2 >> 2;
      if (j < end) uint8Array[j++] = b2 << 6 | base64ReverseLookup[b64.charCodeAt(i+3)];
    }
    return uint8Array;
  }`, ''],
  ['var scratchBuffer = new ArrayBuffer(16);', ''],
  ['var i32ScratchView = new Int32Array(scratchBuffer);', ''],
  ['var f32ScratchView = new Float32Array(scratchBuffer);', ''],
  ['var f64ScratchView = new Float64Array(scratchBuffer);', ''],
  // The scratch functions' calls are renamed to words.js's (SCRATCH): see the visitor.
  ['function wasm2js_scratch_load_i32(index) { return i32ScratchView[index]; }', ''],
  ['function wasm2js_scratch_store_i32(index, value) { i32ScratchView[index] = value; }', ''],
  ['function wasm2js_scratch_load_f64() { return f64ScratchView[0]; }', ''],
  ['function wasm2js_scratch_store_f64(value) { f64ScratchView[0] = value; }', ''],
  ['function wasm2js_scratch_load_f32() { return f32ScratchView[2]; }', ''],
  ['function wasm2js_scratch_store_f32(value) { f32ScratchView[2] = value; }', ''],
  [`function wasm2js_memory_copy(dest, source, size) {
    // TODO: traps on invalid things
    bufferView.copyWithin(dest, source, source + size);
  }`, 'function wasm2js_memory_copy(dest, source, size) { words_copy(dest, source, size); }'],
  [`function wasm2js_memory_fill(dest, value, size) {
    dest = dest >>> 0;
    size = size >>> 0;
    if (dest + size > bufferView.length) throw "trap: invalid memory.fill";
    bufferView.fill(value, dest, dest + size);
  }`, `function wasm2js_memory_fill(dest, value, size) {
    dest = dest >>> 0;
    size = size >>> 0;
    if (dest + size > WBYTES) throw "trap: invalid memory.fill";
    words_fill(dest, value, size);
  }`],
  // In asmFunc.
  ...VIEWS.map((v) => [`var ${v} = new ${{ HEAP8: 'Int8', HEAPU8: 'Uint8', HEAP16: 'Int16', HEAPU16: 'Uint16', HEAP32: 'Int32',
    HEAPU32: 'Uint32', HEAPF32: 'Float32', HEAPF64: 'Float64' }[v]}Array(buffer);`, '']),
  ['bufferView = HEAPU8;', ''],
  [`function __wasm_memory_size() {
  return buffer.byteLength >> 16;
 }`, 'function __wasm_memory_size() { return WBYTES / 65536 | 0; }'],
  [`function __wasm_memory_grow(pagesToAdd) {
  pagesToAdd = pagesToAdd | 0;
  var oldPages = __wasm_memory_size() | 0;
  var newPages = oldPages + pagesToAdd | 0;
  if ((oldPages < newPages) && (newPages < 65536)) {
   var newBuffer = new ArrayBuffer(newPages << 16);
   var newHEAP8 = new Int8Array(newBuffer);
   newHEAP8.set(HEAP8);
   HEAP8 = new Int8Array(newBuffer);
   HEAP16 = new Int16Array(newBuffer);
   HEAP32 = new Int32Array(newBuffer);
   HEAPU8 = new Uint8Array(newBuffer);
   HEAPU16 = new Uint16Array(newBuffer);
   HEAPU32 = new Uint32Array(newBuffer);
   HEAPF32 = new Float32Array(newBuffer);
   HEAPF64 = new Float64Array(newBuffer);
   buffer = newBuffer;
   bufferView = HEAPU8;
  }
  return oldPages;
 }`, `function __wasm_memory_grow(pagesToAdd) {
  pagesToAdd = pagesToAdd | 0;
  var oldPages = __wasm_memory_size() | 0;
  var newPages = oldPages + pagesToAdd | 0;
  if ((oldPages < newPages) && (newPages < 65536)) {
   words_grow(newPages - oldPages);
  }
  return oldPages;
 }`],
].map(([from, to]) => [tokens(from), to]);
const known = new Map(KNOWN);

// The exported memory: wasm2js's has grow and a buffer getter; this one has grow, the words (one Array for the
// app's life: growing pushes onto it) and the accessors the shim's word backend reads and writes through.
const MEMORY_FROM = tokens(`Object.create(Object.prototype, {
   "grow": {
    "value": __wasm_memory_grow
   },
   "buffer": {
    "get": function () {
     return buffer;
    }
   }
  })`);
const MEMORY_TO = `({ grow: __wasm_memory_grow, words: W, load8_u: load8_u, store8: store8, load_f32: load_f32,
  store_f32: store_f32, load_f64: load_f64, store_f64: store_f64 })`;

// wasm2js's reinterpret helpers by the name of words.js's.
const SCRATCH = Object.fromEntries(['load_i32', 'store_i32', 'load_f64', 'store_f64', 'load_f32', 'store_f32']
  .map((k) => [`wasm2js_scratch_${k}`, `scratch_${k}`]));

const expr = (src) => parseSync(src, { sourceType: 'script', configFile: false, babelrc: false }).program.body[0].expression;
const stmts = (src) => parseSync(src, { sourceType: 'script', configFile: false, babelrc: false }).program.body;

// A store's value is dropped: the assignment is a statement, or not the last of a sequence, or the last of one
// whose own value is dropped. A typed-array store evaluates to the value stored, before conversion, which no
// rewrite below keeps, so a store used as a value is refused.
function unused(p) {
  const up = p.parentPath;
  if (up.isExpressionStatement()) return true;
  if (up.isSequenceExpression()) {
    const all = up.node.expressions;
    return all[all.length - 1] !== p.node || unused(up);
  }
  return up.isForStatement() && p.key !== 'test';
}

// head: wasm2js output with its import lines cut (build.mjs wasm2js). words: the names words.js declares, none of
// which may already be in head. Returns the rewritten source.
export function memoryWords(head, words) {
  const ast = parseSync(head, { sourceType: 'script', configFile: false, babelrc: false });
  const used = new Set();
  traverse(ast, { Identifier(p) { if (p.isReferencedIdentifier() || p.isBindingIdentifier()) used.add(p.node.name); } });
  const clash = words.filter((n) => used.has(n));
  if (clash.length) throw new Error(`memory pass: words.js names already in the wasm2js output: ${clash.join(' ')}`);

  const fail = (p, why) => {
    throw new Error(`memory pass: ${why}: ${generate(p.node).code.slice(0, 200)}`);
  };
  let rewritten = 0, helpers = 0;
  // A statement wasm2js writes around memory: replaced or dropped as KNOWN says, or the declaration of the memory
  // itself (asmFunc's `var buffer = new ArrayBuffer(bytes)`, which becomes words_init(bytes)).
  function statement(p) {
    if (p.isVariableDeclaration() && p.node.declarations.length === 1) {
      const d = p.node.declarations[0];
      if (t.isIdentifier(d.id, { name: 'buffer' }) && t.isNewExpression(d.init) && t.isIdentifier(d.init.callee, { name: 'ArrayBuffer' })
        && d.init.arguments.length === 1 && t.isNumericLiteral(d.init.arguments[0])) {
        p.replaceWith(t.expressionStatement(t.callExpression(t.identifier('words_init'), [d.init.arguments[0]])));
        helpers++;
        return;
      }
    }
    const to = known.get(tokens(head.slice(p.node.start, p.node.end)));
    if (to === undefined) return;
    helpers++;
    if (to) p.replaceWithMultiple(stmts(to)); else p.remove();
  }
  traverse(ast, {
    Program(p) { for (const s of p.get('body')) statement(s); },
    FunctionDeclaration(p) {
      if (p.node.id.name !== 'asmFunc') return;
      for (const s of p.get('body.body')) statement(s);
    },
  });
  // The exported memory object, in asmFunc's return.
  traverse(ast, {
    ObjectProperty(p) {
      if (!t.isStringLiteral(p.node.key, { value: 'memory' }) && !t.isIdentifier(p.node.key, { name: 'memory' })) return;
      const v = p.node.value;
      if (tokens(head.slice(v.start, v.end)) !== MEMORY_FROM) fail(p, 'the exported memory object changed shape');
      p.get('value').replaceWith(expr(MEMORY_TO));
      helpers++;
    },
  });
  traverse(ast, {
    // HEAPxx[i] = v.
    AssignmentExpression(p) {
      const left = p.node.left;
      if (!t.isMemberExpression(left) || !t.isIdentifier(left.object) || !VIEWS.includes(left.object.name)) return;
      if (!left.computed) fail(p, 'a view property assigned');
      if (p.node.operator !== '=') fail(p, 'a compound store');
      if (!unused(p)) fail(p, 'a store whose value is used');
      const view = left.object.name, i = left.property, v = p.node.right;
      const call = (f) => t.callExpression(t.identifier(f), [i, v]);
      if (view === 'HEAP32' || view === 'HEAPU32') {
        p.replaceWith(t.assignmentExpression('=', t.memberExpression(t.identifier('W'), i, true), t.binaryExpression('|', v, t.numericLiteral(0))));
      } else {
        p.replaceWith(call({ HEAP8: 'store8', HEAPU8: 'store8', HEAP16: 'store16', HEAPU16: 'store16', HEAPF32: 'store_f32', HEAPF64: 'store_f64' }[view]));
      }
      rewritten++;
    },
    UpdateExpression(p) {
      const a = p.node.argument;
      if (t.isMemberExpression(a) && t.isIdentifier(a.object) && VIEWS.includes(a.object.name)) fail(p, 'an increment of memory');
    },
    // HEAPxx[i] read, and Math.fround.
    MemberExpression(p) {
      const o = p.node.object;
      if (t.isIdentifier(o, { name: 'Math' }) && !p.node.computed && t.isIdentifier(p.node.property, { name: 'fround' })) {
        p.replaceWith(t.identifier('fround'));
        return;
      }
      if (!t.isIdentifier(o) || !VIEWS.includes(o.name)) return;
      if (!p.node.computed) fail(p, 'a view property read');
      if (p.parentPath.isAssignmentExpression() && p.key === 'left') fail(p, 'a store the pass did not see');
      const i = p.node.property, view = o.name;
      const w = () => t.memberExpression(t.identifier('W'), i, true);
      if (view === 'HEAP32') p.replaceWith(w());
      else if (view === 'HEAPU32') p.replaceWith(t.binaryExpression('>>>', w(), t.numericLiteral(0)));
      else {
        p.replaceWith(t.callExpression(t.identifier({ HEAP8: 'load8_s', HEAPU8: 'load8_u', HEAP16: 'load16_s', HEAPU16: 'load16_u',
          HEAPF32: 'load_f32', HEAPF64: 'load_f64' }[view]), [i]));
      }
      rewritten++;
    },
    // A data segment, and the reinterpret helpers' calls.
    CallExpression(p) {
      const c = p.node.callee;
      if (t.isIdentifier(c, { name: 'base64DecodeToExistingUint8Array' })) {
        const a = p.node.arguments;
        if (a.length !== 3 || !t.isIdentifier(a[0], { name: 'bufferView' }) || !t.isNumericLiteral(a[1]) || !t.isStringLiteral(a[2])) {
          fail(p, 'a data segment of another shape');
        }
        p.replaceWith(t.callExpression(t.identifier('words_base64'), [a[1], a[2]]));
        helpers++;
      } else if (t.isIdentifier(c) && SCRATCH[c.name]) {
        p.get('callee').replaceWith(t.identifier(SCRATCH[c.name]));
      }
    },
  });
  // Nothing of the typed memory may be left: its views, its buffer, a helper the pass did not replace.
  const banned = new Set([...TYPED, ...LEFTOVERS, ...Object.keys(SCRATCH)]);
  const left = new Set();
  traverse(ast, { Identifier(p) { if (banned.has(p.node.name)) left.add(p.node.name); } });
  if (left.size) throw new Error(`memory pass: typed memory left in the wasm2js output: ${[...left].join(' ')}`);
  if (!rewritten || !helpers) throw new Error(`memory pass: nothing rewritten (${rewritten} accesses, ${helpers} helpers): not wasm2js output?`);
  return generate(ast, { comments: false }).code;
}

// The minified file names no typed array, view or buffer: a scan of its tokens, as build.mjs's es5() scans.
export function noTypedArrays(name, code) {
  const bad = new Set();
  for (const k of acorn.tokenizer(code, { ecmaVersion: 5 })) {
    if (k.type.label === 'name' && (TYPED.includes(k.value) || VIEWS.includes(k.value) || ['bufferView', 'copyWithin', 'subarray'].includes(k.value))) {
      bad.add(k.value);
    }
  }
  if (bad.size) throw new Error(`${name} still names typed-array memory: ${[...bad].join(' ')}`);
}
