// The JavaScript 1 transform (docs/js1-transform-design.md): the app's wasm2js output, after the memory pass
// (memory.mjs) on words1.js, rewritten from its AST down to the JavaScript 1.0 that Netscape 2.02 parses
// (docs/descent.md), for the frames tier. tools/legacy/build.mjs app-js1 calls it; js11.mjs and js10.mjs gate what it
// writes; tools/js1.mjs runs it against WebAssembly.
//
// What wasm2js writes and what each becomes:
// - One closure, asmFunc, whose locals the whole module shares: every binding of the file is renamed to a global of
//   its own (c_ and the name, $ spelled S), asmFunc's nested functions go to the top level, and its variables become
//   globals that the factory assigns. One instance a page, as the frames tier has.
// - Labels, labeled blocks, switch and do-while: loops, flags and if chains (lowerFunction).
// - throw (wasm2js's traps): the trap flag T, set, and a return; every statement after a call that can trap checks it
//   and returns (traps).
// - Object and array literals: new Object() and new Array() and one assignment a key, as statements.
// - ===, !==, Infinity, NaN, unary plus, Math.imul, clz32, trunc and fround: ==, !=, words1.js's INF and NAN, x - 0,
//   (x * c | 0) for a small constant c or words1.js's imul, its clz32, trunc and fround.
// - A property named as a method JavaScript 1.0 lacks (the import cdom.call): written o["call"].
// Anything else that JavaScript 1.0 lacks is left for the gates to refuse: the transform does not guess.
import { parseSync, traverse, types as t, generate } from '@babel/core';
import { METHODS } from './js11.mjs';

const parse = (src) => parseSync(src, { sourceType: 'script', configFile: false, babelrc: false });
const id = (n) => t.identifier(n);
const num = (v) => t.numericLiteral(v);
const assign = (l, r) => t.expressionStatement(t.assignmentExpression('=', typeof l === 'string' ? id(l) : l, r));
const isLoop = (n) => t.isWhileStatement(n) || t.isForStatement(n) || t.isDoWhileStatement(n);
// The trap flag, words1.js's T once renamed, and its check.
const TRAP = 'c_T';
const check = () => t.ifStatement(id(TRAP), t.returnStatement(num(0)));
const trapNow = () => [assign(TRAP, num(1)), t.returnStatement(num(0))];

// $ is spelled S (Netscape 2's identifiers are taken to be letters, digits and _), and a top-level name gets c_.
const spell = (name) => name.replace(/\$/g, 'S');

// Every binding renamed: the file's top level and asmFunc's to c_ names (the globals they become), every other one
// with a $ to its spelling. Returns the program path.
function rename(ast) {
  let program;
  const globals = new Set();
  const fresh = (want, scope) => {
    let n = want, k = 1;
    while (globals.has(n) || scope.hasBinding(n) || scope.hasGlobal(n)) n = `${want}_${k++}`;
    return n;
  };
  traverse(ast, {
    Program(p) {
      program = p;
      for (const name of Object.keys(p.scope.bindings)) {
        const to = fresh('c_' + spell(name), p.scope);
        globals.add(to);
        p.scope.rename(name, to);
      }
    },
  });
  const factory = program.get('body').find((s) => s.isFunctionDeclaration() && s.node.id.name === 'c_asmFunc');
  if (!factory) throw new Error('js1: no function asmFunc in the wasm2js output');
  for (const name of Object.keys(factory.scope.bindings)) {
    const to = fresh('c_' + spell(name), factory.scope);
    globals.add(to);
    factory.scope.rename(name, to);
  }
  traverse(ast, {
    Scopable(p) {
      if (p.isProgram() || p.node === factory.node) return;
      for (const name of Object.keys(p.scope.bindings)) {
        if (!name.includes('$') || p.scope.bindings[name].scope !== p.scope) continue;
        p.scope.rename(name, fresh(spell(name), p.scope));
      }
    },
  });
  return { program, factory, globals };
}

