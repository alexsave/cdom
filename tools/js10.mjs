// Checks tools/legacy/js10.mjs, the JavaScript 1.0 gate (what Netscape 2.02 lacked in docs/descent.md's probe), on code
// planted for each rule: what it lacked must be refused, what it ran must pass. node tools/js10.mjs; exit 1 on any
// failure.
import { lacks10 } from './legacy/js10.mjs';

let fails = 0;
const ok = (c, m) => { if (!c) { fails++; console.log('FAIL', m); } else console.log('ok  ', m); };
const refused = (src, what) => lacks10(src).some((f) => f.startsWith(what));
const passes = (src) => lacks10(src).length === 0;

// What the 1.1 gate refuses is refused here too.
for (const [src, what] of [['switch (a) { case 1: f(); }', 'switch'], ['var o = {};', 'an object literal'], ['if (a === b) f();', '==='],
  ['var d = new Date().getTime();', 'a member of a new expression'], ['if (isNaN(x)) f();', 'isNaN']]) {
  ok(refused(src, what), `${src} is refused (${what}, as the 1.1 gate does)`);
}
// What Netscape 2.02 lacked on top of that.
for (const [src, what] of [
  ['if (typeof x == "string") f();', 'typeof'],
  ['x = void 0;', 'void'],
  ['var f = new Function("a", "return a");', 'Function'],
  ['function f() { return arguments.length; }', 'arguments'],
  ['A.prototype.x = 1;', '.prototype'],
  ['if (a.constructor == A) f();', '.constructor'],
  ['var p = s.split(",");', '.split'],
  ['var p = s.substr(1, 2);', '.substr'],
  ['var p = s.replace("a", "b");', '.replace'],
  ['var p = a.join("-");', '.join'],
  ['a.sort();', '.sort'],
  ['a.reverse();', '.reverse'],
  ['var h = (255).toString(16);', 'toString with a radix'],
  ['setInterval("f()", 10);', 'setInterval'],
  ['var i = new Image();', 'Image'],
  ['var n = document.images.length;', '.images'],
  ['if (navigator.javaEnabled()) f();', '.javaEnabled'],
  ['var w = screen.width;', 'screen'],
  ['window.onerror = h;', '.onerror'],
  ['var $a = 1;', 'a $ in an identifier'],
]) ok(refused(src, what), `${src} is refused (${what})`);
// What it ran.
ok(passes(`var o = new Object(), a = new Array(), i, s = "abc", k;
function g(x, y) { if (x != y) return s.charAt(1) + s.indexOf("b") + s.lastIndexOf("c") + s.substring(1, 2) + s.toUpperCase(); return escape(s) + unescape("%41"); }
for (i = 0; i < 10; i++) a[i] = o[i] = i >>> 1 ^ 3 & ~i | i << 2;
for (k in o) i = i > 3 ? i : k, i;
while (i > 0) { i--; if (i == 5) break; else continue; }
setTimeout("g(1, 2)", 10);
var d = new Date(), t = d.getTime(), m = Math.floor(Math.sqrt(Math.abs(-4))) + Math.pow(2, 3) + Math.round(2.5) + Math.max(1, 2);
var n = parseInt("ff", 16) + parseFloat("1.5") + eval("1 + 2") + s.length + document.forms[0].elements[0].value.length;`),
'new Object, new Array, number keys, the bitwise operators, ?:, the comma, charAt, indexOf, substring, escape, eval, setTimeout with a string, Math, a Date in a variable and forms pass');
ok(passes('function f(x) { return f.arguments.length + x; }'), 'f.arguments (the function\'s own property) passes');
ok(passes('var c = cf_raw["call"]; o["push"] = 1;'), 'a computed key named as a method passes: it is a property, not a call of the method');

console.log(fails ? `${fails} FAILED` : 'all ok');
process.exit(fails ? 1 : 0);
