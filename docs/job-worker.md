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