// asmFunc's body to the top level: its function declarations after it, its variables declared there and assigned in
// it, where they were. Math_x = Math.x (and Math_fround = fround) are dropped and remembered, for lowerExpressions.
function flatten(program, factory) {
  const body = factory.node.body.body, keep = [], hoisted = [], vars = [], math = new Map();
  for (const s of body) {
    if (t.isFunctionDeclaration(s)) { hoisted.push(s); continue; }
    if (t.isVariableDeclaration(s)) {
      for (const d of s.declarations) {
        const name = d.id.name;
        const m = /^c_Math_(\w+)$/.exec(name);
        if (m && d.init && (t.isMemberExpression(d.init) || t.isIdentifier(d.init))) { math.set(name, m[1]); continue; }
        vars.push(t.variableDeclarator(id(name)));
        if (d.init) keep.push(assign(name, d.init));
      }
      continue;
    }
    keep.push(s);
  }
  factory.node.body.body = keep;
  const at = program.node.body.indexOf(factory.node);
  program.node.body.splice(at + 1, 0, ...hoisted);
  if (vars.length) program.node.body.splice(at, 0, t.variableDeclaration('var', vars));
  return math;
}

// Labels, switch and do-while, in one function body (docs/js1-transform-design.md, section 4). Every target a jump
// names (a loop, a switch, a labeled block some break leaves) becomes a loop of the output: a switch a while (1) round
// an if chain, a labeled block a while (1) { ...; break; }, a do-while a while (first || test). A jump to the innermost
// output loop is a plain break or continue; one to a loop further out sets t_J to the target's code and breaks, and
// each loop it leaves on the way is followed by a check of t_J that breaks again, or, at the target, clears it and
// breaks or continues.
function lowerFunction(fnBody, locals) {
  let uid = 0, usedJ = false;
  const local = (base) => { const n = `t_${base}${++uid}`; locals.add(n); return n; };
  // Which labeled non-loop statements a break leaves: those become loops; the others lose their label.
  const used = new Set();
  (function mark(n, stack) {
    if (!n || typeof n.type !== 'string') return;
    if (t.isLabeledStatement(n)) {
      const labels = [];
      let b = n;
      while (t.isLabeledStatement(b)) { labels.push(b.label.name); b = b.body; }
      const entry = { labels, node: b, kind: isLoop(b) ? 'loop' : t.isSwitchStatement(b) ? 'switch' : 'block' };
      mark(b, [...stack, entry], true);
      return;
    }
    if ((isLoop(n) || t.isSwitchStatement(n)) && !(stack.length && stack[stack.length - 1].node === n)) {
      stack = [...stack, { labels: [], node: n, kind: isLoop(n) ? 'loop' : 'switch' }];
    }
    if (t.isBreakStatement(n) && n.label) {
      const e = [...stack].reverse().find((x) => x.labels.includes(n.label.name));
      if (!e) throw new Error(`js1: break ${n.label.name} has no target`);
      if (e.kind === 'block') used.add(e.node);
    }
    if (t.isFunction(n)) return;
    for (const k of Object.keys(n)) {
      if (k === 'loc' || k === 'start' || k === 'end' || k === 'leadingComments' || k === 'trailingComments') continue;
      const v = n[k];
      if (Array.isArray(v)) for (const c of v) mark(c, stack); else if (v && typeof v.type === 'string') mark(v, stack);
    }
  })(fnBody, []);

  const stack = []; // the targets around the statement being written, outermost first
  let codes = 0;
  const block = (list) => t.blockStatement(list);
  const stmts = (list) => list.flatMap((s) => stmt(s, []));
  const sub = (s) => { const out = stmt(s, []); return out.length === 1 ? out[0] : block(out); };
  // The check after the loop of target r, which sits in the loop of the target under it on the stack.
  function after(r) {
    if (!r.exits.size) return [];
    usedJ = true;
    const parent = stack[stack.length - 1];
    const here = [...r.exits].filter((c) => c.target === parent), on = [...r.exits].filter((c) => c.target !== parent);
    const settle = (c) => [assign('t_J', num(0)), c.cont ? t.continueStatement() : t.breakStatement()];
    if (!here.length) return [t.ifStatement(id('t_J'), t.breakStatement())];
    if (!on.length && here.length === 1) return [t.ifStatement(id('t_J'), block(settle(here[0])))];
    const inner = here.map((c) => t.ifStatement(t.binaryExpression('==', id('t_J'), num(c.code)), block(settle(c))));
    if (on.length) inner.push(t.breakStatement());
    return [t.ifStatement(id('t_J'), block(inner))];
  }
  function jump(n, cont) {
    let target;
    if (n.label) target = [...stack].reverse().find((r) => r.labels.includes(n.label.name));
    else target = [...stack].reverse().find((r) => (cont ? r.kind === 'loop' : r.kind !== 'block'));
    if (!target) throw new Error(`js1: ${cont ? 'continue' : 'break'} ${n.label ? n.label.name : ''} has no target`);
    if (cont && target.kind !== 'loop') throw new Error('js1: a continue to a statement that is not a loop');
    if (target === stack[stack.length - 1]) return [cont ? t.continueStatement() : t.breakStatement()];
    const key = cont ? 'cont' : 'brk';
    target.codes ??= {};
    const c = target.codes[key] ??= { code: ++codes, target, cont };
    for (let i = stack.length - 1; stack[i] !== target; i--) stack[i].exits.add(c);
    usedJ = true;
    return [assign('t_J', num(c.code)), t.breakStatement()];
  }
  // A loop, a switch or a used labeled block, as an output loop, then its check.
  function target(n, labels) {
    const r = { labels, node: n, kind: isLoop(n) ? 'loop' : t.isSwitchStatement(n) ? 'switch' : 'block', exits: new Set() };
    const pre = [];
    let out;
    if (t.isSwitchStatement(n)) {
      // switch (d) { case v0: ...; default: ...; case v1: ... }: the index of the case taken, then each body from it on.
      const s = local('s'), c = local('c');
      pre.push(assign(s, n.discriminant));
      const cases = n.cases, def = cases.findIndex((k) => !k.test);
      pre.push(assign(c, num(def < 0 ? cases.length : def)));
      let chain = null;
      for (let k = cases.length - 1; k >= 0; k--) {
        if (!cases[k].test) continue;
        chain = t.ifStatement(t.binaryExpression('==', id(s), cases[k].test), assign(c, num(k)), chain);
      }
      if (chain) pre.push(chain);
      stack.push(r);
      const bodies = [];
      cases.forEach((k, i) => {
        if (!k.consequent.length) return;
        bodies.push(t.ifStatement(t.binaryExpression('<=', id(c), num(i)), block(stmts(k.consequent))));
      });
      stack.pop();
      out = t.whileStatement(num(1), block([...bodies, t.breakStatement()]));
    } else if (t.isDoWhileStatement(n)) {
      const f = local('f');
      pre.push(assign(f, num(1)));
      stack.push(r);
      const body = stmts([n.body]);
      stack.pop();
      out = t.whileStatement(t.logicalExpression('||', id(f), n.test), block([assign(f, num(0)), ...body]));
    } else if (isLoop(n)) {
      stack.push(r);
      const body = sub(n.body);
      stack.pop();
      out = t.isWhileStatement(n) ? t.whileStatement(n.test, body) : t.forStatement(n.init, n.test, n.update, body);
    } else {
      stack.push(r);
      const body = stmts([n]);
      stack.pop();
      out = t.whileStatement(num(1), block([...body, t.breakStatement()]));
    }
    return [...pre, out, ...after(r)];
  }
  function stmt(n, labels) {
    if (t.isLabeledStatement(n)) return stmt(n.body, [...labels, n.label.name]);
    if (isLoop(n) || t.isSwitchStatement(n)) return target(n, labels);
    if (labels.length && used.has(n)) return target(n, labels);
    if (t.isBlockStatement(n)) return [block(stmts(n.body))];
    if (t.isIfStatement(n)) return [t.ifStatement(n.test, sub(n.consequent), n.alternate ? sub(n.alternate) : null)];
    if (t.isBreakStatement(n)) return jump(n, false);
    if (t.isContinueStatement(n)) return jump(n, true);
    if (t.isExpressionStatement(n) || t.isReturnStatement(n) || t.isVariableDeclaration(n) || t.isEmptyStatement(n)
      || t.isThrowStatement(n)) return [n];
    if (t.isFunctionDeclaration(n)) throw new Error('js1: a nested function declaration left after flattening');
    throw new Error(`js1: a ${n.type} the transform does not lower`);
  }
  fnBody.body = stmts(fnBody.body);
  if (usedJ) locals.add('t_J=0');
}

