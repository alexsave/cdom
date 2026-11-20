# IE 5.5 and IE 5.0: the fourth tier, further down

Alex asked on 2026-11-20 to descend from IE 6 (docs/ie6-design.md) taking the cheap wins, and to pause once IE 5.0 works, saying what it took.
This is the state of that work.

## Where it stands

IE 5.01 and IE 5.5 run tests/out/legacy.html on the fourth tier's files: the boot picks cdom.legacy-ie6.js and legacy.wasm.ie9.js, the app starts, draws, takes typing and reports its deliberate error.
Two separate things stood in the way, and only the first was cdom's.

## 1. JScript 5.0 refused the tier's file

IE 5.01 has JScript 5.1 (the probe's ScriptEngine version), the JScript 5.0 language.
Two syntax gaps made the whole of cdom.legacy-ie6.js fail to parse, so `cdom` was never defined:

- No binary `in`: JScript 5.0 knows `in` only in for-in, and `"a" in o` is "Expected ';'".
  The tier's file had 15 of them: polyfills-es3.js's array methods and hasOwnProperty, dom-old.js's three feature tests, and polyfills.js's devicePixelRatio test.
  polyfills-es3.js now has holds(o, k) (the value is not undefined, or a for-in over o lists the key), and the feature tests read the property and compare it with undefined.
- No number as an object literal's key: `{1: 0}` is "Expected identifier or string".
  terser writes them for the shim's small lookup tables, so the fourth tier's files are now minified with quote_keys.

And what JScript 5.0 lacks at run time, now supplied or worked around:

- TypeError (and RangeError) do not exist; polyfills-es3.js supplies TypeError, which the polyfills, Babel's helpers and promise-polyfill throw.
- Error(n) takes a number, not a message, so `new Error("x")` has no message.
  polyfills.js's cdomError(m) makes an Error and sets message and description by hand, build.mjs rewrites every `new Error(m)` and `Error(m)` in the fourth tier's files to it (es3.mjs's messages), and the boot has its own error(m).
  It is in polyfills.js, not polyfills-es3.js, because legacy.wasm.ie9.js is rewritten too and the third tier runs that file with cdom.legacy-ie9.js.
- The global undefined, push, pop, shift, unshift, splice, call, apply and hasOwnProperty were already supplied first in polyfills-es3.js, each written without the others.

The build refuses each gap in the fourth tier's files (es3() in tools/legacy/es3.mjs), so none can come back unnoticed:
a binary in, a number as a key, a regular expression JScript 5.0 cannot compile (a flag but g and i, a lazy quantifier, a group starting with ?), a `/` unescaped in a regex class (Firefox 1.0 refuses that one), an Error made with a message, a JScript 5.5 name that nothing supplies, and replace with a function.
boot50 refuses the JScript 5.5 names and an Error made with a message in shim/boot.js, which runs before any polyfill.
tools/es3.mjs checks every gate on planted code, and tools/mutate.py's ie5-* mutants turn each one off and see it fail.

## 2. The Utilu IE 5 builds crash at Done, on any page

The "IE has encountered a problem" crash belongs to the standalone browser builds on this XP, not to cdom.
IE 5.01 crashes natively a moment after any page reaches Done, even `<html><body>x</body></html>` with no script and no title.
The fault is at address 0 in an unknown module (the error report's details), as a call into an unloaded DLL would be.
IE 5.5 does the same on a plain page (tests/probe.html, which has no cdom on it, crashes IE 5.5 at Done after its last probe).
A page still loading never crashes: with one image from a port that never answers, the page stays loading and its scripts run for as long as needed.
That is also why the earlier runs seemed to crash inside cdom.start after Promise.resolve(mount): the start's next step is a timeout, which ran after Done.
The previous note that the standalone IE 5.5 crashes on any broken image is the same crash at Done; the GIF stand-in for no-such-image.png is gone, since tools/legacy.mjs serves the page's broken image as a 404 on purpose.

So to test a page in these two builds:

- Run `python3 tools/ie5hold.py` (127.0.0.1:8096, accepts and never answers).
- Copy the page in tests/out and add `<img src="http://10.0.2.2:8096/x.gif" width="1" height="1">` to the copy.
- tests/build.sh refuses any shipped or built file that names that port, so the hold stays the test rig's.

IE 7 and IE 8 from the same collection do not start on this XP at all ("The ordinal 410 could not be located in urlmon.dll").

## Driving the XP VM

- The VM is /Users/alex/VMs/ie6-xp (run.sh, vm.py for screenshots, keys and taps through QMP's tablet).
- The Utilu IE Collection (archive.org item iecollection1721, sha1 4ad24d25a7ffd1c1daf36e46399cf7e02554269f) has `C:\Program Files\Utilu IE Collection\IE501\iexplore.exe`, IE550 and IE600.
- Launch a browser with Start, Run (`vm.py tap 40 465`, then `tap 238 400`), the quoted exe path and the URL, then `vm.py key ret`; the Windows key alone did not open Run reliably.
- Never type in the address bar of IE 5.01: it crashes there too. Start each page from Run instead.
- Serve the worktree's build with `python3 /Users/alex/VMs/ie9-win7/serve.py /Users/alex/Dev/cdom-ie5/tests/out 8099`; the guest reaches it at http://10.0.2.2:8099/.
- Progress that survives a crash: a synchronous request to /mark/<step> through ActiveXObject("Microsoft.XMLHTTP") shows in the server's log.

## What each engine has (tests/probe.html)

tests/probe.html runs each probe through eval and sends its result to /mark/<order>-probe-<name>-<result>, by synchronous XMLHTTP in IE and by an image request elsewhere.
tests/build.sh copies it to tests/out.
These are the results of 2026-11-20 in the XP VM.
IE 7 and IE 8 are missing because their Utilu builds do not start there.

| Probe | Code | IE 5.01 | IE 5.5 | IE 6 | Firefox 1.0 |
|---|---|---|---|---|---|
| in | `"a" in {a:1}` | error: Expected ';' | `true` | `true` | `true` |
| inArr | `0 in [5]` | error: Expected ';' | `true` | `true` | `true` |
| instanceof | `[] instanceof Array` | `true` | `true` | `true` | `true` |
| trycatch | `1` | `1` | `1` | `1` | `1` |
| undef | `typeof undefined` | `undefined` | `undefined` | `undefined` | `undefined` |
| push | `typeof [].push` | `undefined` | `function` | `function` | `function` |
| regexpI | `/a/i.test("A")` | `true` | `true` | `true` | `true` |
| charAt | `"ab".charAt(1)` | `b` | `b` | `b` | `b` |
| lastIndex | `typeof /a/g.lastIndex` | `undefined` | `number` | `number` | `number` |
| fnLiteral | `(function(){return 3})()` | `3` | `3` | `3` | `3` |
| nested | `(function(){ var f = function g(){ return typeof g; }; return f(); })()` | `function` | `function` | `function` | `function` |
| switch | `(function(){switch(2){case 2: return "s";}})()` | `s` | `s` | `s` | `s` |
| strictEq | `1 === 1` | `true` | `true` | `true` | `true` |
| delete | `(function(){var o={a:1}; delete o.a; return typeof o.a;})()` | `undefined` | `undefined` | `undefined` | `undefined` |
| labels | `(function(){ a: for(;;){ break a; } return 1; })()` | `1` | `1` | `1` | `1` |
| Error | `typeof Error + new Error("x").message` | `functionundefined` | `functionx` | `functionx` | `functionx` |
| toString16 | `(255).toString(16)` | `ff` | `ff` | `ff` | `ff` |
| fromCharCode | `String.fromCharCode(65,66)` | `AB` | `AB` | `AB` | `AB` |
| arguments | `(function(){return arguments.length})(1,2)` | `2` | `2` | `2` | `2` |
| callee | `(function(){return typeof arguments.callee})()` | `function` | `function` | `function` | `function` |
| doWhile | `(function(){var i=0; do { i++; } while(i<3); return i;})()` | `3` | `3` | `3` | `3` |
| void | `typeof void 0` | `undefined` | `undefined` | `undefined` | `undefined` |
| charCodeAt | `"A".charCodeAt(0)` | `65` | `65` | `65` | `65` |
| ushr | `(-1>>>0)` | `4294967295` | `4294967295` | `4294967295` | `4294967295` |
| concat | `[1].concat([2]).length` | `2` | `2` | `2` | `2` |
| sliceNeg | `[1,2,3].slice(-1)[0]` | `1` | `3` | `3` | `3` |
| split | `"a,b".split(",").length` | `2` | `2` | `2` | `2` |
| splitRe | `"a1b".split(/\d/).length` | `2` | `2` | `2` | `2` |
| splitEmpty | `"".split(",").length` | `1` | `1` | `1` | `1` |
| replaceRe | `"aa".replace(/a/g,"b")` | `bb` | `bb` | `bb` | `bb` |
| execGroup | `/(a)(b)?/.exec("a")[2]` | (empty) | (empty) | (empty) | `undefined` |
| nonGreedy | `/a+?/.exec("aa")[0]` | error: Unexpected quantifier | `a` | `a` | `a` |
| lookahead | `/a(?=b)/.test("ab")` | error: Unexpected quantifier | `true` | `true` | `true` |
| noncap | `/(?:a)/.test("a")` | error: Unexpected quantifier | `true` | `true` | `true` |
| getYear | `typeof new Date().getTime()` | `number` | `number` | `number` | `number` |
| fnApply | `typeof Function.prototype.apply` | `undefined` | `function` | `function` | `function` |
| encodeURI | `typeof encodeURIComponent` | `undefined` | `function` | `function` | `function` |
| toFixed | `typeof (1).toFixed` | `undefined` | `function` | `function` | `function` |
| sortFn | `[3,1,2].sort(function(a,b){return a-b}).join("")` | `123` | `123` | `123` | `123` |
| arrLen | `(function(){var a=[]; a[5]=1; return a.length})()` | `6` | `6` | `6` | `6` |
| objLit | `({"a-b":1})["a-b"]` | `1` | `1` | `1` | `1` |
| hex | `0xff` | `255` | `255` | `255` | `255` |
| typeofNull | `typeof null` | `object` | `object` | `object` | `object` |
| nan | `isNaN(NaN)` | `true` | `true` | `true` | `true` |
| cond | `true ? "t" : "f"` | `t` | `t` | `t` | `t` |
| comma | `(1,2)` | `2` | `2` | `2` | `2` |
| throwObj | `(function(){ try { throw {m:1}; } catch(e) { return e.m; } })()` | `1` | `1` | `1` | `1` |
| finally | `(function(){ var r=0; try { r=1; } finally { r=2; } return r; })()` | `2` | `2` | `2` | `2` |
| Math | `Math.floor(1.5)` | `1` | `1` | `1` | `1` |
| protoChain | `(function(){ function A(){}; A.prototype.x=1; return new A().x; })()` | `1` | `1` | `1` | `1` |
| ctorProp | `(function(){ function A(){}; return new A().constructor === A; })()` | `true` | `true` | `true` | `true` |
| fnLength | `(function(a,b){}).length` | `2` | `2` | `2` | `2` |
| RegExpCtor | `new RegExp("a","g").global` | `undefined` | `true` | `true` | `true` |
| escapeU | `escape("☃")` | `%u2603` | `%u2603` | `%u2603` | `%u2603` |
| unicodeStr | `"☃".length` | `1` | `1` | `1` | `1` |
| numKey | `({1:2})[1]` | error: Expected identifier or string | `2` | `2` | `2` |
| strNumKey | `({"1":2})[1]` | `2` | `2` | `2` | `2` |
| bangFn | `!function(){}()` | `true` | `true` | `true` | `true` |
| tinyNum | `5e-324 > 0` | `true` | `true` | `true` | `true` |
| trailingObjComma | `eval("({a:1,})").a` | error: Expected identifier or string | error: Expected identifier, string or number | error: Expected identifier, string or number | `1` |
| quotedReserved | `({"default":1})["default"]` | `1` | `1` | `1` | `1` |
| regexClassSlash | `/[/]/.test("/")` | `true` | `true` | `true` | error: unterminated character class |
| regexFlagM | `typeof /a/m` | error: Expected ';' | `object` | `object` | `function` |
| emptyRegex | `/(?=)/` | error: Unexpected quantifier | `/(` | `/(` | `/(` |
| strIndex | `"ab"[1]` | `undefined` | `undefined` | `undefined` | `b` |
| arrayHoles | `[,1].length` | `2` | `2` | `2` | `2` |
| nestedFnDecl | `(function(){ if (1) { function q(){ return 1; } } return q(); })()` | `1` | `1` | `1` | `1` |
| errorCtorMsg | `new Error("x").description` | (empty) | `x` | `x` | `undefined` |
| errorName | `new TypeError("x").name` | error: 'TypeError' is undefined | `TypeError` | `TypeError` | `TypeError` |
| TypeError | `typeof TypeError` | `undefined` | `function` | `function` | `function` |
| RangeError | `typeof RangeError` | `undefined` | `function` | `function` | `function` |
| charCodeNaN | `isNaN("".charCodeAt(0))` | `true` | `true` | `true` | `true` |
| numberToString2 | `(5).toString(2)` | `101` | `101` | `101` | `101` |
| unsignedShift | `(4294967295 >>> 0)` | `4294967295` | `4294967295` | `4294967295` | `4294967295` |
| imul | `typeof Math.imul` | `undefined` | `undefined` | `undefined` | `undefined` |
| fround | `typeof Math.fround` | `undefined` | `undefined` | `undefined` | `undefined` |
| clz32 | `typeof Math.clz32` | `undefined` | `undefined` | `undefined` | `undefined` |
| parseIntHex | `parseInt("ff", 16)` | `255` | `255` | `255` | `255` |
| getterSyntax | `eval("({get a(){return 1}})").a` | error: Expected ':' | error: Expected ':' | error: Expected ':' | `1` |
| instanceofFn | `(function(){} instanceof Function)` | `true` | `true` | `true` | `true` |
| arrayCtorLen | `new Array(3).length` | `3` | `3` | `3` | `3` |
| joinUndef | `[undefined, null, 1].join("-")` | error: 'undefined' is undefined | `--1` | `--1` | `--1` |
| sliceArgs | `Array.prototype.slice.call((function(){return arguments})(1,2)).length` | error: Object doesn't support this property or method | `2` | `2` | `2` |
| concatArgs | `[].concat(1,[2,3]).length` | `3` | `3` | `3` | `3` |
| lastIndexOfStr | `"aba".lastIndexOf("a")` | `2` | `2` | `2` | `2` |
| substrNeg | `"abc".substr(-1)` | `abc` | `abc` | `abc` | `c` |
| dateNow | `typeof Date.now` | `undefined` | `undefined` | `undefined` | `function` |
| windowProp | `typeof window.undefined` | `undefined` | `undefined` | `undefined` | `undefined` |

What the table changed in the build:

- in, numKey, regexFlagM, nonGreedy, lookahead, noncap and emptyRegex are syntax errors in IE 5.01, now refused by es3().
- TypeError, RangeError, lastIndex and RegExp's global are missing in IE 5.01, so they joined JSCRIPT55 in tools/legacy/es3.mjs (TypeError as supplied).
- errorCtorMsg and Error show the lost message, which cdomError answers.
- regexClassSlash is a syntax error in Firefox 1.0, now refused by es3().
- sliceNeg shows that IE 5.01's Array slice does not count a negative start from the end; nothing in the tier's file passes a negative literal, and it is not yet supplied (see below).

## Not done yet

- A Chrome profile, LEGACY=ie5 in tools/cdp.mjs: LEGACY=ie6's plus Array's push, pop, shift, unshift and splice, Function's call and apply (the profile's own code through Reflect), Object.prototype.hasOwnProperty and propertyIsEnumerable deleted, TypeError deleted, Error given a number's meaning for its argument, replace given a function returning the function's text, and slice counting a negative start from 0.
  Then tools/legacy.mjs passing on it, the polyfills (holds, cdomError, TypeError) checked against the built-ins there, and CI lines in .github/workflows/ci.yml.
- Array slice with a negative start or end, supplied in polyfills-es3.js when [1, 2, 3].slice(-1) is wrong, once the profile can test it.
