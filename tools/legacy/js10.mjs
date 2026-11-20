// The JavaScript 1.0 gate (docs/js1-transform-design.md, section 7): what Netscape 2.02 could not parse or run in
// docs/descent.md's probe (tests/probe-js1.html, "What each engine lacks"), on top of everything the 1.1 gate
// (js11.mjs) refuses. tools/legacy/build.mjs runs it on the frames tier's app file (app-js1) before and after it is
// minified; tools/js10.mjs checks each rule on code planted for it.
//
// Measured on Netscape 2.02 (no run: a syntax error; died: a run-time error):
// - no run: typeof, void, function expressions, nested functions, switch, do-while, labels, ===, in, instanceof, try,
//   throw, with, the literals, and a member of a new expression (new Date().getTime()). js11.mjs refuses all of them
//   but typeof and void (Netscape 3 has both); those two are refused here.
// - died: new Function, prototype and constructor on a user function, delete, hasOwnProperty, the arguments object
//   (f.arguments works), isNaN and the globals NaN, Infinity and undefined (js11.mjs's too: Netscape 3 lacks them), every Array method (join, sort, reverse,
//   push and the rest; new Array() itself works, as an object with number keys), charCodeAt, fromCharCode, split,
//   slice, substr, replace, toString with a radix, setInterval, document.images, new Image, javaEnabled, plugins,
//   screen; window.onerror does nothing.
// - worked, and so let through: new Object() and new Array() into a variable, number keys, the bitwise operators,
//   charAt, indexOf, lastIndexOf, substring, toUpperCase, escape, unescape, eval, setTimeout with a string, Math.floor,
//   abs, sqrt, pow, round and max, parseInt and parseFloat, for-in, ?: and the comma operator.
// Not measured, and refused as a precaution: $ in an identifier (Netscape's JavaScript 1.0 guide gives letters, digits
// and _ only).
import * as acorn from 'acorn';
import { lacks as lacks11 } from './js11.mjs';

const NAMES = new Set(['arguments', 'Function', 'setInterval', 'Image', 'screen']);
const MEMBERS = new Set(['prototype', 'constructor', 'hasOwnProperty', 'join', 'sort', 'reverse', 'push', 'pop', 'shift',
  'unshift', 'splice', 'slice', 'concat', 'charCodeAt', 'fromCharCode', 'split', 'substr', 'replace', 'images',
  'javaEnabled', 'plugins', 'onerror', 'callee']);

function walk(node, visit, parent) {
  if (!node || typeof node.type !== 'string') return;
  visit(node, parent);
  for (const k of Object.keys(node)) {
    if (k === 'loc' || k === 'start' || k === 'end') continue;
    const v = node[k];
    if (Array.isArray(v)) for (const c of v) walk(c, visit, node);
    else if (v && typeof v.type === 'string') walk(v, visit, node);
  }
}

// What src has that Netscape 2.02 lacks, one string each with its line, or []: the 1.1 gate's findings and these.
// allowed: names of MEMBERS the file uses behind a check, as js11.mjs takes them.
export function lacks10(src, { allowed = [] } = {}) {
  const found = lacks11(src, { allowed }), ok = new Set(allowed);
  let tree;
  try { tree = acorn.parse(src, { ecmaVersion: 5, sourceType: 'script', locations: true }); } catch (e) {
    return [...found, `not even ES5: ${e.message}`];
  }
  const add = (what, n) => found.push(`${what} (line ${n.loc.start.line})`);
  walk(tree, (n, parent) => {
    if (n.type === 'UnaryExpression' && (n.operator === 'typeof' || n.operator === 'void')) add(n.operator, n);
    if (n.type === 'UnaryExpression' && n.operator === 'delete') add('delete', n);
    if (n.type === 'MemberExpression') {
      if (!n.computed && MEMBERS.has(n.property.name) && !ok.has(n.property.name)) add(`.${n.property.name}`, n);
    }
    if (n.type === 'CallExpression' && n.callee.type === 'MemberExpression' && !n.callee.computed
      && n.callee.property.name === 'toString' && n.arguments.length) add('toString with a radix', n);
    if (n.type === 'Identifier') {
      const prop = parent && parent.type === 'MemberExpression' && parent.property === n && !parent.computed;
      const key = parent && parent.type === 'Property' && parent.key === n;
      if (!prop && !key && NAMES.has(n.name)) add(n.name, n);
      if (n.name.includes('$')) add('a $ in an identifier', n);
    }
  });
  return found;
}

// Throws, naming each construct, when src is not the JavaScript 1.0 Netscape 2.02 runs; returns src.
export function js10(name, src, options) {
  const found = lacks10(src, options);
  if (found.length) throw new Error(`${name} is not JavaScript 1.0: ${[...new Set(found)].slice(0, 12).join(', ')}`);
  return src;
}
