// shim/cdom.strip, made from shim/cdom.min.js by tools/minshim.mjs: how tools/shimstrip.c cuts the
// minified shim down to the imports one module uses (cdom.mk, a bundled page). cdom's Node build makes
// the table; the app's build only reads it (C, no Node). A text table of byte ranges of cdom.min.js:
//
//   cdomstrip 1 <bytes of cdom.min.js> <units>
//   F <start> <end> <deps...>         a function declared in the shim's closure
//   V <start> <end> <group> <deps...> one declarator of a closure-level const or let (one whose
//                                     initializer may do something, not a literal or new X(literals), is in R)
//   G <start> <end>                   a const or let statement: cut once every declarator of it is a cut V
//   I <start> <end> <name> <deps...>  one property of start's imports.cdom
//
// The V of a group, and the I, are lists in source order joined by single commas: the cutter drops a
// cut entry with the comma after it, or, after the last entry kept, with the comma before it.
//   R <deps...>                       what the rest of the shim names: always kept
//
// deps are unit indexes (0-based, in line order). A unit is kept when R or a kept unit names it, or, an
// I, when the module imports cdom.<name>; every other unit's bytes are cut. A unit names a closure-level
// F or V when an identifier in it (outside units nested in it) resolves to that closure-level name:
// scopes are followed (params, var, let, const, function, class, catch), so a local that reuses a
// mangled name is not a reference.
import { createRequire } from 'node:module';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const top = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const acorn = createRequire(path.join(top, 'package.json'))('acorn');

const isFunction = (n) => n.type === 'FunctionDeclaration' || n.type === 'FunctionExpression' || n.type === 'ArrowFunctionExpression';