// The functions that can trap: one with a throw, one that calls one, and, through the table, every one when any
// function in the table can. Imports are the shim's, which never calls back into C from one (tools/legacy/frames.js:
// only an entry from the browser does), so a call of one cannot trap.
function trappers(fns, table, tableFns) {
  const may = new Set(), calls = new Map();
  for (const [name, f] of fns) {
    const out = new Set();
    let throws = false, indirect = false;
    walkNoFn(f.body, (n) => {
      if (t.isThrowStatement(n)) throws = true;
      if (t.isCallExpression(n) && t.isIdentifier(n.callee) && fns.has(n.callee.name)) out.add(n.callee.name);
      if (t.isCallExpression(n) && t.isMemberExpression(n.callee) && t.isIdentifier(n.callee.object, { name: table })) indirect = true;
    });
    calls.set(name, { out, indirect });
    if (throws) may.add(name);
  }
  const tableTraps = () => [...tableFns].some((g) => g === '*' || may.has(g));
  for (;;) {
    const before = may.size;
    for (const [name, c] of calls) {
      if (!may.has(name) && ([...c.out].some((g) => may.has(g)) || (c.indirect && tableTraps()))) may.add(name);
    }
    if (may.size === before) break;
  }
  return { may, tableTraps };
}
function walkNoFn(n, visit) {
  if (!n || typeof n.type !== 'string') return;
  visit(n);
  if (t.isFunction(n)) return;
  for (const k of Object.keys(n)) {
    if (k === 'loc' || k === 'start' || k === 'end' || k.endsWith('Comments')) continue;
    const v = n[k];
    if (Array.isArray(v)) for (const c of v) walkNoFn(c, visit); else if (v && typeof v.type === 'string') walkNoFn(v, visit);
  }
}

