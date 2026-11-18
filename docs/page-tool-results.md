# The page tool: results

The measurements of [page-tool-design.md](page-tool-design.md), its predictions Q1 to Q10 marked confirmed, refuted or not measurable, with numbers.
Measured 2026-11-18 on this Mac (Apple silicon, headless Chrome), branch `strings`.

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
