# Strings: plain C for the literals a macro cannot see

Status: options with measurements, 2026-11-17, branch `strings-opt` (from `strings` at 7b4441b).
Nothing here is merged; the prototype on this branch is for measuring.

## The problem

`cd_s` classifies an argument by how it is spelled, so a literal that is not written directly as the argument is a runtime string.
Two shapes are left in the lessons with `cd_s("...")` written by hand:

```c
cd_set_text_node(t, open ? cd_s("Hide details") : cd_s("Show details"));   /* a ternary */
static cd_id tab_button(cd_str label) { ... }                               /* a helper */
tabs[1] = tab_button(cd_s("Usage"));
```

The goal is plain C: `open ? "Hide details" : "Show details"`, and `tab_button("Usage")` with `const char *label`, at a cost as close to a literal's as possible.

## The answer in one table

Measured in headless Chrome 157, one browser at a time, the string resolved only (no DOM call), ns per `cd_text`, two passes.
The strings are 4 to 13 ASCII bytes, eight call sites per iteration.

| Option | Ternary, plain C | Helper, plain C | Page bytes (brotli) | Main risk |
| --- | ---: | ---: | --- | --- |
| Today with `cd_s` on each literal (the ceremony) | 6.9 / 6.9 | | 0 | none; it is what the author must not have to write |
| A. Plain C, accept the runtime read (baseline) | 46.5 / 47.1 | 40.6 / 40.6 | +10 to +130 per lesson (the strings go back into the wasm) | none; it is slow |
| B. A pointer into read-only data is its own id; the shim keeps its string by address (recommended) | 8.4 | 8.7 | +120 for the shim, +20 bytes of wasm, plus A's | needs wasm-ld's `__rodata_start` and `__rodata_end` (LLVM 23 or later); cdom.mk refuses an older linker |
| B with a `Map` only | 17.1 / 16.7 | 11.9 / 11.8 | a little less shim | none beyond B's |
| B with an array indexed by offset | 8.0 / 8.0 | 8.8 / 8.9 | a little less shim | 4 bytes of heap per byte of read-only data |
| C. C unchanged; the shim tests each runtime string's pointer against the read-only range | 25.9 / 26.0 | 22.5 / 22.5 | about B's | B's, for a third of the gain |
| D. A cache by address for every runtime string, checked against the bytes on each use | 40.7 / 40.7 | 34.1 / 34.1 | about +150 | a string that changes every time gets 70% slower (96 against 57 ns) |
| E. A cache by address with no check | 25.0 / 25.1 | 21.6 / 21.7 | | unsound: a reused buffer shows its old text |

A direct literal (a slot) measured 7.5 to 8.0 ns in the same runs, and 6.8 to 6.9 ns in the loop that is otherwise identical to the ternary one.
So option B puts plain C within 1.5 to 2 ns of a literal, from 41 to 47 ns.

## Why no classifier in the header can do it

These were tried or worked through before measuring anything:

- **The macro cannot look inside its argument.**
  `open ? "a" : "b"` reaches `cd_s` as one expression, and the preprocessor cannot split it at the `?`.
  `__builtin_constant_p(__builtin_strlen(...))` is false for it, because which string it is depends on `open`.
- **A helper's parameter is a variable.**
  Inside `tab_button(const char *label)`, `label` is not a literal at any optimization level the front end can see, and the choice must be made in the front end (`__builtin_choose_expr`), or it would change with `-O` and with inlining.
- **The type does not help.**
  A literal decays to `char *`, the same type as a stack buffer, and `const char *` is what a helper takes for both.
- **A section for every literal does not exist.**
  `#pragma clang section rodata="cd_ro"` moves `const` variables but leaves string literals in `.rodata..L.str` (checked with clang 23.1.5).
- **cdlits cannot find "all the strings" in `.rodata`.**
  After the link, read-only data is bytes with no boundaries: literals, `const` tables and switch tables are merged, and identical literals share an address.

So the build cannot turn these into slots.
What it can know is weaker but enough: whether a pointer points into memory that never changes.

