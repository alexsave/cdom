# js1c: a WebAssembly to JavaScript 1.0 compiler, in C, with an explicit stack

The frames tier (docs/frames-design.md) loads the app's IE 9 build, which no frames-tier browser can parse.
docs/descent.md (on main) has what Netscape 2.02, 3.04, 4.08 and IE 4.01 really do.
This is the third way down to them, beside js1-transform (rewriting wasm2js's output) and js1-interp (a wasm interpreter in JavaScript 1.0): a compiler, tools/js1c.c, that reads the app's .wasm and writes JavaScript 1.0 directly.
This document was written before the code; the predictions of section 9 were committed before any measuring and are not edited.

## 1. The pipeline

1. clang builds the module as for every tier (tests/build.sh's `wasm`, cdom.mk's flags), and tools/cdlits takes the string table out.
2. binaryen's wasm-opt (version 130, the one CI already fetches) lowers what JavaScript 1.0 cannot do natively, in this order: `--legalize-js-interface --remove-non-js-ops --flatten --i64-to-i32-lowering -O2`.
   legalize-js-interface splits any i64 of an import or export into two i32 halves; remove-non-js-ops turns the i64 multiply, divide, remainder, rotates, counts and the f32 and f64 rounding that JavaScript lacks into calls of small wasm helpers it links in; flatten is what i64-to-i32-lowering requires; i64-to-i32-lowering leaves only i32 (with the high half of a returned i64 in a global); -O2 shrinks what flatten grew.
   The features stay on as clang writes them (bulk memory, sign extension, the non-trapping float to int conversions, mutable globals); js1c handles them itself.
3. tools/js1c reads the lowered .wasm and writes app.wasm.js1.js.
   It refuses, with the reason, anything it does not handle: an i64 or v128 left over, a second memory or table, an opcode it does not know.
4. tools/js10.mjs, the JavaScript 1.0 gate, checks the output, and the build stops on a failure.

## 2. The language the output uses

Only what docs/descent.md measured working in Netscape 2.02:
functions declared at the top level, `var`, `if`/`else`, `while`, `for`, `break`, `continue`, `return`, `?:`, the comma operator, `==` and `!=`, every arithmetic and bitwise operator, `new Object()` and `new Array(n)` used as plain objects with number keys, `o.m(x)` and `o[k](x)` calls, a function name assigned to a property (`o.m = f`), string `charAt`, `indexOf`, `substring` and `length`, `parseInt` with a radix, `Math.floor`, `Math.abs`, `Math.sqrt`, `Math.pow`, `Math.log`, `setTimeout` with a string.
Never: `typeof`, `void`, literals of any object, array or regular expression, function expressions, nested functions, `switch`, `do`-`while`, labels, `===`, `!==`, `in`, `instanceof`, `try`, `throw`, `with`, `delete`, `arguments`, a member of a `new` expression, the globals `undefined`, `NaN`, `Infinity` and `isNaN`, `charCodeAt`, `fromCharCode`, `split`, `join`, `slice`, `substr`, `replace`, `prototype`, `Math.imul`, `Math.fround`.
NaN is written `(0/0)` and Infinity `(1/0)`; a comparison used as a value is written `(a<b?1:0)`, never relying on a boolean's conversion.

## 3. Calls: the explicit stack and the driver

Every wasm function is one of two kinds, decided at build time from the call graph:

- direct: it makes no indirect call, it is not on a cycle of the call graph, and the longest chain of calls below it is at most DEPTH (8) functions, all of them direct.
  It compiles to a plain JavaScript function with its parameters and its return value, and it is called directly.
- driven: every other function.
  It compiles to a JavaScript function of no parameters that never calls another driven function.

A driven function's call of a driven function:

1. spills, onto the explicit stack (a plain object, J.S, of numbers with a stack pointer), the locals live at the call (the rest are not written), then the call site's number and its own function number;
2. puts the arguments in the argument registers, and the callee's number in the driver's register;
3. returns to the driver.

The driver loop calls the function its register names, through a table of the functions (`JF[k]()`), until the register is 0.
A driven function's return puts the value in the return register, takes the caller's number from the stack's top as the next to run, and sets the resume flag.
The caller, entered with the flag, reads its call site number, restores its locals, pops its frame and goes back to the call site, where it takes the return value.

Going back to a call site inside nested loops and ifs is done as Asyncify does it, on the structured code: the call sites are numbered in order, so every statement covers a range of them; in resume mode every statement whose range does not hold the site is skipped, every if takes the branch that holds it without evaluating its condition, and every loop is entered.
A normal run pays for it one test of the resume variable a statement that contains a call, and nothing in straight code without calls.

So the JavaScript call depth is at most: the shim's own, plus the export's wrapper, plus the driver, plus one driven function, plus DEPTH direct functions, plus an import.
js1c computes the bound at build time, prints it, and refuses a module whose bound is over 32; a driven function's import can call an export back (the shim does not today), which starts a nested driver on the same explicit stack and adds the same bound again.

An indirect call (call_indirect) checks the table index and the function's type number, and always goes through the driver; a direct function that is in the table or exported gets a small driven wrapper too.

## 4. Control flow inside a function

block, loop and if become `while (1) { ... break; }` loops, `while (1) { ... }` for a wasm loop, and `if`; a branch to the innermost of them is `break` or `continue`; a branch further out sets a branch variable and breaks, and each loop that a branch crosses tests it on the way out (only those that one does).
br_table becomes a tree of ifs on the index.
A block or if with a result has a variable it is assigned to.

Expressions: js1c keeps wasm's value stack as JavaScript expressions and writes them out as one expression where wasm nests them, putting a pending value into a temporary only when something with a side effect comes before its use (a store, a call, a set of a local or global, a branch).

## 5. Memory

C's memory is a plain object of 32-bit words (J.M[address >> 2]), the layout of tools/legacy/words.js: the shim reads it as memory.words, as it reads the IE 9 build's.
An i32 load at an address known 4-aligned is one read; any other load or store of 8, 16 or 32 bits shifts and masks, across two words where it straddles one.
A missing word reads as 0 (`undefined | 0`), so memory is never filled with zeros, except where window.Int32Array exists (section 10, Q4: a modern engine only).
f64 loads and stores convert between a double and its two words by arithmetic (Math.log, Math.pow, Math.floor), as words.js does without typed arrays; f32 the same on one word, and f32 arithmetic is rounded to f32 by the same routine.
memory.size and memory.grow move a page count only; memory.copy and memory.fill are loops over bytes, by words where both ends are aligned.

The data segments: two forms, chosen by a flag, measured in section 9:

- statements: one `M[k]=v;` per non-zero word;
- digits: the non-zero words as a string of base 36 numbers, each a skip and a value, decoded by indexOf, substring and parseInt, in strings of at most 4,096 characters each.

## 6. Traps

No throw: a trap (unreachable, an integer divide by zero or overflow, a float to integer conversion out of range, an indirect call of the wrong type or out of the table) sets J.T and returns.
A direct call of a function that can trap is followed by `if (JT) return`, a driven function's return to the driver is checked there, and the driver stops.
The export's wrapper puts the stack pointer back and sets the exports object's `js1_trap` to 1 (prefixed, since an export may be named trap, as tests/js1diff.c's is); the frames shim (cf_cRun) then leaves its counts up, as a throw out of the call did, and the next entry recovers (frames-design.md section 4).
Loads and stores out of bounds do not trap (the memory is an object of any size); that is the one difference from wasm, and a test notes it.

## 7. Imports and exports

The output defines one global function, cdomFactoryJs1(imports), and sets window.cdomFactory to it, as the IE 9 build does; the frames shim calls it the same way.
An import is called as a method of its module's object (`I.open(x)`, `I["class"](x)`), so the shim's cdom imports are used by the same names.
The exports object has one property a function, each a wrapper that starts the driver on the export, plus memory (with its words), js1_trap, and js1_paused, js1_result and js1_more for slicing.

## 8. Time slicing

The driver counts the functions it runs; with a budget set (J1 slicing on), it stops after that many with the state kept, and the export returns with js1_paused set; the caller continues with js1_more() from a `setTimeout("...", 0)` until it answers 0, and reads js1_result.
The frames tier's exports run with no budget (the shim expects an answer); tests/out/js1diff.html, the page for the real browsers, runs its long tests sliced.

## 9. Predictions

Written before any measuring; each has a falsifier.
Timings are node 22 on a Linux x86-64 VM, indicative only (the real browsers are the repo manager's to measure, section 11).

- Q1, size: tests/frames.c's js1c output is at most 2.0 times the bytes of its IE 9 build (frames.wasm.ie9.js as tests/build.sh writes it, already minified), and at most 1.6 times gzipped (gzip -9).
  Falsifier: either ratio larger.
- Q2, statements: the js1c output of tests/frames.c has at most 1.5 statements a wasm instruction of the lowered module (acorn's count of statement nodes).
  Falsifier: more.
- Q3, integer loop: 1e5 iterations of integer ops (tests/js1diff.c's loop) run in node in at most 1.5 times the IE 9 build's time and at most 10 times plain wasm's.
  Falsifier: slower than either.
- Q4, recursion: 1000-deep recursion (the same test) runs in the js1c output in at most 5 times the IE 9 build's time; the IE 9 build itself runs it in node.
  Falsifier: more than 5 times.
- Q5, depth: the JavaScript call depth reached, counted in node by wrapping every compiled function, is at most the static bound js1c prints, for every test of tests/js1diff.c and for tests/frames.c's whole run, and that bound is at most 12 (the 1000-deep recursion included).
  Falsifier: a depth over the bound, or a bound over 12.
- Q6, data: the digits form is at least 40% smaller than the statements form for frames.wasm's data, and has at least 90% fewer statements; in node, each loads in under 5 ms.
  Falsifier: less saving, or a load over 5 ms.
- Q7, slicing: the integer loop sliced at a budget of 1,000 driver steps is at most 5% slower than unsliced (both in node, the yields excluded).
  Falsifier: more than 5%.
- Q8, correctness: every export of tests/js1diff.c gives the same results through js1c as plain wasm, bit for bit, the trap included.
  Falsifier: any difference.

## 10. Results

Measured after the predictions were committed (2ce4030), with node 22 and headless Chromium on a Linux x86-64 VM, so indicative only.
`JS1_MEASURE=1 node tools/js1.mjs` prints them; the timings are the median of 5 processes, each build's file run in the main realm (as a browser runs a script), and three runs gave the same verdicts.

| | Predicted | Measured | Verdict |
| --- | --- | --- | --- |
| Q1, size | at most 2.0 times the IE 9 build, 1.6 gzipped | frames: 32,278 bytes against 15,552 (2.08), 7,190 gzipped against 5,576 (1.29); js1diff: 1.81 and 1.26 | refuted (frames' bytes) |
| Q2, statements | at most 1.5 a wasm instruction | frames 3,159 for 2,924 instructions (1.08); js1diff 0.78 | confirmed |
| Q3, integer loop, 1e5 | at most 1.5 times the IE 9 build, 10 times wasm | 0.218 ms against 0.489 (0.45 times) and wasm's 0.217 (1.0 times) | confirmed |
| Q4, 1000-deep recursion | at most 5 times the IE 9 build | as first built 0.285 ms against 0.021 (13.7 times); after the fix below 0.032 to 0.036 ms (1.46 to 1.55 times, 2.1 to 2.4 times wasm) | refuted, then fixed |
| Q5, depth | at most js1c's bound, the bound at most 12 | bound 7 for both modules; js1diff's deepest 4 over all 279 cases (rec(1000) and rec(10) alike), frames.c's 5 in Chromium | confirmed |
| Q6, data | digits 40% smaller, 90% fewer statements, each under 5 ms | 234 bytes and 4 statements against 701 and 47 (67% and 91%); 0.8 and 0.6 ms for the whole file | confirmed |
| Q7, slicing at 1,000 steps | at most 5% slower on the loop | the loop -2.5% to -0.4%; rec(1000) from -14% to +69% over four runs | confirmed as worded, but see below |
| Q8, correctness | every export bit for bit, the trap included | 279 of 279 cases and the whole memory word for word; tests/out/js1diff.html 26 of 26 in node and in Chromium; 19 of 19 mutants caught (tools/mutate.py) | confirmed |

What the misses say:

- Q1: js1c's runtime (the helpers of section 5 and 6, written once) is about 5.5 KB of the 32 KB; the rest is about 1.7 times the IE 9 build, from the explicit stack's spills and resume guards and from the plain statements where wasm2js writes nested expressions. Gzipped, the output is only 1.28 times, as its repetition compresses.
- Q4: the cost was not the driver but V8: the memory is an object whose first number key is high (the data, above the C stack), which V8 keeps as a slow dictionary; the stack and the driver alone cost 1.5 times the IE 9 build.
  The factory now writes the initial memory with zeros first, in order, where window.Int32Array exists, which no frames-tier browser has (they keep every object as a hash table, and would pay 35 microseconds a word on Netscape 2 for nothing).
  Timed in a node vm context instead, rec(1000) was 75 times the IE 9 build: a vm context's globals go through interceptors, and js1c's state is all globals (JavaScript 1.0 has no closures to keep it in), so tools/js1.mjs times each build in a process of its own.
- Q7: the loop is a direct function, so the driver runs one step for it and slicing never applies; the prediction was about the wrong test.
  On the recursion, which does go through the driver, rec(1000) is about 2,000 driver steps, so two pauses; the sliced time ranged from 14% faster to 69% slower over four runs, noise on a 0.03 ms call, so node says only that the pauses are not the cost; the real browsers' times on tests/out/js1diff.html, which runs sliced, are the measurement.
- Not predicted: the 1000-deep recursion's JavaScript depth is 4, the same as at 10, and tests/out/js1diff.html runs rec(5000) in Chromium.

## 11. Verifying in the real browsers

1. Build: `tests/build.sh` writes, in tests/out, js1diff.html (self-contained: js1c's output of tests/js1diff.c is inlined, since Netscape 2 has no SCRIPT SRC, with wasm's answers computed at build time), and frames.html, frames-body.html, cdom.frames.js and frames.wasm.js1.js (the frames tier, as docs/frames-design.md section 12 has it, its app now from js1c).
2. Serve tests/out: `python3 -m http.server 8000 --directory tests/out` on the host.
3. In Netscape 2.02, 3.04 and 4.08 and IE 4.01, open http://10.0.2.2:8000/js1diff.html.
   The TEXTAREA gets the user agent, then one line a case, `ok` or `FAIL` with the case, its milliseconds and, on a failure, what came and what wasm gave; the field below it ends as `26 of 26`.
   The cases run one a tick (setTimeout with a string), with the driver sliced at 1,000 steps, so the browser keeps drawing; rec(1000), rec(5000), loop(100000) and deeptrap(300) are the long ones, and their times are the measurements of Q3 and Q4 on the real engines.
4. Then http://10.0.2.2:8000/frames.html, the frames tier.
   Expected: Netscape 4 and IE 4 run it (the app file is JavaScript 1.0, the shim 1.1, the string table an array literal they parse); Netscape 3 stops at the string table (`var cdomLits=[...]`, an array literal, tools/cdpage's); Netscape 2 stops before both, at SCRIPT SRC (A12 of frames-design.md: Netscape 2 has none), and would stop again at cdom.frames.js (typeof, new Function, Array methods).
   Those two are the frames shim's and the page tool's, not js1c's, and this branch leaves them.

## 12. The real browsers: what broke and the fixes

The run of 2026-11-20 on main's js1-real branch (docs/descent.md there, "The three code paths in the real browsers") put this branch's output through Netscape 2.02, 3.04, 4.08 and IE 4.01.
Netscape 4.08 and IE 4.01 ran tests/out/js1diff.html to 26 of 26, but five things broke, and this section is their fixes, each with its prediction written before the run that tests it.

1. Netscape 2.02 dies on `undefined | 0` ("33041 is not a number."): the factory zeroes the memory only where `window.Int32Array` exists, so a read of a word nothing wrote is fatal there.
2. Netscape 3.04 refuses the file: "missing ) after for-loop control." at `for(i=n-1;i>=0;i--)` in JMc.
3. Netscape 3.04 gives NaN for `x | 0`, `x >>> 0` and `x & 255` once x is 2^32 or more (main's tests/probe-js1d.html), so loop and rec answer NaN.
4. Netscape 2.02 makes `o[0]` the first property set by name on o, so an object indexed by number must never have a named property.
5. IE 4.01 answered rec(1000) and rec(5000) wrong on the unsliced js1bench page, after rec(100)'s third run died there.

### 12.1 What I expect from the probe, written before it runs

tests/probe-js1e.html (tools/gen_probe_js1e.py) isolates items 2 and 3 in Netscape 3.04 (and runs in Netscape 2.02 for comparison).

- The for loop: the construct is a postfix `--` directly before the `)` that closes the for header.
  `i++)` parses (JMc's forward loop, on the same line, did), so I expect `forDecGe`, `forDecGt`, `forDecBraces`, `forDecInit` and `forDecCall` to die as a syntax error (no mark at all), `forDecSpaced` (`i -- )`) to die too (a token rule, not a character one), and `forPreDec` (`--i`), `forMinusAssign` (`i = i - 1`) and `forDecNoTest` to parse.
  A postfix `--` elsewhere (`stmtDec`, `exprDec`, `whileDec`, `parenDec`, `callDec`) I expect to parse: if `parenDec` or `callDec` dies too, the rule is "`--` then `)`", wherever it is.
- The ranges: Netscape 3.04 converts to 32 bits by a C cast of the double, which is right in [-2^31, 2^32) and gives NaN (an exception value it then prints) outside it.
  So `| 0`, `>>> 0`, `& 255`, `<< 1`, `>> 1` and `>>> 1` are right for every value from -2^31 to 2^32 - 1 and NaN for 2^32 and above and for -2^31 - 1 and below.
  Netscape 2.02 gives the same right answers inside that range and its own (0 or a wrapped value) outside it.
- `x % 4294967296` brings any of these into (-2^32, 2^32), which is not yet safe for a negative x below -2^31: the reduction must be followed by a fold of the negative half, or written as `x - Math.floor(x / 4294967296) * 4294967296` (always in [0, 2^32)), which I expect to be right in both browsers.
- Cost on Netscape 3.04 for 20,000 iterations: the bare loop about 0.5 s, `| 0` adds under 10%, the `%` form 20 to 40% more, the `if` fold about the same as `%`.

### 12.2 What I expect of IE 4's wrong answers, written before looking

The export wrapper puts JP and JQ back only when the call returns normally; IE 4.01 has no `try`, so when a call dies (its "Stack overflow" or another error), JP stays where the dead call left it and JQ may be 1.
The next call then starts mid-stack, a resumed frame reads the wrong slots, and every later run answers wrong: that is rec(1000) and rec(5000) after rec(100)'s third run died.
So the cause to find is why rec(100)'s third run died at all on IE 4: I expect an IE 4 dialog for the JScript call depth or for the driver's object stack, and I will look at the screen before it is dismissed.

### 12.3 What the probe found

tests/probe-js1e.html in Netscape 3.04 (and 2.02), 2026-11-20:

- The for loop: every form parses on its own, spaced or not, `i--` or `--i`, with `>=` or `>`, with the `n-1` start and in an `else`, and js1c's own JMc, copied whole into a block, parses and runs (all wrong predictions: the construct is not the cause).
  What differs is where it sits: in the bench file JMc's line is 278 characters and its `i--)` sits at columns 253 to 256.
  A third run put one token at a chosen column after a padding comment: `x--;` parses with its `--` at columns 250, 253, 255 and 256 and is refused at 254 alone, while `>=` and `>>>` parse at 254.
  So Netscape 3.04 refuses a `--` that starts at column 254 of a line (a seam in how it reads a line, presumably); whether `++` or a later seam (510, 766) does the same is not measured.
- The ranges, exactly as predicted: in Netscape 3.04, `| 0`, `>>> 0`, `& 255`, `<< 1`, `>> 1` and `>>> 1` are right for every value from -2^31 to 2^32 - 1 (2147483648 | 0 is -2147483648, 4294967295 >>> 0 is 4294967295, -2147483648 >>> 0 is 2147483648) and NaN for 2^32, 2^32 + 5, 2^33, 2^52 + 5, 2^53 + 3, -2^31 - 1, -2^32, -2^32 - 5, -2^33 - 5 and -2^53 - 3.
  Netscape 2.02 is right for every value tried under 2^53 in magnitude, and differs only past it (2^53 + 3 gives 2 where a modern engine gives 4, the literal's rounding).
- The reductions: `x % 4294967296` keeps the sign (-4294967301 % 4294967296 is -5, whose `| 0` is right, -5), and `x - Math.floor(x / 4294967296) * 4294967296` gives 0 to 2^32 - 1; both are right in Netscape 3 for every value tried.
- Their cost, 20,000 iterations: on Netscape 3.04 the bare loop 265 ms, with `| 0` 313 ms, with `% 4294967296` and `| 0` 359 ms (2.3 us more an operation than `| 0`), with an `if` fold 516 ms; on Netscape 2.02 203, 219, 234 and 344 ms.

### 12.4 The fixes

- Item 1: the factory writes 0 to every word of the initial memory, in every browser, and memory.grow (JMg) writes its new pages' words.
  tools/js1.mjs runs every case again on a memory that throws, as Netscape 2 does, on a read of a word nothing wrote, and checks that every word of the initial memory and of two grown pages exists (tests/js1diff.c gains `grow`).
- Item 2: js1c writes no `++` or `--` at all (`i=i+1`), so no line length can put one at column 254; tools/legacy/js11.mjs refuses a `++` or `--` at column 254 (the measured rule, one owner, used by tools/js10.mjs too), and tools/js1.mjs checks the output has none.
  The rule is narrow on purpose: tools/legacy/frames.js has 40 of them and its minified file is one 30,014-character line, none of them at column 254.
- Item 3: an i32 add, subtract or multiply never leaves -2^31 to 2^32 - 1 before its `| 0`.
  With a constant operand the range is known and the form is picked so it needs nothing: `x + c` for c >= 0, `(x >>> 0) - c` for c > 0, which is every `n - 1` and address offset; otherwise `(x + y + 4294967296) % 4294967296 | 0`; a multiply by a constant c with |c| > 1 adds 2^32 times ceil(|c| / 2) and reduces; JMu multiplies 16-bit halves, each partial product under 2^32, and reduces the sum; JPc works unsigned.
  tools/js1.mjs runs every case again with every bitwise operand of the file passed through a check that throws outside Netscape 3's range (1,429 operands).
- Item 4: nothing to fix: no table js1c writes (JM, JS, JE, JY, JF, JPT) has a named property, and the exports object has no number key; tools/js1.mjs now checks that after every case and in the file.
- Found on the way, from js1-interp's dialog on Netscape 2.02: "function does not always return a value." refuses, at parse time, a function with both `return x;` and `return;`.
  js1c wrote that in every void function that can trap (25 in frames.wasm.js1.js, none in js1diff's); it now writes `return 0;`, and tools/js10.mjs refuses the mix.
- Item 5: not js1c's.
  A page that runs rec(100) three times, rec(1000) unsliced, then rec(1000) and rec(5000) with JB=1000, writing into a TEXTAREA instead of images, gives every answer right on IE 4.01 (shots/js1cr-ie4rec.png).
  On js1-real's bench page the answers chain through the program's state (rec stores into `sink`), and the wanted values assume every earlier run ran; IE 4 lost the marks of rec(100)'s third run and of rec(500) (its page stalls on image requests, as the probe pages did), so those runs either never ran or never reported, and every later wanted value was off.
  The prediction of 12.2 (a dead call leaving JP up) describes what would follow a real error, but no error was seen; the two reloads of that page on IE 4.01 stalled after their first mark.

### 12.5 What I expect of the four browsers now, written before the runs

- tests/out/js1diff.html: 26 of 26 in all four (it was 26 of 26 on Netscape 4.08 and IE 4.01; Netscape 3.04 now parses the file and its loop and rec no longer reach NaN; Netscape 2.02 no longer reads an unwritten word).
- js1-real's bench page for this path, the plain page (no NS2FIX): every answer right in all four; IE 4's run may lose marks again, which the table will say.
- Start-up on Netscape 2.02: the bench module's initial memory is 3 pages, 49,152 words, and writing them costs about what the transform's load and the NS2FIX page's zeroing did (2.1 s and 1.9 s): the load mark 2 to 3 s on Netscape 2 and 3, 0.3 to 0.6 s on Netscape 4, 0.1 to 0.2 s on IE 4.
  If it is that, a lazier way is worth proposing (not building).
- The cost of the reductions on loop(2000) (a multiply by 2654435761 through JMu and about three general adds an iteration): 10 to 25% slower than the old file on Netscape 4.08 (78 to 125 ms then) and on Netscape 3.04 (719 to 906 ms then, when it answered NaN).

### 12.6 Found during the four-browser run, and fixed

- Netscape 3.04 also gives NaN for the `| 0` of a fraction (7 / 3 | 0): js1diff.html answered 18 of 26 there, every signed and unsigned division and every float-to-integer conversion wrong.
  Every quotient is now `(a - a % b) / b` (exact, truncated toward zero) and every float-to-integer conversion goes through `Math.floor` or `Math.ceil` before its `| 0`; tools/js1.mjs's Netscape 3 check refuses a fraction as well as a value out of range.
- Netscape 2.02's `1 / 0` is 0 and Netscape 3.04 lost the infinity too: infinity and NaN now come from overflow (`JIn = 1e308 * 10`, `JNa = JIn - JIn`), and tools/js1.mjs checks the output divides by no literal 0.
- js1diff.html's TEXTAREA lines end in CR LF: Netscape 2's shows a bare LF as a box, all on one line.

### 12.7 Results

The runs of 2026-11-20 and 21 in the XP VM, tests/out served on port 8109, the screenshots /Users/alex/VMs/ie6-xp/shots/js1cr-*.png.

tests/out/js1diff.html, the final file:

| | Netscape 2.02 | Netscape 3.04 | Netscape 4.08 | IE 4.01 |
|---|---|---|---|---|
| before this branch | does not reach a case (undefined \| 0) | does not parse (`--` at column 254) | 26 of 26 | 26 of 26 |
| after | 23 of 26 | 23 of 26 | 26 of 26 | 26 of 26 |

The three in Netscape 2.02 and 3.04 are the same, dbl's: words 31 and 32 (a subnormal made at run time, `x * 1e-315`, is 0 there), 33 (a product of that subnormal) and 37 (`isnan(z - z)` with z infinite: infinity minus infinity is not NaN there, nor in Netscape 2.02, where `JIn - JIn` prints 0).
Those are the engines' doubles, not js1c's: Netscape 2.02 and 3.04 have no subnormals (5e-320 is 0) and no NaN from infinity minus infinity, and Netscape 2.02 has no negative zero either (`1 / -0` and `Math.atan2(0, -0)` are 0, shots/js1cr-ns2-dbl.png); a C program that needs those cannot get them there.

js1-real's bench page for this path, the plain page (no NS2FIX), the final file; every answer right in every browser (Netscape 3.04's last three runs from the run before the division fix, which the bench does not use, since the final run's page stalled after rec100):

| run | Netscape 2.02 | Netscape 3.04 | Netscape 4.08 | IE 4.01 |
|---|---|---|---|---|
| load (the memory's 49,152 words) | 1656 ms | 2375 ms | 1156 ms | 188 ms |
| loop(2000), three runs | 313, 390, 360 ms | 625, 500, 594 ms | 250, 219, 172 ms | 78, 63, 125 ms |
| mem, 64 KB | 2656 ms | 3625 ms | 1297 ms | 469 ms |
| rec(100), three runs | 47, 16, 31 ms | 47, 32, 47 ms | 110, 15, 94 ms | 0, 0, 0 ms |
| rec(500) | 156 ms | 219 ms | 453 ms | 31 ms |
| rec(1000) | 250 ms | 390 ms | 328 ms | 109 ms |
| rec(5000) | 1719 ms | 2203 ms | 1828 ms | 688 ms |

Before this branch the same page died at load on Netscape 2.02 and did not parse on Netscape 3.04 (js1-real's run).

The cost of the fixes:

- Start-up: writing the 49,152 words of the bench's 3 pages takes 1.7 s on Netscape 2.02, 2.4 s on Netscape 3.04, 1.2 s on Netscape 4.08 and 0.2 s on IE 4.01 (the old file's load was 0 ms on Netscape 4.08), as predicted.
  That is seconds, so a lazier way is worth building next (not built here): zero eagerly only the words below the module's `__heap_base` (the data and the C stack, which the module reads before it writes), and zero each page past it when memory.grow or the C allocator first hands it out, which in cdom's own allocator is one place; for the bench that is about a third of the words.
- The reductions: on Netscape 4.08, the old file (js1-real's) against the new in one session: loop(2000) 406 to 468 ms against 328 to 391 ms, mem 3,938 ms against 2,547 ms, rec(5000) 3,235 ms against 2,969 ms: the new file is faster, not 10 to 25% slower as predicted; the memory written in full from 0 seems to keep Netscape 4's object a dense one, which outweighs the reductions.
  On Netscape 3.04 the old file does not parse, so the cost there is the probe's: a `% 4294967296` adds 2.3 us an operation over `| 0` (0.75 us on Netscape 2.02).
- The predictions of 12.5: right that js1diff and the bench now run in all four, and about the start-up; wrong that js1diff would be 26 of 26 in Netscape 3 (the doubles above), and wrong about the reductions' cost on Netscape 4.

What is still open:

- The doubles Netscape 2 and 3 cannot represent (subnormals, NaN from infinity minus infinity, and Netscape 2's -0).
- The seam rule is the one measured column (254); a `--` at a later seam of a long line (if any) is not measured, and js1c writes none anyway.
- IE 4.01's lost image marks on long pages (item 5) are the page's, not js1c's; the page's wanted values chain through the program's state, so js1-real's bench page could compute each run's wanted value from a fresh instance to be robust to a lost run.

### 12.8 The frames tier's runs (2026-11-21)

docs/frames-design.md section 15 has the runs of the frames tier in Netscape 4.08, IE 4.01 and Netscape 3.04.
They changed js1c in three places: the initial memory is written only below __heap_base on engines that read a missing word as 0 (Netscape 2.02, told by an Array with no length, writes every word as before; the factory's start-up on Netscape 4 and 3 fell from about 1.1 and 1.5 s to 0.45 and 0.8 s); every direct function ends in `return 0;` (Netscape 3.04 refuses a function that returns a value and does not end in a return, as Netscape 2.02 does, and the gate checks it); and a data word is a signed int32 in base 36 (Netscape 3.04's parseInt keeps a value only up to 2^31 - 1).
