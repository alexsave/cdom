# The JavaScript 1 transform: the app's wasm2js output for the frames tier

The frames tier (docs/frames-design.md) serves Netscape 2 to 4 and IE 3 and 4, and loads the app's IE 9 build, which none of them can parse.
docs/descent.md measured what each of those engines lacks.
Alex asked for the choice between a WebAssembly interpreter written in JavaScript 1.0 and a transform of the wasm2js output to be made by measurement.
This document is the transform's side (branch js1-transform); the interpreter is another job's (branch js1-interp).
It was written before the code, with the predictions in section 2; the results were added after measuring, and the predictions were not edited.

## 1. What it is

A build step, `node tools/legacy/build.mjs app-js1 <in.wasm> <out.wasm.js1.js>`, next to app and app-ie9.
It takes the same wasm2js output and the same memory pass (tools/legacy/memory.mjs) as app-ie9, on a JavaScript 1.0 copy of words.js's memory (the same layout: one 32-bit word a key), and rewrites the result down to JavaScript 1.0.
The frames tier's boot loads it (url + ".js1.js") instead of url + ".ie9.js".

Two targets:

- A: JavaScript 1.1 as tools/legacy/js11.mjs enforces it (Netscape 3 and 4, IE 4).
- B: JavaScript 1.0 as docs/descent.md measured it in Netscape 2.02 (tools/legacy/js10.mjs, the gate written from that list).

## 2. Predictions

Written before any measuring, each with what would refute it.
Timings are headless Chrome on a Linux x86-64 VM, under LEGACY=frames (the transformed file) and LEGACY=frames with the IE 9 build in its place, and indicative only: the repo manager measures the real browsers.

- T1, size: the transformed tests/frames.c (frames.wasm.js1.js), minified, is between 0.8 and 1.3 times the size of frames.wasm.ie9.js, and gzipped (gzip -9) between 0.9 and 1.3 times.
  Refuted outside either range.
- T2, CPU loop: tests/js1cpu.c's loop of 100,000 iterations of integer operations (multiply, shift, xor, a load and a store) takes between 0.5 and 1.5 times as long in the transformed file as in the IE 9 build (the median of 7 runs each).
  Refuted outside that range.
- T3, calls: 1,000 runs of a 20-deep C recursion take between 0.5 and 1.5 times as long in the transformed file as in the IE 9 build.
  Refuted outside that range.
- T4, boot and click: tests/frames.c's boot (navigation to the first draw) and one click on its button (to the redraw) each take between 0.67 and 1.5 times as long with the transformed file as with the IE 9 build.
  Refuted outside that range.
- T5, call depth: C calls stay JavaScript calls, one for one.
  tests/frames.c, driven through all of tools/frames.mjs, reaches at most 15 JavaScript frames from a browser entry, shim and C together, and js1cpu's 20-deep recursion reaches 20 C frames plus at most 8 others.
  Refuted if either is deeper.
  With Netscape 2 and 3 dying between depth 100 and 300 (docs/descent.md), that means a C program recursing up to about 90 deep runs there, and one recursing past about 290 does not.
- T6, target B: the transformed files of tests/frames.c and tests/js1cpu.c pass the JavaScript 1.0 gate (tools/legacy/js10.mjs), not only the 1.1 one.
  Refuted if any construct the transform leaves cannot be lowered below 1.1.
- T7, correctness: LEGACY=frames tools/frames.mjs passes on the transformed file, and tests/js1cpu.c's exports and tests/memforms.c's give the same answers in the transformed file, in a context with no Math.imul, clz32, trunc or fround and no charCodeAt, push or typed arrays, as in WebAssembly.
  Refuted by any failing check or differing answer.

## 3. The pipeline

`build.mjs app-js1` runs, in order:

1. wasm2js, as for the other tiers (the same binaryen 130, the same float to integer rewrite).
2. The memory pass (tools/legacy/memory.mjs), unchanged, on the names tools/legacy/words1.js declares: every typed-array access becomes one of words.js's accessors or `W[i]`.
3. The transform (tools/legacy/js1.mjs), on words1.js followed by that output, from the AST.
4. The 1.1 gate (tools/legacy/js11.mjs, nothing allowed) and the 1.0 gate (tools/legacy/js10.mjs), which refuse the build.
5. terser with compress off and only the locals mangled, to names of letters, digits and _ (no $), then both gates again, and the typed-array scan.

terser's compressor is off because it writes what the gates refuse (`void 0`, `.call` from `["call"]`) and saves about 10%.

## 4. The lowering

