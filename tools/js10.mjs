// The JavaScript 1.0 gate of js1c's output (docs/js1-compile-design.md, section 2): what Netscape 2.02 could not
// parse or did not have in docs/descent.md's measured table (on main), refused by AST node and by name. Self-contained
// (tools/legacy/js11.mjs is the frames shim's gate, with Alex's assumptions; this one is the measurements).
//   node tools/js10.mjs check <file.js>...   exit 1, naming each construct and its line, on any refusal
//   node tools/js10.mjs                      checks each rule on code planted for it; exit 1 on any failure
import * as acorn from 'acorn';
import fs from 'node:fs';
import { pathToFileURL } from 'node:url';
// Netscape 3.04's "--" at column 254: one rule, owned by the frames tier's gate (tools/legacy/js11.mjs).
import { seam } from './legacy/js11.mjs';

// Syntax: "no run" in Netscape 2.02's column (a syntax error kills the whole file).
const SYNTAX = {
  SwitchStatement: 'switch', DoWhileStatement: 'do-while', LabeledStatement: 'a label', TryStatement: 'try',
  ThrowStatement: 'throw', ObjectExpression: 'an object literal', ArrayExpression: 'an array literal',
  FunctionExpression: 'a function expression', DebuggerStatement: 'debugger', WithStatement: 'with',
};
// Globals Netscape 2.02 lacks ("DIED(start)" or absent): NaN, Infinity, undefined and isNaN, arguments (f.arguments
// exists), Function (new Function died), setInterval, the URI functions; and what is newer still.
const GLOBALS = new Set(['undefined', 'NaN', 'Infinity', 'isNaN', 'isFinite', 'arguments', 'Function', 'setInterval',
  'encodeURIComponent', 'decodeURIComponent', 'encodeURI', 'decodeURI', 'Image', 'screen', 'JSON', 'Promise', 'Symbol',
  'Map', 'Set', 'globalThis', 'RegExp', 'Error', 'ArrayBuffer', 'Int32Array', 'Uint8Array', 'Float64Array']);
// Members that died in Netscape 2.02: every Array method, prototype and constructor, hasOwnProperty, the string methods
// of 1.1 and 1.2, Function's call and apply, and Math's ES6 functions; onerror (an error always raises the alert).
const MEMBERS = new Set(['join', 'sort', 'reverse', 'push', 'pop', 'shift', 'unshift', 'splice', 'slice', 'concat',
  'prototype', 'constructor', 'hasOwnProperty', 'charCodeAt', 'fromCharCode', 'split', 'substr', 'replace', 'match',
  'search', 'call', 'apply', 'bind', 'images', 'onerror', 'imul', 'fround', 'clz32', 'trunc', 'sign', 'log2', 'log10',
  'forEach', 'map', 'filter', 'keys', 'create', 'defineProperty', 'toFixed', 'getElementById',
  'createElement', 'appendChild', 'innerHTML', 'addEventListener', 'attachEvent']);

function walk(node, visit, parent, depth) {
  if (!node || typeof node.type !== 'string') return;
  visit(node, parent, depth);
  const inner = node.type === 'FunctionDeclaration' || node.type === 'FunctionExpression' ? depth + 1 : depth;
  for (const k of Object.keys(node)) {
    if (k === 'loc') continue;
    const v = node[k];
    if (Array.isArray(v)) { for (const c of v) walk(c, visit, node, inner); }
    else if (v && typeof v.type === 'string') walk(v, visit, node, inner);
  }
}

// The constructs of src Netscape 2.02 lacks, one string each with its line, or [] when it has none.
export function lacks10(src) {
  const found = [];
  let tree;
  try { tree = acorn.parse(src, { ecmaVersion: 5, sourceType: 'script', locations: true }); } catch (e) {
    return [`not even ES5: ${e.message}`];
  }
  const add = (what, n) => found.push(`${what} (line ${n.loc.start.line})`);
  for (const f of seam(src)) found.push(f);
  walk(tree, (n, parent, depth) => {
    if (SYNTAX[n.type]) add(SYNTAX[n.type], n);
    if (n.type === 'FunctionDeclaration' && depth > 0) add('a nested function declaration', n);
    if (n.type === 'VariableDeclaration' && n.kind !== 'var') add(n.kind, n);
    if (n.type === 'Literal' && n.regex) add('a regex literal', n);
    if (n.type === 'Literal' && typeof n.value === 'string' && /\\u/.test(n.raw)) add('a \\u escape', n);
    if (n.type === 'BinaryExpression' && ['===', '!==', 'in', 'instanceof'].includes(n.operator)) add(n.operator, n);
    if (n.type === 'UnaryExpression' && ['typeof', 'void', 'delete'].includes(n.operator)) add(n.operator, n);
    if (n.type === 'Property' && n.kind !== 'init') add('a getter or setter', n);
    // new X().y and new X()(): a syntax error in Netscape 2 and 3 (new Date().getTime()).
    if ((n.type === 'MemberExpression' && n.object.type === 'NewExpression') ||
        (n.type === 'CallExpression' && n.callee.type === 'NewExpression')) add('a member of a new expression', n);
    // new of anything but a declared function, Object, Array or Date: new Function and new Image died.
    if (n.type === 'NewExpression' && n.callee.type === 'Identifier' && GLOBALS.has(n.callee.name)) add(`new ${n.callee.name}`, n);
    if (n.type === 'MemberExpression' && !n.computed && MEMBERS.has(n.property.name)) add(`.${n.property.name}`, n);
    // toString with a radix died; toString() is allowed.
    if (n.type === 'CallExpression' && n.callee.type === 'MemberExpression' && !n.callee.computed &&
        n.callee.property.name === 'toString' && n.arguments.length) add('toString(radix)', n);
    if (n.type === 'Identifier' && GLOBALS.has(n.name) &&
        !(parent && parent.type === 'MemberExpression' && parent.property === n && !parent.computed) &&
        !(parent && parent.type === 'Property' && parent.key === n)) add(n.name, n);
  }, null, 0);
  return found;
}

