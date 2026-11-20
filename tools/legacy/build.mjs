// Builds the files a browser without WebAssembly loads instead of cdom.js and app.wasm:
//   node tools/legacy/build.mjs shim <out/cdom.legacy.js>       the polyfills, then shim/cdom.js in ES5
//   node tools/legacy/build.mjs shim-ie9 <out/cdom.legacy-ie9.js>  the same on memory-words.js, for no typed arrays
//   node tools/legacy/build.mjs shim-ie6 <out/cdom.legacy-ie6.js>  that again in ES3 on the old DOM (dom-old.js), for IE 6 to 8
//   node tools/legacy/build.mjs app <in.wasm> <out/in.wasm.js>   the same wasm compiled to JS by wasm2js
//   node tools/legacy/build.mjs app-ie9 <in.wasm> <out/in.wasm.ie9.js>  the same, on a plain Array of words (memory.mjs),
//                                                                in ES3: IE 9 and the fourth tier (IE 6 to 8) share it
//   node tools/legacy/build.mjs page <in.html> <out.html>        inlines shim/boot.js at /*CDOM_BOOT*/
//   node tools/legacy/build.mjs inputs <shim|app|page> <args>    prints what that build reads, one per line
// Both outputs must parse as ES5 and name no ES2015 global or method that the polyfills do not supply,
// or nothing is written; both are then minified by the repo root's terser (the one bench/build.sh minifies
// the shim with) at ecma 5, and checked as ES5 again. Needs `npm ci` here and at the repo root, and
// wasm2js (binaryen) on PATH.
//
// An output is rebuilt only when it is missing, differs from what this script last wrote there, or any of
// its inputs (inputs() below, the one list) differs by content from the last build. Contents, not mtimes:
// tests/build.sh relinks every .wasm on each run, and tools/mutate.py copies trees. The stamp is a line of
// two sha256s (the inputs, the output) in node_modules/.fresh here, named by the output's absolute path, so
// nothing lands beside the output (bench/build.sh writes into a jfb entry), and `npm ci` here drops every
// stamp. A build that reads a file inputs() does not name throws before it writes, so the list cannot fall
// behind what the build opens; tools/legacyfresh.mjs changes each input in turn and checks the rebuild.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { createRequire } from 'node:module';
// The words pass of app-ie9 (memory.mjs) and Babel under it: loaded, like acorn, only when that build runs.
let memoryPass;

const here = path.dirname(new URL(import.meta.url).pathname);
const top = path.resolve(here, '../..');
const require = createRequire(import.meta.url);
const opened = new Set();
const read = (p) => { opened.add(path.resolve(p)); return fs.readFileSync(p, 'utf8'); };
// Loaded only when a build runs, so a fresh output costs no Babel, terser or acorn start-up.
let acorn;

// What a browser without WebAssembly is assumed to be: typed arrays, but nothing from ES2015.
const TARGETS = 'ie 11, safari 9, chrome 49, firefox 45, android 4.4, ios 9';
const GLOBALS = new Set(['Symbol', 'Map', 'Set', 'WeakMap', 'WeakSet', 'Reflect', 'Proxy', 'BigInt', 'globalThis']);
const METHODS = new Set(['includes', 'find', 'findIndex', 'fill', 'repeat', 'padStart', 'padEnd', 'endsWith',
  'assign', 'entries', 'values', 'from', 'of', 'codePointAt', 'fromCodePoint', 'flat', 'flatMap', 'at',
  'trimStart', 'trimEnd', 'copyWithin', 'allSettled', 'any']);
