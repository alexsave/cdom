# cd_job: a big C job in a worker that ends

Built from candidate B-worker of docs/memory-shrink-design.md (copied here unchanged).
Alex decided to build it, now.
Candidate A (re-instancing) is not built.
This file holds the predictions first, then (after the measurements) the results.
The predictions are fixed at the commit that adds this file; they are never edited after a measurement.

## What is built

- `src/cdom_job.h` and `src/cdom_job.c` (module `job`): `cd_job_cb` and `cd_job_file_cb`, answered to `cd_job_fn(ctx, status, bytes, len)`.
- `src/cdom_job_mod.h` and `src/cdom_job_mod.c`: the job module's side, `run` and one answer import, over cdom's heap and mini.c, no DOM.
- The shim: `job_run`, a blob: worker, the module compiled once and posted, the input transferred, the worker terminated at its answer.
- `cdom.mk`: `APP_JOBS` builds job modules beside the app module; `tools/cdpage` bundles them into a one-file page.

## Environment of every number below

Linux x86-64 VM, not Alex's arm64 Mac.
The numbers are indicative; the cdom manager re-measures on the Mac.
CPU, cores, Chrome and clang versions are stated beside the results.
Meter: the renderer process's `/proc/<pid>/smaps_rollup`, Private_Dirty plus Swap (primary), VmRSS (cross-check).
The renderer pid comes from CDP `SystemInfo.getProcessInfo` on the browser target; if more than one renderer shows, the one whose CPU time grows during a 200 ms spin.

## Predictions

All taken from the design's own table and plan, restated here with the Linux meter.
The workload is a synthetic job that touches 42 MB of its own memory and answers 64 KB (tests/shrink_job.c), in an app whose live heap is about 1 MB.
"Rise" is the renderer's Private_Dirty plus Swap above its value just before the job.
Each prediction has its falsifier after it.

### M0, the meter check (run first; if it fails, stop and report)

| id | prediction | falsifier |
| --- | --- | --- |
| P0a | touching 100 MB (written) raises the meter 95 to 105 MB | outside 95 to 105 MB |
| P0b | growing memory by 100 MB with no write raises it under 5 MB | 5 MB or more |

### M1, today's cost (the "nothing" row): touch 42 MB, release, 60 s idle, then a forced collection

| id | prediction | falsifier |
| --- | --- | --- |
| P1a | peak rise during the touch 41 to 45 MB | outside 41 to 45 MB |
| P1b | held after the release and 60 s idle, no forced collection: 41 to 45 MB | under 35 MB (the meter does not keep freed wasm pages and the problem is smaller than assumed) |
| P1c | held after a forced collection (HeapProfiler.collectGarbage) and 10 s more: 41 to 45 MB | under 35 MB |

(P1c is the design's "held after, collection forced" column for the "nothing" row, 41 to 45 MB: a live app's memory is held by its instance, so a collection frees nothing.)

### M3, B-worker (cd_job): the same 42 MB job in a throwaway worker