// The trap checks, in one lowered function body: a throw sets T and returns; a statement whose evaluation calls a
// function that can trap is followed by `if (T) return 0;`; an if's or a while's test that does is evaluated into
// t_q first. A return passes the trap up as it is: its caller checks. stats counts what was written.
function traps(fnBody, can, locals, stats) {
  const calls = (n) => { let found = 0; walkNoFn(n, (x) => { if (can(x)) found++; }); return found; };
  const list = (l) => l.flatMap(one);
  const sub = (s) => { const out = one(s); return out.length === 1 ? out[0] : t.blockStatement(out); };
  function one(n) {
    if (t.isThrowStatement(n)) { stats.throws++; return trapNow(); }
    if (t.isExpressionStatement(n) && t.isCallExpression(n.expression) && t.isIdentifier(n.expression.callee, { name: 'c_wasm2js_trap' })) {
      stats.throws++;
      return trapNow();
    }
    if (t.isBlockStatement(n)) return [t.blockStatement(list(n.body))];
    if (t.isExpressionStatement(n) || t.isVariableDeclaration(n)) {
      const k = calls(n);
      if (!k) return [n];
      stats.checks++;
      if (k > 1) stats.multi++;
      return [n, check()];
    }
    if (t.isIfStatement(n)) {
      const out = [];
      let test = n.test;
      if (calls(test)) {
        locals.add('t_q');
        stats.checks++;
        out.push(assign('t_q', test), check());
        test = id('t_q');
      }
      out.push(t.ifStatement(test, sub(n.consequent), n.alternate ? sub(n.alternate) : null));
      return out;
    }
    if (t.isWhileStatement(n)) {
      const body = sub(n.body);
      if (!calls(n.test)) return [t.whileStatement(n.test, body)];
      locals.add('t_q');
      stats.checks++;
      return [t.whileStatement(num(1), t.blockStatement([assign('t_q', n.test), check(),
        t.ifStatement(t.unaryExpression('!', id('t_q')), t.breakStatement()), body]))];
    }
    if (t.isForStatement(n)) {
      if (calls(n.test || num(0)) || calls(n.update || num(0)) || calls(n.init || num(0))) throw new Error('js1: a call that can trap in a for head');
      return [t.forStatement(n.init, n.test, n.update, sub(n.body))];
    }
    if (t.isReturnStatement(n)) {
      if (n.argument && calls(n.argument) > 1) stats.multi++;
      return [n];
    }
    return [n];
  }
  fnBody.body = list(fnBody.body);
}

// The small constant of Math.imul(a, c) that (a * c | 0) multiplies exactly: an i32 or u32 operand is under 2^32, so
// the product stays under 2^52.
const SMALL = 1 << 20;
const small = (n) => t.isNumericLiteral(n) && Math.abs(n.value) <= SMALL
  || (t.isUnaryExpression(n, { operator: '-' }) && t.isNumericLiteral(n.argument) && n.argument.value <= SMALL);

