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
