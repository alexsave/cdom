// Builds the files a browser without WebAssembly loads instead of cdom.js and app.wasm:
//   node tools/legacy/build.mjs shim <out/cdom.legacy.js>       the polyfills, then shim/cdom.js in ES5
//   node tools/legacy/build.mjs app <in.wasm> <out/in.wasm.js>   the same wasm compiled to JS by wasm2js
//   node tools/legacy/build.mjs page <in.html> <out.html>        inlines shim/boot.js at /*CDOM_BOOT*/
// Both outputs must parse as ES5 and name no ES2015 global or method that the polyfills do not supply,
// or nothing is written; both are then minified by the repo root's terser (the one bench/build.sh minifies
// the shim with) at ecma 5, and checked as ES5 again. Needs `npm ci` here and at the repo root, and
// wasm2js (binaryen) on PATH.
import fs from 'node:fs';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { createRequire } from 'node:module';
import * as acorn from 'acorn';
import { transformSync } from '@babel/core';
import { memoryWords, noTypedArrays, TYPED } from './memory.mjs';

const here = path.dirname(new URL(import.meta.url).pathname);
const require = createRequire(import.meta.url);
const { minify } = createRequire(path.join(here, '../../package.json'))('terser');
const read = (p) => fs.readFileSync(p, 'utf8');

// What a browser without WebAssembly is assumed to be: typed arrays, but nothing from ES2015.
const TARGETS = 'ie 11, safari 9, chrome 49, firefox 45, android 4.4, ios 9';
const GLOBALS = new Set(['Symbol', 'Map', 'Set', 'WeakMap', 'WeakSet', 'Reflect', 'Proxy', 'BigInt', 'globalThis']);
const METHODS = new Set(['includes', 'find', 'findIndex', 'fill', 'repeat', 'padStart', 'padEnd', 'endsWith',
  'assign', 'entries', 'values', 'from', 'of', 'codePointAt', 'fromCodePoint', 'flat', 'flatMap', 'at',
  'trimStart', 'trimEnd', 'copyWithin', 'allSettled', 'any']);
// The only names allowed past the scan, each in the one file that may use it: promise-polyfill defines
// Promise.allSettled and Promise.any itself; PEP uses Map only behind a check for it, and includes only on
// touch-action strings; wasm2js output calls copyWithin and fill on a Uint8Array; the shim reads
// globalThis.crypto and Object.entries, and Babel's own helpers name Symbol only behind a typeof check.
// polyfills.js supplies (and so names) everything here but Symbol. wasm2js output also reads the shim's
// import named "at" (cd_at) off the imports object, which the scan cannot tell from Array.prototype.at.
const ALLOWED = { 'promise-polyfill': new Set(['.allSettled', '.any']), pepjs: new Set(['Map', '.includes']),
  polyfills: new Set(['.includes', '.copyWithin', '.fill', 'globalThis', '.entries']),
  wasm2js: new Set(['.copyWithin', '.fill', '.at']), shim: new Set(['Symbol', 'globalThis', '.entries']) };

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

// Minified as ES5: terser keeps the syntax at ecma 5, and the parse proves it.
async function small(name, src) {
  const { code } = await minify(src, { ecma: 5, compress: { passes: 2 }, mangle: true });
  try { acorn.parse(code, { ecmaVersion: 5, sourceType: 'script' }); } catch (e) {
    throw new Error(`${name} is not ES5 once minified: ${e.message}`);
  }
  return code;
}

// The shim for a browser without typed arrays: shim/cdom.js's C-memory functions are each replaced by
// memory-words.js's function of the same name, every one of which must find its match; what memory-words.js
// declares besides goes in front of the first. One shim source, two backings of its memory.
function wordsBacked({ types: t, parseSync: parse }) {
  return {
    visitor: {
      Program(p) {
        const own = parse(read(path.join(here, 'memory-words.js')), { sourceType: 'script', configFile: false, babelrc: false }).program.body;
        // The shim is one `var cdom = (() => { ... })();`: its functions are the statements of that arrow's body.
        const init = p.node.body.length === 1 && p.node.body[0].declarations?.[0]?.init;
        const body = init && t.isCallExpression(init) && t.isArrowFunctionExpression(init.callee) && init.callee.body.body;
        if (!body) throw new Error('shim-ie9: shim/cdom.js is no longer one var cdom = (() => { ... })()');
        const at = (name) => body.findIndex((n) => t.isFunctionDeclaration(n) && n.id.name === name);
        const extra = own.filter((n) => !t.isFunctionDeclaration(n));
        let first = -1;
        for (const f of own.filter((n) => t.isFunctionDeclaration(n))) {
          const i = at(f.id.name);
          if (i < 0) throw new Error(`shim-ie9: memory-words.js's ${f.id.name} has no function of that name in shim/cdom.js`);
          body[i] = f;
          first = first < 0 ? i : Math.min(first, i);
        }
        body.splice(first, 0, ...extra);
      },
    },
  };
}

