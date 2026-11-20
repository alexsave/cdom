# js1-interp: a WebAssembly interpreter in JavaScript 1.0 for the frames tier

Alex asked whether Netscape 2's code path is better made as a WebAssembly interpreter written in JavaScript 1.0, or as a transform of the wasm2js output (docs/descent.md, "What this means for the interpreter against transform decision").
A separate job builds the transform (branch js1-transform); this branch builds the interpreter, so the two can be measured against each other.
This document was written before the code; section 9 holds the predictions, committed before any measuring and not edited after; section 10 holds the results.

## 1. What the interpreter must live with

From docs/descent.md's measured table, Netscape 2.02 (JavaScript 1.0) has no `typeof`, no literals but strings, numbers and booleans, no function expressions, no nested functions, no `switch`, no `===`, no `try`, no `throw`, no `with`, no working `Array`, no `delete`, no `charCodeAt`, no `fromCharCode`, no `split`, no `new Function`, and no `isNaN`, `NaN`, `Infinity` or `undefined` globals.
It has the bitwise operators (all of them, `>>>` included), doubles, number keys on a plain object (`o[1]`), `Math.floor`, `abs`, `sqrt`, `pow`, `round` and `max`, `parseInt`, `parseFloat`, `charAt`, `indexOf`, `substring`, `escape`, `unescape`, `eval` and `setTimeout` with a string.
Calls nest to a depth between 100 and 300 on Netscape 2 and 3, and die past it.
A three-operator statement in a loop costs about 27 microseconds there on the emulated Pentium, a store into an object by number about 35, a call of a one-line function about 31.

So the interpreter is:

- every function declared at the top level, all state in globals (prefix `wi_`), every table a `new Object()` with number keys;
- one loop that never calls itself per wasm call: C's call stack is the interpreter's, in plain objects, bounded by a limit of its own and not by the engine's 100 to 300;
- the hot path free of function calls (a call costs as much as a statement), so i32 multiply, loads and stores are written out where they are used.

tools/js10.mjs is the language gate, from the measured list (section 7), on the interpreter and on every module file the build writes.

## 2. The build: a lowering, not a transform

The work that needs no browser is done at build time, in node (tools/legacy/js1lower.mjs), and the browser gets data, not code.

