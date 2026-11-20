// Writes the frames tier interpreter's pages for the real browsers (docs/js1-interp-design.md, section 8): tests/js1cpu.c
// on the interpreter, everything inline (Netscape 2 has no SCRIPT SRC), each result written into the page and reported
// to /mark/<tag>/... as docs/descent.md's probe pages do. node tools/js1page.mjs <tests/out>, after cdom.js1.js and
// js1cpu.wasm.js1.js are built.
//   js1.html             the shipped files: cdom.js1.js (the weighted dispatch) and the module as a digit string
//   js1-statements.html  the same interpreter, the module as statements (the format measurement, P2)
//   js1-mixed.html, js1-tree.html  the other dispatch shapes (P7), the module as a digit string
// Each page's scripts pass the js10 gate. The wanted results and the instruction counts come from node's WebAssembly
// and the interpreter's profiling build, here.
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { createRequire } from 'node:module';
import * as L from './legacy/js1lower.mjs';
import { js10, declared } from './legacy/js10.mjs';

const out = path.resolve(process.argv[2] || 'tests/out');
const top = path.resolve(path.dirname(new URL(import.meta.url).pathname), '..');
const { minify } = createRequire(path.join(top, 'package.json'))('terser');
const wasm = fs.readFileSync(path.join(out, 'js1cpu.wasm'));
const m = L.parseWasm(wasm), low = L.lower(m), order = L.opOrder();
// What the page runs, small enough for Netscape 2 (some 5,000 instructions a second, docs/js1-interp-design.md).
const RUNS = [['loop', 2000], ['rec', 100], ['rec', 1000], ['rec', 5000]];
const ref = new WebAssembly.Instance(new WebAssembly.Module(wasm), {}).exports;
// Instructions each run takes, from the profiling build.
const counts = RUNS.map(([f, n]) => {
  const ctx = vm.createContext({}); ctx.window = ctx;
  vm.runInContext(L.interpreter(order, { profile: true }), ctx);
  vm.runInContext(L.moduleFile(m, low, order, 'x.js1.js'), ctx);
  const x = vm.runInContext('cdomFactory(new Object())', ctx);
  x[f](n);
  let c = 0; for (const k of Object.keys(ctx.wi_P)) c += ctx.wi_P[k];
  return c;
});
const driver = `var js1n = 0;
var js1tag = "" + location.search;
js1tag = js1tag.substring(1, js1tag.length);
if (js1tag == "") js1tag = "x";
function js1mark(name, v) {
  js1n = js1n + 1;
  document.write("<TR><TD>" + name + "</TD><TD>" + v + "</TD></TR>");
  document.write("<IMG SRC=\\"/mark/" + js1tag + "/p-" + js1n + "-" + name + "-" + escape("" + v) + "\\" WIDTH=1 HEIGHT=1>");
}
function js1now() {
  var d = new Date();
  return d.getTime();
}
var js1X = 0;
function js1run(name, f, n, want, count) {
  var t = js1now(), r, ms;
  if (f == "loop") r = js1X.loop(n);
  else r = js1X.rec(n);
  ms = js1now() - t;
  r = r >>> 0;
  if (ms < 1) ms = 1;
  js1mark(name, ms + "ms r=" + r + (r == want ? " ok" : " WANT " + want) + " trap=" + wi_trapped() + " ips=" + Math.round(count * 1000 / ms));
}`;
async function small(src) {
  const { code } = await minify(src, { ecma: 5, compress: false, mangle: true, format: { ascii_only: true } });
  return code;
}
async function page(file, interp, form, title) {
  const mod = L.moduleFile(m, low, order, 'js1cpu.wasm.js1.js', form);
  const scripts = [interp, mod, driver];
  const known = scripts.flatMap(declared);
  scripts.forEach((s, i) => js10(`${file} script ${i}`, s, { known }));
  const blocks = [
    'js1mark("ua", navigator.userAgent);',
    `var js1t = js1now();\njs1X = wm_factory(new Object());\njs1mark("load", (js1now() - js1t) + "ms");`,
    ...RUNS.map(([f, n], i) => `js1run("${f}${n}", "${f}", ${n}, ${ref[f](n) >>> 0}, ${counts[i]});`),
    'js1mark("done", "end");',
  ];
  blocks.forEach((s, i) => js10(`${file} block ${i}`, s, { known }));
  const html = `<HTML>
<HEAD>
<TITLE>cdom ${title}</TITLE>
<SCRIPT LANGUAGE="JavaScript">
${interp}
</SCRIPT>
<SCRIPT LANGUAGE="JavaScript">
${mod}
</SCRIPT>
<SCRIPT LANGUAGE="JavaScript">
${driver}
</SCRIPT>
</HEAD>
<BODY>
<H3>cdom ${title}</H3>
<P>tests/js1cpu.c on the frames tier's WebAssembly interpreter (docs/js1-interp-design.md). Reports to /mark/ as it runs; ips is the interpreter's instructions a second.
<TABLE BORDER=1>
${blocks.map((s) => `<SCRIPT LANGUAGE="JavaScript">\n${s}\n</SCRIPT>`).join('\n')}
</TABLE>
</BODY>
</HTML>
`;
  fs.writeFileSync(path.join(out, file), html);
}
const shipped = fs.readFileSync(path.join(out, 'cdom.js1.js'), 'utf8');
await page('js1.html', shipped, 'digits', 'js1 (the shipped interpreter, digit string)');
await page('js1-statements.html', shipped, 'statements', 'js1-statements (the module as statements)');
await page('js1-mixed.html', await small(L.interpreter(order, { shape: 'mixed' })), 'digits', 'js1-mixed (a chain of 8, then a balanced tree)');
await page('js1-tree.html', await small(L.interpreter(order, { shape: 'tree' })), 'digits', 'js1-tree (a balanced tree)');
