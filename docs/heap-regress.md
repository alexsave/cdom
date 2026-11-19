# The two regressions of PR #43 (the heap's large blocks)

PR #43 (docs/file-read-design.md) left two costs it did not predict.

- 22_run-memory is 3 to 4% higher than main before it (b59e476): 1.75 and 1.79 MB against 1.83 and 1.84.
- The keyed bench app at -Oz grew from 10,198 to 11,157 bytes (+959), where 200 to 500 was predicted; a hello world grew 470.

The base is b59e476 (main before #43) and the branch is 7279030 (main after it), both built the same way.

## Predictions (written before any measurement)

### 22_run-memory

jfb's memory benchmarks read `performance.measureUserAgentSpecificMemory()` after a forced GC, and that counts the wasm memory's ArrayBuffer.
The differences (0.08 and 0.05 MB) are about one wasm page (65,536 bytes is 0.0625 MB).

- M1: after a fresh load and one click on #run, the branch's wasm memory is larger than the base's by 1 page, at most 2.
  Falsifier: the same page count on both, which would put the cost in the JS heap (the shim is 559 bytes larger) and not in the allocator.
- M2: the cause is the doc's guess: a large block freed below the top serves only large requests, so small blocks bump past it.
  At the end of the click, the free large runs below the top hold at least 64 KiB on the branch.
  Falsifier: the free runs hold under 64 KiB at the peak, or a heap that lets small requests take from the runs does not lower the page count.
- M3: a fix that serves a small request from a free large run (split off its front, the class's size) when its class list is empty and the run list is not, brings the page count back to the base's or under it, and leaves every case of tools/alloc.mjs (S1 to S8, H, P12, P24) at its page count.
  Falsifier: the fix changes any alloc.mjs case by more than 2 pages, or the click still takes more pages than the base.

### The keyed app's size

- Z1: the hello world's +470 is the three new functions (heap_big, heap_big_free, heap_grow_top) and the compare in malloc and free, as the doc says.
- Z2: most of the keyed app's further +489 is duplicated code, not new logic: heap_realloc_or is always_inline and now inlined into both realloc and cd_realloc_try, each with a full copy of heap_alloc_or (the small path and the call to heap_big), where before realloc called malloc.
  Prediction: at least 300 of the 489 bytes are in realloc and cd_realloc_try, and making the shared body one function (not inlined) wins back at least 250 bytes.
  Falsifier: realloc and cd_realloc_try together grew by under 200 bytes, or un-inlining wins back under 150.
- Z3: the frees added to the answer exports (cd_encoded, cd_net_msg, cd_net_http, cd_store_got, cd_url_changed) are a call each, under 10 bytes apiece, so under 50 in all.
  Falsifier: they account for over 100 bytes.

### Predictions added before the CPU pass (after the size changes below)

- C1: the changes to the allocator (the bump shared by small and large blocks, with one more store; the grow out of line) move the keyed CPU benchmarks by less than 2% in median against main (7279030), inside the spread.
  Falsifier: a median slower by 2% or more, outside the spread of both sides.

## Results

### What 22_run-memory counts

tools/heapregress_probe.mjs loads an entry, clicks #run once as the harness does, and reads cdom's heap through cd_heap_probe (results/heapregress/probe.c) and the wasm memory's size.

| entry | pages before the click | pages after | heap_ptr after | free small blocks | free large runs |
| --- | ---: | ---: | ---: | ---: | ---: |
| base (b59e476) | 3 | 6 | 359,696 | 0 | 0 |
| main (7279030) | 3 | 5 | 298,256 | 0 | 0 |

- M1 is refuted: main's wasm memory after the click is 1 page smaller than the base's, not larger.
- M2 is refuted: nothing is free at the end of the click, small or large; the large-block rules can only have lowered the heap (the arena's 64 KiB is a 68 KiB block now, not a 128 KiB class).
- And `performance.measureUserAgentSpecificMemory()` does not count wasm memory at all: growing the memory by 16 pages (1 MiB) after the measure and measuring again moved it by 0.5 KB on the base and 6 KB on main, noise of the measure (GROW=16 in the probe).
  So no change to the allocator can move 22_run-memory, and the cause is in the JS heap or the DOM.

### 22_run-memory, paired, in one session

tools/pair.sh ran 22_run-memory for six entries in one pass (TAG=hr22, 15 iterations each, every start gated on a load under 4, Chrome 157.0.8103.79): the keyed entry of the base and of main, the same two with cd_heap_probe linked, and the two shims crossed (main's C with the base's shim, the base's C with main's shim), to split a JS cost from a C one.

| entry | median MB | q1 to q3 |
| --- | ---: | --- |
| base (b59e476) | 1.841 | 1.821 to 1.843 |
| main (7279030) | 1.843 | 1.840 to 1.843 |
| base, probe | 1.842 | 1.840 to 1.844 |
| main, probe | 1.842 | 1.807 to 1.844 |
| main's C, the base's shim | 1.842 | 1.838 to 1.843 |
| the base's C, main's shim | 1.844 | 1.843 to 1.844 |

The six medians are within 0.003 MB (0.2%) of each other: the 3 to 4% of #43's table does not reproduce.
#43's base read 1.75 and 1.79 MB; the base reads 1.84 here, the same as main did then and does now, so it was the base side of that table that was low, not main that grew.
Every entry has a low tail (minimums 1.73 to 1.79), iterations where the measure caught less; a pass whose base landed there more often reads as main costing more.
Those runs' raw logs were not kept (results/pairs is not committed), so why that pass's base sat low cannot be settled now; this pass's logs are in results/heapregress/hr22.
Verdict: there is no 22_run-memory regression to fix, and none of the allocator's could show in it, since the measure does not count wasm memory; main's heap after the click is in fact one page (64 KiB) smaller than the base's.

### Where the keyed app's bytes went

tools/heapregress_build.sh builds each module twice: as shipped, and through the same link with wasm-opt -Oz run by hand with `-g`, so the functions keep their names (clang runs wasm-opt -Oz after the link whenever it is on PATH, which drops the name section; both builds have byte-identical code sections).
The per-function sizes are in results/heapregress/*/funcs.txt.

Keyed app, base (b59e476) to main (7279030), code bytes by function (+952 in all; +959 with the function and type sections):

| function | base | main | change | what it is |
| --- | ---: | ---: | ---: | --- |
| cd_realloc | 88 | 440 | +352 | heap_realloc_or inlined, and in it a full copy of heap_alloc_or (the small path), heap_grow_top and heap_cap; before, realloc called malloc |
| free | 37 | 318 | +281 | heap_big_free, inlined by wasm-opt (one caller; wasm-opt ignores clang's noinline) |
| heap_big | 0 | 270 | +270 | the large path: the run search and split, and its own bump off the top |
| heap_cap | 0 | 37 | +37 | the room of a small or a large block |
| cdom_heap_malloc | 215 | 232 | +17 | the compare that sends a large block to heap_big |
| cd_bytes_buf | 32 | 23 | -9 | cd_alloc instead of cd_realloc |
| cd_encoded | 4 | 8 | +4 | its cd_free |

- Z2 is confirmed: 352 bytes are in realloc, and taking the copy out (realloc calls heap_alloc, cd_realloc_try calls heap_alloc_try) won back 221.
- Z3 is confirmed: the frees in the exports cost 4 bytes in this app (cd_encoded is the only one it links), and cd_bytes_buf got 9 smaller.
- Z1 is refuted in its mechanism: the hello world never frees, so before #43 it had no free at all; cd_encoded's new cd_free links free into every app, and with it heap_big_free.
  Of the hello world's +471 code bytes, cd_encoded is +351 (free and heap_big_free inlined into it, heap_class with them, -42 as its own function) and cd_bytes_buf +162 (cd_alloc's small and large paths where it had realloc's small one).

### What changed here, and what each won (keyed app before cdlits, stripped)

| step | keyed bytes | change |
| --- | ---: | ---: |
| main (7279030) | 11,940 | |
| realloc and cd_realloc_try call heap_alloc and heap_alloc_try, not an inlined copy | 11,719 | -221 |
| the memory grow out of line (heap_more), its compare still inlined | 11,672 | -47 |
| heap_big_free walks the runs once (no second walk to unlink a run under the top) | 11,651 | -21 |
| small and large blocks share one bump off the top (the run search its own function) | 11,572 | -79 |
| memory.grow's -1 read as a refusal (the bug below) | 11,596 | +24 |

Shipped (after cdlits): the keyed app is 10,198 (base), 11,157 (main) and 10,813 now, so +615 over the base where #43 left +959.
The hello world is 2,067, 2,537 and 2,440: +373 where #43 left +470.

What is left over the base is the large-block logic itself, in the keyed app (results/heapregress/keyed-fix/funcs.txt against cdom-hrbase): heap_alloc +152 over the old malloc (the grain rounding, the run search and split, the second header word), free +260 (the address-ordered insert, both merges, the top lowered), cd_realloc +107 (the grow in place at the top, a large block's room), heap_more +56 and heap_cap +37, which are shared.
That is inherent to blocks that cost their size and runs that merge, unless the code is made smaller still by rewriting it.
In the hello world the remaining +373 is free coming into an app that never frees (cd_encoded +339) and cd_bytes_buf's large path (+77): an app that never frees pays for free because cd_encoded, in every module, frees the bytes it was handed.
Moving cd_encoded (and cd_encode_image) out of cdom.c into a file of its own, linked only by an app that encodes, would take those 339 bytes out of such an app; that is an API and build change for the manager to decide, not made here.

### A bug found on the way: memory.grow's refusal read as room

`heap_grow` returned `__builtin_wasm_memory_grow(0, pages) >= 0`; the builtin's type is size_t, so the compare was always true, and a refused grow (-1) read as success.
heap_end then moved past the memory's end: malloc and realloc wrote out of bounds (a trap, by luck, at the header store), and cd_alloc_try and cd_realloc_try never answered 0 in a build without CDOM_HEAP_PAGES_MAX, which every real app is (so cd_file_read's CD_FILE_NO_ROOM could never happen, the shim writing past the memory instead).
It is older than #43 (b59e476 has the same line), and the tests missed it because every _try case ran at CDOM_HEAP_PAGES_MAX, whose check returns before memory.grow is reached.
It showed itself here: with the compare always true, may_fail made no difference, so clang merged heap_alloc and heap_alloc_try into one function and tests/link's _try gate saw a gap of 90.
tools/alloc.mjs now runs the _try and trap cases twice, at the cap and on a module linked with `--max-memory` of 40 pages, where memory.grow itself refuses; before the fix that second run failed 3 of its cases ("threw memory access out of bounds", "grow answered 0", "realloc ... no trap").

### Tests and mutants

- tests/link/realloc_size.c and its gate in tests/link/run.sh: cd_realloc over cd_alloc costs 255 bytes now, 478 with realloc's inlined copy of the allocator back; the gate fails over 350.
- tools/mutate.py: the heap mutants follow the new code, and two are new: grow-refusal-unsigned (the unsigned compare back, caught by the `--max-memory` cases) and realloc-alloc-copy (the inlined copy back, caught by the size gate).
  All 26 heap and answer-bytes mutants are caught (results/heapregress/mutants.txt).
  A third, heap-grow-copies (the grow inlined again), was not caught: it adds 26 bytes to realloc_size.c, inside the gate's margin, so the out-of-line grow is not gated and the mutant was dropped.
- tools/alloc.mjs: 30 of 30 pass, the photo cases among them at 857 pages (12 MP, peak 56.3 MB) and 1,658 pages (24 MP, 108.8 MB) with a second photo at 0, as #43 measured.