// The only names allowed past the scan, each in the one file that may use it: promise-polyfill defines
// Promise.allSettled and Promise.any itself; PEP uses Map only behind a check for it, and includes only on
// touch-action strings; wasm2js output calls copyWithin and fill on a Uint8Array; the shim reads
// globalThis.crypto and Object.entries, names Map only behind a typeof check (the read-only string table falls back to a plain object), and Babel's own helpers name Symbol only behind a typeof check.
// polyfills.js supplies (and so names) everything here but Symbol. wasm2js output also reads the shim's
// import named "at" (cd_at) off the imports object, which the scan cannot tell from Array.prototype.at.
const ALLOWED = { 'promise-polyfill': new Set(['.allSettled', '.any']), pepjs: new Set(['Map', '.includes']),
  polyfills: new Set(['.includes', '.copyWithin', '.fill', 'globalThis', '.entries']),
  wasm2js: new Set(['.copyWithin', '.fill', '.at']), shim: new Set(['Symbol', 'globalThis', '.entries', 'Map']) };

function es5(name, src) {
  try { acorn.parse(src, { ecmaVersion: 5, sourceType: 'script' }); } catch (e) {
    const line = src.split('\n')[e.loc.line - 1];
    throw new Error(`${name} is not ES5: ${e.message}\n  ${line.trim().slice(0, 160)}`);
  }
  const bad = [], ok = ALLOWED[name] || new Set();
  let prev = null;
  for (const t of acorn.tokenizer(src, { ecmaVersion: 5 })) {
    if (t.type.label === 'name') {
      const dot = prev && prev.type.label === '.';
      const key = dot ? '.' + t.value : t.value;
      if (((dot && METHODS.has(t.value)) || (!dot && GLOBALS.has(t.value))) && !ok.has(key)) bad.push(key);
    }
    prev = t;
  }
  if (bad.length) throw new Error(`${name} uses ES2015 names: ${[...new Set(bad)].join(' ')}`);
  return src;
}

// The fourth tier (IE 6 to 8, docs/ie6-design.md) reads ES3 as JScript does: a reserved word is a syntax error as a
// property name there (`a.class`, `{ default: 1 }`), which acorn's ES3 lets through, so this walk finds them.
const RESERVED = new Set(('break case catch continue default delete do else finally for function if in instanceof new return ' +
  'switch this throw try typeof var void while with null true false abstract boolean byte char class const debugger double ' +
  'enum export extends final float goto implements import int interface long native package private protected public short ' +
  'static super synchronized throws transient volatile').split(' '));
// What ES5 added that the tier's file may name: each must be one polyfills-es3.js supplies (ES5_SUPPLIED), or be
// allowed in the one part that names it behind a check (ES5_ALLOWED). A name the old DOM lacks is OLD_DOM's.
const ES5_NAMES = new Set(['JSON', '.keys', '.create', '.defineProperty', '.defineProperties', '.getPrototypeOf',
  '.getOwnPropertyNames', '.getOwnPropertyDescriptor', '.freeze', '.seal', '.preventExtensions', '.isFrozen', '.isSealed',
  '.isExtensible', '.isArray', '.indexOf', '.lastIndexOf', '.forEach', '.map', '.filter', '.some', '.every', '.reduce',
  '.reduceRight', '.bind', '.now', '.trim', '.toISOString', '.toJSON']);
const ES5_SUPPLIED = new Set(['.keys', '.create', '.isArray', '.now', '.trim', '.bind', '.indexOf', '.lastIndexOf',
  '.forEach', '.map', '.filter', '.some', '.every', '.reduce']);
// polyfills.js and dom-old.js try defineProperty and fall back; Babel's regenerator helper tries defineProperty and
// reads getPrototypeOf only when it exists. polyfills-es3.js defines the supplied ones, so it names them all.
const ES5_ALLOWED = { polyfills: new Set(['.defineProperty']), shim: new Set(['.defineProperty', '.getPrototypeOf']),
  'polyfills-es3': ES5_NAMES };