1. Parse the app's .wasm (the same module the first tier runs, after the same nontrapping float-to-int rewrite the wasm2js build does).
2. Lower each function from wasm's stack machine to a register code: every local and every slot of the value stack (whose height is static in wasm) is a register of the function's frame.
   `local.get` emits nothing (the consumer reads the local's register), a constant is an immediate where the op has an immediate form, a result is written straight to the local a `local.set` after it names, and a compare before a `br_if` or an `if` becomes one compare-and-jump.
   Blocks become jumps to absolute positions in one code stream; a branch that carries a value moves it to the label's slot first.
3. Encode the code stream, the data segments, the function table and the module's shape as the module file, `app.wasm.js1.js`, which sets `window.cdomFactory` the way the IE 9 build does.

The interpreter itself, tools/legacy/interp.js, is the same for every app: tools/legacy/build.mjs writes it as `cdom.js1.js`, next to cdom.frames.js.
Its handlers are written by hand, one per op, and the build arranges them into the dispatch (section 4), with each op's number taken from the frequency order the build keeps in tools/legacy/js1ops.txt.

## 3. The module format

Netscape 2 has no `charCodeAt`, so a file cannot be read as bytes cheaply.
Two forms are built and measured (section 9, P2):

- statements: `wm_c(12,3,4,5);` lines, one per instruction, the numbers parsed by the engine itself;
- a digit string: the numbers as a variable-length code over 64 printable characters (5 bits a character and a continuation bit, signed numbers zigzagged), read with `indexOf` on the alphabet, one character at a time.

The one picked ships; the other stays a build option for the real-browser page (section 8).
Data segments are 32-bit words in the same code, with runs of zero words skipped by a count.

## 4. Dispatch

There is no `switch`.
The ops are numbered in the order of their dynamic frequency (measured on tests/frames.c and tests/js1cpu.c, frozen in tools/legacy/js1ops.txt), and the build writes the dispatch as an if-chain over the hottest ops followed by a binary if-tree over the rest.
Two other shapes are built for the measurement: the chain alone, and the tree alone.
The number that carries to the old engines is the mean count of comparisons an instruction pays, which the profiling build counts and which does not depend on the browser.
After P7 refuted that shape (section 10), a fourth was added and ships: the search tree of least expected comparisons for the weights in js1ops.txt, by Knuth's dynamic program (js1lower.mjs, dispatchTree), with no test at all for the last op of a range.

## 5. Values, i64 and memory

- An i32 is a JavaScript number kept as a signed 32-bit integer (`|0` after every op that could leave the range); unsigned compares and divisions go through `>>>0`.
- i32 multiply is in software, in 16-bit halves, written out in the handler.
- An i64 is a pair: its low word in the register file `wi_R` and its high word in a second file `wi_H` at the same index, so a slot, a local, an argument and a result are one index whatever their type, and the i32 ops never touch `wi_H`.
  The i64 ops (add and subtract with the carry, multiply in 16-bit pieces, shifts and rotates across the pair, division by shift and subtract, the compares, the conversions) are handlers on those pairs.
  binaryen's own i64 lowering (`--flatten --i64-to-i32-lowering`, what wasm2js runs) was the alternative; P8 measures what it costs in instructions, and the interpreter does not use it.
- f32 and f64 are JavaScript numbers; f32 results go through a software fround (tools/legacy/words.js's, written again in JavaScript 1.0: no `Math.log`, which docs/descent.md did not measure, but a binary search over a table of powers of two).
- Linear memory is one plain object of 32-bit words, `wi_M[a >> 2]`, signed, little-endian within a word, the layout of tools/legacy/words.js, so the frames shim reads it as it reads the IE 9 build's `memory.words`.
  Every word of the memory is written at instantiation and at each `memory.grow` (no read of a missing key, whose value is not measured on Netscape 2).
  An aligned load is one lookup; an unaligned one is put together from bytes.
  There are no bounds checks: an address past the end reads 0 and a store there is kept but never seen, as the IE 9 build does.

## 6. Calls, traps and imports

- A frame is a window into the register files at `fp`.
  A call's arguments are already in the caller's top slots, so the callee's `fp` is the caller's `fp` plus the first argument's slot, and its parameters are its registers 0 to n-1 with no copy; the other locals are zeroed on entry.
  The return address and the caller's `fp` go on `wi_S`, a plain object used as a stack.
  The depth is limited by `wi_LIMIT` registers (one million by default), past which the call traps.
- A trap (unreachable, an integer division by 0 or its overflow, a bad `call_indirect`, a float to int conversion out of range, the stack limit) ends the run: the loop returns, `wi_trap` holds its name, and the export returns 0.
  JavaScript 1.0 has no throw, so the frames shim (tools/legacy/frames.js, `cf_cRun`) checks `wi_trap` after each call into C and leaves its counts up, as a thrown trap did on the IE 9 build: nothing is drawn, and the next entry from the browser recovers (docs/frames-design.md, section 4).
- Imports are resolved at instantiation to the functions of the imports object the shim passes (`cf_imports.cdom`, by name), kept in a number-keyed object, and called with their arguments out of the caller's slots.
  An import may call back into C: the run is re-entered with its frames above the caller's.
- Exports are top-level functions the module file declares, one per export (`function wm_e3(a, b, c, d) { return wi_call(3, a, b, c, d); }`), since JavaScript 1.0 has no function expressions.

## 7. The js10 gate

tools/legacy/js10.mjs refuses, by construct and with its line, what docs/descent.md measured missing on Netscape 2.02: the syntax that did not parse (`typeof`, `void`, the literals, function expressions, nested functions, `switch`, `do`-`while`, labels, `===`, `!==`, `in`, `instanceof`, `try`, `throw`, `with`, `new X().y`), `delete`, and the names that died at run time (`arguments`, `prototype`, `constructor`, `Array`, `Function`, `isNaN`, `NaN`, `Infinity`, `undefined`, `charCodeAt`, `fromCharCode`, `split`, `slice`, `substr`, `replace`, `toString` with a radix, `setInterval`, `Image`, `screen`, `onerror` and the Array methods).
It also refuses what was not measured either way: a global that is neither declared in the file nor on the measured-present list, and a `Math` member other than the six measured.
tools/js10.mjs checks each rule on planted code, and gates the files it is given; tests/build.sh and CI run it on cdom.js1.js and the module files, and tools/legacy/build.mjs refuses to write a file that fails it.

## 8. The real-browser page

tests/out/js1.html runs tests/js1cpu.c on the interpreter with no DOM at all (a JavaScript 1.0 page that writes its results with `document.write`), and tests/out/frames.html runs tests/frames.c on the frames tier with the interpreter.
The PR lists the files to serve and the URLs.

## 9. Predictions

Written before any measuring; each has what would refute it.
Timings are headless Chrome on a Linux x86-64 VM, indicative only; the repo manager measures the real browsers.

- P1, size: the interpreter, cdom.js1.js, minified, is at most 24,000 bytes and at most 7,000 gzipped (gzip -9); frames.wasm.js1.js, the module, is at most 12,000 bytes and at most 5,000 gzipped; the interpreter and the module together, gzipped, are at most twice the IE 9 build's frames.wasm.ie9.js (5,596 bytes gzipped).
  Refuted by any of them larger.
- P2, the module format: the digit string is at most half the bytes of the statements form, and its decoding in Chrome takes at most three times as long as the statements' parse and run; the shipped form is the digit string.
  Refuted if the digit string is more than half the statements' size, or decodes more than three times slower.
- P3, boot and a click on tests/frames.c: cd_start through the first draw takes at most 10 times as long on the interpreter as on the IE 9 build, and so does one click's call into C (the draw itself is the shim's and the same on both).
  Refuted by a ratio above 10 for either.
- P4, CPU: on tests/js1cpu.c (1e5 iterations of integer ops; a recursion 1000 deep), the interpreter is between 20 and 100 times slower than the IE 9 build in Chrome, and it runs between 20 and 200 million of its own instructions a second.
  Refuted by a ratio outside 20 to 100, or a rate outside 20 to 200 million.
- P5, recursion: the interpreter reaches a C recursion depth of at least 100,000 frames of the test's recursive function before its own limit traps, and that trap ends the call cleanly (the next call works).
  Refuted by a lower depth, an engine stack overflow, or a next call that fails.
- P6, the register lowering: the interpreter executes at most 0.6 times as many instructions as the wasm module does on the same run (wasm instructions counted by the same lowering before its folding).
  Refuted by a ratio above 0.6.
- P7, dispatch: with the frequency-ordered chain and the tree behind it, an instruction pays on average at most 4 comparisons; the tree alone at least 6; and in Chrome the chain with the tree is the fastest of the three shapes.
  Refuted by a mean above 4, the tree below 6, or another shape faster.
- P8, binaryen's i64 lowering: `wasm-opt --flatten --i64-to-i32-lowering` on frames.wasm grows its instruction count by at least 1.5 times.
  Refuted by a growth below 1.5.

## 10. Results

Measured after the predictions were committed (cbf9f33), in headless Chrome 157 on a Linux x86-64 VM under LEGACY=frames, so indicative only.
`LEGACY=frames node tools/js1measure.mjs` prints them; three runs are in docs/js1-interp-measure.txt, and the ranges below are over those runs.
The instruction counts come from the profiling build (`node tools/js1interp.mjs profile`) and do not depend on the browser.

| | Predicted | Measured | Verdict |
| --- | --- | --- | --- |
| P1, size | interpreter at most 24,000 bytes and 7,000 gzipped; module at most 12,000 and 5,000; both gzipped at most twice the IE 9 build (5,596) | interpreter 24,894 bytes, 6,438 gzipped; module 12,822 bytes, 4,834 gzipped; both 11,272 gzipped, 2.02 times the IE 9 build's 5,576 | refuted (by 894, 822 and 120 bytes) |
| P2, module format | the digit string at most half the statements' bytes, and at most 3 times their load time | 0.17 of the bytes (12,829 against 77,456; 4,841 against 23,605 gzipped), 0.75 to 0.91 of the time (1.4 to 1.9 ms against 1.6 to 2.1) | confirmed; the digit string ships |
| P3, tests/frames.c boot and a click | each at most 10 times the IE 9 build | boot 1.01 to 1.53 times (8.2 to 11.6 ms against 7.6 to 8.9); a click 0.93 to 1.24 times (0.92 to 1.17 ms against 0.93 to 1.13) | confirmed |
| P4, tests/js1cpu.c | 20 to 100 times slower than the IE 9 build; 20 to 200 million instructions a second | loop(1e5): 34 to 47 times warm, 6.1 to 8.6 times cold; rec(1000): 4.5 to 6.8 times warm, 2.2 to 6.9 cold; 61 to 106 million instructions a second warm, 38 to 71 cold | refuted (the ratio is below 20 for the recursion, and cold) |
| P5, recursion | at least 100,000 frames, a clean trap past the limit, the next call works | 524,285 frames of rec (wi_LIMIT 1,048,576 registers, 2 a frame), "call stack exhausted", and the next call works; the IE 9 build reaches 9,864 to 11,695 in this Chrome | confirmed |
| P6, register lowering | at most 0.6 of the wasm instructions | loop(1e5) 0.44 (2,300,480 for 5,201,107); rec(1000) 0.61; tests/frames.c's cd_start and grow(5), with stub imports, 0.74 | refuted for the recursion and the app's boot |
| P7, dispatch | the chain with the tree behind it at most 4 comparisons an instruction, the tree alone at least 6, and the chain with the tree the fastest in Chrome | comparisons: chain with tree 8.54, tree 7.56, chain 9.07, and the weighted tree added after 4.38; loop(1e5) in Chrome: weighted 24.6 to 29.1 ms, chain with tree 23.4 to 31.6, tree 28.4 to 32.6, chain 22.3 to 28 | refuted (8.54 comparisons; no shape is reliably fastest in Chrome) |
| P8, binaryen's i64 lowering | at least 1.5 times the instructions | frames.wasm 2,908 to 6,237 (2.14 times), memforms.wasm 167 to 758 (4.54 times); but 2,958 (1.02) and 204 (1.22) with -O2 after it | confirmed as stated, and weak as a reason |

What the results say:

- P1: the sizes are within 7% of the guesses, and the gzipped ones within them; the interpreter is the same file for every app, so the per-app cost is the module alone, 4,834 bytes gzipped against the IE 9 build's 5,576.
- P2: the statements form is six times the bytes and no faster in Chrome; on Netscape 2, where a store costs 35 us and a character read through `charAt` and an object lookup costs an unmeasured amount, js1-statements.html against js1.html answers it there.
- P3: tests/frames.c's boot and click are the shim's draw (Chrome parsing the written frame), not C: the call into C alone (5,000 calls of the export touch, no draw) is 3.4 to 5.8 times the IE 9 build's, 0.42 to 0.7 us against 0.10 to 0.14, and loading the module (the factory) 1.0 to 1.7 ms against 0.5 to 0.6.
- P4: the loop is where the interpreter pays its whole dispatch (34 to 47 times); the recursion is calls, where wasm2js's JavaScript calls are themselves costly and the interpreter's are three stores (4.5 to 6.8 times).
  Cold, before Chrome's optimizing compiler, the gap is 6 to 9 times, which is closer to what an engine with no JIT (all of the frames tier's) should see.