## Option A: plain C, accept the runtime read (baseline)

```
author   cd_set_text_node(t, open ? "Hide details" : "Show details");
build    cd_s sees no literal: cd_tmpz(p), which measures the string and writes a record (p, len)
shim     str(): reads the record, tests the bytes for ASCII, builds the string with fromCharCode
```

- Speed: 46.5 and 47.1 ns for the ternary, 40.6 for the helper, against 6.9 with `cd_s`.
  A real `cd_set_text_node` or `setAttribute` costs 90 to 440 ns (BRIDGE.md), so this adds about 10% to 40% to such a call.
- Size: the strings stay in the module (inside the base64) instead of the page's table.
  Measured by rewriting the four lessons in plain C: 04-regions +130 bytes of brotli, 05-todo +18, 06-stopwatch +4, 07-particles +7.
- Memory: nothing is kept.
- Breaks: nothing.

## Option B: a pointer into read-only data is its own id (recommended)

```
author   cd_set_text_node(t, open ? "Hide details" : "Show details");
         static cd_id tab_button(const char *label) { ... cd_text(label); ... }   tab_button("Usage");
build    cd_s sees no literal: cd_tmpz(p).
         cd_tmpz: if p lies in [__rodata_start, __rodata_end), return p itself as the cd_str. No strlen, no record.
         Otherwise as today: measure, write the record.
shim     str(s): s in the slot range: lits[(s - litBase) >> 2], as today.
         s positive and below it: the string kept for that address, read from memory the first time only.
         s negative: a runtime record, read on use, as today.
```

In C (the whole change, in `cdom.c`):

```c
extern const char __rodata_start[], __rodata_end[];
cd_str cd_tmpz(const char *s) {
  if ((uintptr_t)s - (uintptr_t)__rodata_start < (uintptr_t)__rodata_end - (uintptr_t)__rodata_start)
    return (cd_str){(uint32_t)(uintptr_t)s};
  ...
}
```

In the shim, between the slot lookup and the runtime read:

```js
const h = s & 255;                       // a 256-entry direct-mapped cache in front of a Map
if (roKey[h] === s) return roVal[h];
let v = roMap.get(s);
if (v === undefined) roMap.set(s, v = /* the bytes at s up to the 0, through readString */);
roKey[h] = s; roVal[h] = v;
return v;
```

Why it is sound: wasm-ld puts every `const` object and every string literal in `.rodata`, and since LLVM PR 172102 (December 2025, first in a release with LLVM 23: LLD 22.1.8 still leaves them undefined) it defines `__rodata_start` and `__rodata_end` around it.
C may not write a `const` object, so a string at such an address is the same string for the life of the page, and the address can stand for it.
A stack buffer, a `static char buf[]`, heap memory and `.data` are all outside the range and are read on every use, exactly as today.
Verified here: the two symbols are 65536 and 65592 for a test module whose link map shows `.rodata` at 0x10000 with size 0x38, a `static const char NAME[]` is inside, a mutable `char mut[16]` and a `.bss` array are outside.

What it also makes fast, for free: `FRUITS[i]` from a `static const char *const FRUITS[]`, a `static const char NAME[]`, and any other constant string passed plainly.

- **Speed.**
  Ternary 8.4 ns, helper 8.7 ns, against 6.9 for a slot written with `cd_s` and 41 to 47 for the read.
  A mutable buffer costs what it does today: 35.7 against 37.9 ns at 5 bytes and 59.2 against 58.8 at 12 (one more compare in C, inside the noise).
  The first use of each address pays one read (about 40 to 60 ns), once.
- **The three ways to keep the strings, measured.**
  A `Map` alone: 16.7 to 17.1 and 11.8 to 11.9 ns.
  An array indexed by `s - __rodata_start`, made that long at start: 8.0 and 8.8 ns, but it holds 4 bytes of heap per byte of read-only data (0.6 KB for lesson 1's 160 bytes, 44 KB for amgi's 11 KB, 4 MB for an app with 1 MB of embedded assets).
  The direct-mapped cache in front of the `Map`: 8.4 and 8.7 ns with about 3 KB of fixed memory, and a collision costs one `Map` lookup.
