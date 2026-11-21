# The descent below IE 5

## The ladder and Alex's calls

Alex's calls of 2026-11-20:

- Netscape 2 (1995) is the floor.
  cdom runs on every browser from there up.
- Below IE 5 a frames tier covers Netscape 2 to 4 and IE 3 and 4.
  Each page is a frameset, and script rewrites a child frame.
- Side effects are acceptable (the Back history, flicker), but avoid them where it costs nothing.
- A browser with no script gets, at build time, a screenshot of the app, Alex's message and a snowman.
  There is no server for it.
- Whether Netscape 2's code is a WebAssembly interpreter written in JavaScript 1.0 or a transform of wasm2js's output is decided by measurement, after the probe below.

The ladder, from the top:

| Tier | Browsers | Files |
|---|---|---|
| modern | anything with WebAssembly | cdom.js and the module |
| legacy | ES5 without WebAssembly | cdom.legacy.js and the wasm2js file |
| legacy-ie9 | no typed arrays | cdom.legacy-ie9.js |
| legacy-ie6 | IE 5.5 to 8 (docs/ie6-design.md, docs/ie5-design.md) | cdom.legacy-ie6.js |
| frames | Netscape 2 to 4, IE 3, IE 4 | to be designed (branch frames-tier) |
| no script | any | a build-time screenshot, a message and a snowman |

## The probe

tests/probe-js1.html measures what each old engine has, one test per script block, so a syntax error or a run-time error kills only its own block.
It runs from the page's load: each block writes a 1x1 image, /mark/\<tag\>/p-\<n\>-\<name\>-\<escaped value\>, before the test (`start`), with its value, and after it (`ok`).
tools/probe_marks.py reads the serve.py log and prints the table.
tests/probe-frames.html checks that a script can rewrite a child frame (`parent.f.document.open`, `write`, `close`), and whether the Back history grows.
tools/gen_probe_js1.py writes probe-js1.html.

### What I expect, written before the first run

Netscape 2.02 is JavaScript 1.0, which I expect lacks:

- `typeof`, `void`, and every literal: `{a:1}`, `[1,2]`, `/re/`.
- Function expressions and `new Function`.
- `new Array` as an object (JS 1.0 arrays are plain objects with no length); the Array methods.
- `switch`, `do`-`while`, labels, `===`, `in` as an operator, `delete`, `try`/`catch`/`throw`, `with` probably present.
- `charCodeAt` and `String.fromCharCode` (JavaScript 1.2), `split` maybe present, `replace` absent, `escape` and `unescape` present.
- `setInterval`, `window.onerror`, `document.images`, `new Image`.
- Present: `for`-`in`, `arguments`, `this`, `new` with a function constructor, `Math.floor`, `parseInt` with a radix, the bitwise operators, `eval`, `setTimeout` with a string, forms and their elements.
- Number precision: doubles, so 2^53+1 rounds; `>>>` should work.
- Recursion: I expect depth 100 to work and 1000 to be doubtful; 5000 to fail.

Netscape 3 adds `typeof`, `void`, `new Function`, `Array` and `Image`, `document.images`, and `onerror`.
Netscape 4 adds literals, function expressions, switch, do-while, labels, regular expressions, `charCodeAt`, `fromCharCode`, `delete`, and `try` stays out until 4.5/JS 1.4.
IE 3 (JScript 1.0) I expect to lack arrays' methods, `new Image` and `document.images`.

Speed: the loops should run in the tens of milliseconds on this Mac's emulated Pentium: an interpreter's overhead decides the matter, not these engines' absolute speed.

## What the runs found

The runs are of 2026-11-20 in the XP VM (docs/ie5-design.md, "Driving the XP VM"): Netscape 2.02, 3.04, 4.08 and IE 4.01, IE 5.01 and IE 6 on this Mac's QEMU, which runs a Pentium in software, so the timings below are slow in absolute terms and meaningful only against each other.
The marks of every run are in docs/descent-marks.txt (serve.py log lines, one tag per browser), and `tools/descent_table.sh` prints the table from them.
Netscape 1.1 is installed and shows pages, but the probe was not run in it.

How each browser was run:

- Netscape: `C:\NS202\Program\netscape.exe`, `C:\NS304\Program\netscape.exe` and `C:\NS408\Program\netscape.exe` (Netscape 1.1 is in `C:\NS11`), with the page's URL and a tag in its query string (`probe-js1.html?ns2`).
- Netscape 2 and 3 pop a JavaScript Error alert for each failed block, and Netscape 2 stacks them as windows that must each be dismissed (`tools/vmalerts.sh` taps the OK).
  Netscape 4 shows none.
- IE 4 shows a Script Error dialog for each failed block, behind its window, and takes the "Yes" only from a click (`tools/vmpart.sh`).
- IE 5.01 stays silent (a status bar icon) and crashes at Done, so its pages carry the hold image (`sed 's#</BODY>#<IMG SRC="http://10.0.2.2:8096/x.gif" WIDTH=1 HEIGHT=1></BODY>#'`, a copy that is never committed).
- IE 6 is the control.
- IE 3.00 (the Utilu collection's IE300; IE301 was not tried) starts and shows pages, but run no script at all: not one mark of the first block arrived, though "Run ActiveX scripts" is on.
  A page with script is plain HTML there.
- IE 4 and IE 5.01 stop on a deep recursion (IE 4 at recurse500, IE 5.01 at recurse500, with a "Stack overflow" dialog in IE 4 and IE 6), so the whole-page runs were cut into `probe-js1-part<k>.html` pages, 30 tests each and without the recursion tests.
  IE 4's page also stalled loading twice after about 120 marks, so twenty cells of it are "n/c" (not collected) in the table.
- The page's `window.onerror` handler crashes Netscape 3.04 when it is installed on the whole page, so the main page has none; tests/probe-onerror.html tests it alone.

### The table

Rows are the tests of tests/probe-js1.html in page order, then the speed tests, which ran on their own pages (probe-speed-\<name\>.html: one test each, so that no alert waits on a person inside a timing).
`no run` means no mark at all: the block did not parse (a syntax error) or never started.
`DIED(start)` means the block started and a run-time error ended it before its answer.
`n/c` means not collected.
Speed values are `new Date().getTime()` differences in milliseconds, on this slow emulated Pentium.

| test | ns2 | ns304 | ns408 | ie4 | ie501 | ie6 |
|---|---|---|---|---|---|---|
| ua | Mozilla/2.02 (WinNT; I) | Mozilla/3.04 (WinNT; I) | Mozilla/4.08 [en] (WinNT; I ;Nav) | Mozilla/4.0 (compatible; MSIE 4.01; Windows NT 5.0; SV1) | Mozilla/4.0 (compatible; MSIE 5.01; Windows NT 5.0; SV1) | Mozilla/4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1) |
| appVersion | 2.02 (WinNT; I) | 3.04 (WinNT; I) | 4.08 [en] (WinNT; I ;Nav) | 4.0 (compatible; MSIE 4.01; Windows NT 5.0; SV1) | 4.0 (compatible; MSIE 5.01; Windows NT 5.0; SV1) | 4.0 (compatible; MSIE 6.0; Windows NT 5.1; SV1) |
| appName | Netscape | Netscape | Netscape | Microsoft Internet Explorer | Microsoft Internet Explorer | Microsoft Internet Explorer |
| onerror_pre | <undefined> | undefined | undefined | null | null | null |
| newArray3 | <undefined> | 3 | 3 | 3 | 3 | 3 |
| newArrayCall | no run | no run | 3 | 3 | 3 | 3 |
| typeof | no run | number | number | number | number | number |
| typeofUndef | no run | undefined | undefined | undefined | undefined | undefined |
| void | no run | undefined | undefined | undefined | undefined | undefined |
| protoAssign | DIED(start) | DIED(start) | 1 | 1 | 1 | 1 |
| objLit | no run | no run | 1 | 1 | 1 | 1 |
| arrLit | no run | no run | 2 | 2 | 2 | 2 |
| fnExpr | no run | no run | 1 | 1 | 1 | 1 |
| newFunction | DIED(start) | 2 | 2 | 2 | 2 | 2 |
| newFunctionCall | no run | no run | 2 | 2 | 2 | 2 |
| arrJoin | DIED(start) | 1-2-3 | 1-2-3 | 1-2-3 | 1-2-3 | 1-2-3 |
| arrGrow | <undefined> | 6 | 6 | 6 | 6 | 6 |
| arrSort | DIED(start) | 123 | 123 | 123 | 123 | 123 |
| arrReverse | DIED(start) | 213 | 213 | 213 | 213 | 213 |
| arrPush | DIED(start) | DIED(start) | 2 | DIED(start) | DIED(start) | 2 |
| arrPop | DIED(start) | DIED(start) | 7 | DIED(start) | DIED(start) | 7 |
| arrSlice | DIED(start) | DIED(start) | 2 | 2 | 2 | 2 |
| arrSplice | DIED(start) | DIED(start) | 2 | DIED(start) | DIED(start) | 2 |
| arrConcat | DIED(start) | DIED(start) | 3 | 3 | 3 | 3 |
| arrNoLen | 0 | 0 | 0 | 0 | 0 | 0 |
| ctorProp | DIED(start) | true | true | true | true | true |
| instanceof | no run | no run | no run | no run | true | true |
| dateGetTime | true | true | true | true | true | true |
| dateGetTimeCall | no run | no run | true | true | true | true |
| isNaN | DIED(start) | DIED(start) | true | true | true | true |
| NaNGlobal | DIED(start) | DIED(start) | NaN | NaN | NaN | NaN |
| InfinityGlobal | DIED(start) | DIED(start) | Infinity | Infinity | Infinity | Infinity |
| undefinedGlobal | DIED(start) | DIED(start) | undefined | DIED(start) | DIED(start) | undefined |
| stringProto | DIED(start) | 1 | 1 | 1 | 1 | 1 |
| objectProto | DIED(start) | 2 | 2 | 2 | 2 | 2 |
| strLength | 3 | 3 | 3 | 3 | 3 | 3 |
| charCodeAt | DIED(start) | DIED(start) | 65 | 65 | 65 | 65 |
| fromCharCode | DIED(start) | DIED(start) | AB | AB | AB | AB |
| split | DIED(start) | 3 | 3 | 3 | 3 | 3 |
| charAt | b | b | b | b | b | b |
| indexOf | 1 | 1 | 1 | 1 | 1 | 1 |
| indexOfFrom | 3 | 3 | 3 | 3 | 3 | 3 |
| lastIndexOf | 3 | 3 | 3 | 3 | 3 | 3 |
| substring | bc | bc | bc | bc | bc | bc |
| substr | <undefined> | undefined | bcd | bcd | bcd | bcd |
| slice | DIED(start) | DIED(start) | bc | bc | bc | bc |
| toUpperCase | ABC | ABC | ABC | ABC | ABC | ABC |
| replace | DIED(start) | DIED(start) | a-b | a-b | a-b | a-b |
| strCompare | true | true | true | true | true | true |
| strIndex | null | null | b | undefined | undefined | undefined |
| escape | a%20b%E9 | a%20b%E9 | a%20b%C3%A9 | a%20b%E9 | a%20b%E9 | a%20b%E9 |
| unescape | A | A | A | A | A | A |
| encodeURIComponent | DIED(start) | DIED(start) | DIED(start) | DIED(start) | DIED(start) | a%20b |
| strConcatNum | a12 | a12 | a12 | a12 | a12 | a12 |
| toStringNum | DIED(start) | ff | ff | ff | ff | ff |
| toStringBin | DIED(start) | 101 | 101 | 101 | 101 | 101 |
| bitAnd | 2 | 2 | 2 | 2 | 2 | 2 |
| bitOr | 7 | 7 | 7 | 7 | 7 | 7 |
| bitXor | 5 | 5 | 5 | 5 | 5 | 5 |
| bitNot | -6 | -6 | -6 | -6 | -6 | -6 |
| shl | 16 | 16 | 16 | 16 | 16 | 16 |
| shr | -4 | -4 | -4 | -4 | -4 | -4 |
| ushr | 15 | 15 | 15 | 15 | 15 | 15 |
| ushrNeg1 | 4294967295 | 4294967295 | 4294967295 | 4294967295 | 4294967295 | 4294967295 |
| shl31 | -2147483648 | -2147483648 | -2147483648 | -2147483648 | -2147483648 | -2147483648 |
| add2p31 | 2147483648 | 2147483648 | 2147483648 | 2147483648 | 2147483648 | 2147483648 |
| big53 | 9007199254740992 | 9007199254740992 | 9007199254740992 | 9007199254740992 | 9007199254740992 | 9007199254740992 |
| bigMul | 281462092005375 | 281462092005375 | 281462092005375 | 281462092005375 | 281462092005375 | 281462092005375 |
| floor | -2 | -2 | -2 | -2 | -2 | -2 |
| parseInt16 | 255 | 255 | 255 | 255 | 255 | 255 |
| parseIntHexLit | 31 | 31 | 31 | 31 | 31 | 31 |
| parseFloat | 150 | 150 | 150 | 150 | 150 | 150 |
| mod | -1 | -1 | -1 | -1 | -1 | -1 |
| div | 0.33333333333333331 | .3333333333333333 | .3333333333333333 | 0.3333333333333333 | 0.3333333333333333 | 0.3333333333333333 |
| hexLit | 255 | 255 | 255 | 255 | 255 | 255 |
| mathAbs | 2 | 2 | 2 | 2 | 2 | 2 |
| mathSqrt | 4 | 4 | 4 | 4 | 4 | 4 |
| mathPow | 1024 | 1024 | 1024 | 1024 | 1024 | 1024 |
| mathRound | 3 | 3 | 3 | 3 | 3 | 3 |
| mathMax | 3 | 3 | 3 | 3 | 3 | 3 |
| numKeys | ab | ab | ab | ab | ab | ab |
| numKeyAsStr | a | a | a | a | a | a |
| forIn | ba | abbar | abbar | ab | barab | barab |
| forInVar | a | abar | abar | a | bara | bara |
| argsLength | DIED(start) | 3 | 3 | 3 | 3 | 3 |
| argsIndex | DIED(start) | 2 | 2 | 2 | 2 | 2 |
| fnArguments | 3 | 3 | 3 | 3 | 3 | 3 |
| hasArrayObject | function Array() { <native code> } | function Array() { [native code] } | function Array() { [native code] } | function Array() { [native code] } | function Array() { [native code] } | function Array() { [native code] } |
| hasDateObject | function Date() { <native code> } | function Date() { [native code] } | function Date() { [native code] } | function Date() { [native code] } | function Date() { [native code] } | function Date() { [native code] } |
| recurse300 | DIED(start) | DIED(start) | 300 | n/c | 300 | 300 |
| recurse500 | DIED(start) | DIED(start) | 500 | DIED(start) | DIED(start) | 500 |
| callee | DIED(start) | DIED(start) | true | true | true | true |
| fnLength | 2 | 2 | 2 | 2 | 2 | 2 |
| recurse100 | 100 | 100 | 100 | n/c | n/c | 100 |
| recurse1000 | DIED(start) | DIED(start) | DIED(start) | n/c | n/c | 1000 |
| recurse5000 | DIED(start) | DIED(start) | DIED(start) | n/c | n/c | DIED(start) |
| closureVar | no run | no run | 5 | 5 | 5 | 5 |
| thisInMethod | 4 | 4 | 4 | 4 | 4 | 4 |
| newCtor | 3 | 3 | 3 | 3 | 3 | 3 |
| newCtorCall | no run | no run | 3 | 3 | 3 | 3 |
| hasOwnProperty | DIED(start) | DIED(start) | DIED(start) | DIED(start) | DIED(start) | true |
| toStringObj | DIED(start) | [object Object] | [object Object] | [object Object] | [object Object] | [object Object] |
| switch | no run | no run | two | two | two | two |
| switchStr | no run | no run | A | A | A | A |
| doWhile | no run | no run | 3 | 3 | 3 | 3 |
| labeled | no run | no run | 4 | 4 | 4 | 4 |
| regexLit | no run | no run | true | true | true | true |
| regexExec | no run | no run | aa | n/c | aa | aa |
| regexNew | no run | no run | true | n/c | true | true |
| strictEq | no run | no run | true | n/c | true | true |
| inOp | no run | no run | no run | n/c | no run | true |
| tryCatch | no run | no run | no run | n/c | 1 | 1 |
| tryFinally | no run | no run | no run | n/c | 1 | 1 |
| throw | no run | no run | no run | n/c | 1 | 1 |
| delete | DIED(start) | null | undefined | n/c | undefined | undefined |
| ternary | y | y | y | n/c | y | y |
| comma | 2 | 2 | 2 | n/c | 2 | 2 |
| withStmt | no run | undefined | 5 | n/c | 5 | 5 |
| whileBreakContinue | 8 | 8 | 8 | n/c | 8 | 8 |
| nestedFunc | no run | no run | 3 | n/c | 3 | 3 |
| fnAssignedVar | no run | no run | 7 | 7 | 7 | 7 |
| evalBasic | 3 | 3 | 3 | 3 | 3 | 3 |
| evalFn | <undefined> | DIED(start) | undefined | undefined | undefined | undefined |
| setTimeoutStr | set | set | set | set | set | set |
| setTimeoutFn | set | set | set | set | set | set |
| setInterval | DIED(start) | DIED(start) | set | set | set | set |
| documentImages | DIED(start) | 264 | 356 | 19 | 19 | 377 |
| documentImagesType | <undefined> | [object ImageArray] | [object ImageArray] | [object] | [object] | [object] |
| newImage | DIED(start) | loaded | loaded | loaded | loaded | loaded |
| formsLength | 1 | 1 | 1 | 1 | 1 | 1 |
| formRead | v0 | v0 | v0 | v0 | v0 | v0 |
| formWrite | v1 | v1 | v1 | v1 | v1 | v1 |
| formByName | v1 | v1 | v1 | v1 | v1 | v1 |
| locationHref | http://10.0.2.2:8102/probe-js1.html?ns2 | http://10.0.2.2:8102/probe-js1.html?ns304e | http://10.0.2.2:8102/probe-js1.html?ns408 | http://10.0.2.2:8102/probe-js1-part4.html?ie4p4 | http://10.0.2.2:8102/zz-hold-part4.html?ie501p4 | http://10.0.2.2:8102/probe-js1.html?ie6 |
| documentLastModified | Fri Nov 20 10:23:22 2026 | 11/20/26 10:55:28 | 11/20/26 10:55:28 | 11/20/26 19:16:51 | 11/20/2026 11:46:48 | 11/20/2026 10:55:28 |
| windowName |  |  |  | ok |  |  |
| historyLength | 1 | 1 | 1 | 0 | 0 | 0 |
| javaEnabled | DIED(start) | true | true | true | true | true |
| pluginsLen | DIED(start) | 5 | 5 | 0 | 0 | 0 |
| documentGetElementById | <undefined> | undefined | undefined | undefined | function getElementById() { [native code] } | function getElementById() { [native code] } |
| documentAll | <undefined> | undefined | undefined | [object] | [object] | [object] |
| documentLayers | <undefined> | undefined | [object LayerArray] | ok | undefined | undefined |
| documentCookie |  |  |  |  |  |  |
| screenWidth | DIED(start) | DIED(start) | 640 | 640 | 640 | 640 |
| innerWidth | <undefined> | undefined | 620 | ok | undefined | undefined |
| documentBgColor | #ffffff | #ffffff | #ffffff | #ffffff | #ffffff | #ffffff |
| speedLoop | 2672ms x=11952 | 3484ms x=11952 | 547ms x=11952 | 203ms x=11952 | 359ms x=11952 | 203ms x=11952 |
| speedObjKeys | 3453ms m5=99333 | 4672ms m5=99333 | 500ms m5=99333 | 453ms m5=99333 | 312ms m5=99333 | 375ms m5=99333 |
| speedArrKeys | 1531ms m5=99333 | 5094ms m5=99333 | 469ms m5=99333 | 172ms m5=99333 | 312ms m5=99333 | 438ms m5=99333 |
| speedCall | 3140ms x=100000 | 4407ms x=100000 | 1859ms x=100000 | 1047ms x=100000 | 1422ms x=100000 | 3438ms x=100000 |
| speedCharCodeAt | absent | absent | 344ms x=970000 len=10240 | 157ms x=970000 len=10240 | 234ms x=970000 len=10240 | 187ms x=970000 len=10240 |
| speedConcat1000 | 469ms len=1000 | 125ms len=1000 | 110ms len=1000 | 16ms len=1000 | 63ms len=1000 | 63ms len=1000 |
| speedConcat2000 | 1812ms len=2000 | 312ms len=2000 | 188ms len=2000 | 32ms len=2000 | 188ms len=2000 | 109ms len=2000 |
| done | end | end | end | end | end | end |
| tstr | n/c | fired | fired | n/c | fired | fired |
| speedConcat | n/c | n/c | 1547ms len=10000 | 656ms len=10000 | 1266ms len=10000 | 797ms len=10000 |
| tfn | n/c | no run | fired | n/c | fired | fired |
| interval | n/c | no run | 3 | n/c | 3 | 3 |