- Names.
  Every top-level binding and every binding of asmFunc is renamed to `c_` and its name, with `$` spelled `S`, then, once the file is done, to `c_` and a base-36 number, the most used first (`c_0` is the memory `W`).
  The frames shim's names are `cf_` and the page's `cdom...`, so neither can collide; the exports keep their names, which are keys of the exports object.
- The closure.
  wasm2js writes one closure, asmFunc, whose locals the whole module shares.
  Its nested functions go to the top level, and its variables become globals that asmFunc (the factory) assigns where they were declared.
  That is one instance a page, which is what the frames tier has.
- Labels, labeled blocks, switch and do-while (`lowerFunction`).
  Every target a jump names becomes a loop of the output: a loop stays one, a switch becomes `while (1)` around an if chain, a labeled block that a break leaves becomes `while (1) { ...; break; }`, and a do-while becomes `t_f = 1; while (t_f || test) { t_f = 0; ... }`, so that its continue still goes to the test.
  A jump to the innermost output loop is a plain break or continue.
  A jump further out sets `t_J` to the target's code and breaks; each loop it leaves is followed by `if (t_J) ...`, which breaks again, or, after the loop inside the target, clears `t_J` and breaks or continues.
  A switch's discriminant goes into `t_s`, the index of the case taken into `t_c` (the default's index when none matches), and each case body runs under `if (t_c <= k)`, which is fallthrough.
- Literals.
  `x = [..]`, `x = {..}` and `return {..}` become `new Array()` or `new Object()` and one assignment a key, with a temporary for a literal inside one; wasm2js writes exactly two (the function table and the exports), both in the factory.
- Expressions.
  `===` and `!==` become `==` and `!=` (every operand is a number); `Infinity` and `NaN` become words1.js's `INF` and `NAN`, made by arithmetic; unary plus becomes `x - 0` (not measured, done as a precaution: the same value for every number, -0 included); `Math.imul(a, c)` with a constant c of at most 2^20 becomes `(a * c | 0)`, exact since an i32 or u32 operand is under 2^32, and any other imul, clz32, trunc and fround words1.js's; `Math_abs` and the other aliases become `Math.abs` itself; a property named as a method that 1.0 lacks (the import `cdom.call`) is written `o["call"]`.
- What nothing calls (words1.js's floats in an app without floats, wasm2js's helpers it did not need) is dropped: 15 functions and variables in tests/frames.c's file.

## 5. Traps

JavaScript 1.1 has no throw, and docs/frames-design.md, section 4, says a trap ends the call and the next entry recovers.
The transform makes that real with a flag, `T` in words1.js:

- A `throw` (wasm2js's `wasm2js_trap()` and memory.fill's bounds check) becomes `T = 1; return 0;`.
- The functions that can trap are found by a fixpoint over the call graph: one with a throw, one that calls one, and every one that calls through the function table when any function in the table can.
  Imports are the shim's, which never calls back into C from one (only an entry from the browser does), so a call of one cannot trap.
- Every statement that calls a function that can trap is followed by `if (T) return 0;`; an if's or a while's test that does is evaluated into `t_q` first.
  A return passes the trap up as it is, since its caller checks, and so the check is not written where only the function's end or a plain return would follow it.
- The exports gain `cdom_trapped`, which answers the flag and clears it.
  The shim (cf_cRun) calls it after every call into C: on a trap it notes "cdom: C trapped in <export>", answers 0, and ends the call as any other, so what C did before the trap is drawn, as the first tier's page shows what C did before its exception.
  The shim still recovers a call that never came back (an app file that throws), as before.

What is not exact: a statement with two calls that can trap, where the first traps, still runs the second, since the check is after the statement.
That is 12 statements in tests/frames.c's file and 6 in js1cpu's (`stats.multi`); the fix, if one is wanted, is to give each such call a statement of its own (the call into a temporary, then the check).
In tests/frames.c's file 37 of 80 functions can trap (the allocator's failure path reaches most of cdom), and 144 checks are written.

## 6. Memory

