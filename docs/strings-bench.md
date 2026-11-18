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

Added 2026-11-17 18:22 EST, before the table runs, for the two forms found while writing them ((e) dense ids as an array literal, (f) dense ids kept whole and sliced lazily):

- T6. (e) has the lowest table-script-plus-install time at every N, because the parser builds the table and no JS runs. Falsified if any form beats it by more than the noise at 10,000 or more.
- T7. (f) has the lowest heap after the install at 10,000 and 100,000. Falsified if any form is lower.

Added 2026-11-17 18:23 EST, after the micro and before any whole-app run: the micro found that in Chrome a TextDecoder call costs about 120 ns whatever the length, so the design's read (TextDecoder above 12 bytes) pays that for most of the benchmark's labels (10 to 25 bytes), while today's path decodes a create's 1,000 fresh labels in one call.

- W1 is now expected to fail on create: B's create 1k and create 10k script 5% to 15% worse than A (about 0.07 ms per 1,000 labels). Falsified if B is within the noise of A on both.
- W3. sb-bx, B with the faster read (fromCharCode with the bytes as arguments up to 32 ASCII bytes, the view checked by its length), is within the noise of A or better on every operation. Falsified if any operation is more than 3% worse beyond the noise.

Added 2026-11-17 19:01 EST, after the first table run and before measuring it: that run showed (d) is the fastest to install and (e) the leanest, because splitting a string that holds any non-ASCII text makes every piece two-byte.

- T8. (g), dense ids numbered ASCII first and carried as two joined strings (the ASCII ones, then the rest), installs within 20% of (d)'s table-plus-install time and has a heap within 15% of (e)'s at 10,000 and 100,000. Falsified if either bound fails.

## Method

The designs, all built from this branch's `src/cdom.c` and `src/cdom.h`:

- **A**, today: `CD_LIT` interns on first use (hash and copy in C, the batch decode in the shim), then `interned[id]`; `cd_tmp` copies into the batch and crosses once more before the op.
- **B**, the design, prototyped with `-DCDOM_STRB=1`: `CD_LIT` is a slot in `cd_lits` pointing at its bytes in `cd_chars`; `bench/strings/lits.py` (a Python stand-in for `tools/cdlits`) writes the table into the page and cuts `cd_chars` out of the module; the shim's lookup is `lits[(s - base) >> 2]`; `cd_tmp` records `(pointer, length)` and the op reads the bytes (a char loop up to 12 bytes, TextDecoder above, as the design says).
- **C**: B's module with `cd_chars` left in; the shim decodes each slot from wasm memory once at load. **C1**: the same with one TextDecoder call over all of `cd_chars`, split at its NULs.
- **D** (`-DCDOM_LITD=1`): the literal's own address is the id, cached in a `Map` on first sight.
- **E**: dense ids, `lits[id]`, measured in the micro only (it needs a rewrite of the linked code).
- **Bx**: B with the faster runtime read this work found: `String.fromCharCode` with the bytes as arguments, 8 at a time, up to 32 ASCII bytes, TextDecoder otherwise, and the memory view checked for detachment by its length rather than by `memory.buffer`.

The prototype differs from the design in two ways that do not change a timing: `cd_str` stays a `uint32_t`, and a `cd_tmp` window closes at the next `cd_tmp` after a call rather than right after the call (the same stores, in another place).
The proto shim is `bench/strings/proto/cdom-b.js`, a copy of `shim/cdom.js` with `stringOf` replaced; both shims are minified with the same terser settings for the pages.
Every page's DOM was checked identical across the designs (`bench/strings/same.mjs`), and `tools/smoke.mjs` passes on every keyed entry.

Timing rules: one headless Chrome 157 at a time, nothing else benchmarking; pages served cross-origin isolated, so `performance.now()` has 5 us resolution; the baseline is run twice (A and A2, or two passes) and the gap between the two is the noise band; a difference inside it is "not measurable".
Micro numbers are the median of 21 samples of at least 10 ms each, after warm-up, one fresh page per configuration.
Startup and memory numbers are medians of 15 fresh Chrome processes per page (cold: no HTTP or code cache), the designs interleaved round by round.

