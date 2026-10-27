# cdom: C replaces React - results

Measured 2026-10-27 on an Apple M1 (16 GB, 8 cores), macOS 27.0.1, Chrome 156.0.8078.47 (shipping build, run headless).
Another agent was building on the same Mac, so every timed run waited until the 1-minute load average was under 4, and each number below carries the load it started at (`@2.25` means a load of 2.25).
No run hit the 20-minute wait limit; every gate passed (results/loads.tsv has the load before and after each run).

## Verdict

cdom lands at a weighted geometric mean of 1.04 against vanillajs's 1.01, inside the 1.2x success line, and ahead of wasm-bindgen (1.09), dioxus (1.30) and React (1.52).
A second full pass gave the same order: vanillajs 1.00, cdom 1.06, wasm-bindgen 1.07 (seven of its nine benchmarks; replace1k errored in that pass), dioxus 1.32 and React 1.49.
It ships 7.7 KB brotli in total (shim plus wasm plus HTML), against React's 51.4 KB.
Its first paint is 87 ms against vanillajs's 63 ms and React's 308 ms.

## Alex's three predictions

1. **React faster on "create 10,000 rows": wrong.**
   React takes 510.2 ms @2.31 and cdom 320.2 ms @2.33; vanillajs takes 310.0 ms @2.26.
   Script time is 204.9 ms for React and 25.2 ms for cdom (20.8 ms for vanillajs).
   Moving 10,000 labels out of C costs almost nothing, because they cross in a few large batches and are decoded with one TextDecoder call per batch.
   The rest is paint and layout, which is the same for everyone.
2. **About equal on "select row": cdom ties vanillajs, and React is about 2x slower.**
   cdom takes 5.4 ms @2.90, vanillajs 5.1 ms @3.06 and React 11.3 ms @3.63 (the harness runs this one at 4x CPU slowdown).
   Script time is 0.4 ms for cdom and 4.2 ms for React.
   Style and paint dominate cdom and vanillajs as predicted, but React re-renders the 1,000-row list to change one class.
   The first pass agrees: cdom 6.3 ms, vanillajs 5.3 ms and React 9.9 ms.
3. **C faster on "swap rows": right, by 7.5x.**
   cdom takes 18.7 ms @2.96 and React 141.0 ms @3.29 (4x CPU slowdown); vanillajs takes 22.5 ms @2.41.
   Script time is 0.7 ms against 27.7 ms.
   cdom's diff sees a head that became a tail and a tail that became a head, and emits two moves.

## Every operation (pass 2, median of 15)

Each cell is the median total in ms, then the factor against the fastest of these seven, then the load at the start of that run.
The weighted geometric mean uses the harness's own CPU weights (webdriver-ts-results/src/Common.ts), with factors taken against the fastest of these seven entries rather than the full official table.

| benchmark (ms, median) | vanillajs | react-hooks | cdom | cdom-pertext | cdom-notpl | wasm-bindgen | dioxus |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 01_run1k | 28.8 (1.00) @2.25 | 35.6 (1.24) @2.13 | 29.9 (1.04) @2.02 | 31.3 (1.09) @2.49 | 32.6 (1.13) @2.85 | 32.1 (1.11) @3.95 | 36.4 (1.26) @3.40 |
| 02_replace1k | 32.5 (1.00) @3.82 | 43.9 (1.35) @3.50 | 33.1 (1.02) @3.51 | 33.3 (1.02) @3.79 | 33.5 (1.03) @3.72 | 35.0 (1.08) @3.16 | 39.3 (1.21) @2.91 |
| 03_update10th1k_x16 | 15.6 (1.00) @2.70 | 22.5 (1.44) @3.96 | 16.0 (1.03) @3.10 | 20.4 (1.31) @2.86 | 20.4 (1.31) @3.68 | 17.0 (1.09) @3.25 | 22.7 (1.46) @3.31 |
| 04_select1k | 5.10 (1.02) @3.06 | 11.3 (2.26) @3.63 | 5.40 (1.08) @2.90 | 6.00 (1.20) @3.40 | 5.00 (1.00) @2.70 | 5.60 (1.12) @1.95 | 5.90 (1.18) @2.40 |
| 05_swap1k | 22.5 (1.23) @2.41 | 141.0 (7.70) @3.29 | 18.7 (1.02) @2.96 | 18.3 (1.00) @2.99 | 18.6 (1.02) @2.86 | 20.5 (1.12) @3.22 | 27.0 (1.48) @2.95 |
| 06_remove-one-1k | 15.9 (1.05) @2.38 | 19.9 (1.32) @2.15 | 16.1 (1.07) @2.92 | 15.3 (1.01) @2.61 | 15.1 (1.00) @2.44 | 15.9 (1.05) @2.63 | 18.5 (1.23) @2.28 |
| 07_create10k | 310.0 (1.00) @2.26 | 510.2 (1.65) @2.31 | 320.2 (1.03) @2.33 | 315.2 (1.02) @3.00 | 329.0 (1.06) @3.81 | 341.9 (1.10) @3.25 | 373.6 (1.21) @2.69 |
| 08_create1k-after1k_x2 | 34.1 (1.00) @2.85 | 41.7 (1.22) @2.99 | 35.4 (1.04) @2.95 | 35.1 (1.03) @2.69 | 36.4 (1.07) @2.87 | 36.5 (1.07) @2.37 | 40.1 (1.18) @2.67 |
| 09_clear1k_x8 | 13.0 (1.00) @2.67 | 24.9 (1.92) @2.38 | 13.8 (1.06) @3.57 | 13.6 (1.05) @2.94 | 13.4 (1.03) @2.98 | 13.9 (1.07) @3.13 | 23.3 (1.79) @3.70 |
| **weighted geometric mean** | **1.01** | **1.52** | **1.04** | **1.08** | **1.08** | **1.09** | **1.30** |