// JScript (IE 6 to 8) gives a catch clause no scope of its own: its parameter is a variable of the enclosing function,
// so `function f(e) { try { ... } catch (e) {} use(e); }` loses f's e to the error. Babel's own helpers do exactly
// that (_regeneratorDefine2's defineProperty probe, which throws in IE 6, so every async function failed there). So
// every catch parameter in the fourth tier's file is renamed to a name of its own (catchScoped), and the minified
// file is refused if a catch parameter's name is used anywhere else in its function (catchLeaks).
async function catchScoped(src) {
  const { transformSync } = await import('@babel/core');
  return transformSync(src, { babelrc: false, configFile: false, sourceType: 'script', comments: false,
    plugins: [({ types: t }) => ({ visitor: { CatchClause(p) {
      if (t.isIdentifier(p.node.param)) p.scope.rename(p.node.param.name, p.scope.generateUid('caught'));
    } } })] }).code;
}
function catchLeaks(tree) {
  const leaks = new Set();
  const kids = (node, f) => {
    for (const k in node) {
      const v = node[k];
      if (Array.isArray(v)) { for (const c of v) if (c && typeof c.type === 'string') f(c); }
      else if (v && typeof v.type === 'string') f(v);
    }
  };
  // The names a function binds itself: its name as an expression, its parameters, its vars and function declarations
  // (not those of the functions inside it).
  const binds = (fn) => {
    const out = new Set(fn.params.map((p) => p.name));
    if (fn.type === 'FunctionExpression' && fn.id) out.add(fn.id.name);
    const scan = (n) => {
      if (n.type === 'VariableDeclarator') out.add(n.id.name);
      if (n.type === 'FunctionDeclaration') { out.add(n.id.name); return; }
      if (/Function/.test(n.type)) return;
      kids(n, scan);
    };
    kids(fn.body, scan);
    return out;
  };
  // Whether name is used under node, outside skip, where it means the enclosing function's: a function inside that
  // binds name itself has its own.
  const uses = (node, name, skip) => {
    if (node === skip) return false;
    if (node.type === 'Identifier' && node.name === name) return true;
    if (/Function/.test(node.type) && binds(node).has(name)) return false;
    let found = false;
    kids(node, (c) => { found = found || uses(c, name, skip); });
    return found;
  };
  const walk = (node, fn) => {
    if (/Function/.test(node.type)) fn = node;
    if (node.type === 'CatchClause' && node.param && fn) {
      const n = node.param.name;
      let found = fn.params.some((p) => p.name === n) || (fn.type === 'FunctionExpression' && fn.id && fn.id.name === n);
      kids(fn.body, (c) => { found = found || uses(c, n, node); });
      if (found) leaks.add(n);
    }
    kids(node, (c) => walk(c, fn));
  };
  walk(tree, null);
  return [...leaks];
}

// Each part is checked for ES3 syntax and ES5 names as written (minified, the parts are one file); the reserved words
// once minified, since terser's ie8 quotes the ones the parts name (promise-polyfill's .catch).
function es3(name, src, minified = false) {
  let tree;
  try { tree = acorn.parse(src, { ecmaVersion: 3, sourceType: 'script', allowReserved: true }); } catch (e) {
    const line = src.split('\n')[e.loc.line - 1];
    throw new Error(`${name} is not ES3: ${e.message}\n  ${line.trim().slice(0, 160)}`);
  }
  const bad = new Set();
  const walk = (n) => {
    if (n.type === 'MemberExpression' && !n.computed && RESERVED.has(n.property.name)) bad.add('.' + n.property.name);
    if (n.type === 'Property' && n.key.type === 'Identifier' && RESERVED.has(n.key.name)) bad.add(n.key.name + ':');
    for (const k in n) {
      const v = n[k];
      if (Array.isArray(v)) { for (const c of v) if (c && typeof c.type === 'string') walk(c); }
      else if (v && typeof v.type === 'string' && k !== 'loc') walk(v);
    }
  };
  if (minified) walk(tree);
  const leaks = minified ? catchLeaks(tree) : [];
  if (leaks.length) throw new Error(`${name}: a catch parameter's name used elsewhere in its function, which JScript's catch would overwrite: ${leaks.join(' ')}`);
  if (bad.size) throw new Error(`${name} names a reserved word as a property, which JScript refuses: ${[...bad].join(' ')}`);
  if (minified) return src;
  const ok = ES5_ALLOWED[name] || new Set(), es5 = new Set();
  let prev = null;
  for (const t of acorn.tokenizer(src, { ecmaVersion: 3, allowReserved: true })) {
    if (t.type.label === 'name') {
      const key = prev && prev.type.label === '.' ? '.' + t.value : t.value;
      if (ES5_NAMES.has(key) && !ES5_SUPPLIED.has(key) && !ok.has(key)) es5.add(key);
    }
    prev = t;
  }
  if (es5.size) throw new Error(`${name} uses ES5 names polyfills-es3.js does not supply: ${[...es5].join(' ')}`);
  return src;
}

