// The JavaScript 1.0 gate (docs/js1-interp-design.md, section 7): what Netscape 2.02 was measured to lack
// (docs/descent.md, the probe's table and "What each engine lacks"), refused by construct with its line, in the frames
// tier's interpreter (tools/legacy/interp.js, built as cdom.js1.js) and in every module file the build writes. Where the
// probe measured a thing present, it passes; where it measured nothing either way, a global or a Math member is refused
// too, so the files use only what was seen to work. tools/legacy/build.mjs calls it; tools/js10.mjs checks each rule on
// planted code and gates the files it is given.
import * as acorn from 'acorn';

// Syntax Netscape 2.02 did not parse ("no run" in the table), by AST node type.
const SYNTAX = {
  SwitchStatement: 'switch', DoWhileStatement: 'do-while', LabeledStatement: 'a label', TryStatement: 'try',
  ThrowStatement: 'throw', ObjectExpression: 'an object literal', ArrayExpression: 'an array literal',
  FunctionExpression: 'a function expression', DebuggerStatement: 'debugger', WithStatement: 'with',
};
// Names that died at run time there (DIED(start)): properties and globals alike, by name, since the gate cannot tell
// whose a property is. split, slice, substr and replace are a string's; join, sort, reverse and the rest an Array's.
const DIED = new Set(['arguments', 'prototype', 'constructor', 'Array', 'Function', 'isNaN', 'NaN', 'Infinity', 'undefined',
  'charCodeAt', 'fromCharCode', 'split', 'slice', 'substr', 'replace', 'setInterval', 'Image', 'images', 'screen', 'onerror',
  'javaEnabled', 'plugins', 'hasOwnProperty', 'join', 'sort', 'reverse', 'push', 'pop', 'shift', 'unshift', 'splice',
  'concat', 'call', 'apply', 'callee', 'encodeURIComponent', 'getElementById', 'createElement', 'innerHTML']);
// Globals the probe saw work on Netscape 2.02: Math (floor, abs, sqrt, pow, round, max), parseInt, parseFloat, escape,
// unescape, eval, setTimeout with a string, new Object, new Date and getTime, and window, document, navigator and
// location as the page's.
const PRESENT = new Set(['Math', 'parseInt', 'parseFloat', 'escape', 'unescape', 'eval', 'setTimeout', 'Object', 'Date',
  'window', 'document', 'navigator', 'location']);
const MATH = new Set(['floor', 'abs', 'sqrt', 'pow', 'round', 'max']);

function children(n) {
  const out = [];
  for (const k of Object.keys(n)) {
    if (k === 'loc' || k === 'start' || k === 'end') continue;
    const v = n[k];
    if (Array.isArray(v)) { for (const c of v) if (c && typeof c.type === 'string') out.push(c); }
    else if (v && typeof v.type === 'string') out.push(v);
  }
  return out;
}
// The names a function (or the program) declares: its parameters, and every var and function declaration in it that
// is not inside a function of its own.
function declares(fn) {
  const names = new Set();
  if (fn.params) for (const p of fn.params) names.add(p.name);
  const visit = (n) => {
    if (n.type === 'VariableDeclarator') names.add(n.id.name);
    if (n.type === 'FunctionDeclaration' && n !== fn) { names.add(n.id.name); return; }
    if (n.type === 'FunctionExpression' && n !== fn) return;
    for (const c of children(n)) visit(c);
  };
  if (fn.type === 'Program') for (const s of fn.body) visit(s);
  else visit(fn.body);
  return names;
}
// The top-level names a script declares, for the other files of the same page (known, below).
export function declared(src) {
  return [...declares(acorn.parse(src, { ecmaVersion: 5, sourceType: 'script' }))];
}

// What src uses that JavaScript 1.0 lacks, one string each with its line, or [] when nothing. known: globals another
// script of the page declares (a module file calls the interpreter's wi_ functions).
export function lacks10(src, { known = [] } = {}) {
  const found = [];
  let tree;
  try { tree = acorn.parse(src, { ecmaVersion: 5, sourceType: 'script', locations: true }); } catch (e) {
    return [`not even ES5: ${e.message}`];
  }
  const add = (what, n) => found.push(`${what} (line ${n.loc.start.line})`);
  const globals = declares(tree), extra = new Set(known);
  const walk = (n, parent, scope, depth) => {
    if (SYNTAX[n.type]) add(SYNTAX[n.type], n);
    if (n.type === 'FunctionDeclaration' && depth > 0) add('a nested function declaration', n);
    if (n.type === 'VariableDeclaration' && n.kind !== 'var') add(n.kind, n);
    if (n.type === 'Literal' && n.regex) add('a regex literal', n);
    if (n.type === 'Literal' && typeof n.value === 'string' && /\\u/.test(n.raw)) add('a \\u escape', n);
    if (n.type === 'BinaryExpression' && (n.operator === '===' || n.operator === '!==')) add(n.operator, n);
    if (n.type === 'BinaryExpression' && (n.operator === 'in' || n.operator === 'instanceof')) add(`the binary ${n.operator}`, n);
    if (n.type === 'UnaryExpression' && ['delete', 'typeof', 'void'].includes(n.operator)) add(n.operator, n);
    if (n.type === 'Property' && n.kind !== 'init') add('a getter or setter', n);
    // new X(...).y and new X(...)(): a syntax error on Netscape 2 and 3.
    if (n.type === 'MemberExpression' && n.object.type === 'NewExpression') add('a member of a new expression', n);
    if (n.type === 'CallExpression' && n.callee.type === 'NewExpression') add('a call of a new expression', n);
    if (n.type === 'MemberExpression' && !n.computed) {
      const name = n.property.name;
      if (DIED.has(name)) add(`.${name}`, n);
      if (n.object.type === 'Identifier' && n.object.name === 'Math' && !MATH.has(name)) add(`Math.${name}, which was not measured`, n);
    }
    if (n.type === 'CallExpression' && n.callee.type === 'MemberExpression' && !n.callee.computed && n.callee.property.name === 'toString' && n.arguments.length)
      add('toString with a radix', n);
    if (n.type === 'Identifier' && !(parent && parent.type === 'MemberExpression' && parent.property === n && !parent.computed)
      && !(parent && parent.type === 'Property' && parent.key === n)
      && !(parent && (parent.type === 'FunctionDeclaration' || parent.type === 'FunctionExpression'))) {
      if (DIED.has(n.name)) add(n.name, n);
      else if (!scope.has(n.name) && !globals.has(n.name) && !extra.has(n.name) && !PRESENT.has(n.name)) add(`the global ${n.name}, neither declared nor measured`, n);
    }
    const inner = n.type === 'FunctionDeclaration' || n.type === 'FunctionExpression';
    const sc = inner ? declares(n) : scope;
    for (const c of children(n)) walk(c, n, sc, inner ? depth + 1 : depth);
  };
  walk(tree, null, new Set(), 0);
  return found;
}

// Throws, naming each construct, when src is not JavaScript 1.0; returns src.
export function js10(name, src, options) {
  const found = lacks10(src, options);
  if (found.length) throw new Error(`${name} is not JavaScript 1.0: ${[...new Set(found)].slice(0, 12).join(', ')}`);
  return src;
}
