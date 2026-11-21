// js1c's checks in node (docs/js1-compile-design.md): tests/js1diff.c run as real wasm and as js1c's JavaScript 1.0
// (tests/out/js1diff.wasm.js1.js) must agree on every return, every out() call, every trap and every byte of memory;
// the JavaScript call depth, counted by wrapping every compiled function, stays at or under the bound js1c printed;
// every js1c output passes the JavaScript 1.0 gate (tools/js10.mjs). JS1_MEASURE=1 also prints section 9's
// measurements (sizes, statements, timings), which are never asserted. node tools/js1.mjs; exit 1 on any failure.
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import zlib from 'node:zlib';
import { execFileSync, spawnSync } from 'node:child_process';
import * as acorn from 'acorn';
import { lacks10 } from './js10.mjs';

// node tools/js1.mjs bench <file> <wasm|ie9|js1> <export> <arg> <reps> [budget]: the mean ms of one call, measured in a
// process of its own with the file run in the main realm (as a browser runs a script: its globals are the global
// object's, which a vm context's interceptors would slow); the measurements below call it.
if (process.argv[2] === 'bench') {
  const [, , , file, kind, name, arg, reps, budget] = process.argv;
  let f;
  if (kind === 'wasm') {
    const { instance } = await WebAssembly.instantiate(fs.readFileSync(file), { env: { out() {} } });
    f = () => instance.exports[name](+arg);
  } else {
    globalThis.window = globalThis;
    (0, eval)(fs.readFileSync(file, 'utf8'));
    const o = globalThis.cdomFactory({ env: { out() {} } });
    if (budget) f = () => { globalThis.JB = +budget; o[name](+arg); while (o.js1_paused) o.js1_more(); };
    else f = () => o[name](+arg);
  }
  for (let i = 0; i < 20; i++) f();
  const t0 = process.hrtime.bigint();
  for (let i = 0; i < +reps; i++) f();
  console.log(Number(process.hrtime.bigint() - t0) / +reps / 1e6);
  process.exit(0);
}
// node tools/js1.mjs page: tests/out/js1diff.html, the page for the real browsers (docs/js1-compile-design.md,
// section 11). Netscape 2 has no SCRIPT SRC, so js1diff.wasm.js1.js is inlined; a subset of the cases, with wasm's
// answers computed here, runs one a tick (setTimeout with a string), the long ones sliced, and the page writes each
// result, its time and the tally into a TEXTAREA. All of it JavaScript 1.0, checked by the gate.
if (process.argv[2] === 'page') {
  const dir = path.resolve(path.dirname(new URL(import.meta.url).pathname), '../tests/out');
  const log = [];
  const { instance } = await WebAssembly.instantiate(fs.readFileSync(path.join(dir, 'js1diff.wasm')), { env: { out: (v) => log.push(v | 0) } });
  const x = instance.exports;
  const cases = [['ints', 7, 3], ['ints', -0x80000000, -1], ['ints', 0x55aa55aa, 37], ['ints', -123456789, 0], ['i64s', 0x12345678, 0x9abcdef0 | 0, 0, 17],
    ['i64s', -5, 12345, 7, -99], ['i64s', 0x7fffffff, -1, 0, 3], ['dbl', 7, 3], ['dbl', -2000000000, 1000], ['dbl', 123456, -4], ['mem', 3], ['mem', 77777],
    ['ind', 100], ['sw', 200], ['loop', 1000], ['rec', 100], ['rec', 299], ['rec', 1000], ['loop', 100000], ['trap', 0], ['trap', 1], ['divz', 7, 0],
    ['divz', -0x80000000, -1], ['ind_bad', 1], ['deeptrap', 300], ['rec', 5000]];
  const lines = [];
  cases.forEach(([n, ...a], i) => {
    log.length = 0;
    let r;
    try { r = String(x[n](...a)); } catch (e) { r = 'trap'; }
    const args = [0, 0, 0, 0].map((d, k) => (a[k] === undefined ? 0 : a[k]));
    lines.push(`CN[${i}]="${n}";CA[${i}]=${args[0]};CB[${i}]=${args[1]};CC[${i}]=${args[2]};CD[${i}]=${args[3]};CE[${i}]="${r + log.map((v) => ',' + v).join('')}";`);
  });
  const js1 = fs.readFileSync(path.join(dir, 'js1diff.wasm.js1.js'), 'utf8');
  const test = `var CN=new Object(),CA=new Object(),CB=new Object(),CC=new Object(),CD=new Object(),CE=new Object();
${lines.join('\n')}
var NC=${cases.length},I=0,OK=0,L="",O=0,T0=0,R0="";
function out(v){L=L+","+v;}
function show(s){document.forms[0].elements[0].value=document.forms[0].elements[0].value+s+"\\n";}
function now(){var d=new Date();return d.getTime();}
function finish(r){var got,ms;if(O.js1_trap)r="trap";got=r+L;ms=now()-T0;
if(got==CE[I]){OK++;show("ok   "+CN[I]+"("+CA[I]+","+CB[I]+") "+ms+" ms");}
else show("FAIL "+CN[I]+"("+CA[I]+","+CB[I]+") "+ms+" ms: got "+got.substring(0,120)+" want "+CE[I].substring(0,120));
I++;setTimeout("step()",0);}
function more(){if(O.js1_more()){setTimeout("more()",0);return;}finish(O.js1_result);}
function step(){var r;if(I>=NC){show(OK+" of "+NC+" agree with wasm");document.forms[0].elements[1].value=OK+" of "+NC;return;}
L="";T0=now();JB=1000;r=O[CN[I]](CA[I],CB[I],CC[I],CD[I]);if(O.js1_paused){setTimeout("more()",0);return;}finish(r);}
function start(){var env=new Object(),imp=new Object();env.out=out;imp.env=env;O=cdomFactoryJs1(imp);
show(navigator.userAgent);show("loaded; "+NC+" cases, each a tick, the driver sliced at 1000 steps");setTimeout("step()",0);}
`;
  const bad = lacks10(js1 + test);
  if (bad.length) { console.error('js1.mjs page: not JavaScript 1.0: ' + bad.slice(0, 5).join(', ')); process.exit(1); }
  const html = `<HTML><HEAD><TITLE>js1c differential test</TITLE></HEAD>
<BODY>
<H3>js1c: tests/js1diff.c compiled to JavaScript 1.0 against wasm's answers</H3>
<FORM><TEXTAREA NAME="t" ROWS="32" COLS="100"></TEXTAREA><BR><INPUT TYPE="text" NAME="tally" SIZE="20" VALUE="running"></FORM>
<SCRIPT LANGUAGE="JavaScript">
${js1}</SCRIPT>
<SCRIPT LANGUAGE="JavaScript">
${test}start();
</SCRIPT>
</BODY></HTML>
`;
  fs.writeFileSync(path.join(dir, 'js1diff.html'), html);
  process.exit(0);
}
const top = path.resolve(path.dirname(new URL(import.meta.url).pathname), '..');
const out = path.join(top, 'tests/out');
let fails = 0;
const ok = (c, m) => { if (!c) { fails++; console.log('FAIL', m); } else console.log('ok  ', m); };