// Minified as ES5: terser keeps the syntax at ecma 5, and the parse proves it. ie8: terser also quotes reserved words
// used as property names and works around JScript's named function expressions and catch scopes, and writes ASCII
// only, for the files the fourth tier runs (which are then checked as ES3).
async function small(name, src, ie8 = false) {
  const { minify } = createRequire(path.join(top, 'package.json'))('terser');
  // LEGACY_PRETTY=1 lays the output out one statement a line, so an old browser's line number says where (debugging only).
  const { code } = await minify(src, { ecma: 5, ie8, compress: { passes: 2 }, mangle: true, format: { ascii_only: ie8, beautify: !!process.env.LEGACY_PRETTY } });
  try { acorn.parse(code, { ecmaVersion: 5, sourceType: 'script' }); } catch (e) {
    throw new Error(`${name} is not ES5 once minified: ${e.message}`);
  }
  if (ie8) es3(name, code, true);
  return code;
}

// A shim built on functions of a file's own: shim/cdom.js's functions are each replaced by file's function of the
// same name, every one of which must find its match; what file declares besides goes in front of the first. One
// shim source: memory-words.js backs its memory with words (the third and fourth tiers), shim-old.js does what the
// old DOM needs done differently (the fourth).
const replacedFrom = (file) => ({ types: t, parseSync: parse }) => ({
  visitor: {
    Program(p) {
      const own = parse(read(path.join(here, file)), { sourceType: 'script', configFile: false, babelrc: false }).program.body;
      // The shim is one `var cdom = (() => { ... })();`: its functions are the statements of that arrow's body.
      const init = p.node.body.length === 1 && p.node.body[0].declarations?.[0]?.init;
      const body = init && t.isCallExpression(init) && t.isArrowFunctionExpression(init.callee) && init.callee.body.body;
      if (!body) throw new Error(`${file}: shim/cdom.js is no longer one var cdom = (() => { ... })()`);
      const at = (name) => body.findIndex((n) => t.isFunctionDeclaration(n) && n.id.name === name);
      const extra = own.filter((n) => !t.isFunctionDeclaration(n));
      let first = -1;
      for (const f of own.filter((n) => t.isFunctionDeclaration(n))) {
        const i = at(f.id.name);
        if (i < 0) throw new Error(`${file}'s ${f.id.name} has no function of that name in shim/cdom.js`);
        body[i] = f;
        first = first < 0 ? i : Math.min(first, i);
      }
      body.splice(first, 0, ...extra);
    },
  },
});