- **Size.**
  The module grows by 20 bytes (the range test).
  The minified shim grows by 232 bytes, 120 after brotli.
  With the strings back in the module (option A's cost), the four lessons in plain C come to: 04-regions 10,564 to 10,814 bytes of brotli, 05-todo 14,151 to 14,289, 06-stopwatch 13,509 to 13,599, 07-particles 11,761 to 11,888.
- **Memory.**
  One JS string per distinct read-only address ever passed, kept for the life of the page.
  It is bounded by the module's read-only data, so it is not a leak: nothing a running app does can add to it without end.
- **What breaks or gets harder.**
  - *The linker.*
    wasm-ld before LLVM 23 does not define the two symbols (checked with LLD 18.1.3 and 22.1.8; LLD 23.1.6 defines them).
    An earlier version declared them weak, so that there the range was empty and every string took option A's path, which it called the same page, slower.
    It was not: built that way, `tools/unit.mjs` failed three cases, two read-only-strings cases read 0 times from memory where 7 were wanted, and `cd_bound_set` trapped on a read-only pointer that was not a literal, since a bound value needs a literal to be its own id.
    So the symbols are strong, and cdom.mk's toolchain check refuses a wasm-ld older than LLVM 23 with the version it found and the fix, before the link would fail on the undefined symbol.
    A link test asserts that the range is not empty with the toolchain cdom documents, so a regression is seen.
  - *Writing through a cast.*
    Code that casts `const` away and writes into `.rodata` would show the old text for ever.
    That is undefined behaviour in C already, and clang's own optimizer assumes it does not happen.
    `-fwritable-strings` moves literals to `.data`, where they are simply read on use.
  - *LTO and merging.*
    Nothing: identical literals merged to one address share one cache entry, a tail-merged literal is its own address, and a never-written `static` that LTO turns into a constant is constant.
    The id is the linker's own address and no code is rewritten.
  - *The slot range.*
    The prototype tells a slot from a read-only pointer by `s >= litBase`, because wasm-ld lays `.rodata` before `.data` and before the `cd_lits` section.
    The real change should test both bounds of the slot range, so it does not rest on that order.
  - *wasm2js and old browsers.*
    The addresses are the same in the wasm2js build.
    The ES5 tier has no `Map`: it needs the plain-object form (measured in Chrome at 15.9 and 14.2 ns) or a polyfill.
  - *`cd_tmp(p, len)`* with a length stays a read on use: only the 0-terminated form, which is what plain C passes, becomes an id.
- **End to end.**
  The prototype is in `src/cdom.c` (`CDOM_RO`) and `shim/cdom.js` on this branch, with tutorial lessons 04 to 07 rewritten in plain C (no `cd_s` left in `tutorial/`), and `tutorial/run.sh` passes on all seven lessons.

## Option C: the shim tests the pointer, C unchanged

```
author   as B
build    as A: cd_tmpz measures the string and writes a record
shim     str(): reads the record; if its pointer is in the read-only range (two numbers from the layout
         block), the string kept for that address; otherwise read on use
```

- Speed: 25.9 and 22.5 ns.
  The `strlen` and the record in C and the record read in the shim are still paid; only the string building is saved.
- Everything else is B's (the same symbols, the same cache), so it has B's risks for about a third of B's gain.
  Its one advantage is that `cd_tmp(p, len)` of read-only bytes could be cached too.

## Option D: a checked cache for every runtime string

```
author   as B
build    as A
shim     str(): reads the record; looks the pointer up in a Map of {length, a copy of the bytes, the string};
         compares the bytes in memory with the copy; equal: the kept string; else read, and replace the entry
```

- Speed: 40.7 ns for the ternary and 34.1 for the helper, a gain of 12% to 16%, because comparing the bytes costs nearly what building the string does.
  A buffer shown again unchanged: 29.3 against 37.9 ns at 5 bytes, 42.8 against 58.8 at 12.
  A buffer that changes before every call: 73.8 against 33.9 ns at 5 bytes, 96.0 against 57.0 at 12.
- It needs nothing from the linker and is correct for every pointer.
- It makes the common runtime case (a formatted number, a typed name, a label that is new each time) 70% to 120% slower, and it keeps a copy and a string per address, stack and heap addresses included, so it needs a size limit (the prototype clears at 4,096 entries).
- Rejected.

## Option E: an unchecked cache by address

Measured only to see what soundness costs: 25.0 and 21.6 ns.
It is slower than B (it still pays the record) and wrong for any reused buffer, so it has nothing to offer.

## Recommendation

Take option B with the direct-mapped cache.
Plain C then costs 8.4 to 8.7 ns a call, within 2 ns of a hand-written `cd_s` slot, and the author never writes `cd_s` for speed again.
`cd_s` stays in the header as the thing the macros use, and nobody needs to call it.
Direct literals keep their slots: they are still 1 to 2 ns faster, and they are the only strings that leave the module for the page's table, which is where the page bytes are saved.

What it costs: 120 bytes of brotli in the shim, 20 bytes in the module, and the strings written inside a ternary or passed through a helper stay in the module instead of the table (4 to 130 bytes of brotli per lesson).
What it depends on: wasm-ld from LLVM 23 or later, which cdom.mk's toolchain check requires.

If it is taken, the work is small:

1. `cdom.c`: the range test in `cd_tmpz`, with the two linker symbols.
2. The shim: the cache in `str()`, testing both bounds of the slot range; the plain-object form for the ES5 tier; regenerate the minified shim.
3. Tests, each mutation checked: a ternary, a helper, a `const` table and a `static const char[]` come back right and are read once (count the reads through a test hook); a mutable `static`, a stack buffer and heap bytes at a reused address are read every time and show their new text; two strings whose addresses collide in the direct-mapped cache; a link test that the range is not empty; the legacy run.
   Mutants: the range test widened to `__data_end` (the mutable `static` shows old text), the key compare dropped from the direct-mapped cache (the colliding string is wrong), the two symbols swapped.
4. The lessons, the starter and the READMEs: plain C, `cd_s` no longer taught.
5. Measure the lessons and the keyed benchmark again; the benchmark has no such strings, so its prediction is "within the noise".

## How it was measured

- `bench/plainc/micro.c` through this branch's real `src/cdom.c`, built twice (`-DCDOM_RO=0` and `1`) by `bench/plainc/build.sh`.
- `bench/plainc/micro.html` gives the module imports that only resolve the string, one resolver per option, with the shim's own `ascii` and `readString` copied in.
  Every resolver's strings for one pass of each test are compared with the first resolver's, and all matched.
- `node bench/plainc/micro.mjs` runs one configuration per fresh navigation in one headless Chrome 157: warm-up, a sample size calibrated to 10 ms, then the median of 21 samples.
  The page is served cross-origin isolated, so the clock has 5 us resolution.
- Raw rows: `bench/plainc/results-1.tsv` (all tests) and `results-2.tsv` (a second pass of the literal, ternary and helper tests, with the direct-mapped cache added).
  The two passes differ by 0.6 ns at most on any row, which is the noise band.
- The tests: `lit8` (eight direct literals), `terns8` (eight ternaries with `cd_s`), `tern8` (the same in plain C), `help8` (eight calls of a helper that is never inlined), `buf1` (one mutable buffer, unchanged), `bufnew` (rewritten before every call).
- The lesson sizes are `brotli -q 11` of each `build/index.html`, before and after the lessons were rewritten in plain C with the prototype shim.

Source for the linker symbols: [lld, WebAssembly: Add new __rodata_start/__rodata_end symbols (PR 172102)](https://lists.llvm.org/pipermail/llvm-commits/Week-of-Mon-20251208/1798835.html).