tools/legacy/words1.js is words.js in JavaScript 1.0: the same layout (`W[i]` the bytes 4i to 4i + 3, little-endian, as a signed 32-bit integer), the same names and units, so the memory pass and the frames shim (which reads `memory.words`) need no change.
W is a `new Array()` filled by index: on Netscape 2 that is an object with number keys, and docs/descent.md measured 100,000 stores there at 1,531 ms into a `new Array(1024)` against 3,453 ms into a `new Object()`; on Netscape 3 it is 5,094 against 4,672, and on IE 4 172 against 453.
What differs from words.js: no push (stores by index), no array literal or function expression (the tables are built by top-level code), no `===`, no `NaN` or `Infinity` (made by arithmetic: 2^1023 doubled, and that minus itself), no charCodeAt (base64 by a table keyed by the character), no Math.log (the exponent by a binary search of the powers of two, which are built from 1 by doubling and halving, not parsed from a subnormal literal).
`WORDS=words1 node tools/words.mjs` runs words.mjs's checks on it, in a context without Math.imul, clz32, trunc, fround, charCodeAt, fromCharCode, push, pop, slice, splice, concat, call and apply: all of them pass, and tests/memforms.c's every load and store form, through the whole transform, agrees with wasm2js output on its 60,005 checks.

## 7. Target B: the JavaScript 1.0 gate

tools/legacy/js10.mjs is written from docs/descent.md's Netscape 2.02 column: on top of the 1.1 gate it refuses typeof and void, new Function, the arguments object, prototype and constructor, delete, hasOwnProperty, every Array method, charCodeAt, fromCharCode, split, slice, substr, replace, toString with a radix, setInterval, Image, document.images, javaEnabled, plugins, screen and onerror, and, not measured but taken as a precaution, a $ in an identifier.
Two rules measured in Netscape 3 went into the 1.1 gate itself, js11.mjs: a member of a new expression (`new Date().getTime()`, a syntax error there), which found three in tools/legacy/frames.js, now fixed, and isNaN.
Every app file the build writes passes both gates, so target B is reached for the app file.
The frames shim itself is JavaScript 1.1 (typeof, new Function, split, join), so Netscape 2 still stops at cdom.frames.js; making the shim 1.0 is the next step for Netscape 2, and is not this branch's.

## 8. Call depth

C calls stay JavaScript calls, one for one: the transform adds no frame, and clang's -O2 keeps a recursion that stores after its call a recursion.
tools/js1.mjs counts it: the export rec(20) is 22 JavaScript frames, 21 of rec under the export.
tools/frames.mjs (FRAMES_MEASURE=1) counts every function of the shim and of the app file, the handlers, the exports and the imports: tests/frames.c's deepest point is 11 frames from a browser entry, shim and C together.
docs/descent.md measured Netscape 2 and 3 running a recursion 100 deep and dying at 300 (between, unmeasured), Netscape 4 running 500 and dying at 1,000, IE 4 dying at or below 500.
So, honestly: an app's deepest JavaScript stack, its C calls and the shim's together (11 for tests/frames.c), must stay under 100 on Netscape 2 and 3, about 90 C calls deep, and under 500 on Netscape 4 and IE 4; between 100 and 300 on Netscape 2 and 3 is unmeasured, and js1cpu-ns.html's "recurse" finds the edge.
cdom's own C (the tree building, the diff) recurses as deep as the app's tree; an app whose tree is deeper than about 90 does not run on Netscape 2 and 3 with this transform.
An interpreter keeps its own stack and has no such limit: that is the one thing here the transform cannot match.

## 9. Results

Measured after the predictions were committed (b0f8bdd), in headless Chrome 153 on a Linux x86-64 VM under LEGACY=frames, two runs each of `FRAMES_MEASURE=1 LEGACY=frames node tools/frames.mjs` and the same with `FRAMES_APP=ie9`, so indicative only.

| | Predicted | Measured | Verdict |
|---|---|---|---|
| T1, size | 0.8 to 1.3 times frames.wasm.ie9.js minified, 0.9 to 1.3 gzipped | 22,857 against 15,715 bytes minified (1.45), 5,923 against 5,659 gzipped (1.05); js1cpu 1.37 and 1.02; memforms 1.37 and 1.13 | refuted (minified) |
| T2, CPU loop | 0.5 to 1.5 times the IE 9 build | loop(100000) 0.60 and 0.62 ms against 0.70 and 0.62 (0.86 to 1.0) | confirmed |
| T3, calls | 0.5 to 1.5 times | recs(1000, 20) 0.66 and 0.62 ms against 0.64 and 0.64 (0.97 to 1.03) | confirmed |
| T4, boot and click | 0.67 to 1.5 times | boot 38 and 36 ms against 35 and 39 (0.92 to 1.09); click 0.9 and 1.25 ms against 1.2 and 1.25 (0.75 to 1.0) | confirmed |
| T5, call depth | frames.c at most 15 frames; rec(20) 20 plus at most 8 | 11; 21 calls of rec and the export, 0 others | confirmed |
| T6, target B | frames.c's and js1cpu's files pass the 1.0 gate | both, and memforms's, before and after minifying | confirmed |
| T7, correctness | tools/frames.mjs passes; js1cpu and memforms agree with WebAssembly in a context without the 1.2 builtins | all pass | confirmed |