// The fourth tier's one table of what the old DOM (IE 6 to 8) lacks: each W3C call the shim makes, rewritten to the
// tools/legacy/dom-old.js function that does its job (the object it was called on first). call: a method call;
// zero: a call with no arguments only; read: a property read; write: a property assignment; global: a name the
// window holds, read or called. The build refuses cdom.legacy-ie6.js if one of these names is left in the shim.
const OLD_DOM = {
  call: { addEventListener: 'on', removeEventListener: 'off', setProperty: 'setProperty', getPropertyValue: 'getPropertyValue',
    setAttribute: 'setAttribute', createElementNS: 'createElementNS', elementsFromPoint: 'elementsFromPoint' },
  zero: { remove: 'remove', getBoundingClientRect: 'rect' },
  read: { previousElementSibling: 'prevElement' },
  write: { textContent: 'setText' },
  global: { innerWidth: 'viewportWidth', innerHeight: 'viewportHeight', devicePixelRatio: 'pixelRatio', addEventListener: 'on' },
};
function oldDom({ types: t }) {
  const helper = (name) => t.memberExpression(t.identifier('cdomOld'), t.identifier(name));
  // A call of a helper itself (cdomOld.setProperty) is what a rewrite made: never rewritten again.
  const named = (m) => t.isMemberExpression(m) && !m.computed && t.isIdentifier(m.property) &&
    !t.isIdentifier(m.object, { name: 'cdomOld' }) ? m.property.name : null;
  const own = (o, k) => Object.prototype.hasOwnProperty.call(o, k);
  return {
    visitor: {
      CallExpression(p) {
        const c = p.node.callee, m = named(c);
        if (m && (own(OLD_DOM.call, m) || (own(OLD_DOM.zero, m) && !p.node.arguments.length))) {
          p.replaceWith(t.callExpression(helper(OLD_DOM.call[m] || OLD_DOM.zero[m]), [c.object, ...p.node.arguments]));
        } else if (t.isIdentifier(c) && own(OLD_DOM.global, c.name) && OLD_DOM.global[c.name] === 'on' && !p.scope.hasBinding(c.name)) {
          p.replaceWith(t.callExpression(helper('on'), [t.identifier('window'), ...p.node.arguments]));
        }
      },
      AssignmentExpression(p) {
        const m = named(p.node.left);
        if (m && own(OLD_DOM.write, m) && p.node.operator === '=') {
          p.replaceWith(t.callExpression(helper(OLD_DOM.write[m]), [p.node.left.object, p.node.right]));
        }
      },
      MemberExpression(p) {
        const m = named(p.node);
        if (m && own(OLD_DOM.read, m) && !(p.parentPath.isAssignmentExpression() && p.parentPath.node.left === p.node)) {
          p.replaceWith(t.callExpression(helper(OLD_DOM.read[m]), [p.node.object]));
        }
      },
      Identifier(p) {
        const n = p.node.name;
        if (!own(OLD_DOM.global, n) || OLD_DOM.global[n] === 'on' || p.scope.hasBinding(n) || !p.isReferencedIdentifier()) return;
        p.replaceWith(t.callExpression(helper(OLD_DOM.global[n]), []));
      },
    },
  };
}
// The names of OLD_DOM left in the fourth tier's shim, as tokens: a method called after a dot (a read of one is a
// feature test, which the old DOM answers), a property read or written after a dot, a global bare.
function oldDomLeftovers(src, parse) {
  const called = new Set([...Object.keys(OLD_DOM.call), ...Object.keys(OLD_DOM.zero)]);
  const props = new Set([...Object.keys(OLD_DOM.read), ...Object.keys(OLD_DOM.write)]);
  const bare = new Set(Object.keys(OLD_DOM.global)), left = new Set();
  const ks = [...parse.tokenizer(src, { ecmaVersion: 5 })];
  for (let i = 0; i < ks.length; i++) {
    const k = ks[i];
    if (k.type.label !== 'name') continue;
    const dot = i > 0 && ks[i - 1].type.label === '.';
    // cdomOld.setProperty is a helper's call, which the rewrite made
    if (dot && i > 1 && ks[i - 2].type.label === 'name' && ks[i - 2].value === 'cdomOld') continue;
    const call = i + 1 < ks.length && ks[i + 1].type.label === '(';
    if (dot && ((called.has(k.value) && call) || props.has(k.value))) left.add('.' + k.value);
    if (!dot && bare.has(k.value)) left.add(k.value);
  }
  return [...left];
}