- P5: C's recursion is bounded by memory, not by the engine: half a million frames here, where Netscape 2 and 3 die at 100 to 300 nested JavaScript calls and the IE 9 build is one JavaScript call a wasm call.
- P6: the folding pays off on straight-line arithmetic (0.44), less on calls (each costs an enter, a call and a return: 0.61) and on cdom's boot, which is mostly calls of imports with constant arguments that each take a const into an argument slot (0.74, and const is the commonest op of that run).
- P7: an ordered chain of 8 is too short for cdom's boot, whose ops are spread wide, and the balanced tree ignores the weights; the weighted tree pays 4.38 comparisons on the runs it was weighted from (an in-sample figure).
  In Chrome the shapes are within the noise of each other, since its optimizing compiler takes the if-tree apart; on the old engines, which interpret their JavaScript, the comparison count is what carries, and js1-tree.html and js1-mixed.html against js1.html measure it there.
- P8: the lowering's growth comes from --flatten, and binaryen's -O2 takes most of it back; interpreting i64 as pairs was kept anyway, since it needs no second pipeline and the i64 ops are 50 handlers checked one by one against the engine (tools/js1interp.mjs).

What Netscape 2 should see, from docs/descent.md's 27 us a three-operator statement: a handler is about 10 such statements (its dispatch tests, two to four operand lookups, an add per lookup, the store and the step), so about 300 us an instruction, or some 3,000 instructions a second.
tests/frames.c's boot (2,956 instructions with stub imports, more with the shim's real answers) would take about a second of interpreting, plus the module's decoding and the writing of every word of its two pages of memory (32,768 stores, about 1.1 s at 35 us); tests/js1cpu.c's loop(2000), 46,000 instructions, about 15 s.
These are estimates for the repo manager to replace with js1.html's.

