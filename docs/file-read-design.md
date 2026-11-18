# cd_file_read, the heap's large blocks, and the encoder's quality

Three changes raceclub asked for, on the branch `file-read` (stacked on `shared-state`, PR #42).

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