// cdom.legacy.js (tier ''), cdom.legacy-ie9.js (tier 'ie9': the shim on memory-words.js, refused when a typed array
// is left in it outside seed and randomBytes or a view of the typed backing is read anywhere, memory.mjs's
// shimLeftovers), or cdom.legacy-ie6.js (tier 'ie6': that, in ES3 on the old DOM, with no PEP; refused when an ES5 name
// polyfills-es3.js does not supply or a name of OLD_DOM is left in it).
async function shim(out, tier) {
  const words = tier === 'ie9' || tier === 'ie6', old = tier === 'ie6';
  const { transformSync } = await import('@babel/core');
  const babel = transformSync(read(path.join(top, 'shim/cdom.js')), {
    babelrc: false, configFile: false, comments: false, sourceType: 'script',
    // The shim spreads and iterates only arrays, so the helpers need no Symbol.iterator.
    assumptions: { iterableIsArray: true, arrayLikeIsIterable: false, noDocumentAll: true },
    plugins: [...(words ? [replacedFrom('memory-words.js')] : []), ...(old ? [replacedFrom('shim-old.js'), oldDom] : [])],
    presets: [[require.resolve('@babel/preset-env'), { targets: old ? `${TARGETS}, ie 6` : words ? `${TARGETS}, ie 9` : TARGETS, modules: false }]],
  }).code;
  if (words) {
    memoryPass ??= await import('./memory.mjs');
    const left = memoryPass.shimLeftovers(babel, acorn);
    if (left.length) throw new Error(`shim-${tier}: typed memory left in the shim: ${left.join(', ')}`);
  }
  if (old) {
    const left = oldDomLeftovers(babel, acorn);
    if (left.length) throw new Error(`shim-ie6: W3C DOM the old DOM lacks left in the shim: ${left.join(', ')}`);
  }
  const parts = [
    ...(old ? [['polyfills-es3', read(path.join(here, 'polyfills-es3.js'))], ['dom-old', read(path.join(here, 'dom-old.js'))]] : []),
    ['polyfills', read(path.join(here, 'polyfills.js'))],
    ['promise-polyfill', read(require.resolve('promise-polyfill/dist/polyfill.js'))],
    ...(old ? [] : [['pepjs', read(require.resolve('pepjs/dist/pep.js'))]]),
    ['shim', babel],
  ];
  let body = parts.map(([n, s]) => old ? es3(n, es5(n, s)) : es5(n, s)).join('\n;\n');
  if (old) body = await catchScoped(body);
  const name = path.basename(out);
  return `/* ${name}: generated by tools/legacy/build.mjs, do not edit. */\n` + await small(name, body, old);
}