## 11. Verifying in the real browsers

Alex's XP VM serves pages from the host at http://10.0.2.2:<port>/.

1. Build: `tests/build.sh` writes, in tests/out, js1.html, js1-statements.html, js1-mixed.html and js1-tree.html (each self-contained: the interpreter, the module and the driver inline, since Netscape 2 has no SCRIPT SRC), and frames.html with cdom.frames.js, cdom.js1.js and frames.wasm.js1.js (and frames-body.html, the same with a BODY tag).
2. Serve tests/out with the mark log, as for docs/ie5-design.md and docs/descent.md's probe: `python3 /Users/alex/VMs/ie9-win7/serve.py <checkout>/tests/out 8000`, whose log holds the /mark/ lines (`python3 -m http.server 8000 --directory tests/out` serves the pages without them; the table on each page shows the same).
3. In each of Netscape 2.02, 3.04 and 4.08 and IE 4.01, open http://10.0.2.2:8000/js1.html?<tag> (ns2, ns304, ns408, ie4), then js1-statements.html, js1-mixed.html and js1-tree.html with the same tag.
   Each row of the table, and each mark, is: ua, load (the module's decoding and the memory's words), loop2000, rec100, rec1000 and rec5000 (each with its time, its result, ok when it is the engine's own WebAssembly's, the trap if any, and ips, the interpreter's instructions a second), and done.
   rec1000 and rec5000 are past the depth at which Netscape 2 and 3 die on JavaScript recursion.
4. Then http://10.0.2.2:8000/frames.html in Netscape 3.04 and 4.08 and IE 4.01, and the checks of docs/frames-design.md section 12, step 4.
   Netscape 2 cannot run frames.html: it has no SCRIPT SRC, cdom.frames.js is JavaScript 1.1, and the page's string table is an array literal (section 12 below).

## 12. What is left

- The rest of the frames tier for Netscape 2: cdom.frames.js is JavaScript 1.1 (typeof, new Array, split, new Function), the string table is an array literal, and Netscape 2 has no SCRIPT SRC, so a Netscape 2 page must inline its scripts; the interpreter and its modules are already JavaScript 1.0 and inline (js1.html).
- Writing every word of the memory at load costs Netscape 2 about a second for two pages; reading a missing key as 0 (`M[i] | 0`) would skip it, if Netscape 2 turns an undefined into 0 there (not measured).
- i64 to f32 conversion rounds twice above 2^53 (to double, then to float), so it can be one ulp off there; tools/js1interp.mjs tests it within 2^53 and at the edges.
- No bounds checks on memory, as on the IE 9 build: an address past the end reads 0 and does not trap.
- memory.grow stops at 1,024 pages (wi_PAGECAP, 64 MiB).
- The weighted dispatch is weighted from two programs; an app whose op mix differs pays more comparisons (the balanced tree's 7.6 is the bound to expect).
- The comparison with the transform (branch js1-transform) is the repo manager's, on the real browsers.