The entries are vanillajs, react-hooks (React 19.2.0), cdom (interned strings and templates, the default), cdom-pertext (per-op strings), cdom-notpl (no templates: every row is built from open, class and text ops), wasm-bindgen 0.2.84 and dioxus 0.7.2.
wasm-bindgen and dioxus ran from the prebuilt bundles that ship in the benchmark repo, so no Rust was installed.
The harness is krausest/js-framework-benchmark at ca8715d ("Chrome 156 results"), run with its default puppeteer runner and `--headless`.

Script time (ms, median), from the same runs:

| benchmark | vanillajs | react-hooks | cdom | cdom-pertext | cdom-notpl | wasm-bindgen | dioxus |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 01_run1k | 2.2 | 8.8 | 3.1 | 3.0 | 4.0 | 4.4 | 7.0 |
| 02_replace1k | 4.5 | 15.7 | 5.3 | 5.3 | 6.7 | 7.0 | 11.4 |
| 03_update10th1k_x16 | 0.8 | 6.2 | 0.9 | 1.2 | 1.3 | 1.2 | 2.6 |
| 04_select1k | 0.5 | 4.2 | 0.4 | 0.4 | 0.3 | 0.5 | 0.9 |
| 05_swap1k | 0.7 | 27.7 | 0.7 | 0.4 | 0.5 | 0.4 | 3.4 |
| 06_remove-one-1k | 0.5 | 2.0 | 0.3 | 0.3 | 0.1 | 0.3 | 1.5 |
| 07_create10k | 20.8 | 204.9 | 25.2 | 25.2 | 37.2 | 35.2 | 65.1 |
| 08_create1k-after1k_x2 | 2.2 | 9.5 | 3.1 | 2.9 | 4.2 | 4.3 | 7.7 |
| 09_clear1k_x8 | 9.8 | 22.0 | 10.6 | 10.8 | 10.6 | 11.1 | 19.7 |

## Startup, memory and bytes

