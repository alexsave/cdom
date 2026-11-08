// Builds the files a browser without WebAssembly loads instead of cdom.js and app.wasm:
//   node tools/legacy/build.mjs shim <out/cdom.legacy.js>       the polyfills, then shim/cdom.js in ES5
//   node tools/legacy/build.mjs app <in.wasm> <out/in.wasm.js>   the same wasm compiled to JS by wasm2js
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
  const { minify } = createRequire(path.join(top, 'package.json'))('terser');
  const { code } = await minify(src, { ecma: 5, compress: { passes: 2 }, mangle: true });
  try { acorn.parse(code, { ecmaVersion: 5, sourceType: 'script' }); } catch (e) {
    throw new Error(`${name} is not ES5 once minified: ${e.message}`);
  }
  return code;
}

async function shim(out) {
  const { transformSync } = await import('@babel/core');
  const babel = transformSync(read(path.join(top, 'shim/cdom.js')), {
    babelrc: false, configFile: false, comments: false, sourceType: 'script',
    // The shim spreads and iterates only arrays, so the helpers need no Symbol.iterator.
    assumptions: { iterableIsArray: true, arrayLikeIsIterable: false, noDocumentAll: true },
    presets: [[require.resolve('@babel/preset-env'), { targets: TARGETS, modules: false }]],
  }).code;
  const parts = [
    ['polyfills', read(path.join(here, 'polyfills.js'))],
    ['promise-polyfill', read(require.resolve('promise-polyfill/dist/polyfill.js'))],
    ['pepjs', read(require.resolve('pepjs/dist/pep.js'))],
    ['shim', babel],
  ];
  const body = parts.map(([n, s]) => es5(n, s)).join('\n;\n');
  return '/* cdom.legacy.js: generated by tools/legacy/build.mjs, do not edit. */\n' + await small('cdom.legacy.js', body);
}

async function app(wasm, out) {
  opened.add(path.resolve(wasm));
  const js = execFileSync('wasm2js', ['-all', '-O2', wasm], { encoding: 'utf8', maxBuffer: 1 << 28 });
  // wasm2js writes an ES module: it imports each import module and exports what asmFunc returns. The page
  // has no module loader, so the imports come in as asmFunc's argument and asmFunc itself is handed over.
  const cut = js.indexOf('\nvar retasmFunc = asmFunc(');
  if (cut < 0 || !/^function asmFunc\(imports\) \{$/m.test(js)) throw new Error('wasm2js output changed shape');
  const head = js.slice(0, cut).replace(/^import \* as \w+ from '[^']*';\n/gm, '');
  if (/^\s*(import|export)\b/m.test(head)) throw new Error('wasm2js output still has an import or export');
  const ver = wasm2jsVersion();
  const src = `/* ${path.basename(out)}: ${path.basename(wasm)} compiled to JS by ${ver} through tools/legacy/build.mjs,` +
    ` do not edit. */\n(function () {\n${head}\nwindow.cdomFactory = asmFunc;\n})();\n`;
  es5('wasm2js', src);
  return `/* ${path.basename(out)}: ${path.basename(wasm)} compiled to JS by ${ver} through tools/legacy/build.mjs,` +
    ` do not edit. */\n` + await small(path.basename(out), src);
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
  if (cmd === 'shim') files.push(path.join(top, 'shim/cdom.js'), path.join(here, 'polyfills.js'),
    require.resolve('promise-polyfill/dist/polyfill.js'), require.resolve('pepjs/dist/pep.js'));
  else if (cmd === 'app') { files.push(path.resolve(a)); tools.push(wasm2jsVersion()); }
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
const builds = { shim: [1, (out) => shim(out)], app: [2, (out) => app(a, out)], page: [2, (out) => page(a, out)] };
if (cmd === 'inputs' && builds[a]) {
  const list = inputs(a, process.argv[4]);
  process.stdout.write(list.files.join('\n') + '\n');
} else if (builds[cmd] && process.argv.length === 4 + builds[cmd][0] - 1) {
  const out = builds[cmd][0] === 1 ? a : b, list = inputs(cmd, a), want = inputsHash(cmd, a, out, list);
  if (!fresh(out, want)) {
    acorn = await import('acorn');
    write(out, await builds[cmd][1](out), want, list);
  }
} else { console.error('usage: build.mjs shim <out.js> | app <in.wasm> <out.js> | page <in.html> <out.html> | inputs <build> <args>'); process.exit(2); }