| id | prediction | falsifier |
| --- | --- | --- |
| P3a | peak rise during the job 43 to 50 MB (the job plus 2 to 5 MB for the worker's isolate) | outside 43 to 50 MB |
| P3b | held after, 1 s after the answer, no forced collection: 0 to 2 MB | over 4 MB |
| P3c | held after a forced collection and 2 s more: 0 to 2 MB | over 4 MB |
| P3d | longest main-thread task during the job: under 10 ms | over 20 ms anywhere in the job |
| P3e | wall time (page's call to the answer) grows 10 to 30 ms over the same job run in the app | grown by more than 50 ms |
| P3f | main-thread time added to the page by the job in the "nothing" arm equals the job's own run time, so P3d's gain is the whole job; the job's run time at 8 passes is 100 to 400 ms | outside that range (then the gain is not the size the design assumed) |

### Sizes

| id | prediction | falsifier |
| --- | --- | --- |
| S1 | shim added, minified: 1,200 to 2,000 bytes; brotli -q 11: 500 to 900 bytes | more than 30% outside either range |
| S2 | an app that does not link `job` is byte-identical in its wasm to main's build of the same source (the heap's `CDOM_HEAP_ONLY` guard compiles nothing different) | any byte differs |
| S3 | the job module (heap, libc, run, no DOM) with the synthetic job: under 2 KB brotli -q 11 at -Oz | 2 KB or more |

## Method

Five runs per arm, arms interleaved (nothing, worker, nothing, worker, ...), each in a fresh Chrome.
Load is checked before each run (tools/pair.sh's LOADMAX=4).
Results in docs/job-worker-results.tsv.

## Results

Measured 2026-11-19 on a Linux x86-64 VM: Intel Xeon (2.10 GHz, model name "Intel(R) Xeon(R) Processor"), 4 cores, kernel 6.18.44, Chrome for Testing 153.0.8010.12 headless, clang 18.1.3 (Ubuntu apt).
Load before each run was 0.07 to 0.40 (LOADMAX 4).
Five runs per arm, a fresh Chrome per run, arms interleaved (odd runs: nothing then worker; even runs: worker then nothing).
Every run found five renderers and picked the one whose CPU time grew in a 200 ms spin.
All rows are in docs/job-worker-results.tsv; `python3 tools/shrink_summary.py` prints medians and ranges.
Numbers are Private_Dirty plus Swap in MiB (VmRSS beside it in the tsv), medians with min to max.
The cdom manager re-measures on the Mac; this VM's per-byte speed and its page-fault cost are not the Mac's.

Before the runs below, two smoke runs of the tools (one M0 run, one M1/M3 run with an 8 s idle) went to a scratch file to check that the tools parse the trace and the meter.
Their numbers are not used and were not different in kind.

### M0, the meter

| id | prediction | measured | verdict |
| --- | --- | --- | --- |
| P0a | touch 100 MB: 95 to 105 MB | 99.88 (99.88 to 99.88); VmRSS 104.09 (103.64 to 104.28) | confirmed |
| P0b | grow 100 MB, no write: under 5 MB | 0.11 (0.10 to 0.11); VmRSS 0.11 (0.10 to 0.16) | confirmed |

The meter works, so M1 and M3 were read.

### M1, today's cost (arm "nothing": the 42 MB job in the app's memory, then freed)

| id | prediction | measured | verdict |
| --- | --- | --- | --- |
| P1a | peak rise 41 to 45 MB | 42.73 (42.73 to 42.75) | confirmed |
| P1b | held after 60 s idle, no collection: 41 to 45 MB (falsifier: under 35) | 41.76 (41.73 to 41.94) | confirmed |
| P1c | held after a forced collection and 10 s: 41 to 45 MB (falsifier: under 35) | 41.95 (41.92 to 41.97) | confirmed |

Freed wasm pages stay in the footprint: after the release the page holds 42.7 MiB at 1 s, 41.8 at 60 s, and a forced collection changes nothing.
The "nothing" row's falsifier did not fire, so the problem is as large as the design assumed.
VmRSS at 60 s is 48.45 (48.32 to 48.57), above the peak's 45.25, so the cross-check meter does not show the pages going either.

### M3, B-worker (cd_job): the same job in a throwaway worker

| id | prediction | measured | verdict |
| --- | --- | --- | --- |
| P3a | peak rise 43 to 50 MB | 44.97 (44.95 to 45.41) | confirmed |
| P3b | held 1 s after the answer, no collection: 0 to 2 MB (falsifier: over 4) | 2.70 (2.68 to 2.72) | refuted as a range (over 2), falsifier not hit |
| P3c | held after a forced collection and 2 s: 0 to 2 MB (falsifier: over 4) | 1.59 (1.05 to 1.62) | confirmed |
| P3d | longest main-thread task under 10 ms (falsifier: over 20) | 2.49 ms (1.19 to 2.87); the "nothing" arm's is 907.6 ms (874.1 to 951.2) | confirmed |
| P3e | wall time grows 10 to 30 ms over the same job in the app (falsifier: over 50) | cold: 943.0 (889.4 to 975.4) against 906.7 (873.7 to 950.6), medians 36.3 apart; paired per run +16.1 median, from -24.3 to +68.7 | not resolved (see below) |
| P3f | the "nothing" arm's main-thread time equals the job's run time, and that is 100 to 400 ms | longest task 907.6 against the job's own wall 906.7: equal; but the run time is 906.7 ms cold and 674.5 warm | first half confirmed, second half refuted |

P3b: what the worker leaves is 2.70 MiB at 1 s and again at 5 s, against the 42.7 MiB that "nothing" holds.
A 1 MB job (experiment X1 below) leaves 2.32, so about 2.3 to 2.7 MiB is a floor that comes with starting a worker, not the job's memory.
A forced collection takes it to 1.0 to 1.6.

P3e: the 42 MB job's own run-to-run spread (874 to 951 ms, 77 ms) is larger than the cost of the worker, so the two arms cannot be told apart on this job.
The medians differ by 36 ms (outside 10 to 30) and the paired differences by 16 ms (inside), and the warm runs give 670.4 against 674.5 (the worker arm is not slower).
The falsifier (over 50 ms) is neither shown nor ruled out.

P3f: the job takes 907 ms cold here, not the 100 to 400 ms the prediction put at 8 passes.
The first run of a page pays 42 MB of first-touch page faults, which the warm 674 ms does not, but even the warm figure is outside the range.
This VM runs the byte loop slower than the guess behind the range.
It does not change what the main thread gains: all 907 ms come off it, and the worker arm's longest task is 2.5 ms.

### X1, after the fact (not a prediction): the worker's own cost, on a 1 MB job with no passes

Run to resolve P3e, whose 42 MB job was too noisy for it.
Same method, five runs per arm interleaved, MB=1 PASSES=0 IDLE_S=8.

| metric | nothing (in the app) | worker (cd_job) | paired difference |
| --- | --- | --- | --- |
| wall, first call in the page | 3.90 ms (3.60 to 4.70) | 16.60 ms (16.30 to 23.00) | +12.7 median, +11.9 to +19.1 |
| wall, second call | 1.00 ms (0.90 to 1.20) | 8.60 ms (8.30 to 9.70) | +7.6 median, +7.1 to +8.8 |
| longest main-thread task | 4.37 ms | 2.23 ms | |
| held 1 s after | 1.72 MiB | 2.32 MiB | |

So a worker costs 12.7 ms on a page's first job (the module fetched and compiled, the blob URL made, the worker started) and 7.6 ms after that, on this VM.
That is inside P3e's 10 to 30 ms for the first job.

### Sizes

| what | before | after | added |
| --- | --- | --- | --- |
| shim/cdom.min.js, bytes | 18,169 | 19,779 | 1,610 (S1: 1,200 to 2,000, confirmed) |
| shim/cdom.min.js, brotli -q 11 | 6,846 | 7,349 | 503 (S1: 500 to 900, confirmed, at the edge) |
| the job module of tests/shrink_job.c, -Oz, bytes (brotli) | | 1,388 (737) | S3: under 2 KB brotli, confirmed |
| an app that links `job` and calls both forms, -Oz, stripped (brotli) | 2,371 (1,289) | 3,412 (1,729) | 1,041 (440) |
| an app that links `job` and calls neither | 2,371 (1,289) | 2,776 (1,413) | 405 (124): the two exports, as `file` |
| S2: an app that does not link `job` (demo/click.c, tests/out/click.wasm) | sha256 8bd14e05... | identical | confirmed |

The job module is small because cdom's heap and mini.c are all it links and the linker drops the rest of mini.c.
A real job (the image code) brings its own size.

## How it plugs in (the image module, not merged here)

The image module (branch `image`) becomes a job source with one entry:

```c
#include "cdom_job_mod.h"
CD_JOB_ENTRY uint32_t run(const uint8_t *in, uint32_t in_len, const uint8_t *args, uint32_t args_len) {
  /* decode in (the picked file's bytes), resize by args, encode; allocate with cd_alloc_try and return CD_JOB_RUN_NO_ROOM on 0 */
  cd_job_answer(out, out_len);
  return CD_JOB_RUN_OK;
}
```

The app's Makefile adds `APP_JOBS := image`, `APP_JOB_image_SRC := src/image_job.c`, and `APP_MODULES += job`, and the call is `cd_job_file_cb("image.job.wasm", input, 0, &args, sizeof args, on_done, ctx)`.
The photo never enters the app's memory, the decode's peak is the worker's, and both go when the worker ends.
The job's libc is `src/mini.c` plus whatever the image code brings (the `image-libc` branch).
Encoding in the worker through OffscreenCanvas is not in this change: a job answers bytes from its own C.

## The Mac (written before measuring)

Alex's arm64 Mac, headless Chrome, `node tools/shrink.mjs m0 5` and `m1m3 5` with the meter on macOS: the renderer's phys_footprint (tools/footprint.c, proc_pid_rusage; what Activity Monitor calls Memory), resident size beside it.
Measured after the fix that copies the answer in cd_job_answer (which touches nothing a run measures but the 64 KB copy).

- MP0a: touching 100 MB raises the footprint by 95 to 105 MiB; MP0b: growing 100 MB without writing raises it by under 5. Either missing means the meter is wrong on macOS, and nothing below counts.
- MP1b: the nothing arm (42 MB touched in the app, freed) still holds 40 to 46 MiB after 60 s idle, and MP1c after a forced GC: wasm memory never shrinks, on any platform.
- MP3b: the worker arm holds 0 to 4 MiB at 1 s with no GC (Linux: 2.70); falsifier over 6.
- MP3c: the worker arm holds 0 to 3 MiB after a forced GC.
- MP3d: the worker arm's longest main-thread task is under 10 ms; the nothing arm's is the job's whole time.
- MP3e: a warm job (the module compiled, the blob URL made) adds 5 to 30 ms of wall time over the same work on the main thread; falsifier over 50.
- MP3f: the nothing arm's cold wall time is within 10% of the worker's job time, and the warm call is faster than the cold one in both arms (Linux: 906.7 cold, 674.5 warm).