// A js1c file in a fresh context: its factory's exports, the out() log, and the deepest JavaScript call depth seen
// through the compiled functions, the driver, the export wrappers and the imports (each wrapped to count).
export function loadJs1(file, { count = true } = {}) {
  const src = fs.readFileSync(file, 'utf8');
  const ctx = { window: {}, log: [] };
  vm.createContext(ctx);
  vm.runInContext(src, ctx, { filename: file });
  const st = { depth: 0, max: 0 };
  ctx.st = st;
  if (count) {
    for (const k of Object.keys(ctx)) {
      if (!/^(J[fdx]\d+|Jgo|Jmore|Jstart\d+)$/.test(k) || typeof ctx[k] !== 'function') continue;
      const f = ctx[k];
      ctx[k] = function (...a) { st.depth++; if (st.depth > st.max) st.max = st.depth; try { return f.apply(this, a); } finally { st.depth--; } };
    }
  }
  const env = { out: (v) => { st.depth++; if (st.depth > st.max) st.max = st.depth; ctx.log.push(v | 0); st.depth--; } };
  const o = ctx.window.cdomFactory({ env });
  ctx.X = o;
  // A call with a time limit: a miscompiled loop that never ends is a hung call (the string 'hung'), not a hung run.
  const call = (name, ...a) => {
    try { return vm.runInContext(`X[${JSON.stringify(name)}](${a.map(Number).join(',')})`, ctx, { timeout: 5000 }); } catch (e) {
      if (/timed out/.test(String(e))) { st.depth = 0; return 'hung'; }
      throw e;
    }
  };
  return { o, ctx, st, call };
}
async function loadWasm(file) {
  const log = [];
  const { instance } = await WebAssembly.instantiate(fs.readFileSync(file), { env: { out: (v) => log.push(v | 0) } });
  return { x: instance.exports, log };
}
function loadIe9(file) {
  const ctx = { window: {}, log: [] };
  vm.createContext(ctx);
  vm.runInContext(fs.readFileSync(file, 'utf8'), ctx, { filename: file });
  const x = ctx.window.cdomFactory({ env: { out: (v) => ctx.log.push(v | 0) } });
  return { x, log: ctx.log };
}
const E = [0, 1, -1, 2, 7, -7, 31, 32, 33, 255, 256, 65535, 65536, 0x7fffffff, -0x80000000, 0x55aa55aa, -123456789, 1000000007, 3, 100];
const CASES = [];
for (const a of E) for (const b of [0, 1, -1, 5, 31, 37, -0x80000000, 0x7fffffff, 0x12345678, -3]) CASES.push(['ints', a, b]);
for (const [a, b, c, d] of [[0, 1, 0, 1], [0, 0, 0, 0], [-1, -1, 0, 1], [0x7fffffff, -1, 0, 3], [-0x80000000, 0, -1, -1], [0x12345678, 0x9abcdef0 | 0, 0, 17],
  [0, 1000000007, 0, 10], [-5, 12345, 7, -99], [1, 0, 0, 63], [0x0badf00d, 0x12345, -2, 0x7ff], [0, 0, 0, 0x40]]) CASES.push(['i64s', a, b, c, d]);
