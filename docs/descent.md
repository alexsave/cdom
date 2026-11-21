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
| speedConcat1000 | 469ms len=1000 | 125ms len=1000 | 110ms len=1000 | 16ms len=1000 | 63ms len=1000 | 63ms len=1000 |
| speedLoop | 2672ms x=11952 | 3484ms x=11952 | 547ms x=11952 | 203ms x=11952 | 359ms x=11952 | 203ms x=11952 |
| speedCharCodeAt | absent | absent | 344ms x=970000 len=10240 | 157ms x=970000 len=10240 | 234ms x=970000 len=10240 | 187ms x=970000 len=10240 |
| done | end | end | end | end | end | end |
| speedConcat2000 | 1812ms len=2000 | 312ms len=2000 | 188ms len=2000 | 32ms len=2000 | 188ms len=2000 | 109ms len=2000 |
| speedArrKeys | 1531ms m5=99333 | 5094ms m5=99333 | 469ms m5=99333 | 172ms m5=99333 | 312ms m5=99333 | 438ms m5=99333 |
| speedCall | 3140ms x=100000 | 4407ms x=100000 | 1859ms x=100000 | 1047ms x=100000 | 1422ms x=100000 | 3438ms x=100000 |
| speedObjKeys | 3453ms m5=99333 | 4672ms m5=99333 | 500ms m5=99333 | 453ms m5=99333 | 312ms m5=99333 | 375ms m5=99333 |
| tstr | n/c | fired | fired | n/c | fired | fired |
| speedConcat | n/c | n/c | 1547ms len=10000 | 656ms len=10000 | 1266ms len=10000 | 797ms len=10000 |
| interval | n/c | no run | 3 | n/c | 3 | 3 |
| tfn | n/c | no run | fired | n/c | fired | fired |

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

