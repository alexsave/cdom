# Strings: measured in Chrome

Status: measurements, 2026-11-17, branch `strings-bench` (from `tutorial` at e35056c).
The design under test is docs/strings-design.md.
Everything here was measured in headless Chrome 157 on this Mac (8 cores), serially, with no other benchmark running.

## Predictions

Written before any measurement, 2026-11-17 18:07 EST.
Each has a falsifier; the verdicts are in the tables below.

Literals, `cd_text("Hello")`, per call, the string resolved only (no DOM call):

- L1. A (today, `CD_LIT`, warm) costs 5 to 9 ns per call; B costs the same within 1.5 ns. Falsified if B is more than 2 ns faster or slower than A.
- L2. C has the same per-call cost as B (the same lookup). Falsified if they differ by more than the noise.
- L3. D (address as a `Map` key) costs 5 to 12 ns more than B. Falsified if the gap is under 3 ns or over 20 ns.
- L4. E (dense ids, `lits[id]`) is within 0.5 ns of B. Falsified if it is more than 1 ns faster.
- L5. With the DOM call (createTextNode and append), every design is within 3% of A. Falsified if any differs by more than 3% beyond the noise.

Runtime strings, `cd_text(name)`, per call, resolved only:

- R1. B (read on use: a char loop up to 12 bytes, TextDecoder above) beats today's `cd_tmp` batch path at every length, by at least 20%, because it saves the copy in C, the UTF-16 count and the second crossing. Falsified at any length where B is not at least 20% faster.
- R2. Today's `cd_intern` of a string already in the table costs about 1 ns per byte for the hash and the compare, so at 200 bytes it is slower than B. Falsified if `cd_intern` at 200 bytes beats B.
- R3. The char loop beats TextDecoder up to 16 to 24 ASCII bytes in Chrome (the design set 12 from Node). Falsified if the crossover is below 10 or above 40 bytes.
- R4. `String.fromCharCode.apply` over a subarray beats the char loop above 8 bytes. Falsified if it loses at 12 and 30 bytes.
- R5. Non-ASCII strings cost B about one TextDecoder call (60 to 120 ns) at every length. Falsified if B's non-ASCII cost is under 40 ns or over 200 ns at 9 to 30 bytes.

Startup (navigation to first paint, and to `cd_main` returning; median of 15 fresh loads each, interleaved):

- S1. tutorial/01-hello and the keyed entry: B and C first paint within the noise of A (expected noise about 2 to 5 ms). Falsified if either is more than 5 ms worse than A beyond the noise (that is also the design's P4).
- S2. The synthetic page with 2,000 literal sites: `cd_main` (with B's table install) is 0.2 to 1.0 ms faster in B than in A, which hashes, copies and decodes every literal on first use. Falsified if B is not faster, or by more than 2 ms.
- S3. C's decode at load for 2,000 slots costs 0.1 to 0.5 ms more than B's install. Falsified if C is not slower than B, or more than 1 ms slower.
- S4 (P4). B's table install for the keyed entry and lesson sizes (under 300 strings) is under 0.05 ms. Falsified over 0.2 ms.

Memory:

- M1. Initial wasm memory: the keyed entry drops from 9 pages (A) by 4 to 5 pages in B (sbytes 256 KB and smeta 48 KB go). Falsified if it drops by less than 4.
- M2 (P6). The harness's run-clear memory (25_, five create-and-clear cycles): A about 0.80 MB, B 0.72 MB or less. Falsified at 0.78 MB or more.
- M3. JS heap by CDP after five create-and-clear cycles, after a GC: B is 0.1 to 0.3 MB under A (A's interned label table of up to 3,575 labels). Falsified if the gap is under 0.05 MB.
- M4. Touched wasm memory after five cycles: B is 50 to 150 KB under A (A's pool, hash table and the batch buffers it wrote). Falsified if under 20 KB.

Whole app, js-framework-benchmark, cdom entry, A against B (P5, P8):

- W1. Every operation within 3% of A in total time; create 10k script within 1.0 ms of A either way (B reads 10,000 labels at about 30 ns where A hashed, copied and batch-decoded them). Falsified if any operation is more than 5% worse beyond the noise, or create 10k script more than 1.5 ms worse.
- W2 (P8). Without templates, create 1k script within 3% of A. Falsified if more than 3% worse beyond the noise.

The table's scale (forms a to e, at 10, 1,000, 10,000 and 100,000 unique literals):

- T1. Install cost grows linearly: (a) split plus map about 50 ns per string, so about 5 ms at 100,000 and under 0.01 ms at 10. Falsified if (a) at 100,000 is under 1 ms or over 20 ms.
- T2. (b) a plain array literal costs about the same in parse as (a) costs in parse plus split, within 30%. Falsified if either is more than 2 times the other at 10,000 or more.
- T3. (c) one string kept whole, sliced lazily, has the cheapest install (under 10% of (a) at 100,000) and the smallest heap until the strings are used. Falsified if its install plus parse is not the lowest at 100,000.
- T4. (d) dense ids save the map: about 1 to 2 bytes of brotli per slot, and 10% to 30% of (a)'s install time. Falsified if the saving is under 0.5 bytes per slot.
- T5. A table with some non-ASCII strings makes the joined string of (a) and (c) two-byte, so its heap is up to twice the one-byte size; (b) keeps each ASCII string one-byte. Falsified if (b)'s heap is not lower than (a)'s for a mixed table.

## Method

(Filled in below as the work goes.)