for (const a of [0, 1, -1, 7, 100, -100, 123456, -2000000000, 2000000000]) for (const b of [0, 3, -4, 1000]) CASES.push(['dbl', a, b]);
for (const n of [0, 1, 10, 1000]) CASES.push(['loop', n]);
for (const n of [0, 1, 10, 300, 1000]) CASES.push(['rec', n]);
for (const a of [0, 1, 3, -1, 200, 77777]) CASES.push(['mem', a]);
for (const n of [0, 1, 5, 100]) CASES.push(['ind', n]);
for (const n of [0, 1, 11, 50, 200]) CASES.push(['sw', n]);
CASES.push(['ind_bad', 1], ['trap', 0], ['trap', 1], ['divz', 7, 2], ['divz', 7, 0], ['divz', -0x80000000, -1], ['deeptrap', 50], ['ints', 3, 4]);

const wasmFile = path.join(out, 'js1diff.wasm'), js1File = path.join(out, 'js1diff.wasm.js1.js');
const W = await loadWasm(wasmFile);
const J = loadJs1(js1File);
const words = (u8, p, n) => { const a = []; for (let i = 0; i < n; i++) a.push(u8[p + i]); return a.join(','); };
let agree = 0, first = null;
for (const [name, ...args] of CASES) {
  W.log.length = 0; J.ctx.log.length = 0;
  let wr, wt = false;
  try { wr = W.x[name](...args); } catch (e) { wt = e instanceof WebAssembly.RuntimeError; if (!wt) throw e; }
  const jr = J.call(name, ...args), jt = !!J.o.js1_trap;
  const same = wt === jt && (wt || wr === jr) && W.log.join(',') === J.ctx.log.join(',');
  if (same) agree++;
  else if (!first) first = `${name}(${args}) wasm ${wt ? 'trap' : wr} [${W.log.slice(0, 40)}] js1 ${jt ? 'trap' : jr} [${J.ctx.log.slice(0, 40)}]`;
}
ok(agree === CASES.length, `every export agrees with wasm: ${agree} of ${CASES.length} cases${first ? `\n  first difference: ${first}` : ''}`);
// The whole memory, word by word, after all of it (the C stack, the statics, the packed structs).
const mem = new Int32Array(W.x.memory.buffer), jw = J.o.memory.words;
let diff = -1;
for (let i = 0; i < mem.length; i++) if (mem[i] !== ((jw[i] | 0))) { diff = i; break; }
ok(diff < 0, `the memory agrees word for word after every case${diff < 0 ? '' : `: word ${diff} is ${mem[diff]} in wasm and ${jw[diff]} in js1`}`);
const bp = W.x.buf();
ok(words(new Uint8Array(W.x.memory.buffer), bp, 64).length > 0 && J.o.buf() === bp, 'buf() is the same address in both');
// Traps: each one in its own case above; here, that a trap does not leave the next call broken.
J.call('trap', 1); const afterTrap = J.call('loop', 10);
ok(J.o.js1_trap === 0 && afterTrap === W.x.loop(10), `after a trap the next call runs: trap flag ${J.o.js1_trap}, loop(10) ${afterTrap}`);
J.call('deeptrap', 30); const sp = J.ctx.JP;
ok(J.o.js1_trap === 1 && sp === 0, `a trap 30 calls down puts the explicit stack back: JP ${sp}`);