// ===, Infinity, NaN, unary plus, the Math_ aliases, and a property named as a method: see the head of the file.
function lowerExpressions(ast, math) {
  traverse(ast, {
    BinaryExpression(p) {
      if (p.node.operator === '===') p.node.operator = '==';
      else if (p.node.operator === '!==') p.node.operator = '!=';
    },
    UnaryExpression(p) {
      if (p.node.operator === '+') p.replaceWith(t.binaryExpression('-', p.node.argument, num(0)));
    },
    Identifier(p) {
      if (!p.isReferencedIdentifier() || p.scope.hasBinding(p.node.name)) return;
      if (p.node.name === 'Infinity') p.replaceWith(id('c_INF'));
      else if (p.node.name === 'NaN') p.replaceWith(id('c_NAN'));
    },
    CallExpression(p) {
      const c = p.node.callee;
      if (!t.isIdentifier(c) || !math.has(c.name)) return;
      const m = math.get(c.name), a = p.node.arguments;
      if (m === 'imul') {
        const [x, y] = small(a[1]) ? [a[0], a[1]] : small(a[0]) ? [a[1], a[0]] : [null, null];
        if (x) p.replaceWith(t.binaryExpression('|', t.binaryExpression('*', x, y), num(0)));
        else p.node.callee = id('c_imul');
      } else if (m === 'clz32' || m === 'trunc' || m === 'fround') p.node.callee = id('c_' + m);
      else p.node.callee = t.memberExpression(id('Math'), id(m));
    },
    MemberExpression(p) {
      if (!p.node.computed && t.isIdentifier(p.node.property) && METHODS.has(p.node.property.name)) {
        p.node.property = t.stringLiteral(p.node.property.name);
        p.node.computed = true;
      }
    },
  });
  // A Math_ alias named anywhere but as a callee is not lowered: refused.
  traverse(ast, { Identifier(p) { if (math.has(p.node.name) && p.isReferencedIdentifier()) throw new Error(`js1: ${p.node.name} used other than called`); } });
}

// Object and array literals: x = {..}, var x = [..] and return {..} become new Object() or new Array() and one
// assignment a key, with a temporary for each literal inside one. A literal anywhere else is left for the gate.
function lowerLiterals(fnBody, locals) {
  let uid = 0;
  const literal = (n) => t.isObjectExpression(n) || t.isArrayExpression(n);
  function build(lit, into, out) {
    out.push(assign(into, t.newExpression(id(t.isArrayExpression(lit) ? 'Array' : 'Object'), [])));
    const entries = t.isArrayExpression(lit) ? lit.elements.map((v, i) => [num(i), v])
      : lit.properties.map((pr) => {
        if (!t.isObjectProperty(pr) || pr.computed) throw new Error('js1: an object literal with a method, a spread or a computed key');
        return [t.stringLiteral(t.isIdentifier(pr.key) ? pr.key.name : String(pr.key.value)), pr.value];
      });
    for (const [k, v] of entries) {
      let value = v;
      if (literal(v)) { value = id(`t_o${++uid}`); locals.add(value.name); build(v, value.name, out); }
      out.push(assign(t.memberExpression(id(into), k, true), value === null ? t.nullLiteral() : value));
    }
  }
  function list(l) {
    return l.flatMap((s) => {
      if (t.isExpressionStatement(s) && t.isAssignmentExpression(s.expression, { operator: '=' }) && t.isIdentifier(s.expression.left)
        && literal(s.expression.right)) {
        const out = [];
        build(s.expression.right, s.expression.left.name, out);
        return out;
      }
      if (t.isReturnStatement(s) && literal(s.argument)) {
        const out = [], n = `t_o${++uid}`;
        locals.add(n);
        build(s.argument, n, out);
        return [...out, t.returnStatement(id(n))];
      }
      for (const k of ['consequent', 'alternate', 'body']) {
        if (s[k] && t.isBlockStatement(s[k])) s[k].body = list(s[k].body);
      }
      if (t.isBlockStatement(s)) s.body = list(s.body);
      return [s];
    });
  }
  fnBody.body = list(fnBody.body);
}

// Declares a function's new locals (t_J starts at 0 on every call: a trap or a jump never leaves it set).
function declare(fn, locals) {
  if (!locals.size) return;
  const ds = [...locals].sort().map((n) => {
    const [name, init] = n.split('=');
    return t.variableDeclarator(id(name), init === undefined ? null : num(+init));
  });
  fn.body.body.unshift(t.variableDeclaration('var', ds));
}

// The top-level functions nothing calls, and the variables nothing reads whose value is a number or nothing: dropped,
// until none is left (words1.js's floats in an app that has none, wasm2js's helpers it did not need).
function unused(ast) {
  let dropped = 0;
  for (;;) {
    traverse.cache.clear();
    let gone = 0;
    traverse(ast, {
      Program(p) {
        p.scope.crawl();
        for (const [name, b] of Object.entries(p.scope.bindings)) {
          if (b.referenced || name === 'c_asmFunc') continue;
          if (b.path.isFunctionDeclaration()) { b.path.remove(); gone++; }
          else if (b.path.isVariableDeclarator() && (!b.path.node.init || t.isNumericLiteral(b.path.node.init)) && !b.constantViolations.length) {
            b.path.remove();
            gone++;
          }
        }
        p.stop();
      },
    });
    dropped += gone;
    if (!gone) return dropped;
  }
}