// Typed-array names in the compiled shim, by the function they are in: only seed and randomBytes may have any,
// both behind a check for crypto.getRandomValues, which no browser without typed arrays has.
function typedOutside(code, allowed) {
  const found = new Set();
  const names = new Set(TYPED);
  (function walk(node, fn) {
    if (!node || typeof node.type !== 'string') return;
    if (node.type === 'FunctionDeclaration') fn = node.id.name;
    if (node.type === 'Identifier' && names.has(node.name) && !allowed.has(fn)) found.add(`${node.name} in ${fn || 'the top level'}`);
    for (const k of Object.keys(node)) {
      const v = node[k];
      if (Array.isArray(v)) for (const c of v) walk(c, fn); else if (v && typeof v === 'object' && k !== 'loc') walk(v, fn);
    }
  })(acorn.parse(code, { ecmaVersion: 5, sourceType: 'script' }), null);
  return [...found];
}

async function shim(out, ie9) {
  const babel = transformSync(read(path.join(here, '../../shim/cdom.js')), {
    babelrc: false, configFile: false, comments: false, sourceType: 'script',
    // The shim spreads and iterates only arrays, so the helpers need no Symbol.iterator.
    assumptions: { iterableIsArray: true, arrayLikeIsIterable: false, noDocumentAll: true },
    plugins: ie9 ? [wordsBacked] : [],
    presets: [[require.resolve('@babel/preset-env'), { targets: ie9 ? `${TARGETS}, ie 9` : TARGETS, modules: false }]],
  }).code;
  if (ie9) {
    const left = typedOutside(babel, new Set(['seed', 'randomBytes']));
    if (left.length) throw new Error(`shim-ie9: typed arrays left in the shim: ${left.join(', ')}`);
  }
  const parts = [
    ['polyfills', read(path.join(here, 'polyfills.js'))],
    ['promise-polyfill', read(require.resolve('promise-polyfill/dist/polyfill.js'))],
    ['pepjs', read(require.resolve('pepjs/dist/pep.js'))],
    ['shim', babel],
  ];
  const body = parts.map(([n, s]) => es5(n, s)).join('\n;\n');
  const name = path.basename(out);
  fs.writeFileSync(out, `/* ${name}: generated by tools/legacy/build.mjs, do not edit. */\n` + await small(name, body));
}

// wasm2js output as a script: wasm2js writes an ES module that imports each import module and exports what
// asmFunc returns. The page has no module loader, so the imports come in as asmFunc's argument and asmFunc
// itself is handed over. Returns [the script's body, the comment line naming what made it].
function wasm2js(wasm, out) {
  const js = execFileSync('wasm2js', ['-all', '-O2', wasm], { encoding: 'utf8', maxBuffer: 1 << 28 });
  const cut = js.indexOf('\nvar retasmFunc = asmFunc(');
  if (cut < 0 || !/^function asmFunc\(imports\) \{$/m.test(js)) throw new Error('wasm2js output changed shape');
  const head = js.slice(0, cut).replace(/^import \* as \w+ from '[^']*';\n/gm, '');
  if (/^\s*(import|export)\b/m.test(head)) throw new Error('wasm2js output still has an import or export');
  const ver = execFileSync('wasm2js', ['--version'], { encoding: 'utf8' }).trim();
  return [head, `/* ${path.basename(out)}: ${path.basename(wasm)} compiled to JS by ${ver} through tools/legacy/build.mjs,` +
    ` do not edit. */\n`];
}

async function app(wasm, out) {
  const [head, note] = wasm2js(wasm, out);
  const src = `${note}(function () {\n${head}\nwindow.cdomFactory = asmFunc;\n})();\n`;
  es5('wasm2js', src);
  fs.writeFileSync(out, note + await small(path.basename(out), src));
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
  const [head, note] = wasm2js(wasm, out);
  const src = `${note}(function () {\n${read(path.join(here, 'words.js'))}\n${memoryWords(head, declared(read(path.join(here, 'words.js'))))}\nwindow.cdomFactory = asmFunc;\n})();\n`;
  es5('wasm2js', src);
  const code = await small(path.basename(out), src);
  noTypedArrays(path.basename(out), code);
  fs.writeFileSync(out, note + code);
}

// The boot is the one script every browser runs, so it must parse as ES3.
function page(src, out) {
  // Every page carries it, so its comments are cut.
  const src0 = read(path.join(here, '../../shim/boot.js')), cuts = [];
  try { acorn.parse(src0, { ecmaVersion: 3, onComment: (b, t, s, e) => cuts.push([s, e]) }); } catch (e) {
    throw new Error(`shim/boot.js is not ES3: ${e.message}`);
  }
  let boot = '', at = 0;
  for (const [s, e] of cuts) { boot += src0.slice(at, s); at = e; }
  boot = (boot + src0.slice(at)).replace(/[ \t]+$/gm, '').replace(/\n{2,}/g, '\n').trim() + '\n';
  // The marker sits in a <script> of its own, after the shim's (see shim/boot.js).
  const html = read(src), mark = '/*CDOM_BOOT*/';
  if (html.split(mark).length !== 2) throw new Error(`${src} needs exactly one ${mark}`);
  fs.writeFileSync(out, html.replace(mark, () => boot));
}

const [cmd, a, b] = process.argv.slice(2);
if (cmd === 'shim' && a) await shim(a, false);
else if (cmd === 'shim-ie9' && a) await shim(a, true);
else if (cmd === 'app' && a && b) await app(a, b);
else if (cmd === 'app-ie9' && a && b) await appWords(a, b);
else if (cmd === 'page' && a && b) page(a, b);
else { console.error('usage: build.mjs shim <out.js> | shim-ie9 <out.js> | app <in.wasm> <out.js> | app-ie9 <in.wasm> <out.js> | page <in.html> <out.html>'); process.exit(2); }
