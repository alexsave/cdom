// The frames tier's language gates (docs/frames-design.md, section 10): js11 refuses what Netscape 3's JavaScript 1.1
// cannot parse or does not have, in tools/legacy/frames.js and in its minified output; js10 refuses, on top of that,
// what Netscape 2's JavaScript 1.0 is taken to lack, in the boot's first part (shim/boot.js). tools/legacy/build.mjs
// calls them; tools/js11.mjs checks each rule on code planted for it.
import * as acorn from 'acorn';

// Syntax, by AST node type: what JavaScript 1.2 to 1.4 added (switch, do-while, labels, delete, regex and object and
// array literals, function expressions and nested function declarations in 1.2; === and !== in 1.3; throw, try, in and
// instanceof in 1.4), and what ES5 added that acorn's ES5 reads (getters and setters).
const SYNTAX = {
  SwitchStatement: 'switch', DoWhileStatement: 'do-while', LabeledStatement: 'a label', TryStatement: 'try',
  ThrowStatement: 'throw', ObjectExpression: 'an object literal', ArrayExpression: 'an array literal',
  FunctionExpression: 'a function expression', DebuggerStatement: 'debugger', WithStatement: 'with',
};
// Globals of JavaScript 1.3 (and isNaN, which Netscape 3 lacks: docs/descent.md), and the methods of 1.2 and later (on any object: the gate cannot tell an Array's push
// from another's), and the DOM this tier has not. fromCharCode and charCodeAt are allowed where the caller says the
// file checks for them first. Not here, since 1.0 has them on another object: indexOf and lastIndexOf (a string's;
// an Array's are ES5), search and replace (location's; a string's are 1.2).
const GLOBALS = new Set(['undefined', 'NaN', 'Infinity', 'isNaN', 'JSON', 'Promise', 'Symbol', 'Map', 'Set', 'globalThis']);
export const METHODS = new Set(['push', 'pop', 'shift', 'unshift', 'splice', 'slice', 'concat', 'call', 'apply', 'charCodeAt',
  'fromCharCode', 'bind', 'forEach', 'map', 'filter', 'keys', 'create', 'defineProperty',
  'addEventListener', 'attachEvent', 'createElement', 'appendChild', 'insertBefore', 'removeChild', 'getElementById',
  'innerHTML', 'match', 'test', 'exec', 'toFixed', 'hasOwnProperty']);
// What Netscape 2 is taken to lack beyond that (Alex: "probably no Array, no typeof, no prototype"), for the boot.
const JS10_NAMES = new Set(['Array', 'prototype', 'split', 'join', 'Function', 'Object']);

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

// The constructs of src JavaScript 1.1 lacks, one string each (with its line), or [] when it has none. allowed: the
// names of METHODS the file uses behind a check. js10 adds JS10_NAMES and refuses typeof and void.
export function lacks(src, { allowed = [], js10 = false } = {}) {
  const ok = new Set(allowed), found = [];
  let tree;
  try { tree = acorn.parse(src, { ecmaVersion: 5, sourceType: 'script', locations: true }); } catch (e) {
    return [`not even ES5: ${e.message}`];
  }
  const at = (n) => `line ${n.loc.start.line}`;
  const add = (what, n) => found.push(`${what} (${at(n)})`);
  // Functions are declared at the top level only: depth counts the functions around a node.
  const nested = (n, depth) => {
    if (n.type === 'FunctionDeclaration' && depth > 0) add('a nested function declaration', n);
    const inner = n.type === 'FunctionDeclaration' || n.type === 'FunctionExpression' ? depth + 1 : depth;
    for (const k of Object.keys(n)) {
      if (k === 'loc') continue;
      const v = n[k];
      if (Array.isArray(v)) { for (const c of v) if (c && typeof c.type === 'string') nested(c, inner); }
      else if (v && typeof v.type === 'string') nested(v, inner);
    }
  };
  nested(tree, 0);
  walk(tree, (n, parent) => {
    if (SYNTAX[n.type]) add(SYNTAX[n.type], n);
    if (n.type === 'VariableDeclaration' && n.kind !== 'var') add(n.kind, n);
    if (n.type === 'Literal' && n.regex) add('a regex literal', n);
    if (n.type === 'Literal' && typeof n.value === 'string' && /\\u/.test(n.raw)) add('a \\u escape', n);
    if (n.type === 'BinaryExpression' && (n.operator === '===' || n.operator === '!==')) add(n.operator, n);
    if (n.type === 'BinaryExpression' && (n.operator === 'in' || n.operator === 'instanceof')) add(`the binary ${n.operator}`, n);
    if (n.type === 'UnaryExpression' && n.operator === 'delete') add('delete', n);
    if (js10 && n.type === 'UnaryExpression' && (n.operator === 'typeof' || n.operator === 'void')) add(n.operator, n);
    if (n.type === 'Property' && n.kind !== 'init') add('a getter or setter', n);
    // Measured (docs/descent.md): new Date().getTime() is a syntax error in Netscape 2 and 3, and parentheses are no
    // help in the gate's eyes (acorn drops them), so a new expression goes into a variable first.
    if (n.type === 'MemberExpression' && n.object.type === 'NewExpression') add('a member of a new expression', n);
    if (n.type === 'MemberExpression' && !n.computed) {
      const name = n.property.name;
      if (METHODS.has(name) && !ok.has(name)) add(`.${name}`, n);
      if (js10 && JS10_NAMES.has(name)) add(`.${name}`, n);
    }
    if (n.type === 'Identifier' && !(parent && parent.type === 'MemberExpression' && parent.property === n && !parent.computed)
      && !(parent && parent.type === 'Property' && parent.key === n)) {
      if (GLOBALS.has(n.name)) add(n.name, n);
      if (js10 && JS10_NAMES.has(n.name)) add(n.name, n);
    }
  });
  return found;
}

// Throws, naming each construct, when src is not JavaScript 1.1 (or 1.0, with js10); returns src.
export function js11(name, src, options) {
  const found = lacks(src, options);
  if (found.length) throw new Error(`${name} is not JavaScript ${options && options.js10 ? '1.0' : '1.1'}: ${[...new Set(found)].slice(0, 12).join(', ')}`);
  return src;
}