First paint (the harness's 43_first-paint from its size run, which does one load per run) is the median of five runs:

| | vanillajs | react-hooks | cdom | cdom-pertext | cdom-notpl | wasm-bindgen | dioxus |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| first paint (ms) | 62.6 | 308.3 | 86.6 | 95.8 | 90.6 | 103.7 | 764.6 |
| page bytes, uncompressed (KB) | 11.3 | 190.3 | 26.4 | 26.0 | 25.7 | 47.0 | 419.6 |
| page bytes, brotli (KB) | 2.5 | 51.4 | 7.7 | 7.8 | 7.7 | 14.5 | 114.9 |

The loads at the start of the five first-paint runs were between 3.19 and 3.90 for every entry (results/loads.tsv, tags fp1 to fp5).
cdom pays about 24 ms of first paint over vanillajs, because its buttons and table do not exist until the wasm has been fetched, compiled and run; the HTML preloads the wasm so the fetch starts at once.
The dioxus first paint of 765 ms was not investigated.

Memory is the harness's `performance.measureUserAgentSpecificMemory()` in MB, as the median of five runs (`--count 5`):

| benchmark | vanillajs | react-hooks | cdom | cdom-pertext | cdom-notpl | wasm-bindgen | dioxus |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 21_ready-memory (MB) | 0.57 @3.21 | 1.19 @2.36 | 0.61 @2.47 | 0.60 @2.78 | 0.59 @2.68 | 0.60 @3.23 | 0.77 @2.57 |
| 22_run-memory (MB) | 1.87 @3.92 | 4.43 @3.94 | 1.84 @3.74 | 1.81 @2.45 | 1.90 @2.95 | 1.76 @2.91 | 2.12 @2.27 |
| 25_run-clear-memory (MB) | 0.65 @3.61 | 1.97 @2.73 | 0.80 @2.47 | 0.71 @2.35 | 0.80 @2.94 | 0.69 @2.61 | 2.12 @3.52 |

That metric does not appear to count wasm linear memory.
cdom starts with 9 pages (576 KB), mostly the op, argument and string buffers, yet its ready memory is only 0.04 MB above vanillajs's.
Most of those pages are never touched, so they are not resident either, but the metric flatters every wasm entry.
The 0.15 MB that cdom keeps after five create-and-clear cycles, above vanillajs, is the JS side of the string intern table: about 3,500 label strings stay cached.
cdom-pertext, which caches nothing, keeps 0.06 MB above vanillajs.

The wasm itself:

| build | bytes | gzip -9 | brotli |
| --- | ---: | ---: | ---: |
| cdom -O2 (benchmarked) | 20,452 | 6,633 | 5,458 |
| cdom -Oz | 9,561 | 4,659 | 4,153 |

-Oz costs no measurable speed here: in-page script timings (tools/quick.mjs, load 3.71) were 1.82 ms against 2.08 ms for run 1k, and 18.8 ms against 17.2 ms for create 10k, which is within run-to-run noise.
Less than 10% of cdom's script time is spent in C (CDP profile of create 10k: about 1.5 ms of wasm per click; the rest is the shim's DOM calls).

## The shim

shim/cdom.js is 126 lines (117 non-blank, non-comment), 6,175 bytes, 2,341 bytes gzip.
It has 27 op codes, the per-batch string decoder, the delegated event inbox and one import for reading an input's value.
The design estimated 300-400 lines once history, fetch, storage, clipboard, focus and rAF imports exist; none of those were needed here.

## Step 0: what a call across the boundary costs

Chrome 156, median ns per call over 31 samples of 100,000 calls (2,000,000 for the two cheapest rows); load 3.66 at the start and 4.25 at the end:

| call | ns |
| --- | ---: |
| empty import, wasm to JS | 4.65 |
| empty JS function, JS to JS (V8 inlines it) | 0.45 |
| setAttribute from JS | 73 |
| setAttribute through an import, string looked up by index | 79 |
| setAttribute through an import, string decoded with TextDecoder each call | 205 |
| setAttribute through an import, string decoded with a fromCharCode loop each call | 118 |
| setAttribute batched: C writes records, one apply call per 4,096 | 80 |
| the batch write alone, on the C side | 0.30 |
| textContent from JS | 135 |
| textContent through an import | 146 |

The call itself is nearly free: an import costs about 5 ns, against 73-135 ns for the DOM work it does.
What costs is turning C's bytes into a JS string: a TextDecoder call per string adds 126 ns, which is more than the setAttribute itself.
That is why the design sends each string once and decodes a whole batch in one call, and why batching calls alone buys almost nothing in Chrome.
Firefox is not installed on this Mac.
Safari is installed, but driving it needs "Allow remote automation" turned on in its settings, and it was already running as Alex's own process, so it was not touched.
No Firefox or Safari numbers exist yet.

## Interned strings against per-op strings

Interning wins on partial update (16.0 ms against 20.4 ms, and 16.7 against 19.6 in pass 1) and ties elsewhere.
The per-op build re-sends and re-decodes every label it touches, while the interned one sends an id for a label JS already holds.
Interning costs about 0.09 MB of JS heap after five create-and-clear cycles (0.80 MB against 0.71 MB), and it needs an eviction policy before a real app could use it, because nothing ever leaves the table.
Templates matter more than interning: without them every row is 25 ops instead of 11, and create 10k script goes from 25.2 ms to 37.2 ms.