// The top-level names of the output, renamed to c_ and a short base-36 number, the most used first: the frames shim's
// are cf_ and the page's cdom..., so none can collide; the exports keep their names (they are the keys of the exports
// object).
function shorten(ast) {
  traverse.cache.clear();
  traverse(ast, {
    Program(p) {
      p.scope.crawl();
      const uses = (b) => b.referencePaths.length + b.constantViolations.length;
      const names = Object.entries(p.scope.bindings).filter(([n]) => n.startsWith('c_')).sort((a, b) => uses(b[1]) - uses(a[1]));
      const taken = new Set(names.map(([n]) => n));
      let k = 0;
      for (const [name] of names) {
        let to;
        do to = `c_${(k++).toString(36)}`; while (taken.has(to) && to !== name);
        if (to !== name) p.scope.rename(name, to);
        taken.delete(name);
        taken.add(to);
      }
      p.stop();
    },
  });
}

// src: words1.js, then the memory pass's output (asmFunc and its helpers). Returns the file's body, which sets
// window.cdomFactory to the factory, and stats (the trap checks written, and those after a statement with more than
// one call that can trap, where the calls after the first still run once it has trapped: see the design, section 5).
export function js1(src, { short = true } = {}) {
  const ast = parse(src);
  const { program, factory } = rename(ast);
  const math = flatten(program, factory);
  // The exports gain cdom_trapped, which the shim calls after each call into C.
  const ret = factory.node.body.body.find((s) => t.isReturnStatement(s));
  if (!ret || !t.isObjectExpression(ret.argument)) throw new Error('js1: asmFunc no longer returns an object literal');
  ret.argument.properties.push(t.objectProperty(t.stringLiteral('cdom_trapped'), id('c_trapped')));
  // The exported memory keeps what the frames shim reads (words, grow, load_f64 and the byte accessors), so the f32
  // packing goes with the rest of what nothing calls.
  const mem = ret.argument.properties.find((pr) => t.isStringLiteral(pr.key, { value: 'memory' }) || t.isIdentifier(pr.key, { name: 'memory' }));
  if (!mem || !t.isObjectExpression(mem.value)) throw new Error('js1: the exported memory is not the memory pass\'s object');
  mem.value.properties = mem.value.properties.filter((pr) => !['load_f32', 'store_f32'].includes(pr.key.name || pr.key.value));
  const table = 'c_FUNCTION_TABLE';
  const fns = new Map();
  for (const s of program.node.body) if (t.isFunctionDeclaration(s)) fns.set(s.id.name, s);
  const tableFns = new Set();
  walkNoFn(factory.node.body, (n) => {
    if (t.isAssignmentExpression(n) && t.isIdentifier(n.left, { name: table })) {
      if (!t.isArrayExpression(n.right)) tableFns.add('*');
      else for (const e of n.right.elements) if (t.isIdentifier(e)) tableFns.add(e.name);
    }
  });
  const stats = { throws: 0, checks: 0, multi: 0, functions: 0, trapping: 0 };
  const localsOf = new Map();
  for (const f of fns.values()) {
    const locals = new Set();
    localsOf.set(f, locals);
    lowerFunction(f.body, locals);
  }
  const { may, tableTraps } = trappers(fns, table, tableFns);
  const can = (n) => t.isCallExpression(n) && ((t.isIdentifier(n.callee) && may.has(n.callee.name))
    || (t.isMemberExpression(n.callee) && t.isIdentifier(n.callee.object, { name: table }) && tableTraps()));
  for (const f of fns.values()) {
    stats.functions++;
    if (may.has(f.id.name)) stats.trapping++;
    traps(f.body, can, localsOf.get(f), stats);
    lowerLiterals(f.body, localsOf.get(f));
    declare(f, localsOf.get(f));
  }
  traverse.cache.clear();
  lowerExpressions(ast, math);
  program.node.body.push(assign(t.memberExpression(id('window'), id('cdomFactory')), id('c_asmFunc')));
  stats.dropped = unused(ast);
  if (short) shorten(ast);
  return { code: generate(ast, { comments: false }).code, stats };
}
