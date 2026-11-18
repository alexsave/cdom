# The page tool: results

The measurements of [page-tool-design.md](page-tool-design.md), its predictions Q1 to Q10 marked confirmed, refuted or not measurable, with numbers.
Measured 2026-11-18 on this Mac (Apple silicon, headless Chrome), branch `strings`.
Step 9 of the design: every prediction has its verdict and its numbers below.

## The baselines

Two "before" states matter, because steps 3 and 5 changed every page a little before the lessons moved:

- **Step 0**, the tree before step 1 (d2c6b6c): the pages `tools/page.awk` wrote.
- **Before step 7**, the tree the lessons migrate from (after step 6): step 3 changed one message in `shim/cdom.js`, so the minifier renamed a few variables in `shim/cdom.min.js`, and step 5 gave the default page an empty `<style>` (17 bytes).

Page bytes, brotli (`brotli -q 11 -c build/index.html | wc -c`), `app.wasm` and `app.wasm.lits`, each lesson built by `tutorial/run.sh`:

| Lesson | Step 0 page | Step 0 brotli | Before step 7 page | Before step 7 brotli | `app.wasm` | `app.wasm.lits` | CSS literal (bytes) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 01-hello | 21,745 | 8,871 | 21,760 | 8,887 | 2,515 | 476 | 251 |
| 02-counter | 24,232 | 9,849 | 24,247 | 9,863 | 4,292 | 599 | 401 |
| 03-input | 24,728 | 10,098 | 24,743 | 10,097 | 4,403 | 949 | 695 |
| 04-regions | 27,681 | 11,144 | 27,696 | 11,128 | 5,598 | 2,308 | 1,349 |
| 05-todo | 35,369 | 14,424 | 35,384 | 14,451 | 11,222 | 2,499 | 1,981 |
| 06-stopwatch | 33,526 | 13,750 | 33,541 | 13,758 | 10,483 | 1,635 | 1,313 |
| 07-particles | 28,740 | 11,978 | 28,755 | 12,016 | 7,245 | 1,169 | 953 |

The module and the table did not change between the two states (the same C, the same cdlits).
The brotli column moves by up to 38 bytes with no change to the lesson, from the renamed variables in the minified shim alone: that is the noise floor of one byte count, and the migration's verdicts below are read against the "before step 7" column.

## Predictions for the migration (step 7), restated before measuring

The design's Q2 to Q4 were written against an older tree (lesson 1's brotli was 8,810 then).
Restated against the "before step 7" column, with what step 7 actually writes known (each `app.css` opens with a one-sentence comment, about 95 bytes, which the design's estimate did not count):

| # | Prediction | Refuted by |
| --- | --- | --- |
| Q2 | Every lesson's page, brotli, grows by 30 to 110 bytes: the header comment is new text (about 55 brotli bytes), one declaration per line adds whitespace brotli mostly removes (0 to 20), the module loses a few bytes and the table loses the `style` entry (minus 5 to 15) | any lesson outside -40 to +120 (the design's bound), or all seven outside 30 to 110 |
| Q3 | Every lesson's `app.wasm` is 8 to 30 bytes smaller | not smaller, or more than 60 smaller |
| Q4 | Every lesson's `app.wasm.lits` shrinks by its CSS length plus 13, within 2: 264, 414, 708, 1,362, 1,994, 1,326, 966 | off by more than 2 on any lesson |
| Q8 | The start screenshots of all seven lessons, and the end screenshots of lessons 1 to 6, are byte-identical before and after, in the light and the dark scheme | one differing PNG that two baseline runs agree on |

## The screenshot method (step 6)

`tutorial/test.mjs` gained `SHOTS_START=dir` (each lesson as it starts, two animation frames after the mount is not empty, every `<canvas>` hidden for the shot and shown again) and `SCHEME=light|dark` (`Emulation.setEmulatedMedia`).
Two runs of the unmigrated tree, each in both schemes, with `SHOTS` (the end state) as well: the 14 start shots are byte-identical between the runs, and so are the end shots of lessons 1 to 6; lesson 7's end shot differs between two runs of the same tree (its bursts are random), so it is left out of the end comparison.

## After the migration

The same columns after step 7, with the comment rule added after Q7 (below), each lesson built by `tutorial/run.sh`:

