# cd_file_read, the heap's large blocks, and the encoder's quality

Three changes raceclub asked for, on the branch `file-read`.

## The problems

- No way to get a picked file's bytes into C: the shim reads `files.0.name` and `files.0.type` through `cd_get_str`, but nothing reads a file's contents.
- The heap rounds every block up to a power of two and never merges or returns a freed block (src/cdom.c, "memory").
  Raceclub's resize bench (raceclub/bench/resize/results/RESULTS.md, the caveats) saw a 36.3 MB request take a 64 MiB block, and a freed block reused only by a request of its own size class.
- cdom keeps its room for incoming bytes (`cd_bytes_buf`) after the answer returns, at the size of the largest answer so far, so a 20 MB response stays in the heap after its callback.
- `cd_encode_image` encodes JPEG at a quality of 0.92, fixed in the shim.

## Predictions (written before any measurement)

Every number below is a prediction, made from reading the code, before the harness ran.
The harness is tests/alloc_mem.c, run by tools/alloc.mjs in Node, a fresh instance per case; each case answers the `memory.grow` pages it took (64 KiB each).
The module starts at 2 pages with about 65,280 bytes free above `__heap_base` (a 64 KiB stack, then about 250 bytes of data), and that free room is in every figure.
A falsifier for every row: the measured pages differ from the prediction by more than 2 pages, or a "reused" column differs.

| case | what it does | before (pages) | after (pages) |
| --- | --- | --- | --- |
| S1 | 36,578,304 bytes (12 MP as RGB) taken and freed, twice | 1,024 (one 64 MiB block, reused the second time) | 558 (36,581,376 bytes: the size and the header rounded to 4 KiB) |
| S2 | 36.6 MB, then 20 MB, then 36.6 MB, each freed first | 1,536 (64 MiB, then 32 MiB of another class) | 558 (the 20 MB fits in the freed 36.6 MB) |
| S3 | 36.6 MB freed under a small block, then 30 MB | 1,536, the 30 MB not at the 36.6 MB's address | 558, the 30 MB at the 36.6 MB's address |
| S4 | cd_realloc doubling from 1 MiB to 64 MiB | 4,064 (blocks of 2 to 128 MiB, none reused) | 1,024 (the block at the top grows where it is) |
| S5 | two 20 MB blocks freed, then one of 40 MB | 2,048, the 40 MB not where the first was | 610, the 40 MB where the first was (the two freed blocks merged) |
| S6 | 36.6 MB freed at the top, then 100,000 blocks of 200 bytes | 390 | 0 (the freed top goes back to the bump pointer) |
| H | two 20 MB HTTP answers through cd_bytes_buf and cd_net_http, then the app takes 20 MB | 512 after the answers, 1,024 after the app's 20 MB | 305 after the answers, still 305 after the app's 20 MB |

