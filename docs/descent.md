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