// Netscape 2.02 dies on undefined | 0 ("33041 is not a number.", docs/js1-compile-design.md section 12): every case again,
// on a fresh instance whose memory throws on a read of a word nothing wrote, as Netscape 2 does, and memory.grow's new
// words too.
{
  const N = loadJs1(js1File, { count: false });
  const strict = (o) => new Proxy(o, { get(t, k) { if (typeof k === 'string' && /^\d+$/.test(k) && !(k in t)) throw new Error(`${k} is not a number`); return t[k]; } });
  N.ctx.JM = strict(N.ctx.JM);
  let nok = 0, nfirst = null;
  W.log.length = 0;
  const W2 = await loadWasm(wasmFile);
  for (const [name, ...args] of CASES) {
    W2.log.length = 0; N.ctx.log.length = 0;
    let wr, wt = false, jr, err = null;
    try { wr = W2.x[name](...args); } catch (e) { wt = true; }
    try { jr = N.call(name, ...args); } catch (e) { err = String(e.message || e); N.ctx.JP = 0; N.ctx.JQ = 0; }
    const jt = !!N.o.js1_trap;
    if (!err && wt === jt && (wt || wr === jr) && W2.log.join(',') === N.ctx.log.join(',')) nok++;
    else if (!nfirst) nfirst = `${name}(${args}): ${err || 'differs'}`;
  }
  ok(nok === CASES.length, `every case agrees on a memory that throws on a word never written (Netscape 2): ${nok} of ${CASES.length}${nfirst ? `\n  first: ${nfirst}` : ''}`);
  const fresh = loadJs1(js1File, { count: false }), pages = fresh.ctx.JMp;
  let hole = -1;
  for (let i = 0; i < pages * 16384 && hole < 0; i++) if (!(i in fresh.ctx.JM)) hole = i;
  const got = fresh.call('grow', 2);
  for (let i = pages * 16384; i < (pages + 2) * 16384 && hole < 0; i++) if (!(i in fresh.ctx.JM)) hole = i;
  ok(hole < 0 && got === pages, `every word of the initial memory (${pages} pages) and of memory.grow's 2 new pages is written${hole < 0 ? '' : `: word ${hole} is not`}`);
}

// Netscape 3.04 converts to 32 bits right only from -2^31 to 2^32 - 1, and gives NaN outside (x | 0, x >>> 0, x & 255,
// x << 1 and the rest: tests/probe-js1e.html, docs/js1-compile-design.md section 12). Every case again on a copy of the
// file whose every bitwise operand goes through a check that throws outside that range, as Netscape 3 would answer NaN.
{
  const src = fs.readFileSync(js1File, 'utf8'), ins = [];
  const BIT = new Set(['|', '&', '^', '<<', '>>', '>>>']);
  const wrap = (n) => { ins.push([n.start, 'J3c(', -(n.end - n.start)]); ins.push([n.end, ')', n.end - n.start]); };
  (function w(n) {
    if (!n || typeof n.type !== 'string') return;
    if (n.type === 'BinaryExpression' && BIT.has(n.operator)) { wrap(n.left); wrap(n.right); }
    if (n.type === 'UnaryExpression' && n.operator === '~') wrap(n.argument);
    if (n.type === 'AssignmentExpression' && n.operator !== '=' && BIT.has(n.operator.slice(0, -1))) wrap(n.right);
    for (const k of Object.keys(n)) { const v = n[k]; if (Array.isArray(v)) v.forEach(w); else if (v && typeof v.type === 'string') w(v); }
  })(acorn.parse(src, { ecmaVersion: 5 }));
  // Inserts at one place: closings of inner operands first, then openings of outer ones.
  ins.sort((a, b) => b[0] - a[0] || b[2] - a[2]);
  let t = src;
  for (const [at, text] of ins) t = t.slice(0, at) + text + t.slice(at);
  const ns3File = path.join(out, 'js1diff.ns3.js');
  fs.writeFileSync(ns3File, 'function J3c(x){if(typeof x=="number"&&!(x>=-2147483648&&x<4294967296))throw new Error("Netscape 3 NaN: "+x);return x;}\n' + t);
  const N = loadJs1(ns3File, { count: false });
  const W3 = await loadWasm(wasmFile);
  let nok = 0, nfirst = null;
  for (const [name, ...args] of CASES) {
    W3.log.length = 0; N.ctx.log.length = 0;
    let wr, wt = false, jr, err = null;
    try { wr = W3.x[name](...args); } catch (e) { wt = true; }
    try { jr = N.call(name, ...args); } catch (e) { err = String(e.message || e); N.ctx.JP = 0; N.ctx.JQ = 0; }
    const jt = !!N.o.js1_trap;
    if (!err && wt === jt && (wt || wr === jr) && W3.log.join(',') === N.ctx.log.join(',')) nok++;
    else if (!nfirst) nfirst = `${name}(${args}): ${err || 'differs'}`;
  }
  fs.unlinkSync(ns3File);
  ok(nok === CASES.length && ins.length > 100, `every case agrees with every bitwise operand in Netscape 3's range, -2^31 to 2^32 - 1 (${ins.length / 2} operands checked): ${nok} of ${CASES.length}${nfirst ? `\n  first: ${nfirst}` : ''}`);
}

