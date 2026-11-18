# Strings results: the lesson sizes before and after the change

This file records the tutorial and shim sizes before and after the string change, and the verdict for each prediction in `docs/strings-design.md`.

## Before

Date: 2026-11-17.
Commit: c0c2fb2.

Sizes are from `tutorial/run.sh` at that commit (wasm from `wc -c`, page brotli from `brotli -q 11`, initial pages from `wasm-objdump -x`).

| Item | app.wasm bytes | Page brotli -q 11 bytes | Initial memory pages |
| --- | --- | --- | --- |
| 01-hello | 4030 | 8942 | 6 |
| 02-counter | 5981 | 10022 | 6 |
| 03-input | 6373 | 10287 | 6 |
| 04-regions | 8581 | 11557 | 6 |
| 05-todo | 14739 | 15119 | 6 |
| 06-stopwatch | 13274 | 14275 | 6 |
| 07-particles | 9527 | 12321 | 11 |
| shim/cdom.min.js (the file itself) | 15718 | 5930 | n/a |

## After

Date: 2026-11-17.
Commit: branch `strings`, 886cf91 and the step 10 commits after it (measurement and docs only; no product code changed).
Measured the same way, by `tutorial/run.sh` (all lessons pass).

| Item | app.wasm bytes | Page brotli -q 11 bytes | Initial memory pages |
| --- | --- | --- | --- |
| 01-hello | 2452 (-1,578) | 8810 (-132) | 2 |
| 02-counter | 4239 (-1,742) | 9826 (-196) | 2 |
| 03-input | 4296 (-2,077) | 10018 (-269) | 2 |
| 04-regions | 5144 (-3,437) | 10875 (-682) | 2 |
| 05-todo | 11040 (-3,699) | 14370 (-749) | 3 |
| 06-stopwatch | 10371 (-2,903) | 13649 (-626) | 2 |
| 07-particles | 7213 (-2,314) | 11973 (-348) | 6 |
| shim/cdom.min.js (the file itself) | 17599 (+1,881) | 6672 (+742) | n/a |

