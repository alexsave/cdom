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

Not measured yet.