### What each engine lacks, in plain language

**Netscape 2.02 (JavaScript 1.0)** is the poorest engine here, and the one that decides the design.

- Syntax it cannot parse, so that any file containing it is dead (the alert says "syntax error"):
  `typeof`, `void`, object literals `{a:1}`, array literals `[1,2]`, regular expression literals, function expressions, a function assigned to a variable, a function declared inside a function (so no closures), `switch`, `do`-`while`, labels, `===`, the `in` operator, `instanceof`, `try`/`catch`/`finally`/`throw`, `with`, and member access on a `new` expression (`new Date().getTime()` is a syntax error: it takes `var d = new Date(); d.getTime()`).
  What parses: `if`, `for`, `while`, `break`/`continue`, `?:`, the comma operator, `for (k in o)` (in the reverse of insertion order), `eval`, `new F(x)` into a variable, and function declarations at the top level.
- Run-time gaps:
  the globals `isNaN`, `NaN`, `Infinity` and `undefined` do not exist;
  `arguments` does not exist (`f.arguments` does);
  an `Array` exists but has no `length` growth and no methods (`join`, `sort`, `push` and the rest all die), so arrays must be plain objects with numeric keys, which work (`o[1]`, `o["1"]`);
  `prototype` on a user function ("prototype has no properties"), `constructor`, `hasOwnProperty`, `delete` (it parses and dies), `new Function`;
  strings have `charAt`, `indexOf`, `lastIndexOf`, `substring`, `toUpperCase` and `escape`/`unescape`, but not `charCodeAt`, `String.fromCharCode`, `split`, `slice`, `substr`, `replace`, `toString(radix)` or `s[i]`;
  `setInterval`, `document.images`, `new Image` (so a script cannot make an image request after the page is written), `navigator.javaEnabled`, `navigator.plugins`, `screen`, and `window.onerror` (an error always raises the alert).
- What works: every bitwise operator including `>>>` (so `-1>>>0` is 4294967295 and `1<<31` is negative), doubles with the usual precision (2^53+1 rounds to 2^53, 65535^3 is exact), `Math.floor`, `abs`, `sqrt`, `pow`, `round`, `max`, `parseInt` with a radix and `0x`, `parseFloat`, the number keys of a plain object, `this` in a method, `eval`, `setTimeout` with a string, and forms (`document.forms[0].elements[0].value` reads and writes).
- Calls nest to a depth of at most about 100 to 299: `recurse100` works, `recurse300` dies.
  That is the figure that matters for compiled code: a wasm call is a JavaScript call in the transform.
- Speed: 100000 iterations of `x=(x+i)&0xffff` take 2.7 s, 100000 stores into an object by number 3.5 s, 100000 calls of a one-line function 3.1 s, and building a string by `s=s+"a"` is quadratic: 2000 characters take 1.8 s, 1000 take 0.47 s (10000 would take about 45 s).

**Netscape 3.04 (JavaScript 1.1)** adds `typeof`, `void`, `new Function`, an `Array` with a `length`, `join`, `sort` and `reverse` (not `push`, `pop`, `slice`, `splice` or `concat`), the `constructor` property, `split`, `toString(radix)`, `arguments`, `document.images`, `new Image`, `javaEnabled` and `plugins`, and `setTimeout` still takes only a string (a function is not called).
It still cannot parse literals, function expressions, nested functions, `switch`, `do`-`while`, labels, regular expressions, `===`, `in`, `instanceof` or `try`, and it still lacks `isNaN`, `NaN`, `Infinity`, `undefined`, `charCodeAt`, `fromCharCode`, `slice`, `replace` and `setInterval`.
`delete` gives null and `with` gives undefined (both are wrong).
`recurse300` dies.
It is no faster than Netscape 2 on loops (3.5 s for the 100000-iteration loop).
It stops after about ten errors with "too many JavaScript errors" until the alert is dismissed, and a page that installs `window.onerror` at its top crashed it (a page with only a handler and one error is fine: the handler is called).

**Netscape 4.08 (JavaScript 1.2)** parses all the literals, function expressions, nested functions and closures, `switch`, `do`-`while`, labels, regular expressions, `===` and `with`, has `charCodeAt`, `fromCharCode`, `slice`, `substr`, `replace`, `push`, `pop`, `splice`, `concat`, `isNaN`, `NaN`, `Infinity`, `arguments.callee`, `setInterval` and `s[i]`, and calls `setTimeout` with a function.
It still has no `try`/`catch`/`throw`, no `in`, no `instanceof`, no `hasOwnProperty`, no `encodeURIComponent`, and `escape` writes UTF-8.
It nests calls to at least 500 but not 1000, and is about five times faster than Netscape 2 and 3 (0.55 s for the loop).
`window.onerror` works and no alert is shown.

**IE 3.00** runs no script in this collection: nothing of JScript 1.0 could be measured.

**IE 4.01 (JScript 3.0)** has what Netscape 4 has (literals, closures, `switch`, `do`-`while`, labels, regular expression literals, `===`), but not `push`, `pop` or `splice`, `instanceof`, the `undefined` global, `encodeURIComponent`, or `hasOwnProperty`, and its `s[i]` is undefined.
It stops with "Stack overflow" somewhere at or below depth 500.
Its speed is about that of IE 6 (0.2 s for the loop).
Its tests of `in`, `try`, `throw`, regular expression `exec`, `delete` and a few statements were not collected.