Added later (raceclub's photo path, before it was measured): case P reads a photo with cd_file_read as the shim does (cd_file_room, then cd_file_got), and in the callback, while the file's bytes are live, takes a decode buffer of width x height x 4 and a 900-pixel output of 900 x 675 x 4 (2,430,000 bytes), frees the decode buffer, then the output, then returns (and cdom frees the file's block); then it does the same for a second photo.
The "before" runs the same harness on main's src/cdom.c (b59e476).
Wasm memory never shrinks, so the test that the first photo's room is given back is that the second photo grows the memory by 0 pages.

| case | photo | before: pages for the first, the second, peak memory | after: pages for the first, the second, peak memory |
| --- | --- | --- | --- |
| P12 | 12 MP (4032 x 3024), a 5,000,000-byte file, a 48,771,072-byte decode buffer | 1,216 (8 + 64 + 4 MiB), 0, 79.8 MB (1,218 pages) | 857 (the three sizes rounded to 4 KiB: 56,209,408 bytes), 0, 56.3 MB (859 pages) |
| P24 | 24 MP (5664 x 4248, raceclub's cap), a 10,000,000-byte file, a 96,242,688-byte decode buffer | 2,368 (16 + 128 + 4 MiB), 0, 155.3 MB (2,370 pages) | 1,658 (108,683,264 bytes), 0, 108.8 MB (1,660 pages) |

Falsifier: a page count off by more than 2, or a second photo that grows the memory at all.

For a file read through the new `cd_file_read_cb` in Chrome (tools/file.mjs), with a 20,000,000-byte file read twice and then 20 MB taken by the app:

- The first read grows the memory by 305 to 307 pages (the bytes, plus the module's small blocks).
- The second read grows it by 0 pages, and the app's 20 MB after both by 0 pages: nothing of cdom's holds the bytes after the callback.
- Falsifier: the second read or the app's block grows the memory by more than 2 pages.

The small-object path (the size classes up to 64 KiB) keeps its code but for one compare on the way in and one on free.
Prediction: the keyed bench entries (bench/cdom-keyed, through tools/pair.sh) move by less than their run-to-run spread, and by less than 2% in median.
Falsifier: a median slower by 2% or more, outside the spread of both sides.

Size predictions:

- The shim gains `file_read` (about 30 lines) and a quality argument: shim/cdom.min.js grows by 550 to 900 bytes, and by 200 to 400 bytes brotli.
- The base for both: 17,610 bytes minified and 6,677 brotli (brotli -q 11).
- The picked-image call (`cd_file_fit`) was begun on this branch and moved to the branch `file-fit` before this PR; its prediction (700 to 1,100 bytes minified, 300 to 450 brotli more) goes with it.
- An app that does not link src/cdom_file.c carries none of it: 0 bytes of its module (tests/link checks the exports and imports).
- The large-block code adds 200 to 500 bytes to every module (raw, -Oz), since every app links the heap.

## Results

Every page count below is what tools/alloc.mjs and tools/file.mjs measured; the "before" column ran the same harness on main's src/cdom.c and src/cdom.h (b59e476), the rest of src/ as on this branch.

| case | predicted before | measured before | predicted after | measured after | verdict |
| --- | --- | --- | --- | --- | --- |
| S1 | 1,024 | 1,024 | 558 | 558 | confirmed |
| S2 | 1,536 | 1,536 | 558 | 558 | confirmed |
| S3 | 1,536, not reused | 1,536, not reused | 558, reused | 558, reused | confirmed |
| S4 | 4,064 | 4,064 | 1,024 | 1,024 | confirmed |
| S5 | 2,048, not reused | 2,048, not reused | 610, reused | 610, reused | confirmed |
| S6 | 390 | 390 | 0 | 0 | confirmed |
| H | 512, then 1,024 | 512, then 1,024 | 305, then 305 | 305, then 305 | confirmed |
| P12 | 1,216, 0, 79.8 MB | 1,216, 0, 79.8 MB (79,822,848 bytes) | 857, 0, 56.3 MB | 857, 0, 56.3 MB (56,295,424 bytes) | confirmed |
| P24 | 2,368, 0, 155.3 MB | 2,368, 0, 155.3 MB (155,320,320 bytes) | 1,658, 0, 108.8 MB | 1,658, 0, 108.8 MB (108,789,760 bytes) | confirmed |

S7 (three freed neighbours merging), S8 (a free run under the top going back with it) and the 20,000-step seeded churn were added after the predictions, as tests of the merge rules, not as hypotheses.
In P12 and P24 the second photo grows the memory by 0 pages both before and after: the old size classes also reuse a block of the same class.
What the fix changes for a photo is the first photo's cost: 23.5 MB less at 12 MP and 46.5 MB less at 24 MP, since each block now costs its size.

The file read in Chrome (tools/file.mjs): the first 20 MB read grew the memory by 305 pages, the second by 0, and the app's 20 MB after both by 0, as predicted.

Sizes:

- shim/cdom.min.js: 17,610 to 18,169 bytes (+559, predicted 550 to 900: confirmed, at the bottom), brotli 6,677 to 6,846 (+169, predicted 200 to 400: refuted, below the range; `file_read` compresses better than guessed).
- A hello world at -Oz (one div of text): 2,067 to 2,537 bytes (+470, predicted 200 to 500: confirmed). That is the large-block code (heap_big, heap_big_free, heap_grow_top), which every app links with the heap.
- An app that does not link src/cdom_file.c has none of its exports (tests/link checks it through cdom.mk).
- The keyed bench app at -Oz: 10,198 to 11,157 bytes (+959; the hello world's 470 plus realloc's large path and the frees in the answer exports). For an app that grows arrays with cd_realloc the prediction of 200 to 500 bytes is refuted.

## The small-object path: timed

The keyed bench entry built from main (b59e476, entry cdom-frbase) and from this branch (cdom-fr), both with bench/build.sh, run by tools/pair.sh with LOADMAX=4 (every start gated on a 1-minute load under 4), 15 iterations, interleaved benchmark by benchmark, in two passes with PASS=0 and PASS=1 so neither entry always runs first.
tools/smoke.mjs passed on both entries (wasm and LEGACY=1), and tools/domcheck.mjs found their DOM identical over 22 snapshots.
The load before each start is in the last column (base, branch).
Other jobs on the machine pushed the load to 7.4 during pass 0's 04_select1k (the load after it), so that row of pass 0 is noise; the machine was then cleared for the rest.

Pass 0 (PASS=0):
| benchmark | metric | base median (q1 to q3) | branch median (q1 to q3) | branch/base | loads at start |
| --- | --- | ---: | ---: | ---: | --- |
| 01_run1k | total | 30.20 (29.80 to 30.70) | 30.20 (29.50 to 30.70) | 1.000 | 3.65, 2.94 |
| 02_replace1k | total | 32.40 (31.70 to 32.90) | 31.90 (31.40 to 33.00) | 0.985 | 3.54, 3.04 |
| 03_update10th1k_x16 | total | 19.00 (18.30 to 21.50) | 17.40 (16.70 to 18.90) | 0.916 | 3.57, 3.68 |
| 04_select1k | total | 4.20 (4.00 to 4.90) | 5.50 (5.10 to 7.70) | 1.310 | 3.74, 3.12 |
| 05_swap1k | total | 18.50 (18.20 to 19.00) | 18.30 (17.90 to 18.50) | 0.989 | 2.62, 2.55 |
| 06_remove-one-1k | total | 15.90 (15.40 to 16.80) | 15.90 (15.50 to 17.10) | 1.000 | 2.48, 2.44 |
| 07_create10k | total | 308.20 (305.20 to 310.00) | 306.50 (304.90 to 308.90) | 0.994 | 2.18, 2.02 |
| 08_create1k-after1k_x2 | total | 34.60 (34.20 to 35.00) | 34.70 (34.10 to 35.70) | 1.003 | 3.66, 2.28 |
| 09_clear1k_x8 | total | 13.30 (13.10 to 13.60) | 13.40 (13.10 to 13.60) | 1.008 | 2.76, 2.30 |
| 21_ready-memory | DEFAULT | 0.69 (0.69 to 0.69) | 0.69 (0.69 to 0.69) | 1.006 | 2.04, 2.14 |
| 22_run-memory | DEFAULT | 1.75 (1.75 to 1.75) | 1.83 (1.83 to 1.83) | 1.043 | 1.67, 1.82 |
| 25_run-clear-memory | DEFAULT | 0.79 (0.79 to 0.79) | 0.79 (0.79 to 0.79) | 1.008 | 2.87, 2.47 |

Pass 1 (PASS=1):
| benchmark | metric | base median (q1 to q3) | branch median (q1 to q3) | branch/base | loads at start |
| --- | --- | ---: | ---: | ---: | --- |
| 01_run1k | total | 30.00 (29.60 to 30.40) | 30.10 (29.90 to 30.30) | 1.003 | 3.74, 3.98 |
| 02_replace1k | total | 31.40 (31.20 to 31.90) | 31.60 (31.30 to 31.80) | 1.006 | 3.11, 2.85 |
| 03_update10th1k_x16 | total | 15.90 (15.30 to 16.10) | 15.80 (15.50 to 16.00) | 0.994 | 2.51, 2.85 |
| 04_select1k | total | 4.00 (3.90 to 4.70) | 4.30 (3.90 to 4.60) | 1.075 | 2.73, 2.73 |
| 05_swap1k | total | 17.90 (17.50 to 18.40) | 18.20 (17.70 to 18.60) | 1.017 | 2.05, 2.08 |
| 06_remove-one-1k | total | 15.10 (14.80 to 16.00) | 15.10 (14.80 to 15.30) | 1.000 | 2.29, 2.41 |
| 07_create10k | total | 301.50 (298.30 to 302.60) | 302.30 (300.00 to 303.90) | 1.003 | 1.97, 2.32 |
| 08_create1k-after1k_x2 | total | 34.50 (34.10 to 34.80) | 34.40 (34.10 to 34.70) | 0.997 | 2.24, 2.85 |
| 09_clear1k_x8 | total | 13.00 (12.80 to 13.30) | 13.00 (12.80 to 13.40) | 1.000 | 1.94, 2.37 |
| 21_ready-memory | DEFAULT | 0.69 (0.69 to 0.69) | 0.68 (0.68 to 0.68) | 0.989 | 1.96, 2.08 |
| 22_run-memory | DEFAULT | 1.79 (1.79 to 1.79) | 1.84 (1.84 to 1.84) | 1.028 | 1.64, 1.78 |
| 25_run-clear-memory | DEFAULT | 0.78 (0.78 to 0.78) | 0.79 (0.79 to 0.79) | 1.002 | 1.69, 1.78 |

tools/ci.mjs, branch over base, weighted geometric mean of the CPU benchmarks with a bootstrap 95% interval: pass 0 0.998 (0.975 to 1.016), pass 1 1.004 (0.995 to 1.016).
The prediction (a median change under 2%, inside the spread) is confirmed for the CPU benchmarks.
22_run-memory is 3 to 4% higher on the branch in both passes (1.75 and 1.79 MB against 1.83 and 1.84): not predicted. A likely cause, not yet measured: a large block freed below the top serves only large requests, so small blocks then bump past it where the size classes would have reused its class. That is a follow-up, not part of this PR.