## Literals: `cd_text("Hello")`

`bench/strings/micro.c` calls `cd_text` on eight different literals per iteration through the real `cdom.c` of each design.
"Resolved" gives the module imports that only look the string up (A's are the shim's own `decode`, `keep` and `stringOf`, copied); "with the DOM" runs the real shim, appending a text node to a detached element.
Four passes (two in each of two rounds), ns per call:

| Design | Prediction | Resolved, median of passes | Noise band (passes) | With the DOM | Verdict |
| --- | --- | ---: | ---: | ---: | --- |
| A, today | L1: 5 to 9 ns | 9.3 | 9.16 to 9.35 | 131 / 119 | the baseline; just above the predicted range |
| B, `lits[(s - base) >> 2]` | L1: A within 1.5 ns | 6.8 | 6.77 to 6.91 | 116 / 125 | L1 refuted: B is 2.5 ns faster (27%), more than predicted |
| C | L2: same as B | 6.8 | (same code as B) | (same) | confirmed: the lookup is the same function |
| D, `Map` by address | L3: 5 to 12 ns over B | 9.4 | 9.35 to 9.44 | 186 / 114 | L3 refuted: only 2.6 ns over B; a Map hit costs what A's whole path costs |
| E, dense `lits[id]` | L4: within 0.5 ns of B | 7.1 | 7.06 to 7.10 | | confirmed: 0.3 ns slower than B, no gain |

With the DOM call every design is 114 to 186 ns, and the two passes of one design differ by up to 72 ns (GC and the growing element), so L5 is **not measurable**: the literal's cost (7 to 9 ns) is under the noise of the DOM call it feeds.
A's resolved path costs 2.5 ns more than B because it takes the `CD_LIT` slot check, `ref_name`'s `sent` check and `ready()`'s batch check in C before the import; the import call itself is the 5 to 6 ns floor of every design.

Reproduce: `bench/strings/micro.sh && node bench/strings/micro.mjs 'test=(lit8|dense8)'`.

## Runtime strings: `cd_text(name)`

The same micro, `cd_text(cd_tmp(buf, len))`, resolved only, ns per call, two passes each (`bench/strings/results/micro-r2-*.tsv`; intern and TextDecoder rows from `micro-1.tsv` and `micro-2.tsv`).
UTF-8 strings alternate a two-byte letter and an ASCII one.

| Length, kind | A `cd_tmp` (batch) | A `cd_intern`, hit | A `cd_intern`, 1,024 fresh, then shown | B, the design's read | B, TextDecoder always | Bx: fromCharCode args to 32, length check | Bx, ASCII test in C |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1, ASCII | 68 / 68 | 12.6 / 12.8 | | 23 / 23 | 137 / 135 | 26 / 26 (args, buffer check) | 18 / 18 |
| 9, ASCII | 125 / 125 | 19 / 19 | 69 / 55 | 80 / 80 | 159 / 139 | 48 / 48 | 42 / 42 |
| 12, ASCII | 150 / 147 | 22 / 23 | 67 / 64 | 100 / 101 | 137 / 137 | 57 / 57 | 48 / 49 |
| 13, ASCII | 151 / 156 | 23 / 24 | 104 / 67 | 139 / 141 | 140 / 144 | 55 / 56 | 46 / 47 |
| 30, ASCII | 223 / 226 | 50 / 50 | 146 / 125 | 144 / 146 | 156 / 143 | 94 / 95 | 79 / 79 |
| 64, ASCII | 363 / 357 | | | 142 / 145 | | 143 / 150 | 159 / 157 |
| 200, ASCII | 339 / 339 | 333 / 333 | 464 / 464 | 172 / 156 | 154 / 150 | 153 / 162 | 233 / 233 |
| 9, UTF-8 | 220 / 227 | 19 / 19 | 64 / 110 | 218 / 233 | 160 / 154 | 163 / 185 | 153 / 171 |
| 12, UTF-8 | 230 / 238 | 22 / 23 | 63 / 66 | 252 / 255 | 166 / 157 | 169 / 186 | 159 / 172 |
| 13, UTF-8 | 227 / 241 | 24 / 24 | 72 / 73 | 172 / 182 | 159 / 165 | 169 / 189 | 159 / 172 |
| 30, UTF-8 | 270 / 281 | 51 / 51 | 146 / 159 | 192 / 206 | 198 / 188 | 217 / 233 | 195 / 206 |
| 200, UTF-8 | 787 / 800 | 333 / 333 | 696 / 696 | 616 / 623 | 616 / 604 | 623 / 616 | 702 / 690 |

