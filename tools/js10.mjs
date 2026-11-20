// Checks tools/legacy/js10.mjs, the JavaScript 1.0 gate (docs/js1-interp-design.md, section 7), on code planted for each
// rule: what Netscape 2.02 was measured to lack must be refused, what it was measured to have must pass, and a global
// or a Math member measured neither way is refused. Then it gates files: node tools/js10.mjs [file ...] takes the
// files of one page together (a module file calls the interpreter's names), and with no files gates tests/out's
// cdom.js1.js with each module file there (*.js1.js). Exit 1 on any failure.
import fs from 'node:fs';
import path from 'node:path';
import { lacks10, declared } from './legacy/js10.mjs';

let fails = 0;
const ok = (c, m) => { if (!c) { fails++; console.log('FAIL', m); } else console.log('ok  ', m); };
const refused = (src, what, options) => lacks10(src, options).some((f) => f.startsWith(what));
const passes = (src, options) => { const f = lacks10(src, options); if (f.length) console.log('  ', f.join('; ')); return !f.length; };

// Syntax that did not parse on Netscape 2.02 ("no run").
for (const [src, what] of [
  ['var t = typeof x;', 'typeof'], ['var v = void 0;', 'void'], ['var o = {};', 'an object literal'], ['var a = [1, 2];', 'an array literal'],
  ['var r = /a/;', 'a regex literal'], ['var f = function () {};', 'a function expression'],
  ['function f() { function g() {} g(); }', 'a nested function declaration'], ['switch (x) { case 1: f(); }', 'switch'],
  ['do { f(); } while (x);', 'do-while'], ['l: for (;;) break l;', 'a label'], ['if (x === 1) f();', '==='], ['if (x !== 1) f();', '!=='],
  ['if ("k" in o) f();', 'the binary in'], ['if (o instanceof F) f();', 'the binary instanceof'], ['try { f(); } catch (e) {}', 'try'],
  ['function f() { throw 1; }', 'throw'], ['with (o) f();', 'with'], ['var t = new Date().getTime();', 'a member of a new expression'],
  ['var t = new F()();', 'a call of a new expression'], ['delete o.k;', 'delete'], ['var s = "\\u00e9";', 'a \\u escape'],
]) {
  const decl = 'var x, o, F; function f() {} function g() {} ';
  ok(refused(decl + src, what), `${src} is refused (${what})`);
}
// Names that died at run time (DIED(start)), as properties and as globals.
for (const m of ['split', 'slice', 'substr', 'replace', 'charCodeAt', 'join', 'sort', 'push', 'pop', 'concat', 'prototype', 'constructor',
  'hasOwnProperty', 'call', 'apply', 'javaEnabled', 'images', 'onerror'])
  ok(refused(`var o, x; x = o.${m};`, `.${m}`), `.${m} is refused`);
for (const g of ['Array', 'Function', 'isNaN', 'NaN', 'Infinity', 'undefined', 'arguments', 'setInterval', 'Image', 'screen'])
  ok(refused(`var x = ${g};`, g), `${g} is refused`);
ok(refused('var s = 255; s = s.toString(16);', 'toString with a radix'), 'toString with a radix is refused');
ok(refused('var x = String.fromCharCode(65);', '.fromCharCode'), 'String.fromCharCode is refused');
// What was measured neither way.
ok(refused('var x = Math.log(2);', 'Math.log'), 'Math.log, not measured, is refused');
ok(refused('var x = Math.ceil(2);', 'Math.ceil'), 'Math.ceil, not measured, is refused');
ok(refused('var x = clearTimeout;', 'the global clearTimeout'), 'clearTimeout, not measured, is refused');
ok(refused('f();', 'the global f'), 'a call of a function no file declares is refused');
// What Netscape 2.02 has.
ok(passes(`var o = new Object(), d = new Date(), t = d.getTime(), s = "abc", k, n = 0;
function f(a, b) { var i, x = a; for (i = 0; i < b; i++) x = (x + i) & 65535 | x >>> 3 ^ ~x << 2 >> 1; return x ? x : -1; }
o[1] = "a"; o.m = f; o.m(1, 2); for (k in o) n++;
while (n > 0) { if (n == 3 || n != 4 && !o) break; n--; continue; }
s = s.charAt(1) + s.indexOf("b") + s.lastIndexOf("c") + s.substring(0, 1) + s.toUpperCase() + s.length + escape(s) + unescape("%41");
n = Math.floor(1.5) + Math.abs(-1) + Math.sqrt(4) + Math.pow(2, 3) + Math.round(2.5) + Math.max(1, 2) + parseInt("ff", 16) + parseFloat("1.5");
eval("1 + 2"); setTimeout("f(1, 2)", 10); window.x = document.title + navigator.userAgent + location.href; n += 0x1f, n -= 1;`),
'new Object, new Date, functions, for, for-in, while, break, continue, ?:, every bitwise operator, charAt, indexOf, lastIndexOf, substring, toUpperCase, escape, the six Math functions, parseInt, parseFloat, eval, setTimeout and the page objects pass');
ok(passes('wi_call(1);', { known: ['wi_call'] }), "a name another file of the page declares passes, given as known");

// The files.
let files = process.argv.slice(2);
if (!files.length) {
  const out = path.resolve(path.dirname(new URL(import.meta.url).pathname), '../tests/out');
  files = [path.join(out, 'cdom.js1.js'), ...fs.readdirSync(out).filter((f) => f.endsWith('.js1.js')).map((f) => path.join(out, f))];
}
const srcs = files.map((f) => fs.readFileSync(f, 'utf8'));
const known = srcs.flatMap((s) => { try { return declared(s); } catch { return []; } });
files.forEach((f, i) => {
  const found = lacks10(srcs[i], { known });
  ok(!found.length, `${path.basename(f)} is JavaScript 1.0${found.length ? ': ' + [...new Set(found)].slice(0, 8).join(', ') : ''}`);
});

console.log(fails ? `${fails} FAILED` : 'all ok');
process.exit(fails ? 1 : 0);
