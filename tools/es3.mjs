// Checks tools/legacy/es3.mjs, the fourth tier's checks and passes (IE 6 to 8, docs/ie6-design.md), on code planted
// for each: what JScript refuses or reads differently must fail the build, and what it reads as written must pass.
// node tools/es3.mjs; exit 1 on any failure. Needs `npm ci` in tools/legacy (Babel and acorn).
import { createRequire } from 'node:module';
import { es3, catchScoped, oldDom, oldDomLeftovers } from './legacy/es3.mjs';

const require = createRequire(new URL('./legacy/package.json', import.meta.url));
const { transformSync } = require('@babel/core');
let fails = 0;
const ok = (c, m) => { if (!c) { fails++; console.log('FAIL', m); } else console.log('ok  ', m); };
// The message es3 throws on src (as part name, minified or not), or '' when it passes.
const refusal = (name, src, minified) => { try { es3(name, src, minified); return ''; } catch (e) { return e.message; } };

// Syntax JScript 5.6 cannot parse: ES5's getters, and (once minified, where terser has quoted the rest) a reserved word
// as a property name.
ok(/is not ES3/.test(refusal('t', '({ get a() { return 1; } })')), 'a getter is not ES3');
ok(/reserved word as a property/.test(refusal('t', 'a.class = 1;', true)), 'a.class is refused');
ok(/reserved word as a property/.test(refusal('t', 'x = { default: 1 };', true)), '{ default: 1 } is refused');
ok(refusal('t', 'a["class"] = 1; x = { "default": 1 };', true) === '', 'quoted, the same names pass');

// JScript's catch has no scope: its parameter overwrites the enclosing function's variable of that name.
ok(/catch parameter/.test(refusal('t', 'function f(e) { try { g(); } catch (e) {} return e; }', true)), "catch (e) over the function's own e is refused");
ok(/catch parameter/.test(refusal('t', 'function f() { try { g(); } catch (e) {} return function () { return e; }; }', true)),
  'catch (e) where a closure reads an outer e is refused');
ok(refusal('t', 'function f() { try { g(); } catch (e) {} function h(e) { return e; } return h; }', true) === '',
  'catch (e) beside a function with its own e passes');
ok(refusal('t', 'function f() { try { g(); } catch (e) { h(e); } }', true) === '', 'catch (e) used only in its clause passes');
// catchScoped renames each catch parameter apart, which is what makes Babel's _regeneratorDefine2 pass.
const fixed = await catchScoped('function f(e) { try { g(); } catch (e) { h(e); } return e; }');
ok(refusal('t', fixed, true) === '' && /return e;/.test(fixed) && !/catch \(e\)/.test(fixed), `catchScoped renames the catch apart: ${fixed.replace(/\s+/g, ' ')}`);

// ES5 names: only what polyfills-es3.js supplies, or a part's own guarded use.
ok(/ES5 names/.test(refusal('shim', 'Object.getOwnPropertyNames(o);')), 'Object.getOwnPropertyNames is refused');
ok(/ES5 names/.test(refusal('dom-old', 'Object.defineProperty(o, "a", {});')), 'defineProperty is refused outside the parts that guard it');
ok(refusal('polyfills', 'Object.defineProperty(o, "a", {});') === '', 'polyfills.js may try defineProperty');
ok(refusal('shim', '[1].forEach(f); [1].indexOf(1); f.bind(o); Object.keys(o); Date.now();') === '', 'the supplied ES5 names pass');
ok(/ES5 names/.test(refusal('shim', 'var j = JSON.stringify(o);')), 'JSON is refused (IE 6 and 7 have none)');

// The old DOM's table: each W3C call rewritten to its dom-old.js helper, and any left over found.
const out = transformSync('x.style.setProperty(a, b); y.textContent = ""; v = z.previousElementSibling; addEventListener("t", f);' +
  ' w = innerWidth; n.remove(); m.remove(1); r = q.getBoundingClientRect(); p.setAttribute("class", c); function k(innerWidth) { return innerWidth; }',
  { babelrc: false, configFile: false, sourceType: 'script', plugins: [oldDom] }).code.replace(/\s+/g, ' ');
for (const want of ['cdomOld.setProperty(x.style, a, b)', 'cdomOld.setText(y, "")', 'v = cdomOld.prevElement(z)', 'cdomOld.on(window, "t", f)',
  'w = cdomOld.viewportWidth()', 'cdomOld.remove(n)', 'm.remove(1)', 'r = cdomOld.rect(q)', 'cdomOld.setAttribute(p, "class", c)', 'return innerWidth;'])
  ok(out.includes(want), `the rewrite makes ${want}`);
// m.remove(1) is not ChildNode.remove, so it is not rewritten, and the scan, which cannot tell, still refuses it.
ok(JSON.stringify(oldDomLeftovers(out)) === '[".remove","innerWidth"]', `after it, the leftovers are m.remove(1) and the parameter innerWidth: ${JSON.stringify(oldDomLeftovers(out))}`);
ok(JSON.stringify(oldDomLeftovers('x.addEventListener("a", f); n.textContent; if (b.getBoundingClientRect) g(); devicePixelRatio;')) ===
  '[".addEventListener",".textContent","devicePixelRatio"]', 'a call, a property and a global left are found; a method read as a test is not');

console.log(fails ? `${fails} FAILED` : 'all ok');
process.exit(fails ? 1 : 0);