**IE 5.01 (JScript 5.1)** adds `try`/`catch`/`finally`/`throw` and `instanceof`, calls `setTimeout` with a function and runs `setInterval` (IE 4's run of those was not collected), and still lacks `push`, `pop`, `splice`, `in`, `hasOwnProperty`, `encodeURIComponent` and the `undefined` global.
It dies at recursion depth 500.

**IE 6** has all of it, `recurse1000` works, and `recurse5000` is a stack overflow.

### Frames and the Back history

tests/probe-frames.html is a frameset of two frames, f and g.
g's onload calls the frameset's `go()`, which does `parent.f.document.open()`, two `write` calls and `close()`; the written document marks by an image (and by a script that writes an image), and g reports `history.length` before and, 1.5 s later, after.

| Browser | f rewritten from outside | the written script ran | `history.length` before, after | Back after |
|---|---|---|---|---|
| Netscape 2.02 | yes | yes | 1, no mark (g had no way to report: no `Image`) | greyed: the history did not grow |
| Netscape 3.04 | yes | yes | 1, 1 | enabled |
| Netscape 4.08 | yes | yes | 1, 1 | enabled |
| IE 4.01 | yes | yes | 0, 0 | enabled |
| IE 5.01 | yes | yes | 0, 0 | enabled |
| IE 6 | yes | yes | 0, 1 | enabled |
| IE 3 | no script | no | no | no |

So every engine that can run script can replace a child frame's document from the frameset, and a script inside that document runs.
Netscape 2 does it without touching the history; the others leave a Back entry (the Back button was greyed before the write in the Netscape 4 and IE 4 screenshots, and enabled after), where `history.length` says nothing useful.
A document written into a frame has no base URL in Netscape 2: a relative `/mark/...` image in it is broken, and an absolute URL works.
The screenshots are in /Users/alex/VMs/ie6-xp/shots (frames-*.png, p-*.png).

### The expectations, marked

- Confirmed for Netscape 2: no `typeof`, `void`, literals, function expressions, `new Function`, `switch`, `do`-`while`, labels, `===`, `in`, `try`, `charCodeAt`, `fromCharCode`, `replace`, `setInterval`, `window.onerror`, `document.images`, `new Image`, and working bitwise operators, doubles, `parseInt` with a radix, `escape`, `eval`, forms and number keys.
- Wrong: I expected `split` to exist (it does not), `arguments` to exist (it does not, only `f.arguments`), `with` to parse (it does not), `new Array` to fail (the call works, the array is just a plain object with no length), the depth limit to be between 100 and 1000 (it is between 100 and 300), and `delete` to fail to parse (it parses and dies).
- A surprise that no expectation covered: `new X(...).y` is a syntax error in Netscape 2 and 3, which `new Date().getTime()` hits, and the string `s=s+"a"` is quadratic in Netscape 2 and 3 to a degree that makes building a 10000-character string take most of a minute.
- Netscape 3 and 4: as expected, 3 adds `typeof`, `void`, `Array` and `Image`, and 4 adds the literals and statements; 4 does not have `try`.
- IE 3 was expected to lack arrays' methods and `Image`; it runs no script at all.

### What this means for the interpreter against transform decision

It is the measurement's reading, not the decision.

- Syntax: wasm2js's output is full of what Netscape 2 cannot parse (function expressions, `switch`, closures, literals, `===`, `new X().y`, `typeof`), so a transform must rewrite the whole language down to the statements above, and every generated file stays one syntax error from dead.
  An interpreter written in JavaScript 1.0 is written once, by hand, in that subset.
- Calls: compiled code nests JavaScript calls as deep as the C program's calls (the LaTeX app and the layout code recurse well beyond 100 deep), and Netscape 2 and 3 die at depth 300 or less.
  An interpreter keeps its own stack in a plain object and calls nothing recursively.
  This alone argues for the interpreter on these two engines.
- Speed: Netscape 2 runs a three-operator statement in a loop at about 27 microseconds on this emulated Pentium (2.7 s per 100000), which is about 13 times slower than IE 4 and 5 times slower than Netscape 4 on the same hardware.
  An interpreter adds a further factor of tens on top of that for its dispatch, so only small programs (the Mosaic demo, forms, a table of numbers) are practical on Netscape 2 either way, and the transform is only the faster by that factor.
- Memory and data: numbered keys on a plain object work (`o[1]`), but a store costs 35 microseconds, so a wasm linear memory kept as an object per byte is slow and large; a memory of packed numbers per word is the shape to measure next.
- Missing helpers the runtime needs are all small: `charAt` and `indexOf` and `substring` exist, so strings can be walked by `indexOf` into a lookup string (there is no `charCodeAt`), and `escape` gives the code of a character as `%XX`.

## Inside Netscape 2.02's engine

Read from the binary on 2026-11-20: NS202/Program/netscape.exe (2,201,088 bytes, dated 1996-04-29), copied off the XP VM's disk, with strings and llvm-objdump.
The engine is linked into netscape.exe (there is no separate DLL); its own name in the strings is Mocha.

- It compiles each script to bytecode and interprets it ("unimplemented Mocha bytecode %d", "internal error: stack underflow at pc %d").
- The recursion limit is not a count of calls.
  The function at 0x55190c allocates each context with its value stack inline (malloc of size + 0x118), and the page's context ("mozilla", created at 0x4419b6) passes 0x2000: an 8 KB stack of 16-byte values, 512 slots.
  The checks at 0x54f7df and 0x54f956 raise "stack overflow in %s" when sp + n * 16 reaches the limit.
  So the depth is 512 divided by the slots each frame holds: the probe's `return 1 + r(n-1)` holds about 3 (the 1, the function, the argument), which gives about 170, between the 100 that passed and the 300 that died.
  A frame with more arguments or deeper expressions nests less; any code path for Netscape 2 must keep its live JavaScript values under 512 in all, not only its call depth.
- Nested functions are refused by name: "function defined inside a function".
- Reserved but not implemented: switch, case, default, throw, catch, delete, and the Java words; `typeof` is not even a keyword.
- Built-ins named in the binary: String (substring, toLowerCase, toUpperCase, charAt, indexOf, lastIndexOf, chop, match, split, substr and the HTML wrappers), Boolean, Number, parseInt, parseFloat, Math (floor, round, sqrt, ceil and the trig functions), Date, Array, eval and setTimeout.
  No charCodeAt or fromCharCode is there, which agrees with the probe.
- Two of those disagree with the table above and need a second run: the probe called split on a string literal (`'a,b,c'.split(',')`) and found it missing, and found `new Array(3).length` undefined, but both names are in the binary.
  The next probe should call split on a variable, and test what `new Array(3)` gives (number keys, no length).
- Typing `mocha:` in the location field opens the engine's own console (two frames, MochaInput and MochaOutput).
- An assignment written as a condition is silently turned into a comparison: the parser's messages "test for equality (==) mistyped as assignment (=)?" and "Assuming equality test".
  Generated code for Netscape 2 must never assign inside an `if` or `while` condition (`while (x = next())` would compare instead).
- The event handlers the binary knows are onload, onunload, onfocus, onblur, onchange, onselect, onsubmit, onclick, onmouseover and onscroll.
  There is no onmouseout, no onmousedown or onmouseup, and no key event at all, so on Netscape 2 input is clicks, field changes and form submits.
- `<SCRIPT LANGUAGE=...>` accepts JavaScript, LiveScript and Mocha, and `livescript:` and `mocha:` URLs work beside `javascript:`.
- The Mocha console is a frameset (75% output, 25% input) with an ISINDEX field posting into the output frame: the engine's own authors built interactive UI in Netscape 2 with frames and a form, which is the frames tier's approach.
- Proxy auto-config starts here: the binary calls `FindProxyForURL("%s","%s")` in Mocha, the same function every PAC file still defines.
- Functions print their source by decompiling the bytecode (format strings like `while (%s) {` and `function %s(`), and String has a Perl-style `chop` that later versions dropped.

## The second probe

Reading netscape.exe (the section "Inside Netscape 2.02's engine", on the ns2-engine branch, PR #101) left questions that only a run can settle.
tests/probe-js1b.html asks them, written by `tools/gen_probe_js1.py b` in the first probe's style: one test per script block, marks by `document.write` of an image, the same `tools/probe_marks.py` (with `--page tests/probe-js1b.html`).
Its string tests use double quotes, where the first probe's used single ones.
The recursion grid comes last, so that an engine which stops at a stack overflow has reported the rest first.
tests/probe-nested.html is a function inside a function alone, for the text of its alert.
tests/probe-events.html has a link, a button and a text field with every handler, each of which appends its name to a text field, and a "report" button that sends the field to /mark/; it is driven through vm.py.

### What I expect, written before the run

- split: methods on a literal work in Netscape 2 (`'abc'.charAt(1)` gave b), so a literal against a variable should make no difference, and I expect split to die on the variable too, as it did on the literal.
  The name in the binary would then belong to something else.
  Falsifier: `splitVar0` is `a`.
- Methods on a double-quoted literal: `"abc".length` 3, `charAt(1)` b, `indexOf("c")` 2, as with single quotes.
- `new Array(3)`: no `length` (undefined, as in the first probe), `a[0]` undefined, still no `length` after `a[5] = 1`; `new Array()` works and has no `length`; `new Array(1,2,3)` stores its arguments, so `a[1]` is 2 (a guess at even odds).
- `s.chop()` gives `hell` and leaves `s` as `hello`; `s.match("ell")` gives true and `s.match("zz")` false (a string match, not a regular expression); `s.substr(1,2)` gives undefined, as on the literal.
- The assignment in a condition is compared in Netscape 2: `ifAssign05` gives `compared-false x=0`, `ifAssign00` gives `compared-true x=0`, `whileAssign03` gives `c=0 x=0`, and `whileAssign00` runs to its guard, `c=6 x=0`.
  Netscape 3 does the same (the same parser); Netscape 4.08 (with `LANGUAGE="JavaScript"`) and IE 4 assign (`assigned-true x=5`, `assigned-false x=0`, `c=6 x=3`, `c=0 x=0`).
- The value stack: 512 slots of 16 bytes per page context, shared with the block's own `mark("name", ...)` (about 3 slots).
  A frame of `r0` holds the function and its argument (2 slots), of `r1` the 1, the function and the argument (3), of `r3` the function and four arguments (5).
  So the deepest call that works is about 254 for r0 (250 passes, 275 dies), 170 for r1 (160 passes, 180 dies) and 101 for r3 (100 passes, 120 dies).
  Falsifiers: if the three die at the same depth, the limit counts calls, not slots; if the ratios hold but the levels are lower, each frame holds a fixed number of further slots, which the three depths together give.
  Netscape 3 is the same as Netscape 2 (it also died at r1 300); Netscape 4.08 passes 600 for all three; IE 4 passes 400 and dies at 512 or 600 for r1, with its "Stack overflow" dialog.
- Event handlers: Netscape 2 fires onmouseover, onclick and onchange, and never onmouseout, onmousedown, onmouseup or any key event.
  Netscape 3 adds onmouseout; Netscape 4.08 and IE 4 fire all of them.
- A function inside a function: Netscape 2's alert names "function defined inside a function".

### The follow-up, expected before its run

Netscape 2's run of probe-js1b (below) gave undefined from `chop`, `match` and `substr`, and an undefined result from `split` (then "has no properties" on its `[0]`), and the depths fell between grid points.
tests/probe-js1c.html (`gen_probe_js1.py c`) asks whether those methods are there at all, and narrows the depths to a step of 2 for r0 and r1 and 5 for r3.

- `"" + s.charAt` prints a native function, and so do `s.split`, `s.chop`, `s.match` and `s.substr` (the binary names them), while `s.zzz` is undefined and `s.zzz(1)` dies: the four are present and return undefined in 2.02.
  `split` on a `new String` object is the same as on a primitive.
- Depths, with a frame of 2, 3 and 6 slots (r3 died at 100, so it holds more than the 5 I counted: the sixth is a guess) and about 3 slots under the first call: the deepest r0 that works is 252 to 254, r1 167 or 168, r3 83 or 84 (so r3 dies at 85 or 90 on this grid).

### What the second probe found

The runs are of 2026-11-20, in the same XP VM, served from the worktree's tests/ on port 8104.
The marks are in docs/descent-marks-2.txt, and `tools/descent_table2.sh` prints the tables from them.
How each browser was run:

- Netscape 2.02 ran probe-js1b.html, then probe-js1c.html, probe-nested.html and probe-events.html.
  Each failed block raises its own alert window, and the "Assuming equality test" warning also raises one, though the block goes on.
  The alerts sit behind the browser window after the first is dismissed; `taskkill /f /im netscape.exe` from Run clears them all.
- Netscape 3.04 ran probe-js1b.html twice (the first run's marks arrived only after its "too many JavaScript errors" box was dismissed; the two runs agree) and probe-events.html.
- Netscape 4.08 ran both pages in one go with no dialog.
- IE 4.01 stalled loading the whole page after 42 marks, as it did in the first probe, so it ran `probe-js1b-part<k>.html` (`gen_probe_js1.py b part <k> 15`) and, for the tests after its stalls, pages of single tests that were not kept.
  It stops running a page's later scripts after a "Stack overflow at line: 44" box, so its 512 and 600 depths were not collected (they cannot pass where 400 died).
- The events row is the "report" button's mark after the same input in each browser: the pointer over the link and off it, a click on "press me", a click in the text field, typing "ab", and a click outside it.

| test | ns2 | ns304 | ns408 | ie4 |
|---|---|---|---|---|
| ua | Mozilla/2.02 (WinNT; I) | Mozilla/3.04 (WinNT; I) | Mozilla/4.08 [en] (WinNT; I ;Nav) | Mozilla/4.0 (compatible; MSIE 4.01; Windows NT 5.0; SV1) |
| splitVarLen | DIED(start) | 3 | 3 | 3 |
| splitVar0 | DIED(start) | a | a | a |
| splitVar1 | DIED(start) | b | b | b |
| splitVar2 | DIED(start) | c | c | c |
| splitVarStr | <undefined> | a,b,c | a,b,c | a,b,c |
| splitLitVar0 | DIED(start) | a | a | a |
| splitLitLen | DIED(start) | 3 | 3 | 3 |
| litLength | 3 | 3 | 3 | 3 |
| litCharAt | b | b | b | b |
| litIndexOf | 2 | 2 | 2 | 2 |
| newArray3Len | <undefined> | 3 | 3 | 3 |
| newArray3El0 | null | null | undefined | undefined |
| newArray3Grow | <undefined> | 6 | 6 | 6 |
| newArray3Str | <object Array> | ,, | ,, | ,, |
| newArrayEmptyLen | <undefined> | 0 | 0 | 0 |
| newArrayEmptyGrow | <undefined> | 2 | 2 | 2 |
| newArrayList1 | null | 2 | 2 | 2 |
| newArrayListLen | <undefined> | 3 | 3 | 3 |
| newArrayListStr | <object Array> | 1,2,3 | 1,2,3 | 1,2,3 |
| chop | <undefined> | undefined | DIED(start) | DIED(start) |
| chopAfter | hello | hello | DIED(start) | DIED(start) |
| match | <undefined> | undefined | ell | ell |
| matchMiss | <undefined> | undefined | null | null |
| substrVar | <undefined> | undefined | el | el |
| ifAssign05 | compared-false x=0 | compared-false x=0 | assigned-true x=5 | assigned-true x=5 |
| ifAssign00 | compared-true x=0 | compared-true x=0 | assigned-false x=0 | assigned-false x=0 |
| whileAssign03 | c=0 x=0 | c=0 x=0 | c=6 x=3 | c=6 x=3 |
| whileAssign00 | c=6 x=0 | c=6 x=0 | c=0 x=0 | c=0 x=0 |
| depth0_50 | true | true | true | true |
| depth1_50 | 50 | 50 | 50 | 50 |
| depth3_50 | true | true | true | true |
| depth0_100 | true | true | true | true |
| depth1_100 | 100 | 100 | 100 | 100 |
| depth3_100 | DIED(start) | DIED(start) | true | true |
| depth0_120 | true | true | true | true |
| depth1_120 | 120 | 120 | 120 | 120 |
| depth3_120 | DIED(start) | DIED(start) | true | true |
| depth0_140 | true | true | true | true |
| depth1_140 | 140 | 140 | 140 | 140 |
| depth3_140 | DIED(start) | DIED(start) | true | true |
| depth0_160 | true | true | true | true |
| depth1_160 | 160 | 160 | 160 | 160 |
| depth3_160 | DIED(start) | DIED(start) | true | true |
| depth0_180 | true | true | true | true |
| depth1_180 | DIED(start) | DIED(start) | 180 | 180 |
| depth3_180 | DIED(start) | DIED(start) | true | true |
| depth0_200 | true | true | true | true |
| depth1_200 | DIED(start) | DIED(start) | 200 | 200 |
| depth3_200 | DIED(start) | DIED(start) | true | true |
| depth0_225 | true | true | true | true |
| depth1_225 | DIED(start) | DIED(start) | 225 | 225 |
| depth3_225 | DIED(start) | DIED(start) | true | true |
| depth0_250 | true | true | true | true |
| depth1_250 | DIED(start) | DIED(start) | 250 | 250 |
| depth3_250 | DIED(start) | DIED(start) | true | true |
| depth0_275 | DIED(start) | DIED(start) | true | true |
| depth1_275 | DIED(start) | DIED(start) | 275 | 275 |
| depth3_275 | DIED(start) | DIED(start) | true | true |
| depth0_300 | DIED(start) | DIED(start) | true | true |
| depth1_300 | DIED(start) | DIED(start) | 300 | 300 |
| depth3_300 | DIED(start) | DIED(start) | true | true |
| depth0_400 | DIED(start) | DIED(start) | true | DIED(start) |
| depth1_400 | DIED(start) | DIED(start) | 400 | DIED(start) |
| depth3_400 | DIED(start) | DIED(start) | true | DIED(start) |
| depth0_512 | DIED(start) | DIED(start) | true | n/c |
| depth1_512 | DIED(start) | DIED(start) | 512 | n/c |
| depth3_512 | DIED(start) | DIED(start) | true | n/c |
| depth0_600 | DIED(start) | DIED(start) | true | n/c |
| depth1_600 | DIED(start) | DIED(start) | 600 | n/c |
| depth3_600 | DIED(start) | DIED(start) | true | n/c |
| done | end | end | end | end |
| events | over click change | over out click change | over out mousedown mouseup click keydown keypress keyup keydown keypress keyup change | over out mousedown mouseup click keydown keypress keyup keydown keypress keyup change |

Netscape 2.02's run of the follow-up page:

| test | ns2c |
|---|---|
| ua | Mozilla/2.02 (WinNT; I) |
| charAtProp | function charAt() { <native code> } |
| splitProp | function split() { <native code> } |
| chopProp | function chop() { <native code> } |
| matchProp | function match() { <native code> } |
| substrProp | function substr() { <native code> } |
| missingProp | <undefined> |
| missingCall | DIED(start) |
| splitResult | <undefined> |
| splitStrObj1 | DIED(start) |
| strObjCharAt | DIED(start) |
| depth0_252 | true |
| depth0_254 | DIED(start) |
| depth0_256 | DIED(start) |
| depth0_258 | DIED(start) |
| depth0_260 | DIED(start) |
| depth0_262 | DIED(start) |
| depth0_264 | DIED(start) |
| depth0_266 | DIED(start) |
| depth0_268 | DIED(start) |
| depth0_270 | DIED(start) |
| depth0_272 | DIED(start) |
| depth0_274 | DIED(start) |
| depth1_162 | 162 |
| depth1_164 | 164 |
| depth1_166 | 166 |
| depth1_168 | DIED(start) |
| depth1_170 | DIED(start) |
| depth1_172 | DIED(start) |
| depth1_174 | DIED(start) |
| depth1_176 | DIED(start) |
| depth1_178 | DIED(start) |
| depth3_55 | true |
| depth3_60 | true |
| depth3_65 | true |
| depth3_70 | true |
| depth3_75 | true |
| depth3_80 | true |
| depth3_85 | true |
| depth3_90 | true |
| depth3_95 | true |
| done | end |

The dialogs, whose screenshots are in /Users/alex/VMs/ie6-xp/shots (p2-*.png):

- A function inside a function, in Netscape 2.02: "JavaScript Error: ... line 42: syntax error.", with the source line and a caret under the inner `function`.
  The binary's "function defined inside a function" is not what a page sees.
- The assignment in a condition, in Netscape 2.02 and 3.04: "test for equality (==) mistyped as assignment (=)? Assuming equality test.", with the source line and a caret under the condition; the block then runs on, comparing.
- `s.zzz(1)` in Netscape 2.02: "zzz is not a function."; `new String("a,b,c")`: "String is not defined."; `p[0]` after `split`: "has no properties."; the deep calls: "stack overflow in r0." (r1, r3).
- `s.chop()` in IE 4.01: "Object doesn't support this property or method"; the deep calls: "Stack overflow at line: 44".

### The predictions, marked

- split: right for Netscape 2, and the reason is now known: `s.split` is a native function ("function split() { <native code> }"), as are `chop`, `match` and `substr`, but in 2.02 all four return undefined, on a variable or a literal alike.
  So the names in the binary are stubs; an unknown method (`s.zzz(1)`) dies instead.
  Netscape 3.04 splits on a variable and on a literal, with a `length`.
- Methods on a double-quoted literal: right (3, b, 2), in all four.
- `new Array` in Netscape 2: right that there is no `length` (also none after `a[5] = 1`, and none for `new Array()`), and `a[0]` is null; wrong on `new Array(1,2,3)`: `a[1]` is null, the arguments are not stored, and the array prints as `<object Array>`.
  Netscape 3 and later have the real Array.
- `chop`, `match`, `substr`: wrong for Netscape 2 (all undefined, where I expected `hell` and true); Netscape 3.04 is the same; Netscape 4.08 and IE 4 have `match` (`ell`, null) and `substr` (`el`), and no `chop` (it dies).
- The assignment in a condition: right in all four.
  Netscape 2.02 and 3.04 compare, with the warning box; Netscape 4.08 (with `LANGUAGE="JavaScript"`) and IE 4.01 assign.
- The value stack: right in form and close in number.
  The deepest call that works in Netscape 2.02 is r0(252) (r0(254) dies), r1(166) (r1(168) dies) and r3 between 95 and 99 (r3(100) dies), against my 252 to 254, 167 or 168, and (from the first page) 101.
  The limit is in slots, not calls: a frame of r0, r1 and r3 holds 2, 3 and 5 values, and the depths stand at 253, 167 and 96 to 100 frames, 506, 501 and 480 to 500 slots of the 512.
  About 6 slots go to the block itself and the room each call checks for ahead; that r3 stops a few frames short of 101 fits the check reserving room for its four arguments.
  My follow-up guess that r3 holds 6 slots was wrong (it would stop at about 84; r3(95) works).
  Netscape 3.04 stops at the same points on the coarse grid; Netscape 4.08 passes 600 for all three; IE 4.01 passes 300 and dies at 400 for all three, a limit on calls, not on values.
- Event handlers: right in all four.
  Netscape 2.02 fires onmouseover, onclick and onchange only; Netscape 3.04 adds onmouseout; Netscape 4.08 and IE 4.01 fire every one (mousedown, mouseup, keydown, keypress, keyup).
- A function inside a function: wrong; the alert says only "syntax error."

### What this changes

- Netscape 2 has no working `split`, `substr`, `match` or `chop`: they exist and return undefined.
  Code for it walks strings with `charAt`, `indexOf` and `substring`, and that is all.
- Netscape 2 has no usable Array: `new Array(n)` gives an object with no `length` and `new Array(1,2,3)` drops its arguments.
  Plain objects with number keys and a separate count are the only array.
- Netscape 2 and 3 have a 512-slot value stack per page: the deepest recursion is about (512 - 6) / (values per frame), so 253 frames of two values and about 100 of five.
  An interpreter for them must keep its own stack in an object and never recurse in JavaScript; a transform cannot promise any C call depth beyond a hundred or so.
- Netscape 2 and 3 compare where an assignment is written as a condition, so generated code must never assign inside an `if` or `while` condition; Netscape 4 and IE 4 assign, so the code would behave differently by browser.
- Netscape 2's input is clicks, form changes and the pointer entering a link; Netscape 3 adds leaving it; there is no key or button-press event before Netscape 4 and IE 4.
- IE 4 nests calls to between 300 and 400, whatever the frame's size, and stops the page's remaining scripts at the first stack overflow.

## The three code paths in the real browsers

Three branches take the app down to Netscape 2, all on origin/frames-tier: js1-transform (PR #103, wasm2js's output rewritten to JavaScript 1.0), js1-interp (PR #105, a WebAssembly interpreter written in JavaScript 1.0) and js1-compile (PR #104, tools/js1c.c, a compiler from wasm to JavaScript 1.0 with an explicit stack).
This section runs one program through all three in the real browsers of the XP VM.

tests/js1bench.c is that program: js1-transform's tests/js1cpu.c `loop` and `rec` without cdom (no imports, so every path builds it), and `mem`, which fills a 64 KB buffer a word at a time and sums it.
tools/js1bench.sh builds it with each branch's own tool, from that branch's worktree, and tools/js1bench.mjs writes a page per path (tests/out/js1bench-t.html, -i.html, -c.html) with everything inlined, since Netscape 2 has no SCRIPT SRC.
Each run is its own SCRIPT block, timed with `new Date().getTime()` around the call, its answer checked against node's WebAssembly, and reported to /mark/ as the probe pages do.
The runs, in page order: `loop(2000)` three times, `mem(7)`, `rec(100)` three times, then `rec(500)`, `rec(1000)` and `rec(5000)` (the deep ones last, since IE 4 stops a page at its first stack overflow).
In node all three pages give every answer right.

### What I expect, written before the runs

The rates come from the first probe's speed rows: Netscape 2.02 runs a simple operation in about 9 us (the loop `x = (x + i) & 0xffff` at 27 us an iteration of about three), Netscape 3.04 about the same, Netscape 4.08 about five times faster and IE 4.01 about thirteen times faster than Netscape 2; a call costs about 31 us and an object store 35 us on Netscape 2.

- `loop(2000)`: an iteration is about 25 operations in the transform and the compiler's output (a multiply in halves, shifts, two loads and a store into the memory object), so about 0.45 s on Netscape 2 and 3, 0.1 s on Netscape 4 and 40 ms on IE 4, for both.
  The interpreter's own estimate is about 300 us an instruction on Netscape 2 and some 12 register instructions an iteration: about 7 s on Netscape 2 and 3, 1.5 s on Netscape 4, 0.5 s on IE 4, so 10 to 20 times the other two.
- `mem(7)`: 32,768 iterations of about 12 operations: about 3.5 s on Netscape 2 and 3 for the transform and the compiler, 0.7 s on Netscape 4, 0.3 s on IE 4; the interpreter 15 to 20 times that, about 60 s on Netscape 2.
- `rec`: the transform keeps each C call a JavaScript call, and a wasm2js frame of `rec_` holds the function, its argument and its locals, about 7 of the 512 slots, so on Netscape 2 and 3 it dies with "stack overflow" between depth 50 and 100: `rec(100)` dies (if the frame holds 5 or fewer it passes, and the limit is under 100 either way).
  On Netscape 4.08 it passes 500 and dies at 1000 (the probe's `recurse1000` died there); on IE 4 it passes 100 and dies at 500 ("Stack overflow at line"), which also ends the page there.
  The compiler and the interpreter keep their own stacks and pass every depth to 5000 in every browser: the interpreter at about 10 instructions a level (about 3 ms a level on Netscape 2, so `rec(5000)` about 15 s), the compiler at about 20 operations a level through its driver (about 0.2 ms, `rec(5000)` about 1 s on Netscape 2).
- Netscape 2 parses all three pages (each path's file passed its own branch's JavaScript 1.0 gate, and the pages' scripts pass js1-compile's), unless the gates missed something measured in the second probe: a working `split`, `substr`, `match`, a `new Array` with arguments or a `length`, or an assignment inside a condition would load and then give wrong answers, not a syntax error.
- None of the browsers shows a slow-script prompt (none of them has one); Netscape 2 and 3 freeze while a run lasts.

### What the runs found

The runs are of 2026-11-20 in the XP VM, each branch's tests/out served on its own port (8105 transform, 8106 interpreter, 8107 compiler) with the pages copied in, and the marks in docs/descent-marks-3.txt (`tools/js1bench_table.py docs/descent-marks-3.txt <tags>` prints each table).
A cell is the run's time from `new Date().getTime()` when its answer was right, the time and `<got> WANT <wanted>` when it was wrong, `died` when the run started and never reported (a run-time error or a stack overflow), and `-` when no mark came (the page had stopped).
The times are the guest's clock on this Mac's emulated Pentium, so they compare only against each other, and two runs of the same page on Netscape 2 differed by up to 2.4 times (`loop(2000)` took 188 ms in one transform run and 468 ms in the next); the tables are one run each.
Netscape 2 and 3 do have a slow-script prompt, "Lengthy JavaScript still running. Continue?", and a run's time includes the wait on it: the interpreter's `mem` on Netscape 2 (309 s) is mostly that wait, and Netscape 3's 37 s is the better figure for it.

Three of the runs needed the page to work around a blocker first, and those columns are the `NS2FIX=1` pages (tools/js1bench.mjs; the branches are not changed):

- The interpreter does not load on Netscape 2: "function does not always return a value." (the screenshot shots/js1real-ns2i-parse.png: `wi_call` has both `return;` and `return 0;`, which Netscape 2 refuses at parse time, so cdom.js1.js is dead and the page then says "wi_load is not defined.").
  With every such function made to return 0, it parses, then dies decoding the module: "f is not a number.", because of the object aliasing below.
  The ns2if3 and ns3if columns are the page with both worked around (digit keys given a "k" in front); ns3i2, the plain page on Netscape 3.04, did not load either (its dialog was not captured).
- The compiler dies on Netscape 2 at the first read of a memory word nothing wrote: "33041 is not a number." (`undefined | 0` is an error there, measured below), so ns2c's every run but `mem` died; ns2cf sets the words the program touches to 0 after the load (as the transform's own load does).
  On Netscape 3.04 its file does not parse at all: "missing ) after for-loop control." at `for(i=n-1;i>=0;i--)` in the memmove helper (ns3c2); ns3cf writes `i=i-1` there.
- The transform needed nothing more than tests/js1bench.c's data being a multiple of 3 bytes: its base64 decoder dies on Netscape 2 at a padding `=` ("= is not a number.", the load never finishing), and its decoded data is wrong there anyway (below).

js1-transform (wasm2js rewritten):

| run | ns2t3 | ns3t | ns4t | ie4t |
|---|---|---|---|---|
| load | 2125ms | 4797ms | 641ms | 157ms |
| loop2000_1 | 468ms | 1016ms NaN WANT 2973945277 | 93ms | 31ms |
| loop2000_2 | 422ms | 906ms NaN WANT 3269008660 | 93ms | 32ms |
| loop2000_3 | 469ms | 844ms NaN WANT 2585209365 | 109ms | 32ms |
| mem | 4031ms 2974073576 WANT 1000733454 | 5016ms | 610ms | 219ms |
| rec25 | 0ms | 0ms NaN WANT 967463077 | 0ms | 0ms |
| rec50 | 16ms | 16ms NaN WANT 3397157813 | 15ms | 0ms |
| rec75 | 0ms | 32ms NaN WANT 2491738207 | 16ms | 0ms |
| rec100_1 | died | died | 15ms | 0ms |
| rec100_2 | died | died | 15ms | 15ms |
| rec100_3 | died | died | 15ms | 0ms |
| rec500 | died | died | 16ms | died |
| rec1000 | died | died | died | - |
| rec5000 | died | died | died | - |

js1-compile (js1c, an explicit stack); ns2c is the plain page on Netscape 2, the others past it the NS2FIX page on Netscape 2 and 3 and the plain page on Netscape 4 and IE 4:

| run | ns2c | ns2cf | ns3cf | ns4c | ie4c |
|---|---|---|---|---|---|
| load | 0ms | 1890ms | 2750ms | 0ms | 31ms |
| loop2000_1 | died | 531ms | 719ms NaN WANT 2973945277 | 125ms | 125ms |
| loop2000_2 | died | 625ms | 828ms NaN WANT 3269008660 | 78ms | 204ms |
| loop2000_3 | died | 688ms | 906ms NaN WANT 2585209365 | 109ms | 141ms |
| mem | 1531ms | 4890ms | 5921ms | 734ms | 781ms |
| rec25 | died | 16ms | 16ms NaN WANT 967463077 | 0ms | 0ms |
| rec50 | died | 31ms | 31ms NaN WANT 3397157813 | 0ms | 15ms |
| rec75 | died | 47ms | 93ms NaN WANT 2491738207 | 16ms | 16ms |
| rec100_1 | died | 47ms | 63ms NaN WANT 3690416189 | 16ms | 31ms |
| rec100_2 | died | 62ms | 62ms NaN WANT 4120471005 | 31ms | 16ms |
| rec100_3 | died | 109ms | 62ms NaN WANT 983828318 | 16ms | died |
| rec500 | died | 344ms | 454ms NaN WANT 968582496 | 110ms | - |
| rec1000 | died | 547ms | 765ms NaN WANT 4134822262 | 172ms | 110ms 3019458935 WANT 4134822262 |
| rec5000 | died | 2843ms | 4094ms NaN WANT 1509260939 | 859ms | 1125ms 3302671763 WANT 1509260939 |

js1-interp (the interpreter), the NS2FIX page on Netscape 2 and 3 and the plain page on Netscape 4 and IE 4:

| run | ns2if3 | ns3if | ns4i | ie4i |
|---|---|---|---|---|
| load | 844ms | 1328ms | 281ms | 125ms |
| loop2000_1 | 2234ms | 2875ms NaN WANT 2973945277 | 265ms | 312ms |
| loop2000_2 | 2297ms | 2922ms NaN WANT 3269008660 | 266ms | 312ms |
| loop2000_3 | 2250ms | 2875ms NaN WANT 2585209365 | 265ms | 313ms |
| mem | 308875ms | 37078ms | 1984ms | 2516ms |
| rec25 | 31ms | 31ms | 15ms | 0ms |
| rec50 | 47ms | 78ms NaN WANT 3397157813 | 15ms | 15ms |
| rec75 | 94ms | 110ms NaN WANT 2491738207 | 16ms | 0ms |
| rec100_1 | 109ms | 140ms NaN WANT 3690416189 | 32ms | 16ms |
| rec100_2 | 110ms | 141ms NaN WANT 4120471005 | 15ms | 16ms |
| rec100_3 | 109ms | 140ms NaN WANT 983828318 | 16ms | 16ms |
| rec500 | 625ms | 719ms NaN WANT 968582496 | 78ms | 78ms |
| rec1000 | 1125ms | 1422ms NaN WANT 4134822262 | 187ms | 141ms |
| rec5000 | 6531ms | 7188ms NaN WANT 1509260939 | 750ms | - |

### What the engines do that the gates did not know

tests/probe-js1d.html, written while diagnosing the runs above (one test a block, as the probe pages), in Netscape 2.02 and 3.04:

| test | ns2d4 | ns3d |
|---|---|---|
| newArrayNoParens | 1 | 1 |
| arrStore1500 | 3 | 3 |
| objStore1500 | 3 | 3 |
| pow2_1023 | 8.9884656743115795e+307 | 8.98846567431158e307 |
| pow2_1024 | 1.#INF | Infinity |
| overflowMul | 1.#INF | Infinity |
| halve1074 | true | true |
| halve1075 | 0 | 0 |
| divZero | 0 | NaN |
| table | 8.9884656743115795e+307 | 8.98846567431158e307 |
| arr32767 | 7 | 7 |
| arr32768 | 7 | 7 |
| arr32768alias | 1,7 | 1,7 |
| arr65536alias | 1,7 | 1,7 |
| arrNeg | <undefined> | undefined |
| obj32768 | 1,7 | 1,7 |
| obj65536alias | 1,7 | 1,7 |
| arrFill40000 | 100,32868,39999 | 100,32868,39999 |
| undefOr0 | DIED(start) | 0 |
| nullOr0 | DIED(start) | 0 |
| toInt32big | -1 | -1 |
| toInt32wrap | 5 | NaN |
| mulBig | -131070 | -131070 |
| objNamedThenIndex | 1,2 | null,null |
| objDigitKeyAlias | 52,52 | 1,52 |
| objDigitKeyLate | 0,9,2 | 0,1,2 |
| or0big | -1321022019 | -1321022019 |
| ushrNegBig | 2973945277 | 2973945277 |
| mul3big | -3 | NaN |
| mulHuge | -1651712 | NaN |
| mulHugeRaw | 7046029209275649000 | 7046029209275649000 |
| orOver2p53 | 0 | NaN |
| orOver2p63 | 0 | NaN |
| orOver2p64 | 0 | NaN |
| ushrOver2p64 | 0 | NaN |
| andBig | 0 | NaN |
| orInf | 0 | NaN |
| done | end | end |

- Netscape 2 aliases a number key to a named property: `o[0]` is the first property set by name, so `o["0"] = 52` overwrites `o.A` (objNamedThenIndex, objDigitKeyAlias).
  Any lookup table keyed by characters that include digits is corrupted: the transform's base64 table (its data comes out wrong: `mem` reads the wrong `mix` words, 2974073576 for 1000733454) and the interpreter's digit-string table ("f is not a number").
  Netscape 3 does not alias.
- Netscape 2 dies on `undefined | 0` and `null | 0` (undefOr0, nullOr0): a never-written word of an object memory must be set before it is read.
- Netscape 3.04 gives NaN for `x | 0`, `x >>> 0` and `x & 255` when x is 2^32 or more (mul3big, orOver2p53 and the rest), where Netscape 2 and every later engine reduce modulo 2^32.
  This is why all three paths give NaN for `loop` and `rec` on Netscape 3 (a product or a sum passes 2^32 before its `| 0`) and the right answer for `mem` (whose values stay small): every path needs its multiply and add to reduce before the conversion to run on Netscape 3.
- Netscape 2's `1 / 0` is 0 (divZero), not infinity; overflow by multiplication does give infinity (printed `1.#INF`).
- Indices past 1,023 and past 32,767 work in both an Array and an Object on Netscape 2 (the transform's open question).

### The predictions, marked

- `loop(2000)` on Netscape 2: transform and compiler about 0.45 s, measured 0.42 to 0.47 s and 0.53 to 0.69 s: right.
  The interpreter 7 s, measured 2.25 s: three times better than its own estimate, 5 times the transform's and 3 to 4 times the compiler's.
  Netscape 4, 0.1 s for both: right (93 to 125 ms); IE 4, 40 ms: right for the transform (31 ms), the compiler took 125 to 204 ms.
- `mem`: transform and compiler 3.5 s on Netscape 2, measured 4.0 s and 4.9 s: about right; the interpreter 60 s, measured 37 s on Netscape 3 (Netscape 2's 309 s is the prompt's wait).
- The transform's depth: right everywhere: it passes 75 and dies at 100 on Netscape 2 and 3 ("stack overflow in c_c."), passes 500 and dies at 1000 on Netscape 4, and passes 100 and dies at 500 on IE 4 ("Stack overflow at line: 6", which ends the page).
- The compiler and the interpreter pass every depth to 5000 in every browser that runs them, as predicted; `rec(5000)` takes 2.8 s and 6.5 s on Netscape 2 against my 1 s and 15 s.
- Wrong: that all three would parse on Netscape 2 (the interpreter does not) and on Netscape 3 (the compiler does not); that a page with right gates would give right answers (Netscape 2's object aliasing and `undefined | 0`, Netscape 3's NaN); and that no browser has a slow-script prompt.
- Not predicted: IE 4 gave wrong answers for the compiler's `rec(1000)` and `rec(5000)` on this page (3019458935 for 4134822262), where js1-compile's own js1diff.html, which runs sliced, gave 26 of 26 on IE 4.01 and Netscape 4.08 (shots/js1real-ie4diff-end.png, js1real-ns4diff-end.png); the page runs it unsliced, and why that differs is not settled.

### The correctness pages and the frames app

- js1-compile's tests/out/js1diff.html: 26 of 26 on Netscape 4.08 and IE 4.01.
- js1-interp's tests/out/js1.html: every run `ok` on Netscape 4.08 and IE 4.01 (loop2000 344 and 454 ms, rec5000 531 and 484 ms, about 100,000 instructions a second).
- frames.html, on Netscape 4.08, for all three branches alike: a blank page, and in the JavaScript console "syntax error." at `var cdom = (() => {` (the page's inlined modern shim), "throw is a reserved identifier." at `setTimeout(function () { throw e; }, 0);` near line 2045, and in cdom.frames.js line 2 "cf_FRAMESET is not defined." (shots/js1real-ns4-frames-*.png).
  So the frames tier itself does not boot on Netscape 4.08 from origin/frames-tier, whatever the code path; the app was not tried on Netscape 3.04 or IE 4.01, nor the transform's js1cpu page, within the time this run had.

### Verdict

The compiler is the path to take, once its two blockers are fixed.
It runs the integer loop within 1.1 to 1.6 times the transform's time on Netscape 2 (0.53 to 0.69 s against 0.42 to 0.47 s) and within 1.2 times on Netscape 4 and `mem` within 1.2 times on Netscape 2, and it never runs out of stack: `rec(5000)` in 2.8 s on Netscape 2, where the transform dies before depth 100 on Netscape 2 and 3, 1000 on Netscape 4 and 500 on IE 4, an inherent limit no fix removes.
The interpreter has the same unlimited depth but is 3 to 4 times slower than the compiler on Netscape 2 and 3 for the loop and about 6 times for memory (`mem` 37 s against 5.9 s on Netscape 3), and 2 to 3 times on Netscape 4 and IE 4.
What blocks each path today, from these runs:

- js1-compile: on Netscape 2, a read of a never-written word (`undefined | 0` dies: zero the memory at load, as the transform does); on Netscape 3, `i--)` in a for loop's update does not parse; IE 4's wrong `rec(1000)` unsliced.
- js1-interp: on Netscape 2, a function returning a value on one path and none on another does not parse ("function does not always return a value"), and the digit-string table is corrupted by Netscape 2's number-key aliasing.
- js1-transform: the call depth; and on Netscape 2 its base64 decoder (a padding `=` stops the load, and digit keys corrupt the data).
- All three: Netscape 3's NaN from `| 0` on a value of 2^32 or more; and the frames tier's own boot on Netscape 4.


## Netscape 2's limits for inline code

Netscape 2 has no SCRIPT SRC (frames-design A12), so the frames tier's app frame has to be one document with every script inline, split into blocks of at most one named constant of bytes.
Five questions decide that constant and whether the plan works at all.
Each is one probe page, generated by tools/gen_probe_ns2_limits.py into tests/probe-ns2-*.html, and run in the real Netscape 2.02 of the XP VM by tools/vmprobe.sh.
A block reports by a 1x1 image written at the end of the page, as the earlier probes do, so what the server's log holds is what ran.

### What I expect, written before the first run

- Q1, one inline block of real statements (`var a123=123;`): 8K, 16K and 32K run; 64K runs; 128K does not.
  The compiler keeps its bytecode in one allocation that grows, so I expect no hard limit before memory, and a failure at 128K only if the VM's 1 GB and the engine's heap give out.
  Past the limit I expect an alert ("out of memory" or a syntax error), not a silent stop.
  Falsifier: 128K runs (no limit near), or 16K fails (a 16-bit offset somewhere).
- Q1, one long string literal: the lexer's token buffer grows the same way, so 64K runs; if there is a limit it is a power of two between 4K and 32K.
  Prediction: 32K runs, 128K fails with an alert.
- Q2, many blocks: there is no limit on the count or the total before memory; 40 blocks of the Q1 size run, and a total of about 1 MB is fine.
  Falsifier: the blocks stop running after some total (the document buffer or the page context).
  The longest string literal equals the longest block (the same buffer).
- Q3: a function defined in one block is callable from a later block, and a global set in block k is seen in block k+1 (the earlier probes had helper functions in the head's block used by every later block, so this is nearly certain).
- Q4: the frameset written by the early script is made, cdomApp's script rewrites cdomView with open("text/html","replace"), write and close, the view's `javascript:parent.cdomApp.f(1)` link arrives in cdomApp, and a rewrite from inside that call works (Netscape 2 has no `replace` argument and may add a history entry, but draws).
  A rewrite of a 5 KB document takes 100 to 400 ms on this QEMU (slow guest: Netscape 3 took tens of seconds for a 230-node board, but that is layout of many nodes; 5 KB is a handful).
- Q5: a 150 KB inline-script document (the size of demo/uttt's compiled module) loads in 2 to 8 s: compile is linear, and the guest is slow.
  Falsifier: over 30 s, which would make the inline plan too slow, or a crash.
- The recommended block constant, if Q1 and Q2 hold as predicted: 30000 bytes is safe.

### What the runs found

The runs are of 2026-11-21 in the XP VM, Netscape 2.02, served by tools/vmserve.py from tests/ (`VMDIR=tests tools/vmprobe.sh`).
The screenshots are /Users/alex/VMs/ie6-xp/shots/ns2lim-*.png (ns2lim-final.png is the empty desktop at the end).
Times are the server's wall clock (vmserve.py logs milliseconds now), from the page's GET to its mark.
This guest's clock runs about 3 times slow (the taskbar clock gained 27 minutes in 79 of the host's, and the engine's `Date` gave 1.3 s for a block the server saw take 3.8 s), so the engine's own ms in the marks are a third of the wall time.

| test | result |
| --- | --- |
| one block of statements `var a123=123;`, 8K, 16K, 32K, 64K, 128K, 256K, 512K | all run; the page takes 0.5 s, 0.6 s, 0.5 s, 1.0 s, 1.5 s, 2.7 s, 3.8 s |
| the same, 1024K (70,000 distinct names) | an alert "JavaScript Error: too many atoms", and the page stops: the later blocks never run, no mark |
| one block of 1024K, 1536K of statements that reuse three names | runs, 13 s and 20 s |
| one block of 2048K (the same statements) | Netscape 2 crashes at 83% of the load: Windows' "has encountered a problem" box (Don't Send), nothing runs |
| one string literal, 1K to 128K (one page) | all run, the length read back is exact; 4.1 s for the page |
| one string literal alone: 512K, 1024K | run, the length exact; 44 s and 165 s (the lexer is quadratic: 128K costs about a second, 512K 44 s) |
| 10, 20 blocks of 30000 bytes of statements with new names | run, 2.2 s and 4.1 s (300 KB, 600 KB) |
| 40 blocks of 30000 bytes with new names (80,000 names) | "too many atoms", then nothing: no mark |
| distinct global names: 30,000, 50,000, 60,000, 65,000 / 70,000 | run / "too many atoms", so the table holds about 65,536 (2 to the 16th) |
| 40, 100 blocks of 30000 bytes that reuse three names (1.2 MB, 3 MB in all) | run, 9.5 s and 22 s |
| a function and a global defined in block 1, used in blocks 2 and 3, and a `var` of block 2 read in the last block | all seen: the global 5, the call 42, the `var` 11, a function of block 3 calling block 1's, 12 |
| 150 KB of the demo/uttt build's code (cdom.frames.js, then its .js1.js module three times, in 6 blocks of at most 30000 bytes), run twice | 2.4 s and 2.3 s from the GET to the mark (the engine's clock said 0.7 s) |
| the frames tier's two frames (probe-ns2-frames.html, driven by two clicks) | the frameset is made, the app frame runs (`window.name` is cdomApp), the view's `javascript:parent.cdomApp.f(1)` link calls in, and the rewrite of the view from inside that call works (the view showed "hit1", shots ns2lim-fr4-call.png); 12 rewrites of a 5.2 KB document, one after the other, take 18.2 s, 1.5 s each (the engine's clock: 5.8 s) |

Other things the runs showed:

- Numeric constants are not atoms: the 512K page has 35,000 distinct names and 35,000 distinct numbers and ran, so a name or a literal string counts and a number does not (a string was not counted separately; the longest literal pages have one each).
- An image written into a document that another frame wrote (the view, after `open`, `write` and `close`) was not requested from the server in the one run; so marks from there are lost, and the probe reports by navigating the view to a /mark/ URL instead.
- A page that writes several marks by `document.write` of an image each lost most of them in 3 of the first runs (the later requests never reached the server, "Waiting for reply" in the status line): the probes now report in one image, as one URL.
- The first all-in-one page hung for ten minutes with no alert on screen: the probe's own bug, `new Date().getTime()` (Netscape 2 has no method call on a `new` expression), raised an alert that sat behind the window.
- The letter u in a URL typed into Run broke the guest's command line: every start of Netscape 2 whose URL had one (reuse40, ?u40) ended with "Unable to locate REG.INI in directory C:\NS202\Program" and a blank window; pages and queries without the letter u started every time.
  This is vm.py's typing, not a limit of the browser, and was not chased further.
- After a long-running page the guest answers slowly (keys arrive minutes late and typed lines pile up into Run dialogs that say "ill" and "explore.exe" cannot be found); waiting, and closing each dialog, clears it.

### The predictions, marked

- Q1, statements 8K to 32K and 64K run: right.
  128K does not run: wrong, it runs, as do 256K, 512K and (with few names) 1536K; the real limit is a crash between 1.5 and 2 MB, and the atom table before that.
  An alert past the limit, not a silent stop: right for the atom table ("too many atoms", and the page stops), wrong for the size (a crash of Netscape 2).
- Q1, string literal: 64K runs, right; 32K runs and 128K fails: wrong, 128K runs, and so do 512K and 1 MB, at a cost that grows with the square of the length.
- Q2: no limit on the number of blocks or on the total before memory: right for the bytes (3 MB in 100 blocks ran), wrong in that the count of distinct names stops it at 65,536 ("too many atoms").
  The longest literal equals the longest block: wrong, both are far above any size that matters, but at different costs (a 1 MB block 13 s, a 1 MB string 165 s).
- Q3: right.
- Q4: the call arrives and the rewrite from inside it works: right.
  100 to 400 ms for a rewrite of 5 KB: wrong, 1.5 s on the wall (0.5 s of the guest's clock), 4 times the top of the prediction.
- Q5: 2 to 8 s: right, 2.3 s.
- The block constant: 30000 is safe: right.

### What this means for the inline plan

The plan works in Netscape 2.02: nothing in it needs more than a small part of the limits.
Keep the block constant at 30000: a block of 1.5 MB runs, so the constant is not near anything, and 30000 keeps each block quick to read.
What can break the plan is not the size of a block or the total but the names: the app frame's script must hold fewer than 65,536 distinct global names and string literals in all (js1c's output for demo/uttt has a few hundred), and a literal of a few hundred KB costs seconds in the lexer, so the string table is better as many literals of a few KB joined by `+` (it already is, `\001"+"4`).
A 150 KB app frame loads in about 2.3 s on this guest, a rewrite of a 5 KB view takes about 1.5 s, and that is the number to design the frames tier's per-click cost around.

## A framebuffer of 1x1 GIFs

Alex's question: how far can a picture made of stretched 1x1 GIFs go in the real Netscape 2.02, and can it fill a 640 x 480 window?
The technique: a palette of 256 one-pixel GIFs (the 216 web-safe colours and 40 more greys, about 40 bytes each), and a picture written as IMG tags stretched with WIDTH and HEIGHT, either one TABLE cell per block (BORDER=0 CELLPADDING=0 CELLSPACING=0) or plain rows of IMGs ended by BR with no white space between tags.
Every page is made by tools/gen_probe_ns2_fb.py (pages and GIFs go to tests/out/fb/, which git ignores), served by tools/vmserve.py, opened by tools/vmns2.sh through the guest agent.
A page's time is read from the server's log (the page's GET to the mark an onLoad handler requests) and from the screen (the first screendump that stays the same), both in the Mac's wall clock; the guest's clock runs about 3 times slow, which does not matter for either.

### What I expect, written before the first run

Q1, correctness.

- A stretched 1x1 GIF is a solid block, and neighbours in one row touch when no white space lies between the tags: no horizontal gap in the IMG+BR form.
- Vertical: in the IMG+BR form each row is a line, and Netscape 2 keeps the line's text descent under images that sit on the baseline.
  Prediction: a stray strip of 3 to 5 pixels under every row (the descent of the default font), so rows are not solid; ALIGN=TOP or ALIGN=ABSMIDDLE (or a BR-free line that holds only the image) does not remove it for sure, I expect it to shrink it to 0 to 2 pixels.
  Falsifier: no strip at all.
- In a TABLE with CELLPADDING=0 and CELLSPACING=0 each cell is as tall as its image plus the same baseline strip, so the table has the same strips; its cells touch horizontally.
  Prediction: both forms show the strip; the TABLE is the one that needs the least care for columns (every cell has its own width), the IMG+BR the least for rows.
- A WIDTH=1 HEIGHT=1 cell works (a 1 pixel block is drawn), but in the TABLE form the row is a whole text line tall (about 16 pixels), so it is only a 1 pixel picture where nothing else is in the row.
  A 1x1 IMG is in the IMG+BR form also a whole line tall.
  So the smallest cell that behaves is about the height of a line, until the strip is removed.

Q2, scale (a 608 x 400 area: 38x25, 76x50, 152x100, 304x200, 608x400 cells at 16, 8, 4, 2, 1 pixels).

- Time grows linearly with the tag count in the IMG+BR form and a little faster in the TABLE form (Netscape 2 lays out a table only when it has the whole of it).
  Prediction, IMG+BR: 950 tags (16 px) 3 s, 3,800 tags (8 px) 10 s, 15,200 tags (4 px) 40 s, 60,800 tags (2 px) 3 minutes or a crash, 243,200 tags (1 px) a crash or a hang (memory, or the window does not draw).
- Prediction, TABLE: the same sizes cost 2 to 3 times as much: 16 px 6 s, 8 px 25 s, 4 px several minutes (over 100 s) or an out of memory alert; 2 and 1 px do not finish.
- HTML size is about 40 bytes a cell in the IMG+BR form (`<IMG SRC=XX.gif WIDTH=16 HEIGHT=16>`), 70 bytes in the TABLE form; so 4 px is 600 KB and 2 px 2.4 MB.
- Where it breaks: an alert, a crash box, or a view that stays grey with "Waiting for reply"; my guess for the first break is the 2 px and 1 px sizes, by memory (about 100 bytes of the engine per image and per cell) and by time, not by a count limit.
- Falsifier: 2 px in IMG+BR draws in under 60 s.

Q3, run-length (one IMG of WIDTH=run * cell for a run of one colour).

- (a) a photo-like dithered picture: runs are short, about 1.2 cells on average, so the tags drop by only 15 to 25 percent, and the time by the same.
- (b) a flat pixel-art picture (the uttt board, flat 4-grey pixel art scaled to the grid): runs average 8 to 20 cells, so the tags drop by 85 to 95 percent and the time with them; a 4 px frame, impossible in the plain form, then draws.
- (c) the checkerboard: no run is longer than 1, no saving at all (0 percent), and the worst case is the plain case.

Q4, rewrite cost.

- A rewrite by document.open, write and close into a frame costs what the same page costs to load, plus about 1.5 s of fixed cost (the earlier 5 KB rewrite took 1.5 s on the wall).
  So a rewrite is within 20 percent of a load of the same HTML, and the plain 8 px picture (about 4 KB of IMG tags in 3,800 tags, 150 KB of HTML) is a rewrite of 12 s.
- Splitting the screen into tiles (frames) so only one tile is rewritten: a small update (one tile of 1/16 of the picture) costs about the fixed cost plus 1/16 of the picture's time, so a 76x50 picture updates a tile in about 2.5 s, the whole picture's 12 s not paid.
  Each extra frame costs about 0.3 s of load time; 16 tiles are the sensible limit.
  Falsifier: a tile rewrite costs the same as the whole picture (the engine relays out the whole frameset).
- A redraw is never faster than 1 per second on this guest: "a few redraws a second" is not reachable; a still picture is the use.

Q5, palette.

- 256 distinct GIFs are each fetched once and kept (the server's log shows 256 GETs for the first page and none for a second page); the first load of all 256 costs 256 requests of about 40 bytes each, about 15 to 40 s on the guest (the log's gap between the first and the last).
  Falsifier: a GIF fetched again for each IMG that names it (Netscape 2 has no cache of that kind), which the log would show as hundreds of GETs of one name.
- The guest's display depth decides the colours: if 256 colours (8 bit), the web-safe colours map exactly and the 40 greys are dithered or snapped; if 24 bit, all 256 are exact.
  The VM's QEMU is Cirrus VGA at the guest's chosen depth; I expect 16 or 24 bit, so exact colours (a 16 bit display shows the 40 greys within 8 levels).

Q6, other limits.

- Netscape 2 has a hard limit on the number of images in a page: the earlier probes put few images in a page, so nothing is known; I expect no hard limit below 10,000 (a 16 bit count would give 65,536), and broken-image icons only when the connection count runs out (it fetches each distinct GIF once, so the 256 are fine).
- Layout time grows faster than the tag count past about 20,000 tags (the line is re-measured on each addition), so time per tag is not constant: the 4 px picture costs more than 4 times the 8 px picture per tag.
- Memory: the 1 GB guest does not run out first; the limit is Netscape 2's own heap (16-bit-era allocator, a 32-bit build) at a few tens of MB.

### Clickable cells, expected before its runs

Alex's addition: wrap the cells in links, `<A HREF="javascript:..."><IMG ... BORDER=0></A>`, at three granularities on a flat pixel-art picture (a few coloured shapes): one link per cell, one per horizontal run, one per shape (every run of a shape shares one handler id).
The page is tools/gen_probe_ns2_fb.py's `lk_*` family; a click is made with the QEMU tablet, the handler reports by navigating a frame to /mark/<id>, and the status text is read from the screen.

- C1, a click on a cell calls the handler with the id of its link: right in all three granularities.
  The handler is `javascript:h(7)` in the page's own frame (the earlier probes called a function of the same document that way), reported in a second frame as /mark/c<id>.
  Falsifier: a click reaches the wrong id, or none (a link with a stretched image as its only content is not hit-tested on its pixels).
- C2, BORDER=0 removes the link border: yes (Netscape 2 draws a 2 pixel border round a linked image without it, and honours BORDER=0 on IMG since 1.1).
  Links keep the tiling gap-free: yes, a link adds no white space between tags; the picture is pixel-identical to the unlinked one.
  Falsifier: any magenta or link-coloured pixel inside the picture.
- C3, page size: a link adds about 30 bytes per tag (`<A HREF="javascript:h(123)">` and `</A>`) to the 40 of an IMG, so the page is 1.7 times the unlinked one per cell, and fewer tags (runs, shapes) mean a proportionally smaller page.
  Load time: Netscape 2 keeps every link as an anchor object in the page's tables, so I expect 1.3 to 2 times the unlinked picture's time for the same tag count; per cell at 8 px (2,880 links) about 15 s against 5.9 s.
- C4, onMouseOver on those links fires and sets the status text: yes (`onMouseOver="window.status='x'; return true"`); I expect the status bar to change within 1 s of the pointer arriving, and the cost not to depend on the granularity (one handler call).
  Falsifier: the status does not change (onMouseOver is in the binary's event list, but not on an anchor around an image).

### What the runs found

The runs are of 2026-11-21 in the XP VM, Netscape 2.02 maximised with no toolbar and no location bar: the view is 636 x 376 pixels (x 2 to 637, y 54 to 429 of the 640 x 480 screen), and a page's first pixel is at 12,69 (body margins of about 10 and 15).
Every picture is 576 x 320 pixels unless the line says otherwise, on a magenta page (a gap would show as magenta); a screendump is compared with the picture the page was made from at the centre of every cell (tools/fbcheck.py), so "exact" means 0 cells different.
Times are the Mac's wall seconds from the page's GET to the request of its last image, and the screen was still for 8 s after it.
The raw lines are in results/ns2fb_runs.txt, the screendumps in /Users/alex/VMs/ie6-xp/shots/ns2fb-*.png and ns2fb_*.png (ns2fb-best-photo-576x320.png is the best full picture, ns2fb-best-art-592x320.png the best flat one, ns2fb-final.png the empty desktop).

A bug of tools/vmserve.py came first, found by the first run (700 images took 160 s): Netscape 2 sends keep-alive but does not end an answer at its Content-Length, it waits for the connection to close, so under HTTP/1.1 every batch of its 4 connections sat out the server's 20 s timeout.
The server now answers `Connection: close` to a Mozilla/2 user agent.
The hangs of the first Q1 runs ("Waiting for reply" with a blank magenta view) were the same cause, and the "start hang" of the frames tier probably is too.
Earlier probes of this document that wrote many images or ran past 20 s may carry the 20 s steps.

Q1, correctness (an 8 x 8 checker of black and white 16 pixel cells, and one of 1 pixel cells):

| layout | result |
| --- | --- |
| rows of IMG ended by BR, no white space; also with ALIGN=TOP, ABSMIDDLE, ABSBOTTOM, BOTTOM, and in FONT SIZE=1 | exact 128 x 128, 0 magenta, every cell 16 x 16 |
| TABLE BORDER=0 CELLPADDING=0 CELLSPACING=0, one IMG per TD; also with ALIGN=TOP, ABSMIDDLE, ABSBOTTOM, and in FONT SIZE=1 | exact 128 x 128, 0 magenta |
| the same with 1 x 1 pixel cells (rows and table, default and ALIGN=TOP) | exact 8 x 8, a pixel each, 0 magenta |

No stray strip under a row, in any variant: a line that holds only images is exactly as tall as the tallest of them, a 1 pixel image included.
Neighbours touch with no white space between tags, and a 1x1 cell works.
The whole palette is drawn exactly: 256 of 256 cells have the colour of their GIF, so the guest's display is 24 bit (or 32), not 256 colours, and nothing dithers (shots/ns2fb_pal.png).

Q2, the scale.
HTML bytes are the file's size; row is IMG+BR, tab is the TABLE; a "-" is not run.

| cells | cell px | tags | row s | row bytes | tab s | tab bytes |
| --- | --- | --- | --- | --- | --- | --- |
| 36 x 20 | 16 | 720 | 4.1 | 25,425 | 3.5 | 32,059 |
| 72 x 40 | 8 | 2,880 | 5.9 | 95,364 | 9.3 | 121,538 |
| 144 x 80 | 4 | 11,520 | 11.3 | 380,684 | 45.4 | 484,818 |
| 288 x 160 | 2 | 46,080 | 31.6 | 1,521,564 | never drew (see below) | 1,937,138 |
| 576 x 320 | 1 | 184,320 | 141.4 | 6,084,284 | - | 7,744,818 |

(The dithered photo, 35 to 76 distinct palette colours; every one of these rows is exact: 0 cells different, 0 magenta.)
The largest grid that renders correctly is the whole 576 x 320 area at 1 pixel, 184,320 IMG tags and 6 MB of HTML, in rows: 141 s.
A row costs about 0.6 to 0.8 ms a tag after a fixed 3 s (the first requests of the palette), linear from 720 tags to 184,320.
A TABLE costs the same at 720 and 2,880 tags and grows faster than the tag count after that: 4 px takes 45 s for 11,520 cells, and at 2 px (46,080 cells) the page had not drawn after 10 minutes in two tries; Netscape 2 then shows the alert "A network error occurred while Netscape was receiving data (Connection reset by peer)" and a blank view (the cut comes at the same ten minutes with the server's timeout at 600 s and at 3600 s, and the server logs no error).
The 1 pixel TABLE was not tried.
Nothing crashed, no broken image appeared, and the memory of the 1 GB guest was never the limit.

How much fits in the window: a page wider than about 596 pixels (592 worked, 600 wrapped) or taller than about 330 (320 worked, 336 grew a scrollbar) shows a vertical scrollbar, which takes 16 pixels, and the rows then wrap and the picture is garbled (shots/ns2fb-wrap-too-wide-616.png).
So the biggest picture that is exact is 592 x 320 pixels, 79 percent of the view's area (62 percent of the 640 x 480 screen); the margins and the scrollbar allowance are Netscape 2's, and its body has no way to a zero margin.
592 x 320 of flat art at 1 pixel, as runs, took 10.2 s and was exact (ns2fb-best-art-592x320.png).

Q3, run lengths (one IMG of WIDTH = run * cell; in the TABLE each run is a TD with COLSPAN=run, and a last row of 1 pixel high cells fixes the column widths: without COLSPAN the cells of a ragged row take the widths of the cells above them and the picture is shifted, 2,222 of 2,704 cells wrong in the first try).

| picture, cell px | tags plain | tags runs | row s plain | row s runs | row bytes plain | row bytes runs | tab s runs |
| --- | --- | --- | --- | --- | --- | --- | --- |
| photo, 8 | 2,880 | 2,522 (-12 %) | 5.9 | 5.8 | 95,364 | 83,789 | 7.9 |
| photo, 4 | 11,520 | 10,085 (-12 %) | 11.3 | 11.0 | 380,684 | 333,593 | 38.5 |
| photo, 2 | 46,080 | 40,359 (-12 %) | 31.6 | 29.9 | 1,521,564 | 1,333,031 | - |
| photo, 1 | 184,320 | 161,826 (-12 %) | 141.4 | 150.8 | 6,084,284 | 5,342,053 | - |
| Flat art, 8 | 2,880 | 1,163 (-60 %) | 5.3 | 6.2 | 95,362 | 39,080 | 4.9 |
| Flat art, 4 | 11,520 | 3,392 (-71 %) | 8.7 | 11.9 | 380,682 | 113,187 | 8.2 |
| Flat art, 2 | 46,080 | 6,851 (-85 %) | 26.7 | 8.0 | 1,521,562 | 228,694 | 20.9 |
| Flat art, 1 | 184,320 | 9,837 (-95 %) | 153.8 | 8.1 | 6,084,282 | 329,617 | 36.2 |
| checkerboard, 8 / 4 | 2,880 / 11,520 | the same (0 %) | 1.9 / 6.5 | 1.7 / 5.4 | 95,364 / 380,684 | +8 bytes | - |

(The flat art is a 4-grey pixel-art night skyline, 640 pixels wide, given by FB_ART; its top 640 x 391 pixels are scaled with a box filter; the checkerboard has 2 colours, so its time is the cost of tags without a palette: 1.9 s at 8 px.)
Runs cut the tags of flat art by 60 to 95 percent and its HTML by 60 to 95 percent, and its time at 2 and 1 pixels by 70 and 95 percent (153.8 s to 8.1 s); at 8 and 4 pixels the page time is the palette's fetch and does not follow the tag count (art at 4 pixels took 8.7 s plain and 11.9 s in runs).
On the dithered photo runs save 12 percent of tags and nothing in time; on the checkerboard nothing.
The time is not a function of the tag count alone: the art at 1 pixel in runs has 9,837 tags and 8 s, 1,000 times the pixels of its tags.

Q4, rewriting (the picture's HTML held in the control frame as string literals of 20,000 bytes, written by `document.open`, `write`, `close` into another frame from a link, an absolute BASE first: without it the written images were never requested, so a picture written by the frames tier needs the page's URL as a BASE; a click is made with the tablet and the wall time is from the tap to the last change of the screen, 0.7 s of it the tap itself):

| picture | rewrite s | the same as a page load s |
| --- | --- | --- |
| photo 8 px, 2,880 tags, 95 KB | 5.5 (5.6 the first time, with the palette's requests) | 5.9 |
| photo 4 px, 11,520 tags, 380 KB | 11.6 | 11.3 |
| photo 8 px in 16 frames (4 x 4 tiles), all 16 rewritten | 5.5 | |
| photo 8 px in 16 tiles, one tile (1/16) rewritten | 1.3 (tile 0) and 2.0 (a middle tile) | |
| photo 4 px in 16 tiles, all 16 rewritten | 10.8 | |
| photo 4 px in 16 tiles, one tile rewritten | 1.6 (tile 0) and 3.0 (a middle tile) | |
| Flat art 4 px in runs (3,392 tags) | 16.0 | 11.9 |
| Flat art 2 px in runs (6,851 tags) | 22.5 to 23.9 | 8.0 |
| an empty page (the blank) over a picture | 0.7 to 1.0 | |

A rewrite costs what a load costs for the photo (the tags are what costs), and more than a load for flat art in runs (three times at 2 px).
A tile rewrite of 1/16 of a picture costs 1.3 to 3 s, against 5.5 and 11 s for the whole, so tiles make a small change fast and cost nothing to the whole (16 frames draw the same 5.5 s as one).
The frames have 6 pixel borders between them, so a tiled picture is not gap-free (ns2fb-tiles-4x4.png).

Q5, the palette: the 256 one-pixel GIFs (35 bytes each) are each requested once: pal.html (256 IMG, 256 files) 256 requests in 20.4 s, 80 ms a GIF, 4 connections at a time; pal4.html (the same 256 cells four times, 1,024 tags) 256 requests, none twice.
Netscape 2 does keep an image per URL for a page; a picture's GIF requests are as many as its colours (35 to 76 for the dithered photo), a few of them twice when two connections ask for the same new colour at once (up to 6 of 82).
So the palette costs about 80 ms a colour on the first page, and a small palette is the real saving: the checkerboard (2 colours) draws 2,880 tags in 1.9 s against 5.9 s for the photo.

Q6, other limits: there is no count of images or cells in Netscape 2 that this reached (184,320 images, 6 MB of HTML in one page, 76 distinct GIFs drawn at once); what limits a TABLE is the time, which grows past 10,000 cells, and what limits a picture's size is the view and its scrollbar.
Layout was never run out of memory; the guest stays quick to answer the clicks afterwards.

Clickable cells (a flat picture of five shapes at 8 px, 72 x 40 cells; the links in lk_*.html, the clicks and the pointer by the tablet):

| link per | links | page bytes | load s | clicks right |
| --- | --- | --- | --- | --- |
| nothing (the plain picture) | 0 | 121,429 | 2.1 | |
| cell | 2,880 | 215,359 | 4.4 | 4 of 4 (ids 73, 512, 1476, 1692) |
| horizontal run | 134 | 10,494 | 1.4 | 4 of 4 (ids 1, 12, 67, 76) |
| shape (each run of a shape names the shape's id, 5 ids) | 134 | 10,336 | 1.8 | 6 of 6 (ids 0 to 4, and two different runs of the disc both gave 2) |
| shape, with onMouseOver | 134 | 15,294 | 1.8 | status "id 2" shown 0.2 s after the pointer arrived (the poll's step), for 3 of 3 |

- C1: a click calls the handler with the id of its link, in all three; the handler's navigation reaches the server 0.9 s after the tap (1.4 s for the first cell link).
- C2: BORDER=0 removes the link border, and the linked pictures are pixel-identical to the unlinked one (0 pixels different in the view, all four pages).
- C3: a link adds about 33 bytes to the 40 of a cell, and 0.8 ms to its time (2,880 links 2.3 s); per run and per shape the picture is a tenth of the plain page, and loads in the same time as the plain one.
- C4: onMouseOver fires on a link round an image and sets the status text; it costs nothing visible.

### The predictions, marked

- Q1: a stray strip of 3 to 5 pixels under each row: wrong, there is none in any variant (rows or table, any ALIGN); neighbours touch: right; a 1x1 cell works: right, but the prediction that its row is a text line tall was wrong, it is 1 pixel; the table "needs the least care for columns": wrong, both are exact.
- Q2: row times 3, 10, 40 s then 3 min or a crash: wrong, 4.1, 5.9, 11.3, 31.6 s and 141 s at 1 pixel, with no crash (the 1 pixel picture of 184,320 tags is the whole area, drawn); table 2 to 3 times as long: right at 8 px (1.6), wrong at 4 px (4 times) and right that 2 px does not finish (more than 10 minutes, then a network alert, not an out of memory); HTML 40 bytes a cell: right (33 in the row, 42 in the table); the break by memory: wrong, by the table's time.
- Q3: (a) a 15 to 25 percent cut: the tags 12 percent, the time none; (b) 85 to 95 percent: right for the tags at 2 and 1 pixels (85 and 95 percent) and for their time (70 and 95 percent), wrong at 8 and 4 pixels (60 and 71 percent of the tags, no time); (c) 0 percent: right.
- Q4: a rewrite is within 20 percent of a load: right for the photo (5.5 against 5.9 s), wrong for flat art in runs (up to 3 times); the 8 px picture in 12 s: wrong, 5.5 s; a tile costs the fixed cost plus a 1/16: right (1.3 to 3 s); 0.3 s a frame: wrong, 16 frames cost nothing extra; "no redraw faster than 1 a second": right for pictures (5 s and more), a blank in 0.7 s and a tile in 1.3 s are the fast ones.
- Q5: each GIF once: right; 15 to 40 s for the first 256: right (20.4 s); a 24 bit display: right (all 256 exact).
- Q6: no hard image limit below 10,000: right (184,320 ran); layout time past 20,000 tags faster than the count: right for the TABLE, wrong for rows (linear); memory a few tens of MB: unmeasured.
- Clickable cells: C1 right; C2 right (0 pixels different); C3 about 30 bytes a link: right (33), 1.3 to 2 times the time: right per cell (2.1 times), and runs and shapes are smaller and no slower; C4 right (0.2 s, the poll's step).

### What this means

A picture of 1x1 GIFs is a real framebuffer in Netscape 2.02, at any resolution that fits the view: 592 x 320 pixels at 1 pixel per cell, exact, in 24 bit colour from 256 palette entries.
For a still picture use rows of IMG with BR (a table is slower and fails past about 10,000 cells), and runs of one colour wherever the picture is flat (95 percent fewer tags, 8 s for the whole area).
A dithered photo at 1 pixel is 184,320 tags and 141 s; at 4 pixels (144 x 80 cells) it is 11 s, at 8 pixels 6 s.
For something that updates, a whole-picture rewrite costs what a load costs (5.5 s at 72 x 40 cells, 11.6 s at 144 x 80), so a few redraws a second are out of reach; one redraw every 5 to 10 seconds is what a full small picture allows, and a change confined to a tile of a 4 x 4 split costs 1.3 to 3 s.
Cells that react to a click cost a link each: at 72 x 40 one link per run or per shape makes a clickable picture of about 10 KB that draws in 2 s, and a link per cell 215 KB and 4.4 s.

## Painting a modern page

Alex's question: can that framebuffer paint a modern website?
The page is designed as plain HTML and CSS in design/ns2paint/ (a design file, to be deleted once this is done): a gradient hero, rounded cards with soft shadows, a badge over a card's corner, an app window over the hero's bottom edge, anti-aliased sans-serif text at several sizes, pill buttons and a row of icons.
None of it is anything Netscape 2 can draw: tools/paintshots.sh renders each page in headless Chrome at its exact size (tools/paintshot.mjs, which also writes the box of every element with a data-link attribute), and tools/gen_ns2_paint.py turns each picture into rows of stretched 1x1 GIFs.
Three pages: still (592 x 320, one screen, the largest picture that needs no scrollbar), tall (the whole long page, 1,570 rows, at the width that stays exact beside the scrollbar) and details (592 x 320, where the links lead).
The generator:

- picks a palette for the picture, not the cube: the 64 most frequent colours of the three pages together exactly (paper, card white, ink, the tints), then a max-coverage cut of the other pixels to 256 in all; no dithering.
  A median cut over everything moved the paper to a mint tint and left errors of 78 levels on the chart's bars; max coverage alone did the paper too; anchors plus max coverage keep the flat colours exact and the worst pixel within 24 levels.
- writes one IMG per horizontal run, and one line of HEIGHT=k for k identical rows in a row (they are few here: the gradient makes most rows differ).
- splits the runs at the edges of each link box and puts the pieces inside a box in one `<A HREF="details.html" onMouseOver="s(2); return true">`, BORDER=0 on the IMGs; s(i) sets the status bar to the box's text.
- has three text treatments: aa (the text as Chrome draws it), snap3 (each text pixel snapped to background, half or ink) and bw (text with no anti-aliasing: each text pixel is its background or its ink).
  A text pixel is one that differs between the normal render and a render with every glyph and icon transparent (the class notext of paint.css), its ink the pixel of its 5 x 5 neighbourhood farthest from its own background.

What the generator gives, before any run (tags are IMG tags, links are A tags, bytes the page's size; the palette is per text treatment, shared by its three pages):

| variant | page | colours | tags | links | bytes |
| --- | --- | --- | --- | --- | --- |
| aa | still | 154 | 23,540 | 106 | 804,959 |
| aa | tall | 218 | 81,665 | 172 | 2,751,558 |
| aa | details | 125 | 19,709 | 63 | 669,205 |
| snap3 | still | 144 | 19,493 | 106 | 668,165 |
| snap3 | tall | 212 | 67,312 | 172 | 2,271,894 |
| bw | still | 138 | 17,484 | 106 | 600,163 |
| bw | tall | 208 | 58,933 | 172 | 1,991,445 |
| bw | details | 107 | 13,867 | 63 | 473,131 |
| no text at all (the text-free render) | still | 122 | 10,658 | | 369,162 |
| no text at all | tall | 181 | 29,703 | | 1,015,201 |
| aa on the fixed cube palette of the probes | still | 86 | 17,423 | | 600,209 |

So text is 55 percent of the still page's tags (12,882 of 23,540) and 64 percent of the tall page's; dropping its anti-aliasing (bw) cuts the still page's tags by 26 percent, snapping to 3 levels by 17 percent.
Merging identical rows saves almost nothing (23,563 tags unmerged against 23,540).
The cube palette has fewer tags (17,423) because it has fewer colours, but it shifts every tint and bands the gradient harder.

### What I expect, written before the first run

- P1, time.
  From the probes: a row costs about 0.7 ms a tag after a fixed 3 s, and a first palette about 80 ms a colour, the fetches overlapping the layout.
  So the tags decide: still aa 20 s (15 to 25), snap3 17 s, bw 15 s; tall aa 60 s (45 to 75), bw tall 45 s; details after a click 14 to 17 s.
  Falsifier: still aa under 12 s or over 30 s, or the tall page not drawn in 3 minutes.
- P2, what the text costs: its share of the time is its share of the tags less the fixed cost, so bw saves 20 to 25 percent of the still page's time (about 5 s) and snap3 about 13 percent.
  Falsifier: bw no faster than aa (the time is the palette's fetch, as for flat art at 8 and 4 pixels).
- P3, the scroll-safe width: a page grew a scrollbar past 592 to 600 pixels of picture and the scrollbar takes 16, so with the scrollbar there the widest exact picture is 580 (576 to 584); 576 stays exact, 584 and wider wrap.
  The tall page (576 wide) is exact at every scroll position: 0 pixels different at the top, the middle and the bottom.
  Falsifier: 576 wraps, or a scrolled view differs from the picture.
- P4, exactness: every page is pixel-exact against its quantised picture (0 pixels different in the view), links included (the probes found links round runs add no pixel).
- P5, the links: a tap on the pill opens details.html (its GET in the server's log within 1.5 s of the tap), and the details page is exact; onMouseOver puts the box's text in the status bar within 0.5 s of the pointer arriving.
  The details page shares the palette: Netscape 2 ignores the server's no-store for images it already holds, so fewer than 30 GIF requests for it after the still page.
  Falsifier: the click does nothing, the status shows the URL, or the details page asks for 100 or more GIFs again.

### What the runs found

The runs are of 2026-11-21 in the XP VM (raw lines in results/ns2paint_runs.txt, shots in /Users/alex/VMs/ie6-xp/shots/ns2paint-*.png).
The work was stopped before the click, hover and video steps: Alex chose the live renderer over precomputed pictures ("we have the <img> tag for that"), so those are listed below as not measured.

The scroll-safe width (w<width>.html: bars of 8 pixels, 800 rows tall, so the page has a vertical scrollbar): 576 to 598 are exact (0 pixels different), 600 to 612 wrap their last bar.
So the widest exact picture is 598 pixels, with the scrollbar or without it: Netscape 2 lays a page out as if the scrollbar were always there, and 600 wraps on a short page too (the probe's "600 wrapped").
The tall page was rendered again at 598 (its feature cards are calc(50% - 26px) wide) and is exact at every scroll position.

A bug found by the first loads: PIL's quantize(palette=) does not map to the nearest colour exactly, and it drew the paper 3 levels darker than the page's BGCOLOR, a visible edge round the picture.
tools/gen_ns2_paint.py now maps each colour to its nearest palette entry itself; the pictures are more faithful and have more runs (the still page 23,303 tags before, 28,012 after).
The numbers after that fix:

| variant | page | colours | tags | links | bytes |
| --- | --- | --- | --- | --- | --- |
| aa | still | 172 | 28,012 | 106 | 954,106 |
| aa | tall (598 x 1,570) | 233 | 103,187 | 172 | 3,463,958 |
| aa | details | 141 | 25,159 | 63 | 849,861 |
| snap3 | still | 162 | 23,176 | 106 | 790,751 |
| snap3 | tall | 227 | 86,381 | 172 | 2,903,117 |
| bw | still | 157 | 20,587 | 106 | 703,451 |
| bw | tall | 224 | 76,862 | 172 | 2,585,430 |
| bw | details | 130 | 18,719 | 63 | 633,673 |
| no text at all | still / tall / details | 142 / 198 / 117 | 15,723 / 48,549 / 10,093 | | |
| aa, fixed cube palette | still | 83 | 17,652 | | 607,682 |

Text is 44 percent of the still page's tags and 53 percent of the tall page's; bw cuts the still page's tags by 27 percent, snap3 by 17 percent.

Load times, the Mac's wall seconds from the page's GET to its end mark (every load drew the whole page):

| variant | page | tags | quiet Mac, s | with another job on the Mac, s |
| --- | --- | --- | --- | --- |
| aa | still | 28,012 | 12.0, 12.0, 15.5 | 41.5, 45.3, 73.4 |
| snap3 | still | 23,176 | 10.4, 11.1, 11.3 | 62.1 |
| bw | still | 20,587 | 9.7, 14.7 | 26.2, 37.1 |
| aa | details | 25,159 | 9.9, 10.5 | 56.3 |
| snap3 | details | 21,022 | 9.4 | 21.0, 66.9 |
| bw | details | 18,719 | | 31.6, 49.3, 54.6 |
| aa | tall | 103,187 | | 144.1, 217.6 |
| bw | tall | 76,862 | | 240.8 |
| before the mapping fix: aa, snap3, bw still | | 23,303 / 19,884 / 16,703 | 10.3, 9.3, 8.5 | |

"Another job" is the Game Boy work's SameBoy and make, which ran beside some of these, and an unexplained slow phase after 16:25 with no such process, in which the guest's QEMU sat at about 18 percent of a core: the guest was waiting, not computing, so the slow loads are stalls (the server's log shows no 20 s gap, the requests just come slowly), not layout.
The quiet numbers are the engine's cost: about 0.35 to 0.45 ms a tag, half the probes' 0.7.

Exactness: every load of the still and details pages in all three variants was pixel-exact (0 different, 178,826 pixels compared each, XP's activation balloon at the bottom right masked), and the tall page in aa (twice) and bw was exact at scroll 0, 345, 690, 1035 and 1242 (the bottom).
The still and details pages carry 106 and 63 links round runs, so links add no visible pixel.

### The predictions, marked

- P1, time: wrong, faster for one screen and slower for the long page.
  The still page in aa took 12.0 s against 20 predicted (and 10.3 s for the 23,303-tag version the prediction was made for); details 9.4 to 10.5 s against 14 to 17.
  The tall page took 144 to 241 s against 60 (45 to 75): wrong, but none of those runs was on a quiet Mac, so the long page's own cost is not measured.
- P2, the text: right for snap3 (13 percent faster, 10.4 against 12.0 s), a little under for bw (19 percent, 9.7 against 12.0, predicted 20 to 25); with two or three runs each and a spread of 3 s, these are rough.
- P3, the scroll-safe width 580: wrong, it is 598, because the scrollbar takes no width from the layout; "the tall page is exact at every scroll position": right.
- P4, exactness: right, 0 pixels different on every page and every scroll position.
- P5, the links: "pixel-identical": right; the click to details.html, the onMouseOver status text and the GIF requests of the second page: not measured (tools/vmpaint.py links was written and not run).

### What is left

Not done, by the change of plan: tools/vmpaint.py links (hover status, a tap on the pill, the details page after the click, Back home), the video, the bw tall page on a quiet Mac, and the cause of the slow phases (the guest idle while requests come slowly).
What the live renderer can reuse: the width 598 for any page, scrolling or not; the exact nearest-colour mapping (never PIL's quantize(palette=)); a palette that keeps the most frequent colours exact; text without anti-aliasing as the cheapest remedy for runs; tools/paintcheck.py (with the balloon mask) and tools/vmpaint.py as the oracle and driver.
