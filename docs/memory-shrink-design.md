# Giving wasm memory back after a big temporary job

A design with predictions, written 2026-11-19 before any measurement.
Nothing here has been built or run.
Every number below is a prediction or a figure quoted from a named source.

## The problem, and how much of it is left

Wasm memory never shrinks: `memory.grow` has no inverse, and no browser ships the memory-control proposal's `memory.discard`.
A page that once needed 100 MB keeps 100 MB of linear memory until it reloads.
Only touched pages cost real memory (V8 reserves the address space and the OS commits a page on first write), but a freed block was touched, so it stays resident.

The motivating case was raceclub resizing a photo in C.
On main (7279030, with the large-block heap of docs/file-read-design.md), round 1's whole-image decode peaked at 56.3 MB for 12 MP and 108.8 MB for 24 MP (P12, P24).

That case is mostly going away.
raceclub PR #10 (head a7270e4, "photo: low-peak streaming JPEG and PNG resize") decodes JPEG row by row with libjpeg-turbo at DCT scale num/8 and streams PNG through tinfl.
Its own figures (Linux x86 VM, unreviewed, and measured on cdom b59e476, the power-of-two heap before the large-block fix):

| input | wasm memory, PR #10 on b59e476 | library's own peak (native counting allocator) |
| --- | --- | --- |
| 12 MP baseline JPEG | 8.7 MB | 2.4 MB plus the file |
| 48 MP baseline JPEG | 21.3 MB | 2.4 MB plus the file |
| 64 MP baseline JPEG (its new cap) | not given | 3.2 MB plus an 18.7 MB file: 21.9 MB |
| 12 MP progressive JPEG | 59.0 MB | 38 MB plus a 3.5 MB file |
| 16 MP progressive JPEG (its cap) | 63.2 MB | 50 MB plus the file |
| 12 MP PNG, a 22.7 MB file | 38.1 MB (the file in a 32 MiB class) | 2.5 MB plus the file |

On main's heap each block costs its size rounded to 4 KiB, so the wasm figures should fall to about the native ones.
Predictions for PR #10's code on main, one photo, the module's own start of about 0.6 MB included (falsifier: off by more than 3 MB):

| case | predicted peak on main | what holds it |
| --- | --- | --- |
| R1: 12 MP baseline JPEG, 3.5 to 5 MB file | 6 to 8 MB | the file |
| R2: 64 MP baseline JPEG | 21 to 23 MB | the file (18.7 MB) |
| R3: 12 MP progressive JPEG | 41 to 44 MB | libjpeg's whole-image coefficient buffer (about 38 MB) |
| R4: 16 MP progressive JPEG | 53 to 57 MB | the coefficient buffer (about 50 MB) |
| R5: 12 MP PNG, 22.7 MB file | 25 to 27 MB | the file |

So after streaming decode lands, what a photo leaves behind is:

- under 10 MB for the common phone photo (baseline JPEG at 12 MP);
- 20 to 27 MB for a large file (64 MP baseline, a big PNG), nearly all of it the file's own bytes;
- 41 to 57 MB for a progressive JPEG, the only case where the decoder itself is big.

