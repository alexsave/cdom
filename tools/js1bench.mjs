// The pages of docs/descent.md's "The three code paths in the real browsers": tests/js1bench.c built through each of the
// three Netscape code paths, everything inlined (Netscape 2 has no SCRIPT SRC), each run in its own SCRIPT (a run-time
// error or a stack overflow kills only that run), timed with new Date().getTime() around the call, and reported by
// document.write of an IMG to /mark/<tag>/p-<n>-<name>-<escaped value>, as tests/probe-js1.html does, so
// tools/probe_marks.py reads the serve.py log.
//   node tools/js1bench.mjs <t|i|c> <out.html> <file to inline>...
//     t  js1-transform: the app-js1 file (window.cdomFactory, exports.cdom_trapped)
//     i  js1-interp:    cdom.js1.js then the app-js1 module (wm_factory, wi_trapped())
//     c  js1-compile:   tools/js1.sh's file (cdomFactoryJs1, js1_trap, js1_paused and js1_more)
//   NS2FIX=1 writes the page with the two Netscape 2 blockers the first runs found worked around in the page only (the
//   branches are not changed), so that the speed past them is measured:
//     i  in every function of the inlined files that returns a value somewhere, a bare return becomes "return 0" and a
//        "return 0;" goes at the end (Netscape 2 refuses a function that returns a value on one path and none on another:
//        "function does not always return a value", and the interpreter never loads); and the digit table's keys get a
//        "k" in front (Netscape 2's o["0"] is o's first named property, so the table's digit keys overwrite its letters:
//        "f is not a number" at the module's decoding)
//     c  after the load, every memory word the program touches and the data does not hold is set to 0 (Netscape 2 dies
//        on undefined | 0, which js1c's reads of a word never written are: "33041 is not a number"); and a for loop's
//        "i--)" becomes "i=i-1)" (Netscape 3.04 refuses it: "missing ) after for-loop control")
// The files come from the three branches' worktrees (tools/js1bench.sh builds them); the wanted answers from node's
// WebAssembly running tests/out/js1bench.wasm here.
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';

const [kind, outFile, ...inline] = process.argv.slice(2);
const FIX = process.env.NS2FIX === '1';
if (!'tic'.includes(kind) || kind.length !== 1 || !outFile || !inline.length) {
    console.error('usage: node tools/js1bench.mjs <t|i|c> <out.html> <file to inline>...');
    process.exit(2);
}
const wasm = fs.readFileSync(path.join(path.dirname(outFile), 'js1bench.wasm'));
const ref = new WebAssembly.Instance(new WebAssembly.Module(wasm), {}).exports;
// [mark name, export, argument, repeats]. The deep recursions come last: IE 4 stops a page's later scripts after its
// first stack overflow.
const RUNS = [['loop2000', 'loop', 2000, 3], ['mem', 'mem', 7, 1], ['rec25', 'rec', 25, 1], ['rec50', 'rec', 50, 1], ['rec75', 'rec', 75, 1], ['rec100', 'rec', 100, 3], ['rec500', 'rec', 500, 1],
  ['rec1000', 'rec', 1000, 1], ['rec5000', 'rec', 5000, 1]];
