// Checks tools/legacy/js11.mjs, the frames tier's language gates (docs/frames-design.md, section 10), on code planted for
// each rule: what JavaScript 1.1 (Netscape 3) lacks must be refused, and what it has must pass; js10 (the boot's first
// part, Netscape 2) refuses typeof, void, Array and the rest besides. node tools/js11.mjs; exit 1 on any failure.
import { lacks } from './legacy/js11.mjs';

let fails = 0;
const ok = (c, m) => { if (!c) { fails++; console.log('FAIL', m); } else console.log('ok  ', m); };
const refused = (src, what, options) => lacks(src, options).some((f) => f.startsWith(what));
const passes = (src, options) => lacks(src, options).length === 0;

// Syntax of JavaScript 1.2 to 1.4.
for (const [src, what] of [
  ['try { f(); } catch (e) {}', 'try'],
  ['function f() { throw 1; }', 'throw'],
  ['switch (a) { case 1: f(); }', 'switch'],
  ['do { f(); } while (a);', 'do-while'],
  ['x: for (;;) break x;', 'a label'],
  ['delete o.k;', 'delete'],
  ['if (a === b) f();', '==='],
  ['if (a !== b) f();', '!=='],
  ['if ("k" in o) f();', 'the binary in'],
  ['if (o instanceof F) f();', 'the binary instanceof'],
  ['var r = /a+/;', 'a regex literal'],
  ['var o = {};', 'an object literal'],
  ['var a = [1, 2];', 'an array literal'],
  ['var f = function () {};', 'a function expression'],
  ['function f() { function g() {} return g; }', 'a nested function declaration'],
  ['var s = "\\u00e9";', 'a \\u escape'],
  ['var u = undefined;', 'undefined'],
  ['var n = NaN;', 'NaN'],
  ['var i = Infinity;', 'Infinity'],
]) ok(refused(src, what), `${src} is refused (${what})`);
// The methods of 1.2 and later, and the DOM this tier has not.
for (const m of ['push', 'pop', 'shift', 'unshift', 'splice', 'slice', 'concat', 'call', 'apply', 'charCodeAt',
  'fromCharCode', 'createElement', 'appendChild', 'addEventListener', 'getElementById', 'innerHTML'])
  ok(refused(`o.${m}(x);`, `.${m}`), `.${m} is refused`);
ok(passes('if (s.charCodeAt) n = s.charCodeAt(0);', { allowed: ['charCodeAt'] }), 'charCodeAt passes where the file says it checks for it');
ok(refused('o.push(x);', '.push', { allowed: ['charCodeAt'] }), 'and push is still refused there');
// What JavaScript 1.1 has.
ok(passes(`var a = new Array(), o = new Object(), f = new Function("x", "return x"), k;
function g(x, y) { if (typeof x == "string" && x != y) return x.charAt(0) + x.substring(1).indexOf("a"); return void 0; }
for (k in o) a[a.length] = k;
while (a.length > 3) a.length--;
location.replace(location.search);
var s = "a,b".split(",").join(";");`), 'new Array, new Object, new Function, typeof, ==, for-in, charAt, substring, indexOf, split, join, void and location pass');
// Netscape 2, for the boot's first part.
for (const [src, what] of [['if (typeof x == "string") f();', 'typeof'], ['var a = new Array();', 'Array'],
  ['F.prototype.g = h;', '.prototype'], ['var p = s.split(",");', '.split'], ['x = void 0;', 'void'], ['var o = new Object();', 'Object']])
  ok(refused(src, what, { js10: true }), `js10: ${src} is refused (${what})`);
ok(passes('if (x) f(); else g(a[0], "s".length);', { js10: true }), 'js10: if, calls, indexing and length pass');
ok(passes(`function boot(u, m) { var d = document; if (d.getElementById && d.createElement) { dom(u, m); return; }
  var b = u; if (!u.substring) b = u[0]; held = m; d.write("<script src=\\"x.js\\"></scr" + "ipt>"); }`,
  { js10: true, allowed: ['getElementById', 'createElement'] }), "js10: the boot's shape passes, its feature tests allowed");

console.log(fails ? `${fails} FAILED` : 'all ok');
process.exit(fails ? 1 : 0);