Two of those three are the file held whole in C memory by `cd_file_read`.
A chunked read (offset and length, raceclub's noted later ask) would take R2 and R5 down to a few MB for PNG, where tinfl streams naturally.
For JPEG it needs libjpeg's suspending source manager, which is more work.
Progressive JPEG stays big under any read API, since the decoder needs every coefficient before the first row comes out.

### Is shrinking still worth its cost?

Plainly: for memory alone, mostly not.

- The common case leaves under 10 MB, which no shrink is worth.
- The worst remaining case leaves 41 to 57 MB once per page life, only after a progressive JPEG, on a form a host fills in, not on the pages guests browse.
  A mobile tab is killed in the hundreds of MB, not at 50.
- Re-instancing a live app (candidate A) is a permanent constraint on every future shim feature (each must survive a swap), it frees nothing until a garbage collection the page cannot ask for, and it does nothing for the main-thread freeze.

What is still worth doing is the freeze, and the right fix for the freeze returns the memory as a side effect.
PR #10's times, all on the main thread (cdom has no worker): 12 MP baseline 97 ms at 1x and 403 ms at 4x, 12 MP progressive 260 ms and 1,091 ms, 12 MP PNG 462 ms and 2,145 ms.
A job run in a short-lived worker (candidate B in a Worker, below) takes those off the main thread and frees the job's whole memory when the worker ends, with no collection to wait for.
That is the recommendation, if Alex wants the freeze fixed; if he does not, the recommendation is to build nothing now and measure on a real phone first.

## What the browser keeps, and how to see it

`memory.buffer.byteLength` is the size of linear memory, not what the process holds.
Round 1 saw this: 101 MB of linear memory but a 66 MB rise in renderer RSS (raceclub bench/resize/results/RESULTS.md, p12.jpg), because a power-of-two block is only touched up to its use.
So every memory figure in this design is the renderer process's own, and a design that only lowers `byteLength` has not been shown to do anything.

The ways to read it, and what is believed of each before checking:

| meter | what it is | believed status |
| --- | --- | --- |
| `performance.measureUserAgentSpecificMemory()` | per-realm JS memory | needs cross-origin isolation, and README.md (the 22_run-memory note) found it does not count wasm linear memory: not usable |
| `performance.memory` | the JS heap | does not include wasm memory: not usable |
| CDP `SystemInfo.getProcessInfo` (browser target) | every Chrome process's type, pid and CPU time | finds the renderer's pid, the input for the next two |
| `footprint -p <pid>` (macOS) | `phys_footprint`: dirty, compressed and wired pages, what Activity Monitor and jetsam use | the primary meter |
| `ps -o rss= -p <pid>` | resident pages | a cross-check; it misses compressed pages and counts clean shared ones |
| CDP Tracing, category `disabled-by-default-memory-infra`, then `Tracing.requestMemoryDump` | Chrome's own per-process dump, with `private_footprint_bytes` and allocator rows | cross-platform; to check whether a row names wasm memory |
| CDP `HeapProfiler.collectGarbage` | forces a full collection in the target | the "collected" arm of a test, never a product path |

A dedicated worker runs in the renderer's process (its own thread and V8 isolate), so the same pid covers candidate B in a Worker.

M0, the meter's own check, runs before anything else.
A test module touches 100 MB, then a second one grows by 100 MB without writing.
Prediction: the footprint rises 95 to 105 MB for the first and under 5 MB for the second.
Falsifier: either outside its range, in which case the meter is wrong and no later number is read until it is understood.

## The candidates

### A. Re-instance the live app

Between events (C not running), copy the live part of linear memory out, make a fresh instance of the same compiled module, grow its memory to the live size, copy the bytes back, and point the shim at it.
C pointers cannot move, so only free space above the heap's top can go: the heap would need an export answering `heap_ptr`, and one that resets `heap_end` to the new memory's end after the copy (both static in C memory, so copied with the old value).

**(1) The swap's own peak, and whether the process gets anything back.**
The new memory touches only the live bytes, so the swap adds the live size (about 1 to 3 MB for an app like raceclub's form) on top of what the old memory holds, not twice the old `byteLength`.
The old memory is held by the old instance, its exports and any view of its buffer.
The shim reaches C only through its `exports` and `memory` variables, read at each call (timers, fetches, file reads, `deliver` all read `exports.cd_*` when they fire, shim/cdom.js), so swapping those two and calling `views()` drops every reference the shim holds.
The page's glue may hold the `exports` object `start` returned: the swap must refill that same object in place, not make a new one.
A buffer cannot be freed by hand (a wasm memory's buffer cannot be transferred or detached from JS), so the old pages go back only when V8 collects the old `WebAssembly.Memory`.
That memory lives in the old generation, so a full collection is needed; an idle page gets one from V8's memory reducer, believed to run some seconds after activity stops, not on any schedule the page controls.
The data segments are applied to the new memory at instantiation and then written over by the copy, which holds the same bytes or later ones.
The instance must be made with `new WebAssembly.Instance(module, imports)` (synchronous; the module kept from `instantiateStreaming`), because with the async form a timer firing in the gap would run in the old instance and its writes would be lost.

**(2) Hidden mutable state.**
Read from the built modules (`wasm-objdump -x tests/out/host.wasm` and the others in tests/out): one global, `__stack_pointer`, mutable, init 65536; one table, not exported, filled by an element segment; one memory, defined in the module and exported.
Between events the stack is empty, so `__stack_pointer` is 65536 again and carrying it is a check, not a copy.
Other flags add state: raceclub's photo code builds with `-mexception-handling -mllvm -wasm-enable-sjlj`, and threads would add `__tls_base`.
The shim's own state (nodes, listeners, listenMask, timers, sockets, pending requests, the read-only string cache, the layout indexes) is all JS and outlives the swap; everything it keys by is an id or token kept in C memory, which is copied, at the same addresses, because the module is the same.
The tests A would need:
- a link check in tools/cdlits (every link already goes through it): the module has exactly one mutable global, defined, i32, whose init is the stack's top, and its code has no `table.set`, `table.grow`, `table.fill`, `table.init` or `elem.drop`; a new global or a table write fails the build, naming it; mutation check: link a module with a `static _Thread_local` or a second mutable global and see the build stop.
- a unit case that leaves one of each pending answer outstanding (timer, raf, http, file read, encode, store get, websocket message, audio load), trims, then lets them land, and checks C counted every one in the new instance; mutation check: make `timer` capture `const fire = exports.cd_timer` at the call and see the case fail on the timer.
- `__stack_pointer` exported (`-Wl,--export=__stack_pointer`), and the swap refuses (throws) when it is not the stack's top.

**(3) When.**
An explicit plain call: `cd_memory_trim()` in C sets a flag the shim reads; the shim trims in a microtask once `busy` is 0, so the current answer (a file's `cd_file_room` then `cd_file_got`, say) finishes first.
No automatic threshold: the app knows when its big job ended.

The pinning limit: a block allocated during the job that outlives it (an output kept for a preview, a string, a view) caps the trim at that block.
With the file's block allocated first and freed last (`cd_file_got`), an output kept above it pins everything under the output.

### B. A throwaway instance for the job

The big job is its own small module (the photo code, cdom's heap and mini.c, no DOM).
The bytes go in, the result comes out, the instance is dropped, and its memory goes whole.
Nothing of the app's is copied, and nothing pins.
The file's bytes need never enter the app's memory: the shim reads the picked file and writes it straight into the job's.

Two forms:

- **B-same**: a second instance on the main thread.
  The freeze stays, and the job's memory waits for a collection, as in A.
- **B-worker**: the job runs in a dedicated worker that is ended (`worker.terminate()`) when it answers.
  Ending a worker tears down its isolate, so its wasm memory goes at once, with no collection.
  The job leaves the main thread.
  The worker's code is a few lines of JS from a `blob:` URL (so a bundled one-file page keeps working; a CSP then needs `worker-src blob:`); the app's page compiles the job module once and posts the compiled `WebAssembly.Module` to each worker.
  With OffscreenCanvas the worker can encode too, so only the encoded file (about 80 KB for raceclub's 900-pixel JPEG) comes back into C.

A sketch of the C side, explicit calls only (the job module is named by the URL or bundled name the page gives it; no registry):

```c
/* Runs export "run" of the job module at url in a worker of its own: input is a picked file (id, index)
 * or bytes from C memory, args a small block of the app's own layout; the answer is cd_job_fn's
 * (status, bytes, length), the bytes valid until the callback returns. */
void cd_job_file_cb(const char *url, uint32_t id, uint32_t index, const void *args, uint32_t args_len,
                    cd_job_fn fn, void *ctx);
```

The job module's side is one exported function, `uint32_t run(const uint8_t *in, uint32_t in_len, const uint8_t *args, uint32_t args_len)`, answering through an import that hands back its output pointer and length.
On tiers without Worker and WebAssembly together (the legacy tiers), the call answers UNSUPPORTED, as `cd_file_fit` and the IE tiers already do.

### C. The whole app in a worker

C in a worker, DOM operations sent to the main thread.
It fixes the freeze for all C code, not only named jobs.
Its costs: every synchronous read the shim answers today (`cd_get_*`, `hit`, `rect`, `viewport`, the input's value) becomes a blocking round trip through `SharedArrayBuffer` and `Atomics.wait`, which needs cross-origin isolation (COOP and COEP headers, so no file:// page and every third-party resource opted in); every event crosses too.
It does not shrink memory: the app's worker keeps its memory for the page's life exactly as the main thread does.

B-worker is a stepping stone, not a dead end.
The pieces C would reuse: the worker bootstrap from a bundled `blob:`, posting a compiled module, the build of a second module beside the app's, OffscreenCanvas encoding off the main thread.
And B stays needed under C: a big job inside the app's worker would still leave its peak there forever, so even a worker-mode cdom would run big jobs in throwaway workers.
A is the one that has no future under C, and the one that would have to be re-proven against every shim change until then.

### D. Not holding the bytes (added)

The cheapest memory is never taken.
PR #10 already did the large part (the decode buffer).
What remains, apart from progressive JPEG, is the file held whole by `cd_file_read`.
A chunked read would cut R2 and R5 to a few MB with no instance work.
It is listed so it is weighed beside A and B, not designed here; it is a cdom file API change raceclub has noted as a possible ask.

### Rejected without measuring

- `memory.discard` (WebAssembly memory-control proposal): not shipped in any browser.
- Zeroing freed pages so the OS compresses them: macOS compresses only under pressure, and the pages still count; no effect on Linux or Android.
- A second memory (multi-memory) for big buffers: clang's C has no way to put a pointer in a second memory.
- The browser path (`cd_file_fit`, branch file-fit): zero wasm memory and 22 ms of main thread, but Alex chose the C path (raceclub GAPS.md 25), so it stays parked.

## Predictions

Workload: R3 (12 MP progressive JPEG through PR #10's code, or until that lands in cdom, a synthetic job that touches 42 MB and frees it), in an app whose live heap is about 1 MB.
"Held after" is the renderer's footprint after the job, less its footprint before, read by M0's meter.
Each row's falsifier follows it.

| candidate | peak footprint rise during the job | held after, collection forced | held after, no forced collection, 60 s idle | main-thread time added | shim bytes added (minified, brotli) | wasm bytes added |
| --- | --- | --- | --- | --- | --- | --- |
| nothing (today) | 41 to 45 MB | 41 to 45 MB | 41 to 45 MB | 0 | 0 | 0 |
| A, re-instance | 41 to 45 MB, plus 1 to 3 MB at the swap | 0 to 3 MB | 0 to 3 MB, by the memory reducer | 1 to 5 ms (a 1 to 3 MB copy and a synchronous instance) | 300 to 500, 150 to 250 | 60 to 150 per app |
| B-same | 41 to 45 MB | 0 to 2 MB | 0 to 2 MB, as A | under 3 ms more than the job (instance, copying the file in) | 600 to 1,000, 300 to 500 | the job module, 30 to 40 KB brotli, moves out of the app's |
| B-worker | 43 to 50 MB (the job plus 2 to 5 MB for the worker's isolate) | 0 to 2 MB | 0 to 2 MB within 1 s of the answer, with no collection | the job's 260 ms (1x) drops to under 10 ms; wall time grows 10 to 30 ms (starting the worker) | 1,200 to 2,000, 500 to 900 | as B-same, plus about 1 KB brotli for the job's own heap and libc |

Falsifiers:

- nothing: a "held after" under 35 MB means the footprint does not keep freed wasm pages, and the whole problem is smaller than this design assumes.
- A: held after (forced) over 5 MB; or the no-forced arm still over 30 MB after 60 s idle (then A frees nothing in practice without the page forcing a collection, which it cannot); or a swap over 10 ms.
- B-same: held after (forced) over 4 MB.
- B-worker: held after over 4 MB at 1 s after the answer without a forced collection; or a main-thread task over 20 ms anywhere in the job; or wall time grown by more than 50 ms.
- Any candidate: a shim size outside its range by more than 30%.

For R5 (the 22.7 MB PNG) every rise and every "held" figure for "nothing" is 25 to 29 MB, and the rest of each row is the same.
For R1 every row's rise is under 10 MB, which is why R1 is not a reason to build anything.

## The measurement plan (not run: the CPU is someone else's until the manager says)

All scripts are committed with the results (nothing durable in /tmp), and every run is load-gated as tools/pair.sh does (LOADMAX=4).

1. **M0, the meter.**
   `tests/shrink.c`: exports `touch(mb)` (cd_alloc, write every byte, keep it), `grow_only(mb)` (`__builtin_wasm_memory_grow` and nothing written), `release()` (free all).
   `tools/shrink.mjs`: launches Chrome through tools/cdp.mjs, opens the page, finds the renderer pid with `SystemInfo.getProcessInfo` on the browser target (one tab, so one renderer; if a spare renderer shows, the one whose CPU time grows during a 200 ms spin, as raceclub's harness did), and samples `footprint -p <pid>` and `ps -o rss= -p <pid>` every 100 ms into a TSV, plus one `Tracing.requestMemoryDump` per phase.
   Five runs; the prediction is under "What the browser keeps".
2. **M1, today's cost.**
   The same page: `touch(42)`, `release()`, then 60 s idle sampled, then `HeapProfiler.collectGarbage` and 10 s more.
   This is the "nothing" row, and it alone may end the work (its falsifier).
   Then the same with R3 and R5 through PR #10's photo code, once that is in cdom, built with cdom.mk at the app's flags.
3. **M2, A as a prototype.**
   A branch-only shim patch (`trim()` behind a test option): `cd_heap_top`, `cd_heap_reset_end` exports in src/cdom.c, the synchronous re-instance, the swap timed with `performance.now()` around it.
   Arms: forced collection, and none with 60 s idle.
   Before it is trusted: the unit case with every pending answer across a trim, mutation-checked as above.
4. **M3, B-same and B-worker as prototypes.**
   A job module built from tests/shrink_job.c (touch N MB, answer 64 KB) and, later, from the photo code.
   The page posts the compiled module to a `blob:` worker, sends the bytes (transferred), receives the answer, terminates.
   Main-thread time is read from CDP `Tracing` (`devtools.timeline`, the longest task on the main thread during the job), wall time from `performance.now()` in the page.
5. **Sizes**: shim/cdom.min.js and brotli -q 11 before and after each prototype, and the app and job modules at -Oz.

Each of M1 to M3 runs five times per arm, arms interleaved, results in docs/memory-shrink-results.tsv with the load before each run and the Chrome version.

## Recommendation

1. Do not build A.
   After streaming decode it would buy back 20 to 55 MB only after rare inputs, only when V8 gets round to collecting, at the price of every future shim feature having to survive a swap; and it leaves the freeze.
2. Run M0 and M1 first, cheaply, to learn whether held wasm pages really cost the footprint the predictions say.
3. If Alex wants the main thread free during a photo (260 ms at 1x and over a second at 4x for a progressive JPEG today), build B-worker as `cd_job`: it fixes the freeze for the jobs that have it, gives their memory back at once and deterministically, and is the first piece of any later worker mode (C).
4. If he does not, build nothing now; a chunked `cd_file_read` (D) is the cheapest further cut, for PNG and very large baseline JPEGs, and is raceclub's call to ask for.

## Open questions for Alex

1. Is a page holding 20 to 55 MB after one large or progressive photo, on the host's form only, acceptable as it is?
2. Is the main-thread freeze during a photo (0.1 to 0.5 s at 1x, 0.4 to 2.1 s at 4x) worth a job worker in cdom?
3. If so: a second wasm module per job kind in the page, a `blob:` worker (a CSP then needs `worker-src blob:`), and the legacy tiers answering UNSUPPORTED: all acceptable?
4. Whole-app worker mode (C): park it, given it needs COOP and COEP and does not help memory?