The two passes of a cell are its noise band; it is under 3% for ASCII and up to 12% for the short UTF-8 rows.
The ASCII test in C runs over every byte in this prototype (0.4 ns a byte at `-Oz`), which is why it loses at 64 and 200 bytes; bounded to strings of 32 bytes or less it would keep the short-string gain only.

| # | Prediction | Measured | Verdict |
| --- | --- | --- | --- |
| R1 | B beats A's `cd_tmp` by at least 20% at every length | ASCII 1, 9, 12, 30, 200: 66%, 36%, 33%, 35%, 51% faster; ASCII 13: 9% faster; UTF-8 9 and 12: 1% faster and 7% slower | refuted at 13 ASCII bytes and at short UTF-8: the design's 12-byte threshold sends 13 bytes to TextDecoder, and its char loop runs to the end before falling back on a high byte. Bx beats A by 20% or more at every length but UTF-8 200 (21% with the JS test) |
| R2 | `cd_intern` of a known string is slower than B at 200 bytes | 333 against 156 to 172 ns | confirmed; but at 1 to 30 bytes a hit (13 to 51 ns) beats every read on use |
| R3 | the char loop beats TextDecoder up to 16 to 24 ASCII bytes | crossover at about 30 bytes (`jsread.html`: 124 against 124 ns at 30) | confirmed (inside the falsifier's 10 to 40); the design's 12 is too low for Chrome |
| R4 | `fromCharCode.apply` beats the char loop above 8 bytes | 66 against 48 ns at 9, 71 against 67 at 12, 112 against 124 at 30 | refuted at 9 and 12; and `fromCharCode` with explicit arguments beats both everywhere up to 32 bytes |
| R5 | non-ASCII costs B one TextDecoder call, 60 to 120 ns, never over 200 at 9 to 30 bytes | 172 to 255 ns | refuted: a TextDecoder call costs 100 to 140 ns in Chrome on its own, and the char loop before it adds up to 90 |

What the profiles say (Chrome's CPU profiler, `bench/strings/prof.mjs`, and the macOS `sample` command on the renderer, `bench/strings/sample.sh`):

- TextDecoder's 100 to 140 ns is fixed cost outside JS: the JS profile shows the time as idle or program, and `sample` shows it in the (stripped) Chrome framework with `_tlv_get_addr`, `os_unfair_lock` and `memmove` on top of the stack, the shape of a binding call that allocates; `td.decode` on one reused view (no `subarray`) costs 100 to 120 ns, so the view (24 ns, `jsread.html` "subarray") is a fifth of it.
  Node's 84 to 90 ns, which the design used, is about 30 ns too optimistic for Chrome.
- The char loop costs 5.5 ns a character: each `+=` makes a new flat string (V8 only makes cons strings from 13 characters), so it is quadratic in copies at short lengths.
- `get buffer` (the `memory.buffer` getter) is 5.7% of B's 9-byte profile; checking the view's length instead saves 4 to 7 ns per runtime string at short lengths (micro round 2, "/length" rows).
- In A's `cd_tmp` path the cost is two crossings (the `strings` import, then the op), the copy and UTF-16 count in C, and the decode; at 1 byte that is 68 ns where B needs 23.

So for runtime strings: read on use (B) is right, but the read should be `fromCharCode` with arguments for ASCII up to 32 bytes (2 to 3 times faster than the design's read at 9 to 30 bytes), TextDecoder above that and for non-ASCII, and the view checked by length.
The one case where today's code wins is a runtime string shown again and again unchanged (`cd_intern` hit, 13 to 51 ns), and a burst of fresh non-ASCII strings, which today's batch decodes in one TextDecoder call (64 to 73 ns each at 9 to 13 bytes, against 153 to 189 for any read on use).

Reproduce: `bench/strings/micro.sh && node bench/strings/micro.mjs && ROUND=2 node bench/strings/micro.mjs && node bench/strings/jsread.mjs`.

## Startup

Medians of 15 fresh Chrome processes per page, interleaved (`bench/strings/results/startup-pages.tsv`, `startup-keyed.tsv`).
`cd_main` is `cd_start`'s duration, wrapped from a script injected before the page's; the install is the proto shim's own timer around the table install, which runs before `cd_start`; first paint is the paint timing entry.
Pages: tutorial/01-hello (bundled, 38 literal sites), the keyed benchmark entry (module by URL, 38 slots), and `bench/strings/gen_synth.py`'s page with 2,000 literal call sites (bundled, 1,614 unique strings, 3 to 30 bytes, one in ten non-ASCII).

| Page | Design | First paint (ms) | `cd_main` (ms) | Install (ms) | `cd_main` + install | Prediction | Verdict |
| --- | --- | ---: | ---: | ---: | ---: | --- | --- |
| hello | A / A2 | 49.0 / 54.8 | 0.59 / 0.58 | | 0.59 | | first paint noise 5.8 ms |
| hello | B | 58.7 | 0.44 | 0.065 | 0.51 | S1: first paint within the noise | not measurable (first paint quartiles 45 to 62 against 40 to 74) |
| hello | C / C1 | 51.3 / 68.2 | 0.43 / 0.43 | 0.095 / 0.105 | 0.53 / 0.54 | S1 | not measurable |
| keyed | A / A2 | 48.6 / 56.4 | 0.99 / 0.97 | | 0.98 | | first paint noise 7.8 ms |
| keyed | B | 51.1 | 0.87 | 0.07 | 0.94 | S1, S4 (P4): install under 0.05 ms, first paint within 5 ms | first paint confirmed (inside the noise); install refuted narrowly: 0.07 ms, cold code included, under the 0.2 ms falsifier |
| keyed | C / C1 | 50.5 / 55.0 | 0.79 / 0.82 | 0.10 / 0.105 | 0.89 / 0.92 | S1 | not measurable |
| synth | A / A2 | 70.8 / 71.1 | 2.92 / 2.94 | | 2.93 | | |
| synth | B | 86.0 | 1.59 | 0.22 | 1.81 | S2: 0.2 to 1.0 ms under A | confirmed: 1.1 ms under A (38%), inside the falsifier (2 ms) |
| synth | C | 69.0 | 1.52 | 0.92 | 2.43 | S3: 0.1 to 0.5 ms over B | confirmed: 0.62 ms over B, under the 1 ms falsifier |
| synth | C1 | 74.3 | 1.57 | 0.62 | 2.19 | | one decode and a split is 0.3 ms cheaper than a decode per slot |

First paint is not a string measurement on any of these pages: a trace (`tools/startup.mjs` on the synthetic page) shows 28 to 47 ms of layout before the paint, and the same page's median moved from 91.5 to 74.3 ms between two runs of nine, so the synthetic B's 86 ms is noise, not the table.
`cd_main` and the install are stable to a few percent and carry the whole string difference: about 0.55 us per literal site for A's first-use interning against 0.1 us per slot for B's install.
C's install pays one TextDecoder call per slot (0.46 us each); C1 shows that a no-build-tool variant can get within 0.4 ms of B at 2,000 sites.

Reproduce: `bench/strings/build.sh && bench/strings/pages.sh`, then `R=15 node bench/strings/startup.mjs name=path ...` as in the results files' first lines (with the jfb server on 8093 for the keyed pages, `CYCLES=5`).

## Memory

From the same loads (keyed: after five create-1k-and-clear cycles, then a forced GC); JS heap is CDP's `Runtime.getHeapUsage`, touched memory counts 4 KB pages of wasm memory with any nonzero byte.

| Page | Design | Initial wasm pages | JS heap at start | Touched at start | Pages after 5 cycles | JS heap after 5 cycles | Touched after 5 cycles | Prediction | Verdict |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- |
| hello | A | 6 | 588 KB | 20 KB | | | | | |
| hello | B | 2 | 580 KB | 4 KB | | | | | |
| synth | A | 8 | 818 KB | 112 KB | | | | | |
| synth | B | 2 | 710 KB | 16 KB | | | | | B's table and strings cost 108 KB less heap than A's interned array |
| keyed | A / A2 | 8 | 601 KB | 36 KB | 14 | 724 KB | 408 KB | | identical across A and A2 |
| keyed | B | 3 | 601 KB | 16 KB | 6 | 664 KB | 116 KB | M1: 4 to 5 pages fewer; M3: 0.1 to 0.3 MB less heap; M4: 50 to 150 KB less touched | M1 confirmed (5 fewer); M3 below the predicted range (59 KB less) but not falsified (over 0.05 MB); M4 confirmed in direction, larger than predicted (292 KB less) |
| keyed | C / C1 | 3 | 602 / 607 KB | 16 KB | 6 | 666 / 670 KB | 116 KB | | as B |

The harness's own memory numbers (M2, P6) are under "Whole app".
A grows to 14 pages because the intern pool, its hash table and the string batch (256 KB, written through by every label) all grow or get touched; B's labels live in the app's arena (reset on each run) and nothing of cdom's grows.

## Whole app: js-framework-benchmark

`bench/strings/jfb.sh`: `tools/pair.sh` over the entries `bench/strings/build.sh` makes, every start gated on a load under 4 (loads 2.7 to 3.7, `bench/strings/results/jfb-cpu-loads.tsv`), the entries interleaved benchmark by benchmark, 15 iterations each (the harness's default), medians in ms (`bench/strings/results/jfb-cpu.tsv`).
sb-a and sb-a2 are the same build of today's entry, so their gap is the noise; sb-at is today's cdom with the app's labels in an arena and `cd_tmp` at each use (design step 3 on today's string path).

| Benchmark | Metric | A | A2 | Noise | B | B / A | Bx | Bx / A | A with `cd_tmp` labels | / A |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| create 1k | total | 30.1 | 30.0 | 0.3% | 30.5 | 1.013 | 30.0 | 0.997 | 28.8 | 0.957 |
| create 1k | script | 2.8 | 2.7 | 3.6% | 2.9 | 1.036 | 2.6 | 0.929 | 2.8 | 1.000 |
| replace 1k | total | 31.7 | 31.7 | 0% | 32.1 | 1.013 | 31.4 | 0.991 | 30.9 | 0.975 |
| replace 1k | script | 5.0 | 5.0 | 0% | 5.2 | 1.040 | 4.9 | 0.980 | 5.1 | 1.020 |
| update every 10th | total | 16.2 | 16.1 | 0.6% | 16.1 | 0.994 | 16.1 | 0.994 | 16.6 | 1.025 |
| select | total | 4.3 | 4.6 | 7.0% | 4.3 | 1.000 | 4.3 | 1.000 | 4.5 | 1.047 |
| swap | total | 18.3 | 18.3 | 0% | 18.7 | 1.022 | 18.6 | 1.016 | 18.8 | 1.027 |
| remove one | total | 15.3 | 15.5 | 1.3% | 15.3 | 1.000 | 15.2 | 0.993 | 15.4 | 1.007 |
| create 10k | total | 303.1 | 302.9 | 0.1% | 305.6 | 1.008 | 304.1 | 1.003 | 293.0 | 0.967 |
| create 10k | script | 20.4 | 20.5 | 0.5% | 23.2 | 1.137 | 21.2 | 1.039 | 22.4 | 1.098 |
| append 1k to 1k | total | 34.8 | 34.8 | 0% | 35.2 | 1.011 | 34.8 | 1.000 | 33.7 | 0.968 |
| append 1k to 1k | script | 2.7 | 2.7 | 0% | 2.9 | 1.074 | 2.6 | 0.963 | 2.8 | 1.037 |
| clear | total | 13.3 | 13.3 | 0% | 13.3 | 1.000 | 13.2 | 0.992 | 13.3 | 1.000 |

The script times of select, swap and remove are 0.1 to 0.6 ms, at the harness's resolution, so their ratios mean nothing.

Without templates (`-DCDOM_TEMPLATES=0`, `bench/strings/results/jfb-notpl.tsv`):

| Benchmark | Metric | A | A2 | Noise | B | B / A |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| create 1k | total | 31.4 | 31.5 | 0.3% | 32.2 | 1.025 |
| create 1k | script | 4.5 | 4.5 | 0% | 5.2 | 1.156 |
| create 10k | total | 316.9 | 317.2 | 0.1% | 319.9 | 1.009 |
| create 10k | script | 35.9 | 35.3 | 1.7% | 38.1 | 1.061 |

Memory, the harness's `measureUserAgentSpecificMemory`, one iteration each (`bench/strings/results/jfb-mem.tsv`):

| Benchmark | A | A2 | B | Bx |
| --- | ---: | ---: | ---: | ---: |
| ready memory (MB) | 0.66 | 0.65 | 0.67 | 0.60 |
| run memory (MB) | 1.75 | 1.84 | 1.81 | 1.84 |
| run and clear five times (MB) | 0.85 | 0.85 | 0.72 | 0.78 |

B and Bx carry the same module and differ only in the shim's read, so their 0.06 MB gap on run-clear is the noise of a single iteration.

| # | Prediction | Measured | Verdict |
| --- | --- | --- | --- |
| W1 / P5, as the design wrote it | every operation within 3% of A in total; create 10k script 0.0 to 1.0 ms worse, never over 1.5 | totals 0.994 to 1.022; create 10k script 2.8 ms worse (1.137) | refuted on create 10k script; confirmed on totals |
| W1, revised after the micro (18:23) | B's create 1k and 10k script 5% to 15% worse | 3.6% (inside its own noise of 3.6%) and 13.7% | confirmed on create 10k, not measurable on create 1k |
| W3 | Bx within the noise of A or better everywhere | totals 0.991 to 1.016; create 1k script 7% better; create 10k script 0.8 ms (3.9%) worse | confirmed on totals; refuted narrowly on create 10k script |
| W2 / P8 | without templates, create 1k script within 3% of A | 15.6% worse (4.5 to 5.2 ms), total 2.5% worse | refuted: each row reads its label through the design's read (TextDecoder above 12 bytes) |
| M2 / P6 | run-clear memory 0.72 MB or less | 0.72 MB (A 0.85) | confirmed |

Where B's create 10k script time goes: every label (9 to 27 ASCII bytes, "pretty red table") goes through the design's read, a TextDecoder call (about 140 ns in the micro) where A hashes, copies and then decodes the whole create in one call (about 65 ns a label in the micro's `intern_new`); 10,000 x 75 ns is 0.75 ms of the 2.8.
The rest is the app's own change: A with `cd_tmp` labels (sb-at, today's string path, the arena app) is also 2.0 ms worse on create 10k script, so moving the labels from interned ids to the app's arena costs about as much as the read itself (an arena copy per label, a `cd_tmp` record per use, and `cd_tpl_text`'s unchanged-slot check that no longer hits).
Bx's faster read takes back 2 of the 2.8 ms.

Reproduce: the jfb server on 8093 (`cd jfb/server && PORT=8093 npx tsx index.ts`), then `bench/strings/build.sh && bench/strings/jfb.sh`, then `node bench/strings/jfbsum.mjs results/pairs/strings-cpu sb-a sb-a2 sb-b sb-bx sb-at`.
