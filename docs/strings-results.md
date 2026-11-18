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

## Predictions

The predictions are copied from `docs/strings-design.md`.
The Measured and Verdict columns are filled in after the change is measured.

| # | Prediction | Built on | Refuted by | Measured | Verdict |
| --- | --- | --- | --- | --- | --- |
| P1 | Page, brotli -q 11: tutorial/01-hello between 8,450 and 8,700 (today 8,848 in the bench's build); every lesson smaller than its step 1 size; the 2,000-site synthetic page between 18,900 and 19,500 (today 20,573) | B measured 8,597 and 19,299 with a map; one entry per slot is 0.65 bytes per slot smaller on amgi; the shim grows by `ahead` and `ascii` | hello over 8,750, any lesson larger than today, or the synthetic page over 19,800 | | |
| P2 | Lesson 1's `app.wasm` between 2,300 and 2,600 bytes (today 3,981) | B stripped 1,553 bytes; `cd_tmp_ahead` adds one import and a few bytes | above 2,800 | | |
| P3 | Initial memory: hello 2 pages (today 6), the keyed entry 3 (today 8); touched wasm memory after five create-and-clear cycles 150 KB or less (today 408) | measured for B: 2, 3 and 116 KB | hello over 2, keyed over 4, or touched over 200 KB | | |
| P4 | Table install between 0.04 and 0.10 ms on hello and the keyed entry, and between 0.10 and 0.25 ms at 2,000 sites; `cd_main` plus install at 2,000 sites between 1.6 and 2.0 ms (today 2.93); first paint not measurable | B measured 0.065, 0.07, 0.22 and 1.81 ms with a `split` and a map pass; one `split` of 2,000 entries replaces both; first paint noise was 5.8 to 7.8 ms | install over 0.15 ms on hello or the keyed entry, over 0.35 ms at 2,000 sites, or `cd_main` plus install over 2.2 ms | | |
| P5 | Keyed benchmark, `cdom` entry: every total within 2.5% of A; create 10k script between 20.5 and 21.2 ms (A 20.4), that is 0.1 to 0.8 ms worse; create 1k script 2.8 ms or less (A 2.8) | Bx measured totals 0.991 to 1.016, create 10k 21.2 and create 1k 2.6; `cd_tmp_ahead` replaces 10,000 reads of about 60 to 90 ns with cuts of about 25 ns, 0.35 to 0.65 ms; the rest of the gap is the app's arena and the `cd_tmp` records | any total over 1.03 of A beyond the noise in two passes, create 10k script over 21.4 ms beyond the noise, or create 1k script over 3.0 ms | | |
| P5a | In the same build, create 10k script with the `cd_tmp_ahead` call is at least 0.2 ms faster than without it | the same arithmetic | a gain under 0.2 ms beyond the noise: then the call comes out of the benchmark app, and P5's band is Bx's 21.2 ms | | |
| P6 | Run-and-clear memory (harness) 0.78 MB or less (A 0.85) | B 0.72 and Bx 0.78 in single iterations of the same module | 0.82 MB or more in two iterations | | |
| P7 | The minified shim within 250 bytes of today's brotli size (5,930) | the batch decoder goes; `str`, `ascii` (about 400 bytes minified), `ahead`, the table install and the SVG counter come | outside that band | | |
| P8 | Without templates: create 1k script between 4.3 and 4.9 ms (A 4.5), create 10k script between 35 and 38 ms (A 35.9, A2 35.3) | B's 5.2 and 38.1 came from a `TextDecoder` call per label; the new read is 50 to 90 ns faster per label (0.07 ms per 1,000), a cut faster still, and each of the 12 literals per row is 2.5 ns cheaper than today | create 1k over 5.0 ms or create 10k over 38.5 ms, beyond the noise | | |
| P9 | The lexer and clang agree on every literal in cdom's tree and in the torture file | the grammar is small and the prototype agrees on the torture literal | one mismatch | | |
| P10 | No literal that went to cdom is in any final wasm | both sections are removed whole | `strings app.wasm` finds one | | |
| P11 | Read on use (micro, resolved only, the bench's method): 9 ASCII bytes between 44 and 52 ns, 30 ASCII bytes between 86 and 100 ns, 9 to 13 bytes of UTF-8 between 150 and 195 ns; a page that never calls `cd_tmp_ahead` pays under 2 ns for its test | Bx measured 48, 94 to 95 and 163 to 189; the test is one compare with null | 9 ASCII bytes over 55 ns, 30 over 105 ns, or short UTF-8 over 205 ns | | |
| P12 | Inside a range (micro, 1,024 fresh strings announced once and each shown once, the range's decode and index included): 9 to 13 bytes of UTF-8 between 25 and 55 ns each, 9 to 30 ASCII bytes between 20 and 45 ns each | a cut is a record read, three compares and a `substring`; the decode is 100 to 140 ns once plus about 0.3 ns a byte; the index pass 1 to 2 ns a byte, non-ASCII ranges only | short UTF-8 over 64 ns (no better than today's batch, 64 to 73), or ASCII over 48 ns (no better than read on use at 9 bytes) | | |
| P13 | A literal lookup, resolved only, between 6.5 and 7.2 ns (today 9.3) | B measured 6.8; the lookup did not change | over 7.5 ns | | |