function children(node, visit) {
  for (const key of Object.keys(node)) {
    const value = node[key];
    if (Array.isArray(value)) {
      for (const v of value) {
        if (v && typeof v.type === 'string') visit(v, node, key);
      }
    } else if (value && typeof value.type === 'string') {
      visit(value, node, key);
    }
  }
}
// The names a pattern binds.
function bound(pattern, out) {
  if (!pattern) return out;
  switch (pattern.type) {
    case 'Identifier': out.add(pattern.name); break;
    case 'ObjectPattern': for (const p of pattern.properties) bound(p.type === 'RestElement' ? p.argument : p.value, out); break;
    case 'ArrayPattern': for (const e of pattern.elements) bound(e, out); break;
    case 'RestElement': bound(pattern.argument, out); break;
    case 'AssignmentPattern': bound(pattern.left, out); break;
  }
  return out;
}
// The var names declared in a function body, not crossing nested functions.
function varNames(node, out) {
  if (node.type === 'VariableDeclaration' && node.kind === 'var') {
    for (const d of node.declarations) bound(d.id, out);
  }
  children(node, (child) => {
    if (!isFunction(child)) varNames(child, out);
  });
  return out;
}
// The let, const, class and function names declared directly in a statement list.
function lexicalNames(list, out) {
  for (const s of list) {
    if (s.type === 'VariableDeclaration' && s.kind !== 'var') for (const d of s.declarations) bound(d.id, out);
    if ((s.type === 'FunctionDeclaration' || s.type === 'ClassDeclaration') && s.id) out.add(s.id.name);
  }
  return out;
}
// The names a node declares for its own scope, or null for a node that makes no scope.
function scopeOf(node) {
  if (isFunction(node)) {
    const names = new Set();
    for (const p of node.params) bound(p, names);
    if (node.type === 'FunctionExpression' && node.id) names.add(node.id.name);
    if (node.body.type === 'BlockStatement') {
      varNames(node.body, names);
      lexicalNames(node.body.body, names);
    }
    return names;
  }
  if (node.type === 'BlockStatement') return lexicalNames(node.body, new Set());
  if (node.type === 'StaticBlock') return lexicalNames(node.body, new Set());
  if (node.type === 'SwitchStatement') return lexicalNames(node.cases.flatMap((c) => c.consequent), new Set());
  if ((node.type === 'ForStatement' || node.type === 'ForInStatement' || node.type === 'ForOfStatement')) {
    const decl = node.type === 'ForStatement' ? node.init : node.left;
    const names = new Set();
    if (decl && decl.type === 'VariableDeclaration' && decl.kind !== 'var') for (const d of decl.declarations) bound(d.id, names);
    return names;
  }
  if (node.type === 'CatchClause') return bound(node.param, new Set());
  if (node.type === 'ClassExpression' && node.id) return new Set([node.id.name]);
  return null;
}
// An initializer that has no effect when it runs, so its declarator can go when nothing names it.
function pure(node) {
  if (!node) return true;
  switch (node.type) {
    case 'Literal': case 'FunctionExpression': case 'ArrowFunctionExpression': return true; // making a function does nothing
    case 'ArrayExpression': return node.elements.every(pure);
    case 'ObjectExpression': return node.properties.every((p) => p.type === 'Property' && !p.computed && pure(p.value)); // methods too
    case 'UnaryExpression': return node.operator !== 'delete' && pure(node.argument);
    case 'BinaryExpression': return pure(node.left) && pure(node.right);
    case 'NewExpression': return node.callee.type === 'Identifier'
      && ['TextDecoder', 'TextEncoder', 'Uint32Array', 'Uint8Array', 'Map', 'Set'].includes(node.callee.name)
      && node.arguments.every(pure);
    default: return false;
  }
}
export function stripTable(code) {
  if (!/^[\x00-\x7f]*$/.test(code)) {
    throw new Error('striptable: cdom.min.js is not ASCII, and cdom.strip counts bytes');
  }
  const ast = acorn.parse(code, { ecmaVersion: 'latest', sourceType: 'script' });
  const closure = ast.body[0].declarations[0].init.callee; // var cdom=(()=>{...})()
  const units = [];
  const byName = new Map(); // a closure-level name -> its F or V
  const alwaysKept = [];
  for (const s of closure.body.body) {
    if (s.type === 'FunctionDeclaration') {
      byName.set(s.id.name, units.push({ kind: 'F', start: s.start, end: s.end }) - 1);
    } else if (s.type === 'VariableDeclaration' && s.kind !== 'var') {
      const group = units.push({ kind: 'G', start: s.start, end: s.end }) - 1;
      for (const d of s.declarations) {
        const v = units.push({ kind: 'V', start: d.start, end: d.end, group }) - 1;
        if (d.id.type === 'Identifier') byName.set(d.id.name, v);
        if (d.id.type !== 'Identifier' || !pure(d.init)) alwaysKept.push(v); // it may do something as it runs
      }
    }
  }
  // start's imports: the object under the key cdom.
  let found = 0;
  (function find(node) {
    if (node.type === 'Property' && !node.computed && (node.key.name ?? node.key.value) === 'cdom'
      && node.value.type === 'ObjectExpression') {
      found++;
      const list = node.value.properties;
      list.forEach((p, i) => {
        units.push({ kind: 'I', start: p.start, end: p.end, name: p.key.name ?? p.key.value, own: [p.value.start, p.value.end] });
      });
      return;
    }
    children(node, find);
  })(closure.body);
  if (found !== 1) {
    throw new Error(`striptable: ${found} imports objects (an object under the key cdom) in the shim, not one`);
  }
  // A unit owns the references in its range (an I and a V: in the value, the name and comma are no
  // reference); the innermost owner wins.
  const owners = units.map((u, i) => [u.own ?? [u.start, u.end], i]).filter(([, i]) => units[i].kind !== 'G');
  const ownerAt = (pos) => {
    let best = -1;
    let size = Infinity;
    for (const [[s, e], i] of owners) {
      if (pos >= s && pos < e && e - s < size) {
        best = i;
        size = e - s;
      }
    }
    return best;
  };
  const deps = units.map(() => new Set());
  const roots = new Set(alwaysKept);
  const scopes = [];
  function visit(node, parent, key) {
    if (node.type === 'Identifier' && byName.has(node.name)
      && !(parent.type === 'MemberExpression' && key === 'property' && !parent.computed)
      && !((parent.type === 'Property' || parent.type === 'MethodDefinition' || parent.type === 'PropertyDefinition')
        && key === 'key' && !parent.computed && !parent.shorthand)
      && !scopes.some((names) => names.has(node.name))) {
      const unit = byName.get(node.name);
      const owner = ownerAt(node.start);
      if (owner !== unit) {
        (owner < 0 ? roots : deps[owner]).add(unit);
      }
    }
    const names = scopeOf(node);
    if (names) scopes.push(names);
    children(node, visit);
    if (names) scopes.pop();
  }
  children(closure.body, visit); // the closure's own names are the units: no scope for them

  const lines = [`cdomstrip 1 ${code.length} ${units.length}`];
  units.forEach((u, i) => {
    const d = deps[i].size ? ' ' + [...deps[i]].sort((a, b) => a - b).join(' ') : '';
    if (u.kind === 'F') lines.push(`F ${u.start} ${u.end}${d}`);
    if (u.kind === 'V') lines.push(`V ${u.start} ${u.end} ${u.group}${d}`);
    if (u.kind === 'G') lines.push(`G ${u.start} ${u.end}`);
    if (u.kind === 'I') lines.push(`I ${u.start} ${u.end} ${u.name}${d}`);
  });
  lines.push(`R${[...roots].sort((a, b) => a - b).map((r) => ' ' + r).join('')}`);
  return lines.join('\n') + '\n';
}