function selfTest() {
  let fails = 0;
  const ok = (c, m) => { if (!c) { fails++; console.log('FAIL', m); } else console.log('ok  ', m); };
  const refused = (src, what) => lacks10(src).some((f) => f.startsWith(what));
  for (const [src, what] of [
    ['if (typeof x == "number") f();', 'typeof'], ['x = void 0;', 'void'], ['delete o.k;', 'delete'],
    ['var o = {};', 'an object literal'], ['var a = [1];', 'an array literal'], ['var r = /a/;', 'a regex literal'],
    ['var f = function () {};', 'a function expression'], ['function f() { function g() {} }', 'a nested function declaration'],
    ['switch (a) { case 1: f(); }', 'switch'], ['do f(); while (a);', 'do-while'], ['x: for (;;) break x;', 'a label'],
    ['if (a === b) f();', '==='], ['if (a !== b) f();', '!=='], ['if ("k" in o) f();', 'in'],
    ['if (o instanceof F) f();', 'instanceof'], ['try { f(); } catch (e) {}', 'try'], ['function f() { throw 1; }', 'throw'],
    ['with (o) f();', 'with'], ['var t = new Date().getTime();', 'a member of a new expression'],
    ['var n = NaN;', 'NaN'], ['var i = Infinity;', 'Infinity'], ['var u = undefined;', 'undefined'],
    ['if (isNaN(x)) f();', 'isNaN'], ['function f() { return arguments.length; }', 'arguments'],
    ['var f = new Function("x", "return x");', 'Function'], ['var h = setInterval("f()", 9);', 'setInterval'],
    ['var p = s.split(",");', '.split'], ['var c = s.charCodeAt(0);', '.charCodeAt'], ['var s = a.join("");', '.join'],
    ['a.push(1);', '.push'], ['F.prototype.g = h;', '.prototype'], ['var m = Math.imul(a, b);', '.imul'],
    ['var r = s.replace("a", "b");', '.replace'], ['var h = n.toString(16);', 'toString(radix)'],
    ['var s = "\\u00e9";', 'a \\u escape'], ['var s = q.substr(1);', '.substr'], ['var f = Math.fround(x);', '.fround'],
    ['let x = 1;', 'not even ES5'], ['/*' + 'p'.repeat(249) + '*/x--;', '-- at column 254'],
  ]) ok(refused(src, what), `${src} is refused (${what})`);
  ok(lacks10(`var JM = new Object(), JA = new Array(4), d = new Date(), t;
function f(a, b) { var i, s = ""; t = d.getTime(); for (i = 0; i < 3; i++) { if (a == b || a != 0) s = s + "x"; }
  while (a > 0) { a = a >>> 1 | 0; if (a & 1) continue; break; }
  JM[a >> 2] = parseInt(s.substring(0, 2), 36) | 0; o.m = f; o[k](1); x = a ? Math.floor(b / 2) : (0/0);
  return s.charAt(1) + s.indexOf(",", i) + s.length + n.toString(); }
setTimeout("f(1, 2)", 0); window.cdomFactory = f;`).length === 0,
  'what Netscape 2.02 runs passes: new Object, Array and Date, var, for, while, if, ?:, ==, bitwise, charAt, indexOf, substring, parseInt, Math, a function as a value, o[k](), setTimeout with a string');
  console.log(fails ? `${fails} FAILED` : 'all ok');
  return fails;
}

const [cmd, ...files] = process.argv.slice(2);
const main = process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href;
if (!main) { /* imported: lacks10 alone */ } else if (cmd === 'check') {
  let bad = 0;
  for (const f of files) {
    const found = lacks10(fs.readFileSync(f, 'utf8'));
    if (found.length) { bad++; console.error(`js10: ${f} is not JavaScript 1.0: ${[...new Set(found)].slice(0, 12).join(', ')}`); }
  }
  process.exit(bad ? 1 : 0);
} else if (!cmd) process.exit(selfTest() ? 1 : 0);
else { console.error('usage: node tools/js10.mjs [check <file.js>...]'); process.exit(2); }