// js1bench.c's memory, in words: the data (mix, 9 words) at 16384, buf, sink and big up to 33100.
const zero = 'var bk; for (bk = 0; bk < 16384; bk++) JM[bk] = 0; for (bk = 16393; bk < 33100; bk++) JM[bk] = 0;';
const load = {
    t: 'X = cdomFactory(new Object());',
    i: 'X = wm_factory(new Object());',
    c: 'var imp = new Object(); imp.env = new Object(); X = cdomFactoryJs1(imp);' + (FIX ? '\n' + zero : ''),
}[kind];
const trapped = { t: 'X.cdom_trapped()', i: 'wi_trapped()', c: 'X.js1_trap' }[kind];
// The compiler's driver may pause on its step budget (JB, -1: none); go on in place, so the time is the whole call.
const finish = kind === 'c' ? 'if (X.js1_paused) { while (X.js1_more()) { } r = X.js1_result; }' : '';
const driver = `var bn = 0;
var btag = "" + location.href;
var bq = btag.indexOf("?");
if (bq < 0) btag = "x";
else btag = btag.substring(bq + 1, btag.length);
function bmark(name, v) {
  bn = bn + 1;
  document.write("<TR><TD>" + name + "</TD><TD>" + v + "</TD></TR>");
  document.write("<IMG SRC=\\"/mark/" + btag + "/p-" + bn + "-" + name + "-" + escape("" + v) + "\\" WIDTH=1 HEIGHT=1>");
}
function bnow() {
  var d = new Date();
  return d.getTime();
}
var X = 0;
function bcall(f, n) {
  if (f == "loop") return X.loop(n);
  if (f == "rec") return X.rec(n);
  return X.mem(n);
}
function brun(name, f, n, want) {
  var t = 0;
  var r = 0;
  var ms = 0;
  bmark(name, "start");
  t = bnow();
  r = bcall(f, n);
  ${finish}
  ms = bnow() - t;
  r = r >>> 0;
  bmark(name, ms + "ms r=" + r + (r == want ? " ok" : " WANT " + want) + " trap=" + ${trapped});
}`;
const blocks = ['bmark("ua", navigator.userAgent);', `var bt = bnow();\n${load}\nbmark("load", (bnow() - bt) + "ms");`];
for (const [name, f, n, reps] of RUNS) {
    for (let k = 1; k <= reps; k++) blocks.push(`brun("${name}${reps > 1 ? '_' + k : ''}", "${f}", ${n}, ${ref[f](n) >>> 0});`);
}
blocks.push('bmark("done", "end");');
const title = { t: 'js1bench-t (js1-transform)', i: 'js1bench-i (js1-interp)', c: 'js1bench-c (js1-compile)' }[kind];
function fix(src) {
    if (FIX && kind === 'c') return src.replace(/(\w+)--\)/g, '$1=$1-1)');
    if (!FIX || kind !== 'i') return src;
    const acorn = createRequire(import.meta.url)('acorn');
    const edits = []; // [offset, text to insert, length to replace]
    const visit = (node, fn) => {
        if (!node || typeof node.type !== 'string') return;
        if (node.type === 'FunctionDeclaration' || node.type === 'FunctionExpression') {
            const rets = [];
            const inner = (n) => {
                if (!n || typeof n.type !== 'string') return;
                if (n !== node && (n.type === 'FunctionDeclaration' || n.type === 'FunctionExpression')) { visit(n); return; }
                if (n.type === 'ReturnStatement') rets.push(n);
                for (const k of Object.keys(n)) { const v = n[k]; if (Array.isArray(v)) v.forEach(inner); else if (v && typeof v.type === 'string') inner(v); }
            };
            inner(node);
            if (rets.some((r) => r.argument)) {
                for (const r of rets) if (!r.argument) edits.push([r.start, 'return 0', 6]);
                edits.push([node.body.end - 1, ';return 0;', 0]);
            }
            return;
        }
        for (const k of Object.keys(node)) { const v = node[k]; if (Array.isArray(v)) v.forEach((c) => visit(c)); else if (v && typeof v.type === 'string') visit(v); }
    };
    visit(acorn.parse(src, { ecmaVersion: 5 }));
    edits.sort((a, b) => b[0] - a[0]);
    for (const [at, text, len] of edits) src = src.slice(0, at) + text + src.slice(at + len);
    return src.replace(/\[(\w+)\.charAt\((\w+)\)\]/g, '["k"+$1.charAt($2)]');
}
const html = `<HTML>
<HEAD>
<TITLE>cdom ${title}</TITLE>
${inline.map((f) => `<SCRIPT LANGUAGE="JavaScript">\n${fix(fs.readFileSync(f, 'utf8'))}\n</SCRIPT>`).join('\n')}
<SCRIPT LANGUAGE="JavaScript">
${driver}
</SCRIPT>
</HEAD>
<BODY>
<H3>cdom ${title}</H3>
<P>tests/js1bench.c through one of the three Netscape code paths. Reports to /mark/ as it runs.
<TABLE BORDER=1>
${blocks.map((s) => `<SCRIPT LANGUAGE="JavaScript">\n${s}\n</SCRIPT>`).join('\n')}
</TABLE>
</BODY>
</HTML>
`;
fs.writeFileSync(outFile, html);
fs.writeFileSync(outFile + '.driver.js', driver + '\n' + blocks.join('\n') + '\n');