// Depth: the 1000-deep recursion runs at a constant JavaScript depth, at or under js1c's bound.
const lowered = js1File + '.lowered.wasm';
const said = spawnSync(path.join(top, 'tools/out/js1c'), [lowered, path.join(out, 'js1.tmp.js')], { encoding: 'utf8' });
fs.unlinkSync(path.join(out, 'js1.tmp.js'));
const B = +(/JS depth bound (\d+)/.exec(said.stderr) || [0, 0])[1];
J.st.max = 0; J.call('rec', 1000);
const d1000 = J.st.max;
J.st.max = 0; J.call('rec', 10);
ok(d1000 <= B && d1000 === J.st.max, `rec(1000) reaches JavaScript depth ${d1000}, the same as rec(10) (${J.st.max}), at or under the bound ${B}`);
let dmax = 0;
for (const [name, ...args] of CASES) { J.st.max = 0; J.call(name, ...args); if (J.st.max > dmax) dmax = J.st.max; }
ok(dmax <= B, `the deepest JavaScript depth over every case is ${dmax}, at or under the bound ${B}`);
// The gate on every js1c output this build wrote; and no ++ or -- at all (Netscape 3.04 refuses one at column 254,
// tools/legacy/js11.mjs's seam rule: js1c writes none, so no line's length can put one there).
for (const f of fs.readdirSync(out).filter((f) => f.endsWith('.js1.js'))) {
  const src = fs.readFileSync(path.join(out, f), 'utf8');
  const found = lacks10(src);
  ok(found.length === 0, `${f} is JavaScript 1.0 (tools/js10.mjs)${found.length ? ': ' + found.slice(0, 5).join(', ') : ''}`);
  const incdec = [...acorn.tokenizer(src, { ecmaVersion: 5, locations: true })].filter((t) => t.type.label === '++/--');
  ok(incdec.length === 0, `${f} has no ++ or --${incdec.length ? `: ${incdec.length}, the first on line ${incdec[0].loc.start.line}` : ''}`);
}
// Time slicing: with a budget, the export pauses and Jmore() goes on, to the same result.
{
  const S = loadJs1(js1File, { count: false });
  S.ctx.JB = 50;
  let r = S.call('rec', 1000), steps = 0;
  const paused = S.o.js1_paused;
  while (S.o.js1_paused && steps < 100000 && S.call('js1_more') !== 'hung') steps++;
  r = S.o.js1_result;
  W.log.length = 0;
  ok(paused === 1 && steps > 10 && r === W.x.rec(1000), `sliced at 50 steps, rec(1000) pauses (${paused}), goes on ${steps} times and gives wasm's ${r}`);
}

// tests/out/js1diff.html, the real browsers' page, run here with a stand-in document: every case agrees.
{
  const html = fs.readFileSync(path.join(out, 'js1diff.html'), 'utf8');
  const scripts = [...html.matchAll(/<SCRIPT LANGUAGE="JavaScript">\n([\s\S]*?)<\/SCRIPT>/g)].map((m) => m[1]);
  const ta = { value: '' }, tally = { value: '' }, q = [];
  const ctx = { window: {}, navigator: { userAgent: 'node' }, document: { forms: { 0: { elements: { 0: ta, 1: tally } } } }, setTimeout: (s) => q.push(s), Date };
  ctx.window = ctx;
  vm.createContext(ctx);
  for (const sc of scripts) vm.runInContext(sc, ctx);
  try { for (let n = 0; q.length && n < 20000; n++) vm.runInContext(q.shift(), ctx, { timeout: 5000 }); } catch (e) { ta.value += `FAIL the page stopped: ${e}\n`; }
  const m = /(\d+) of (\d+)/.exec(tally.value);
  ok(scripts.length === 2 && m && m[1] === m[2], `tests/out/js1diff.html runs to its end and agrees: ${tally.value}${m && m[1] === m[2] ? '' : '\n' + ta.value.split('\n').filter((l) => l.startsWith('FAIL')).slice(0, 3).join('\n')}`);
}
if (process.env.JS1_MEASURE) await measure();
console.log(fails ? `${fails} FAILED` : 'all ok');
process.exit(fails ? 1 : 0);