What the miss says:

- T1: gzip takes back nearly all of it (1.02 to 1.13), so the bytes are repetition: every global is `c_` and a number (3 to 4 characters where the IE 9 build's closure locals are 1 or 2: 4,058 bytes of names in tests/frames.c's file, W alone 485 times), 144 trap checks of 16 bytes, and no terser compressor (it writes `void 0` and `.call`, which the gates refuse; with it the file is 21,542 bytes).
  Over the wire, gzipped, the transformed file is about 5% larger.
- Node is no measure here: in a vm context the transformed file ran 40 to 96 times slower than the IE 9 build, because every one of its names is a global of the context; in node's own context it is 1.68 and 1.23 times; in Chrome's page, as above, 0.86 to 1.03.
  On the frames tier's engines every name is looked up by name whether global or local (docs/descent.md has no closure-local fast path to lose), but that is the XP VM's to measure.

## 10. Verifying in the real browsers

Alex's XP VM serves pages from the host at http://10.0.2.2:<port>/ (docs/frames-design.md, section 12).

1. Build: `tests/build.sh` writes, into tests/out: frames.html, frames-body.html, frames-ns.html, js1cpu.html, js1cpu-ns.html, cdom.frames.js, frames.wasm.js1.js and js1cpu.wasm.js1.js.
2. Serve tests/out: `python3 -m http.server 8000 --directory tests/out`.
3. Netscape 3.04: http://10.0.2.2:8000/frames-ns.html and http://10.0.2.2:8000/js1cpu-ns.html.
   The -ns pages are the same pages with the string table written as `new Array()` and assignments (tools/js1table.mjs): tools/cdlits writes it as an array literal, which Netscape 3 cannot parse.
4. Netscape 4.08 and IE 4.01: http://10.0.2.2:8000/frames.html and http://10.0.2.2:8000/js1cpu.html (and the -ns pages, which should behave the same).
5. On frames: docs/frames-design.md, section 12, step 4.
   On js1cpu: "loop 100000" shows its answer (1547173995 the first time) and its milliseconds; "1000 recursions 20 deep" its answer and milliseconds; "recurse" runs a recursion as deep as the field says, and shows the deepest C depth reached: try 50, 90, 100, 200, 290 and 490, and note the first that fails.
6. Netscape 2.02 stops at cdom.frames.js (JavaScript 1.1, section 7): what it can check is that frames.wasm.js1.js parses, by loading it alone (a page with `<script src="frames.wasm.js1.js"></script>` and a `document.write(window.cdomFactory ? "parsed" : "not parsed")` after it).

What the VM should look for, unmeasured by docs/descent.md: string escapes `\x00` in the string table and `\n` and the like in the app file's strings (Netscape 3), `1 / x` where x is 0 (-0 detection and INF are by arithmetic, but f32 and f64 sign checks divide by zero), and stores into a `new Array()` past index 1,023 on Netscape 2.

## 11. Tests

- tools/js1.mjs: the lowering on planted code (labeled blocks left from two loops in, a switch with fallthrough and a default in the middle, a continue out of a switch, do-while with a continue, jumps three loops out by break and by continue, Math.imul by small and large constants and by variables at the int32 edges, clz32, trunc, fround, `===`, Infinity, and traps two calls in and in an if's test), each against the original JavaScript, in a context without the 1.2 builtins; the planted output passes both gates and keeps no switch, do, throw, try, label, `===` or Math.imul; js1cpu's exports against WebAssembly; the depth of rec(20).
- tools/js10.mjs: each rule of the 1.0 gate on planted code, and what Netscape 2.02 ran passes.
- tools/js11.mjs: the member of a new expression and isNaN, planted.
- `WORDS=words1 node tools/words.mjs`: words1.js against typed arrays, and memforms through the transform.
- `LEGACY=frames node tools/frames.mjs`: the frames tier on the transformed file, with the trap section rewritten for the flag (a trap ends the call without a throw, what C did before it is drawn, it is noted once, a trap five calls deep stops every caller, the next click works), and frames-ns.html; `FRAMES_APP=ie9` runs the same on the IE 9 build, with the old trap section.
- tools/mutate.py: the mutants named `js1-`, `js10-`, `words1-`, `js11-new-member-allowed` and `js11-isnan-allowed`.