| Lesson | Page | Brotli | Brotli change | `app.wasm` | `app.wasm.lits` | Table change (predicted) | `src/app.css` |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 01-hello | 21,751 | 8,859 | -28 | 2,501 | 213 | -263 (-264) | 367 |
| 02-counter | 24,252 | 9,852 | -11 | 4,278 | 185 | -414 (-414) | 532 |
| 03-input | 24,767 | 10,040 | -57 | 4,389 | 242 | -707 (-708) | 844 |
| 04-regions | 27,764 | 11,135 | +7 | 5,584 | 946 | -1,362 (-1,362) | 1,539 |
| 05-todo | 35,510 | 14,416 | -35 | 11,208 | 506 | -1,993 (-1,994) | 2,232 |
| 06-stopwatch | 33,608 | 13,746 | -12 | 10,469 | 309 | -1,326 (-1,326) | 1,607 |
| 07-particles | 28,804 | 11,966 | -50 | 7,231 | 204 | -965 (-966) | 1,404 |

Against step 0, the brotli sizes moved by -12, +3, -58, -9, -8, -4 and -12 bytes.

Before the comment rule, with every comment shipped, the same pages were 8,920, 9,918, 10,090, 11,178, 14,459, 13,850 and 12,139 bytes brotli (+33, +55, -7, +50, +8, +92, +123 against "before step 7").
Lesson 7's +123 was its two comments: the C comment above `style()` and the one inside it never reached the page, and as CSS they did.

## The verdicts

| # | Verdict | Numbers |
| --- | --- | --- |
| Q1 | Confirmed | After step 2 all seven lesson pages were byte-identical to the ones `page.awk` wrote (`cmp`, against copies taken before the edit). Before `page.awk` was deleted, every other page in the repo was compared too: the eight pages of `tests/build.sh`, the seven entries of `bench/build.sh` and `bench/strings/build-st.sh` (their `index.html` and `cdom.legacy.js`), and `bench/strings/pages-s.sh`'s synth-s, all byte-identical; and `tests/page/run.sh`'s differential ran 93 page, mode and title cases through both tools with the same bytes, exit status and message |
| Q2 | Refuted, on the low side | Brotli changed by -28, -11, -57, +7, -35, -12 and -50 bytes; lessons 3 and 7 are more than 40 bytes smaller, past the bound. The restated prediction (+30 to +110, all seven) was refuted too, before the comment rule as after it (-7 and +8 on lessons 3 and 5, +123 on lesson 7). The CSS codes better as its own text in the page's `<head>` than as one entry of a JavaScript string table, by more than the formatting costs |
| Q3 | Confirmed | Every `app.wasm` is 14 bytes smaller |
| Q4 | Confirmed | Every table shrank by its CSS length plus 12 or 13: within 1 of the prediction on every lesson |
| Q5 | Refuted on the range, by about 0.01 ms; its falsifier did not fire | `mainms`, median of 15 fresh processes, two passes: lesson 1 0.520 to 0.465 and 0.510 to 0.450 ms (-0.055, -0.060; predicted 0.00 to 0.05); lesson 5 1.165 to 1.050 and 1.160 to 1.050 (-0.115, -0.110; predicted 0.01 to 0.10). No gain over 0.15 ms, and not slower. Lesson 5's touched wasm memory went from 32 to 28 KB, its JS heap from 610.7 to 607.0 KB |
| Q6 | Not measurable | First contentful paint, lesson 1: 48.9 to 46.5 ms in one pass, 48.0 to 55.8 in the other; lesson 5: 48.5 to 45.6, then 52.4 to 49.6. The two passes disagree by more than any shift |
| Q7 | Refuted | Leaving the comments out would have saved 61, 66, 50, 43, 43, 104 and 173 brotli bytes: over 100 on lessons 6 and 7, so, as the design said, it was designed and added (see the design's "Deviations"). Measured with a throwaway script on the built pages; the tool's own rule then gave exactly the stripped sizes (8,859 to 14,416 above) |
| Q8 | Confirmed for every pixel the styles decide; by the letter refuted on one PNG, whose difference is a running clock | Every start screenshot, in the light and the dark scheme, is byte-identical before and after (14 pairs), and so are the end screenshots of lessons 1 to 5 in both schemes and lesson 6 in the light one (11 pairs) after step 7; after the comment rule the 14 start pairs and the 10 end pairs of lessons 1 to 5 were compared again, all byte-identical. Lesson 6's dark end shot differed in one line of text, a lap time (00:00.24 against 00:00.25: the laps are real time), so lesson 6's end shot is not a pixel oracle either. Mutation check: lesson 1's `margin: 3rem auto` changed to `4rem auto` made its start shot differ, and putting it back made it equal again |
| Q9 | Refuted, by one byte | The default page with no stylesheet grew by 17 bytes, not 16: `<style>` and its newline are 8, `</style>` and its newline 9 |
| Q10 | Confirmed | `make -q` in lesson 1, median of 20 runs, two passes: 11.5 and 11.4 ms before (step 4's tree), 17.6 and 17.6 ms after, 6.2 ms more (the bar was 10). Make expands the page's prerequisites twice, so the list command runs twice per make |
