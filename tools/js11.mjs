// Checks tools/legacy/js11.mjs, the frames tier's language gates (docs/frames-design.md, section 10), on code planted for
// each rule: what JavaScript 1.1 (Netscape 3) lacks must be refused, and what it has must pass; js10 (the boot's first
// part, frames.js and a page's own scripts, Netscape 2) refuses typeof, void, Array and the rest besides. node tools/js11.mjs;
// exit 1 on any failure.
import fs from 'node:fs';
import { createRequire } from 'node:module';
import { lacks, FRAMES_ALLOWED } from './legacy/js11.mjs';

let fails = 0;
const ok = (c, m) => { if (!c) { fails++; console.log('FAIL', m); } else console.log('ok  ', m); };
const refused = (src, what, options) => lacks(src, options).some((f) => f.startsWith(what));
const passes = (src, options) => lacks(src, options).length === 0;

// Netscape 3.04's seam: a "--" or "++" starting at column 254 of its line (tests/probe-js1e.html).
const pad = (col, stmt) => '/*' + 'p'.repeat(col - 5) + '*/' + stmt;
ok(refused(pad(254, 'x--;'), '-- at column 254'), 'x-- with its -- at column 254 is refused');
ok(refused(pad(254, 'x++;'), '++ at column 254'), 'x++ with its ++ at column 254 is refused');
ok(passes(pad(253, 'x--;')) && passes(pad(255, 'x--;')) && passes(pad(254, 'x>=1;')), 'x-- at columns 253 and 255, and >= at 254, pass');

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
  ['var t = new Date().getTime();', 'a member of a new expression'],
  ['var t = (new Date()).getTime();', 'a member of a new expression'],
  ['var y = (new Date).getYear;', 'a member of a new expression'],
  ['new F()();', 'a member of a new expression'],
  ['if (isNaN(x)) f();', 'isNaN'],
  ['var t = setInterval("f()", 10);', 'setInterval'],
  ['var t = window.setInterval("f()", 10);', '.setInterval'],
  ['var s = t.substr(1, 2);', '.substr'],
  ['with (o) f();', 'with'],
  ['debugger;', 'debugger'],
  ['var o = new F(); o.k = 1; x = { get k() { return 1; } };', 'a getter or setter'],
  ['if (a = f()) g();', 'an assignment in a condition'],
  ['if (b && (a = f())) g();', 'an assignment in a condition'],
  ['while (a = f()) g();', 'an assignment in a condition'],
  ['while (x[a += 1]) g();', 'an assignment in a condition'],
  ['for (i = 0; a = f(); i++) g();', 'an assignment in a condition'],
  ['do g(); while (a = f());', 'an assignment in a condition'],
  ['x = (a = f()) ? 1 : 2;', 'an assignment in a condition'],
  // "function does not always return a value." in Netscape 2.02 and 3.04, at parse time.
  ['function f(a) { if (a) return 1; return; }', 'a function that returns a value and also none'],
  ['function f(a) { while (1) { if (a) return 1; } }', 'a function that returns a value and does not end in a return'],
  ['function f(a) { if (a) return 1; else return 2; }', 'a function that returns a value and does not end in a return'],
]) ok(refused(src, what), `${src} is refused (${what})`);
ok(passes('function f(a) { if (a) return; g(); } function h(a) { if (a) return 1; return 2; } function k() { g(); }'),
  'a function with bare returns alone, one that ends in a return of a value, and one with no return pass');