// wasm2js output as a script: wasm2js writes an ES module that imports each import module and exports what
// asmFunc returns. The page has no module loader, so the imports come in as asmFunc's argument and asmFunc
// itself is handed over. Returns [the script's body, the comment line naming what made it].
function wasm2js(wasm, out) {
  opened.add(path.resolve(wasm));
  const js = execFileSync('wasm2js', ['-all', '-O2', wasm], { encoding: 'utf8', maxBuffer: 1 << 28 });
  const cut = js.indexOf('\nvar retasmFunc = asmFunc(');
  if (cut < 0 || !/^function asmFunc\(imports\) \{$/m.test(js)) throw new Error('wasm2js output changed shape');
  const head = js.slice(0, cut).replace(/^import \* as \w+ from '[^']*';\n/gm, '');
  if (/^\s*(import|export)\b/m.test(head)) throw new Error('wasm2js output still has an import or export');
  const ver = wasm2jsVersion();
  return [head, `/* ${path.basename(out)}: ${path.basename(wasm)} compiled to JS by ${ver} through tools/legacy/build.mjs,` +
    ` do not edit. */\n`];
}

async function app(wasm, out) {
  const [head, note] = wasm2js(wasm, out);
  const src = `${note}(function () {\n${head}\nwindow.cdomFactory = asmFunc;\n})();\n`;
  es5('wasm2js', src);
  return note + await small(path.basename(out), src);
}

// The names a script declares at its top level.
function declared(src) {
  const names = [];
  for (const s of acorn.parse(src, { ecmaVersion: 5, sourceType: 'script' }).body) {
    if (s.type === 'FunctionDeclaration') names.push(s.id.name);
    else if (s.type === 'VariableDeclaration') for (const d of s.declarations) names.push(d.id.name);
  }
  return names;
}

// The same app for a browser with no typed arrays (IE 9): every typed-array access in the wasm2js output becomes
// one on words.js's plain Array of 32-bit words (memoryWords), and the file says so with window.cdomFactory.
async function appWords(wasm, out) {
  memoryPass ??= await import('./memory.mjs');
  const [head, note] = wasm2js(wasm, out);
  const words = read(path.join(here, 'words.js'));
  const src = `${note}(function () {\n${words}\n${memoryPass.memoryWords(head, declared(words))}\nwindow.cdomFactory = asmFunc;\n})();\n`;
  es5('wasm2js', src);
  const code = await small(path.basename(out), await catchScoped(src), true);
  memoryPass.noTypedArrays(path.basename(out), code);
  return note + code;
}

// The boot is the one script every browser runs, so it must parse as ES3.
function page(src, out) {
  // Every page carries it, so its comments are cut.
  const src0 = read(path.join(top, 'shim/boot.js')), cuts = [];
  try { acorn.parse(src0, { ecmaVersion: 3, onComment: (b, t, s, e) => cuts.push([s, e]) }); } catch (e) {
    throw new Error(`shim/boot.js is not ES3: ${e.message}`);
  }
  let boot = '', at = 0;
  for (const [s, e] of cuts) { boot += src0.slice(at, s); at = e; }
  boot = (boot + src0.slice(at)).replace(/[ \t]+$/gm, '').replace(/\n{2,}/g, '\n').trim() + '\n';
  // The marker sits in a <script> of its own, after the shim's (see shim/boot.js).
  const html = read(src), mark = '/*CDOM_BOOT*/';
  if (html.split(mark).length !== 2) throw new Error(`${src} needs exactly one ${mark}`);
  // The table is a script of its own that tools/cdpage wrote before the shim; the fallback needs it too.
  if (!html.includes('var cdomLits=')) throw new Error(`${src} has no string table (tools/cdpage writes it before the shim)`);
  return html.replace(mark, () => boot);
}

let wasm2jsVer;
const wasm2jsVersion = () => (wasm2jsVer ??= execFileSync('wasm2js', ['--version'], { encoding: 'utf8' }).trim());

// Everything a build's output depends on, the one list: each file by content, and each tool by version.
// node_modules/.package-lock.json is the tree npm installed (it changes on any install, even one the
// lockfile beside it does not match), so it stands for Babel, preset-env, acorn and terser, whose own
// files are loaded by import, not read; the two vendored files the shim inlines are read, so they are
// named too. A build that reads a file not named here throws before it writes (see write()).
function inputs(cmd, a) {
  const files = [path.join(here, 'build.mjs'), path.join(here, 'package.json'), path.join(here, 'package-lock.json'),
    path.join(here, 'node_modules/.package-lock.json'), path.join(top, 'package.json'), path.join(top, 'package-lock.json'),
    path.join(top, 'node_modules/.package-lock.json')];
  const tools = ['node ' + process.version];
  if (cmd === 'shim' || cmd === 'shim-ie9' || cmd === 'shim-ie6') files.push(path.join(top, 'shim/cdom.js'), path.join(here, 'polyfills.js'),
    require.resolve('promise-polyfill/dist/polyfill.js'));
  if (cmd === 'shim' || cmd === 'shim-ie9') files.push(require.resolve('pepjs/dist/pep.js'));
  if (cmd === 'shim-ie9' || cmd === 'shim-ie6') files.push(path.join(here, 'memory-words.js'), path.join(here, 'memory.mjs'));
  if (cmd === 'shim-ie6') files.push(path.join(here, 'polyfills-es3.js'), path.join(here, 'dom-old.js'), path.join(here, 'shim-old.js'));
  else if (cmd === 'app') { files.push(path.resolve(a)); tools.push(wasm2jsVersion()); }
  // memory.mjs is imported, not read, so it is named here as build.mjs is.
  else if (cmd === 'app-ie9') {
    files.push(path.resolve(a), path.join(here, 'words.js'), path.join(here, 'memory.mjs'));
    tools.push(wasm2jsVersion());
  }
  else if (cmd === 'page') files.push(path.join(top, 'shim/boot.js'), path.resolve(a));
  return { files: files.map((f) => path.resolve(f)), tools };
}

const sha = (data) => crypto.createHash('sha256').update(data).digest('hex');

// The hash of the build itself (command, output name) and of each input; a missing input hashes as
// missing, so it differs from any content and the build that needs it fails as before.
function inputsHash(cmd, a, out, list) {
  const h = crypto.createHash('sha256').update(`${cmd}\0${path.basename(out)}\0`);
  for (const f of list.files) {
    let c;
    try { c = sha(fs.readFileSync(f)); } catch { c = 'missing'; }
    h.update(`${f}\0${c}\0`);
  }
  for (const t of list.tools) h.update(`${t}\0`);
  return h.digest('hex');
}

const stampOf = (out) => path.join(here, 'node_modules/.fresh', sha(path.resolve(out)).slice(0, 32));

function fresh(out, want) {
  try {
    const [got, made] = fs.readFileSync(stampOf(out), 'utf8').trim().split(' ');
    return got === want && made === sha(fs.readFileSync(out));
  } catch { return false; }
}

// Writes the output and then its stamp, so a build stopped between the two leaves an output whose stamp
// does not match it, and the next run builds again.
function write(out, data, want, list) {
  const named = new Set(list.files);
  const extra = [...opened].filter((f) => !named.has(f));
  if (extra.length) throw new Error(`build.mjs read ${extra.join(', ')}, which inputs() does not name`);
  fs.writeFileSync(out, data);
  fs.mkdirSync(path.dirname(stampOf(out)), { recursive: true });
  fs.writeFileSync(stampOf(out), `${want} ${sha(data)}\n`);
}

const [cmd, a, b] = process.argv.slice(2);
const builds = { shim: [1, (out) => shim(out, '')], 'shim-ie9': [1, (out) => shim(out, 'ie9')], 'shim-ie6': [1, (out) => shim(out, 'ie6')], app: [2, (out) => app(a, out)], 'app-ie9': [2, (out) => appWords(a, out)],
  page: [2, (out) => page(a, out)] };
if (cmd === 'inputs' && builds[a]) {
  const list = inputs(a, process.argv[4]);
  process.stdout.write(list.files.join('\n') + '\n');
} else if (builds[cmd] && process.argv.length === 4 + builds[cmd][0] - 1) {
  const out = builds[cmd][0] === 1 ? a : b, list = inputs(cmd, a), want = inputsHash(cmd, a, out, list);
  if (!fresh(out, want)) {
    acorn = await import('acorn');
    write(out, await builds[cmd][1](out), want, list);
  }
} else {
  console.error('usage: build.mjs shim <out.js> | shim-ie9 <out.js> | shim-ie6 <out.js> | app <in.wasm> <out.js> | app-ie9 <in.wasm> <out.js> | page <in.html> <out.html> | inputs <build> <args>');
  process.exit(2);
}