async function measure() {
  const gz = (b) => zlib.gzipSync(b, { level: 9 }).length;
  const stmts = (src) => { let n = 0; (function w(x) { if (!x || typeof x.type !== 'string') return; if (/Statement$|Declaration$/.test(x.type)) n++; for (const k of Object.keys(x)) { const v = x[k]; if (Array.isArray(v)) v.forEach(w); else if (v && typeof v.type === 'string') w(v); } })(acorn.parse(src, { ecmaVersion: 5 })); return n; };
  const instrs = (wasm) => { const t = execFileSync('wasm-dis', [wasm], { encoding: 'utf8', maxBuffer: 1 << 28 }); return (t.match(/\((?:i32|f64|f32|i64|local|global|call|call_indirect|br|br_if|br_table|block|loop|if|select|drop|return|unreachable|memory|nop)\b[.\w]*/g) || []).length; };
  for (const app of ['frames', 'js1diff']) {
    const j = fs.readFileSync(path.join(out, `${app}.wasm.js1.js`)), i9 = fs.readFileSync(path.join(out, `${app}.wasm.ie9.js`));
    console.log(`measure ${app}: js1 ${j.length} bytes, ${gz(j)} gzipped; ie9 ${i9.length} bytes, ${gz(i9)} gzipped; ratio ${(j.length / i9.length).toFixed(2)}, gzipped ${(gz(j) / gz(i9)).toFixed(2)}`);
    const n = stmts(j.toString()), w = instrs(path.join(out, `${app}.wasm.js1.js.lowered.wasm`));
    console.log(`measure ${app}: ${n} statements, ${w} instructions in the lowered module: ${(n / w).toFixed(2)} a wasm instruction; ie9 ${stmts(i9.toString())} statements`);
  }
  for (const f of ['frames.wasm.js1.js', 'frames.wasm.js1s.js']) {
    const p = path.join(out, f);
    if (!fs.existsSync(p)) continue;
    const src = fs.readFileSync(p, 'utf8'), data = /function Jdata0\(\)\{[\s\S]*?\n(?=function cdomFactoryJs1)/.exec(src)[0];
    const t0 = process.hrtime.bigint();
    for (let i = 0; i < 20; i++) loadJs1(p, { count: false });
    const ms = Number(process.hrtime.bigint() - t0) / 20e6;
    console.log(`measure data ${f}: ${data.length} bytes, ${stmts(data)} statements; the whole file loads and its factory runs in ${ms.toFixed(2)} ms`);
  }
  const bench = (file, kind, name, arg, reps, budget) => { const t = []; for (let i = 0; i < 5; i++) t.push(+execFileSync(process.execPath, [new URL(import.meta.url).pathname, 'bench', file, kind, name, String(arg), String(reps), ...(budget ? [String(budget)] : [])], { encoding: 'utf8' })); return t.sort((x, y) => x - y)[2]; };
  const ie9File = path.join(out, 'js1diff.wasm.ie9.js');
  for (const [k, arg, reps] of [['loop', 100000, 50], ['rec', 1000, 200]]) {
    const tw = bench(wasmFile, 'wasm', k, arg, reps), ti = bench(ie9File, 'ie9', k, arg, reps), tj = bench(js1File, 'js1', k, arg, reps);
    console.log(`measure ${k}(${arg}), the median of 5 processes: wasm ${tw.toFixed(3)} ms, ie9 ${ti.toFixed(3)} ms, js1 ${tj.toFixed(3)} ms: js1 is ${(tj / ti).toFixed(2)} times ie9 and ${(tj / tw).toFixed(1)} times wasm`);
    const ts = bench(js1File, 'js1', k, arg, reps, 1000);
    console.log(`measure ${k}(${arg}) sliced at 1000 steps: ${ts.toFixed(3)} ms, ${((ts / tj - 1) * 100).toFixed(1)}% against unsliced`);
  }
}