ok(passes('for (i = 0; i < n; i += 1) { a = f(); if (a == b) x = a ? 1 : (b = 2); }'), 'an assignment in a for loop\'s init and update, in a body and in a branch of ?: passes');
ok(passes('var d = new Date(); var t = d.getTime();'), 'a new expression kept in a variable, then its member, passes');
// terser, as tools/legacy/build.mjs runs it on frames.js (no compress), keeps a new expression in its variable, and
// what it writes of a member of one, (new Date).getTime(), the gate on the minified output refuses.
const { minify } = createRequire(new URL('../package.json', import.meta.url))('terser');
const terse = async (src) => (await minify(src, { ecma: 5, compress: false, mangle: true, format: { ascii_only: true } })).code;
const kept = await terse('function f() { var d = new Date(); return d.getTime(); }');
ok(passes(kept), `terser keeps var d = new Date(); d.getTime() apart: ${kept}`);
const joined = await terse('function f() { return (new Date()).getTime(); }');
ok(refused(joined, 'a member of a new expression'), `and the gate refuses terser's member of a new expression: ${joined}`);
// The shim itself, before and after terser, against the gate (tools/legacy/build.mjs refuses it too, at build time):
// JavaScript 1.0, Netscape 2's, with only the names it checks for first (FRAMES_ALLOWED).
const shimSrc = fs.readFileSync(new URL('./legacy/frames.js', import.meta.url), 'utf8');
for (const [what, src] of [['frames.js', shimSrc], ['frames.js once minified', await terse(shimSrc)]]) {
  const found = lacks(src, { js10: true, allowed: FRAMES_ALLOWED });
  ok(!found.length, `${what} is JavaScript 1.0: ${found.slice(0, 6).join(', ')}`);
}
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
  ['F.prototype.g = h;', '.prototype'], ['var p = s.split(",");', '.split'], ['x = void 0;', 'void'], ['var o = Object;', 'Object'],
  ['var s = a.join("");', '.join'], ['var f = new Function("return 1");', 'Function'], ['var c = String.fromCharCode(65);', 'String'],
  ['var n = Number.NaN;', 'Number'], ['document.onmousedown = h;', '.onmousedown'], ['document.onmouseup = h;', '.onmouseup'],
  ['document.captureEvents(Event.MOUSEDOWN);', '.captureEvents'], ['document.routeEvent(e);', '.routeEvent'],
  ['function f() { return arguments.length; }', 'arguments'], ['if (isNaN(x)) f();', 'isNaN'], ['t = setInterval("f()", 9);', 'setInterval'],
  ['var s = t.substr(1);', '.substr'], ['if (o.constructor == F) f();', '.constructor'], ['window.onerror = h;', '.onerror'],
  ['onerror = h;', 'onerror'], ['var n = document.images.length;', '.images'], ['var i = new Image();', 'Image'],
  ['var w = screen.width;', 'screen'], ['var w = window.screen.width;', '.screen'], ['var n = navigator.plugins.length;', '.plugins']])
  ok(refused(src, what, { js10: true }), `js10: ${src} is refused (${what})`);
ok(passes('if (x) f(); else g(a[0], "s".length);', { js10: true }), 'js10: if, calls, indexing and length pass');
ok(passes('var o = new Object(); o.k = 1;', { js10: true }), 'js10: new Object() passes (Netscape 2 has it: js1c\'s output runs on it)');
ok(passes('if (window.String && String.fromCharCode) c = String.fromCharCode(65); if (window.Number) n = Number.NaN; document.onmousedown = h;',
  { js10: true, allowed: FRAMES_ALLOWED }), "js10: String, Number and the button hooks pass where the file says it checks for them (frames.js's allowed list)");
ok(refused('var a = new Array();', 'Array', { js10: true, allowed: FRAMES_ALLOWED }), 'js10: and Array is still refused there');
ok(passes('function f(a) { return f.arguments.length + a; } location.replace(location.href);', { js10: true }),
  "js10: a function's own f.arguments and location.replace pass");
ok(passes('function f() { return arguments.length; } var s = t.substring(1);'), 'js11: arguments and substring pass');
ok(passes(`function boot(u, m) { var d = document; if (d.getElementById && d.createElement) { dom(u, m); return; }
  var b = u; if (!u.substring) b = u[0]; held = m; d.write("<script src=\\"x.js\\"></scr" + "ipt>"); }`,
  { js10: true, allowed: ['getElementById', 'createElement'] }), "js10: the boot's shape passes, its feature tests allowed");

console.log(fails ? `${fails} FAILED` : 'all ok');
process.exit(fails ? 1 : 0);