## The library

C owns the model, the view state and the diff, and emits ops into three buffers in linear memory: one byte per op, a u32 argument stream and a string section.
A flush hands the shim the four lengths in one import call, either at the end of an event or when a buffer fills.
Strings are interned in C; the first use of a string sends its bytes with an id, and later uses send only the id.
Each flush decodes all its new strings with one TextDecoder call (a char loop below 64 bytes) and slices them by UTF-16 length, which C counts when it interns.
Tag and attribute names always go through the intern table, which is how they become the enum; numbers cross as u32 and the browser formats them.
"Last node" is implicit: open, close, text, class, attr, name, listen and the walk ops (down, next, up, sel) all act on it, as in sledgehammer.
Templates are built once with the same ops and deep-cloned per row; the row's id and label are set by walking into the clone.
Only list roots get node ids; inner nodes are reached by a path from their root (sel then down and next), so 10,000 rows hold 10,000 JS references; the first version named four nodes per row.
Events are delegated: one listener per event type on the mount element, and each listening node carries one expando with its id.
The shim writes a fixed record into the C inbox: the type, the listening ancestor's id, coordinates, button, key, modifiers and the element-child path from that ancestor down to the target, one byte per level.
So a row has one listener, and C reads which cell was clicked from the path.
The keyed list diff trims matching heads and tails, moves a head that became a tail (or the reverse) in one op only when another survivor sits next to it, and otherwise maps keys and moves only nodes off the longest increasing subsequence.
When a list owns its parent, clearing it is one op, replacing every key is a clear plus a fresh fill, and a large first fill is built off-document and attached once.

## Tests

tools/smoke.mjs drives every button and row action in Chrome and checks the DOM, including that swap and remove keep the same nodes.
The official isKeyed check passes for all three cdom builds.
tests/listfuzz.c runs 20,000 random list transitions per seed through the real shim (five seeds) and checks DOM order, that every surviving key keeps its node, and that the number of moves never exceeds the survivors minus their longest in-order run.
It also checks ASCII, two-byte, three-byte and astral strings in both decode paths.
Every test was mutation-checked: ten mutants of the diff, three of the string path and five of the app and shim were all caught.
The one survivor was the choice between the two decode paths, which give the same strings for ASCII by design.
The fuzz's move bound found a real defect on its first run: the head and tail cross-moves, applied unconditionally, moved nodes that were already in place, and they are now gated as described above.

## What surprised me

The boundary is not the cost; strings are.
An import call is 5 ns and the DOM call behind it is 75-135 ns, so a per-op import design would have been nearly as fast as batching for setAttribute.
C's share of the work is tiny: under 10% of script time, and -Oz at half the bytes is no slower.
cdom is closer to vanillajs than to any framework, because it does what vanillajs does (clone a template, set two text nodes, insert) and adds only a switch dispatch per op.
React's swap cost is its keyed algorithm on this shape: one left-to-right pass that keeps the first row in place and so moves the rows between the two, 27.7 ms of script under 4x slowdown and a much larger paint for what is two moves in cdom and vanillajs.
The memory benchmark is blind to wasm linear memory, so it cannot be used to judge the linear-memory floor the research worried about.
The first version of cdom's swap took 65 us of script, all in the generic map-and-LIS path; the head/tail check took it to 10 us.

## Reproduce

```
cd /Users/alex/Dev/cdom
bench/build.sh                  # cdom, cdom-pertext, cdom-notpl, cdom-oz into jfb/frameworks/keyed
tests/build.sh && node tools/fuzz.mjs
(cd jfb/server && PORT=8093 npx tsx index.ts) &
node tools/smoke.mjs cdom
TAG=p2 KEEP=pass2 tools/final.sh 01_ 02_ 03_ 04_ 05_ 06_ 07_ 08_ 09_
node tools/table.mjs $PWD/results/pass2 <frameworks> results/loads.tsv p2
node tools/run_micro.mjs
```

jfb/ is a shallow clone of krausest/js-framework-benchmark and is not committed; bench/build.sh writes the entries into it.
The harness needs `npm ci` in jfb, jfb/server and jfb/webdriver-ts (then `npm run compile` there), and `npm ci && npm run build-prod` in jfb/frameworks/keyed/react-hooks.
An empty jfb/frameworks/non-keyed directory has to exist, because the shallow clone has none and the server's listing fails without it.