The 2,000-site synthetic page (strings-bench's `gen_synth.py`, here in plain C, linked through cdlits and bundled by page.awk; `bench/strings/pages-s.sh`): 15,831 bytes brotli, against 20,573 before; its module 15,138 bytes, against 48,311.

## How step 10 measured

The harness is strings-bench's (docs/strings-bench.md on branch `strings-bench`), copied to `bench/strings/` on this branch, run one Chrome at a time in headless Chrome 157 with nothing else benchmarking.

- Before: A and A2 are two copies of the same build from `tutorial` (e35056c), made by its own `bench/build.sh` (`bench/strings/build-a.sh`); their gap is the noise band.
- After: `bench/strings/build-st.sh`, this branch's `bench/build.sh` recipe for three entries: st-s (the keyed app as committed, with `cd_tmp_ahead`), st-s-n (no templates) and st-s-na (the app with its two `cd_tmp_ahead` calls removed, in a copy made in `bench/strings/out`, for P5a).
- The micro (`bench/strings/micro-s.c`, `micro-s.html`, `micro-s.mjs`) runs this branch's real `shim/cdom.js` and a module linked through cdlits; "resolved only" is the same shim with one line of `text()` replaced at load so it returns after `str(id)`, into a closure variable as the bench's sink was. Two passes (`bench/strings/results/micro-s-*.tsv`).
- Startup and memory (`bench/strings/startup.mjs`): 15 fresh Chrome processes per page, interleaved. The shim has no install timer of its own, so the install is read as the span from the module's instantiation to `cd_start`, after minus before.
- Whole app (`bench/strings/jfb.sh`): `tools/pair.sh`, 15 iterations, loads 1.7 to 3.7 at each start; memory twice; swap twice. Summaries in `bench/strings/results/jfb-*.tsv`.

## Predictions

The predictions are copied from `docs/strings-design.md`.
The Measured and Verdict columns are filled in after the change is measured.

| # | Prediction | Built on | Refuted by | Measured | Verdict |
| --- | --- | --- | --- | --- | --- |
| P1 | Page, brotli -q 11: tutorial/01-hello between 8,450 and 8,700 (today 8,848 in the bench's build); every lesson smaller than its step 1 size; the 2,000-site synthetic page between 18,900 and 19,500 (today 20,573) | B measured 8,597 and 19,299 with a map; one entry per slot is 0.65 bytes per slot smaller on amgi; the shim grows by `ahead` and `ascii` | hello over 8,750, any lesson larger than today, or the synthetic page over 19,800 | hello 8,810 (-132); every lesson smaller (-132 to -749); synthetic 15,831 (-4,742) | refuted: hello is over 8,750, because the shim grew 742 bytes of brotli (P7); every lesson is smaller, and the synthetic page is far below its band |
| P2 | Lesson 1's `app.wasm` between 2,300 and 2,600 bytes (today 3,981) | B stripped 1,553 bytes; `cd_tmp_ahead` adds one import and a few bytes | above 2,800 | 2,452 | confirmed |
| P3 | Initial memory: hello 2 pages (today 6), the keyed entry 3 (today 8); touched wasm memory after five create-and-clear cycles 150 KB or less (today 408) | measured for B: 2, 3 and 116 KB | hello over 2, keyed over 4, or touched over 200 KB | hello 2 pages, keyed 3; touched after five cycles 116 KB (A and A2 408) | confirmed |
| P4 | Table install between 0.04 and 0.10 ms on hello and the keyed entry, and between 0.10 and 0.25 ms at 2,000 sites; `cd_main` plus install at 2,000 sites between 1.6 and 2.0 ms (today 2.93); first paint not measurable | B measured 0.065, 0.07, 0.22 and 1.81 ms with a `split` and a map pass; one `split` of 2,000 entries replaces both; first paint noise was 5.8 to 7.8 ms | install over 0.15 ms on hello or the keyed entry, over 0.35 ms at 2,000 sites, or `cd_main` plus install over 2.2 ms | install (instantiation to `cd_start`, after minus before): hello +0.005 to +0.015 ms, keyed +0.02 to +0.04, 2,000 sites +0.085 to +0.09, against a noise between A and A2 of 0.01 to 0.02; `cd_main` plus install at 2,000 sites 1.69 + 0.09 = 1.78 ms (A 3.22, A2 2.93); first paint medians hello 47.3 (A 50.5, A2 50.6), keyed 47.6 (54.6, 54.8), synthetic 74.7 (74.7, 77.7), each inside the others' interquartile ranges (keyed 46.1 to 56.5 against 47.9 to 58.7) | confirmed: `cd_main` plus install in its band, first paint not measurable; the install is under the noise on hello and the keyed entry (not measurable) and 0.09 ms at 2,000 sites, just under its 0.10 to 0.25 band |
| P5 | Keyed benchmark, `cdom` entry: every total within 2.5% of A; create 10k script between 20.5 and 21.2 ms (A 20.4), that is 0.1 to 0.8 ms worse; create 1k script 2.8 ms or less (A 2.8) | Bx measured totals 0.991 to 1.016, create 10k 21.2 and create 1k 2.6; `cd_tmp_ahead` replaces 10,000 reads of about 60 to 90 ns with cuts of about 25 ns, 0.35 to 0.65 ms; the rest of the gap is the app's arena and the `cd_tmp` records | any total over 1.03 of A beyond the noise in two passes, create 10k script over 21.4 ms beyond the noise, or create 1k script over 3.0 ms | totals 0.952 to 1.027 of A, except swap: 1.055 in pass 1 and 1.075 in pass 2 (A2/A 1.011 and 1.000); create 10k script 19.9 ms (A 20.4, A2 20.4); create 1k script 2.2 ms (A 2.6, A2 2.5) | refuted on swap (over 1.03 beyond the noise in two passes); confirmed elsewhere, and create 10k script beat its band (0.5 ms faster than A, not 0.1 to 0.8 slower). Profile below |
| P5a | In the same build, create 10k script with the `cd_tmp_ahead` call is at least 0.2 ms faster than without it | the same arithmetic | a gain under 0.2 ms beyond the noise: then the call comes out of the benchmark app, and P5's band is Bx's 21.2 ms | create 10k script 19.9 ms with `cd_tmp_ahead`, 21.2 without (st-s-na): 1.3 ms; noise 0.0 | confirmed |
| P6 | Run-and-clear memory (harness) 0.78 MB or less (A 0.85) | B 0.72 and Bx 0.78 in single iterations of the same module | 0.82 MB or more in two iterations | 0.79 MB in both iterations (A 0.85, A2 0.85) | not refuted (under the 0.82 falsifier), but 0.01 MB over the predicted 0.78; the harness reports 0.01 MB steps |
| P7 | The minified shim within 250 bytes of today's brotli size (5,930) | the batch decoder goes; `str`, `ascii` (about 400 bytes minified), `ahead`, the table install and the SVG counter come | outside that band | 6,672 brotli, +742 over 5,930 | refuted: a copy of the shim without the read-only cache is 137 bytes of brotli smaller, one without `cd_tmp_ahead` 164 (`bench/strings/shimsize.py`); the other 440 or so are `ascii`, `readString`, `str`, the table install, `stale` and the SVG counter |
| P8 | Without templates: create 1k script between 4.3 and 4.9 ms (A 4.5), create 10k script between 35 and 38 ms (A 35.9, A2 35.3) | B's 5.2 and 38.1 came from a `TextDecoder` call per label; the new read is 50 to 90 ns faster per label (0.07 ms per 1,000), a cut faster still, and each of the 12 literals per row is 2.5 ns cheaper than today | create 1k over 5.0 ms or create 10k over 38.5 ms, beyond the noise | create 1k script 4.3 ms (A 4.6, A2 4.5), create 10k script 35.7 (A 35.2, A2 35.6) | confirmed (create 10k inside the noise) |
| P9 | The lexer and clang agree on every literal in cdom's tree and in the torture file | the grammar is small and the prototype agrees on the torture literal | one mismatch | `tests/lits/run.sh` 38 passed (torture, kinds and table at -O0 and -Oz agree); `bench/strings/litcheck.sh`: every lesson, the keyed app, the click demo, the micro and the 2,000-site page built with `-DCD_LITS_CHECK` agree (2,001 literals on the last) | confirmed |
| P10 | No literal that went to cdom is in any final wasm | both sections are removed whole | `strings app.wasm` finds one | no slot literal's bytes are left in any lesson's data; a search does find 12 strings of the tables (for example "Show details" in 04, "Pause" in 07), each also used through a ternary, a helper or a `const` table, so option B keeps its copy in read-only data, and import names ("clear", "remove", "text") | confirmed for what went to the table; `strings app.wasm` does find such text, by option B's design |
| P11 | Read on use (micro, resolved only, the bench's method): 9 ASCII bytes between 44 and 52 ns, 30 ASCII bytes between 86 and 100 ns, 9 to 13 bytes of UTF-8 between 150 and 195 ns; a page that never calls `cd_tmp_ahead` pays under 2 ns for its test | Bx measured 48, 94 to 95 and 163 to 189; the test is one compare with null | 9 ASCII bytes over 55 ns, 30 over 105 ns, or short UTF-8 over 205 ns | 9 ASCII 50.3 / 51.3 ns, 30 ASCII 96.5 / 96.2, UTF-8 9 to 13 bytes 163 to 173; the range test alone cannot be separated (strings-bench's Bx: 47.7 and 93.8) | confirmed |
| P12 | Inside a range (micro, 1,024 fresh strings announced once and each shown once, the range's decode and index included): 9 to 13 bytes of UTF-8 between 25 and 55 ns each, 9 to 30 ASCII bytes between 20 and 45 ns each | a cut is a record read, three compares and a `substring`; the decode is 100 to 140 ns once plus about 0.3 ns a byte; the index pass 1 to 2 ns a byte, non-ASCII ranges only | short UTF-8 over 64 ns (no better than today's batch, 64 to 73), or ASCII over 48 ns (no better than read on use at 9 bytes) | ASCII 9 to 30 bytes 23.2 to 33.8 ns; UTF-8 9 to 13 bytes 68.7 to 85.5 (30 bytes 145) | confirmed for ASCII; refuted for UTF-8 (over 64). Profile below |
| P13 | A literal lookup, resolved only, between 6.5 and 7.2 ns (today 9.3) | B measured 6.8; the lookup did not change | over 7.5 ns | 12.8 / 12.9 ns (strings-bench's micro in the same session: A 9.2, B 6.9) | refuted. Profile below |

## The refutations, profiled

**P13, the literal lookup (12.8 ns, not 6.5 to 7.2).**
Chrome's CPU profiler on the micro (`node bench/strings/prof.mjs 'micro-s.html?mode=resolve&test=lit8'`) shows `text` 35%, `str` 24% and the wasm-to-JS wrapper 22% as separate frames; in strings-bench's B the three were one frame, because TurboFan inlined `str` into `text` and `text` into the wrapper.
`str` now also holds the read-only cache and the `cd_tmp_ahead` cut, and it is too big to inline.
A diagnosis copy of the shim (`split=1` in the micro: the literal test alone in `str`, the rest moved to a second function, nothing else changed) measures 8.0 / 8.0 ns for the literal, with the read-only and runtime rows unchanged (ternary 12.5 / 13.0, 9 ASCII bytes 49.8 / 50.1).
So most of the 5.9 ns is lost inlining, and a split `str` would take back about 4.8 of it; the other 1.1 ns over B was not traced.
2026-11-17 fix: `str` is now the slot test and the `lits` lookup only, with the rest in `strRest` (the shape of the diagnosis copy).
Prediction written before measuring: the literal 7 to 9 ns, the other rows within their step 10 noise.
Two passes on the real shim, one Chrome at a time: literal 7.87 / 7.97 ns (was 12.8 / 12.9), ternary 12.72 / 12.68, helper 13.83 / 13.81, `const` table entry 14.27 / 14.06, runtime 9 ASCII bytes 53.0 / 53.0 (diagnosis copy 49.8 / 50.1, about 3 ns up, not traced), ahead 9 ASCII bytes 32.5 / 32.0.
Option B's own rows: ternary 12.6, helper 13.4, `const` table entry 14.0 ns, each read once and then cached, within 1.2 ns of a literal in the same build.
With the DOM call the literal costs 138 ns and the ternary 123, inside the DOM call's noise.

**P12, UTF-8 inside a range (69 to 86 ns, not 25 to 55).**
The profile of 13-byte UTF-8 strings in a range has `ahead` at 56% of the time: the decode of the 13 KB range, and the index pass, which allocates a `Uint32Array` of the range's length plus one and fills it a byte at a time, about 3.6 ns a byte where the design predicted 1 to 2.
The cut itself (`str`, 18%) is as fast as the ASCII one; ASCII ranges skip the index and measure 23 to 34 ns.
It still halves the read on use for UTF-8 (69 to 86 against 163 to 173 ns), but does not beat today's batch (64 to 73).

**P5, swap (total 1.055 and 1.075 of A in two passes; A2/A 1.011 and 1.000).**
In the harness, swap has 0.2 to 0.3 ms more script (0.7 against 0.4 to 0.6) and 0.6 to 1.0 ms more paint (16.8 and 17.1 against 15.8 to 16.7); no string is read in a swap (two moves, and row patches that compare revisions).
A trace of the swap click, unthrottled (`node tools/onetrace.mjs <entry> '#run' '#swaprows' 7`, twice for st-s and once each for A and A2), shows the same Layout (1.95 to 2.04 ms), Paint (1.03 to 1.08), UpdateLayoutTree (0.95 to 0.98) and PrePaint (0.62 to 0.69) for all four, and one difference that repeats: HitTest 0.21 and 0.20 ms for st-s, against 0.10 and 0.11 for A and A2.
The harness runs swap under 4 times CPU throttling, so that would be about 0.4 ms of the 1.0 to 1.4; the rest does not show unthrottled.
The page differs from A's by one element (the table's `<script>` in the head), and its rows' text is cut from the range with `substring` where A's came from a decode; which of these doubles the hit test was not found.
No product code was changed: this is for the repo manager to decide.
