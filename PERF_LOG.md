# cdom perf log (track 2: the plain-JS floor)

## API changes for track 1

- **The op stream is gone (#4).** Each DOM operation is now its own wasm import (module "cdom": at, open, close, text, text_num, attr, class, name, listen, value, value_num, tpl_save, clone, slot, tpl_text, tpl_text_num, walk, walk_value, walk_value_num, set_text, set_text_num, set_attr, set_class, remove, free, move, clear, detach, attach), called the moment C emits the op.
  Only strings are batched (import "strings"), and interned strings are queued when interned.
  The input-value import is renamed from "value" to "input". cd_layout() now returns 3 words: string bytes, string meta, inbox.
  cdom.h is unchanged; cd_flush() now sends pending strings and any held walk. Walks (cd_sel/cd_down/cd_next/cd_up) are held in C and cross as one call.

- `cdom.start(url, mount, env)`: `mount` may now be an element id (a string) as well as an element.
  With an id, the wasm is fetched, compiled and instantiated at once, and the app starts when the DOM is ready.
  Passing an element works as before.
- The repo root now has a package.json with one devDependency (terser 5.44.0); run `npm install` once at the root before bench/build.sh, which minifies the shim before inlining it.
  terser is a build-time minifier only: nothing from node_modules is shipped or loaded at run time, the shipped page carries only the minified output of shim/cdom.js, and shim/cdom.js stays the readable source.
- bench/build.sh builds -Oz by default; the cdom-oz variant became cdom-o2.
- New in cdom.h (additive): template slots. `cd_slot(k)` marks the last built text node as slot k of the template being built; `cd_tpl_text(t, k, s)` / `cd_tpl_text_num(t, k, n)` set it in the template itself before `cd_clone(t)`, so the clone needs no walk. Unchanged values are not re-sent. A slot keeps its last value: set every slot before each clone.
- The benchmark page inlines the shim (bench/build.sh replaces `/*CDOM_SHIM*/` in index.html); there is no separate cdom.js in the entry any more.

## How things are measured

- Paired, load-gated harness runs: tools/pair.sh (vanillajs and the cdom entries in the same run, results in results/pairs/<tag>), tools/ratio.mjs (ratio to vanillajs per benchmark, weighted geometric mean).
- First paint and bytes: tools/fpruns.sh, the harness's own 40_ size run, R rounds interleaved.
- Gates: tools/gate.sh (harness keyed check, smoke, DOM identity against the frozen baseline entry cdomb, listfuzz all seeds).
- Ratios are to vanillajs in the same run; loads are the 1-minute load at the start of each run.

## Baseline (results/pairs/base, loads 2.34-3.99)

Weighted geomean against vanillajs: cdom 1.005 and 1.028 (two identical builds, so about 2% is noise), react-hooks 1.518.
First paint (11 rounds, loads 3.44-3.93): vanillajs 50.3 ms, cdom 90.5 ms (ratio 1.80). Compressed bytes 2.5 KB against 7.7 KB.

## Accepted changes

| # | change | operation | old ratio | new ratio | cause |
| --- | --- | --- | ---: | ---: | --- |
| 1 | inline the shim, start the wasm from <head> before the stylesheet, mount on DOMContentLoaded | first paint | 1.80 | 1.45 | the external script waited for the stylesheets (and their @import) before it could even start fetching the wasm; now compile and instantiate overlap the CSS |
| 2 | wasm at -Oz (20,452 -> 9,561 bytes) and the inlined shim minified with terser (index.html 6.8 KB -> 3.7 KB) | first paint | 1.45 | 0.98 | the harness server brotli-compresses every response synchronously at quality 11 (about 1.4 ms per KB raw), and the CSS waits behind it; compressed total 7.6 KB -> 5.5 KB. -Oz costs about 0.1 ms of script on 1k-row creation (fresh.mjs), under 0.5% of those totals; the paired CPU runs (results/pairs/oz, oz2) moved within noise apart from load spikes visible as 22-26 ms Layout outliers in single iterations |
| - | noise check of #2 (results/pairs/noise1, every start at load < 3, range 1.98-2.99) | all CPU | parent 1.039 | 1.014 | no regression: -O2+minified alone gave 1.027; every per-op difference is inside the parent's own spread |
| 3 | template slots: text set in the template, then one clone; no walk of the clone | create (run1k, replace, create10k, append) | create10k 1.023, run1k 1.070 | create10k 1.004, run1k 1.060 | vanilla does clone + firstChild x4 + nextSibling + 2 nodeValue + appendChild (9 DOM calls per row); cdom did the same plus a parentNode (10); cdom now does 2 nodeValue on the template + cloneNode + insertBefore (4). Script, harness medians: create10k 26.3 -> 21.0 ms (vanilla 20.8), run1k 3.2 -> 2.8, append 3.2 -> 2.8 (x2 throttle). results/pairs/slots, loads 1.15-2.50; geomean 1.037 -> 1.033 |
| - | final check of #3 (results/pairs/final1, final2; loads 1.76-2.98) | all CPU | first build 1.038 / 1.029 | 1.020 / 1.018 | react-hooks 1.477 / 1.464 in the same passes |
| rejected | freed ids coalesced into runs (one FREE op + Array.fill per run instead of one op per id) | clear | 1.048 | 1.040 | script 10.3 -> 10.1 ms at x8 throttle, inside noise; not worth the extra C state. The run code had a wraparound bug the new test found (a reset index of 0xffffffff matched nops == 0). The two-list adjacent-id test stays in listfuzz: it checks that freeing one list's ids never drops a live neighbour's reference |
| 4 | direct calls: every op is its own import, called at once; only strings are batched (queued at intern time); walks cross as one call | all CPU | 1.045 / 1.035 (op list, same passes) | 1.009 / 1.001 | the direct-call experiment below; results/pairs/dir3, dir4 (loads 1.83-2.96). Script vs vanilla, dir4: run1k 2.4 vs 2.3 (list 2.7), create10k 19.1 vs 20.8 (list 21.6), clear 9.6 vs 9.4 (list 10.4). First paint 1.005 (list 1.029), compressed 5.8 KB |

## Experiment: direct calls against the op list (branch direct-calls, worktree ~/Dev/cdom-direct)

Question: instead of C writing an op stream that the shim interprets, does C calling DOM functions directly through wasm imports win?

Arms, all with template slots, interned strings, -Oz and the inlined minified shim; only the emit layer differs:
- all-list: the op stream (shim 136 lines).
- (a) all-direct: one arrow import per op, generated from apply()'s cases (exp/gen_direct.py), sharing the shim's node and string tables; a direct op flushes pending list ops first.
- (b) direct-native: DOM methods imported as `Function.prototype.call.bind(method)`, with nodes, strings, templates and walk state in externref tables (`__externref_t`, `__builtin_wasm_table_*`, `-mreference-types`; clang 22 expressed it without trouble; shim 92 lines).
- hybrids: 12 balanced random-half configurations over the 16 ops the benchmark executes (each op direct in 6 of 12), the fit's best assignment, and "all direct except the update walk and CLEAR".

Micro (micro/direct, ns per call from a wasm loop, Chrome 156, quiet machine): nodeValue arrow 41, native-bound 48.5, op list 39.5; firstChild arrow 15-18, native 18, op list 9.5; cloneNode and insertBefore 570-1000 for all three.
V8 does not fast-path natively bound DOM methods called from wasm here: the bound setter is slower than an arrow. A crossing costs about 5 ns more than an op-list record.

Harness, weighted geomean vs vanillajs, three load-gated sessions (screen, val1, val2; loads 1.55-2.99):
| arm | screen | val1 | val2 |
| --- | ---: | ---: | ---: |
| all-list | 1.034 | 1.039 | 1.031 |
| (a) all-direct | 1.010 | 1.011 | 0.999 |
| (b) direct-native | 1.005 | 1.022 | 1.022 |
| best hybrid from the fit (CLOSE, LISTEN, TPL_TEXT, FREE) | - | 1.020 | 1.008 |
| hybrid: all direct but SEL/DOWN/NEXT/VALUE/CLEAR | - | 1.004 | 1.001 |

Fitted per-op deltas (direct minus list, ms of harness script, standard error; exp/fit.py over 210 iterations per benchmark): FREE -0.22 +-0.03 (replace) and -0.33 +-0.06 (clear); LISTEN -0.09 +-0.03 (run1k); CLOSE -0.46 +-0.12 and TPL_TEXT -0.65 +-0.18 (create10k); list better: CLEAR +0.15 +-0.05, DOWN +0.28 +-0.06, NEXT +0.12 +-0.05 (update), CLOSE +0.03 +-0.015 (append). The rest are inside 2 SE.
Flushes forced per click: random halves 1,000-2,000 per 1k rows (every switch between modes); all-direct 125-720, all from first-time strings; the update walk 100-300 crossings direct.
Why direct wins although each crossing costs more: in the harness's measured click the list arm takes incremental-GC marking steps and V8 interrupt handling (about 0.3 ms on run1k) that the direct arm does not, while synchronous script time is about equal; the one big apply() after five warm-up clicks is the likely difference. Not proven further.
The hybrids do not beat all-direct beyond noise. What the hybrid taught was where direct loses: the update walk (one crossing per step) and strings decoded one at a time. The shipped design fixes both inside the direct model: a walk crosses as one call, and strings are queued at intern time so a burst decodes in one call. That made update 1.03 / 1.01 instead of 1.05-1.06.
Every configuration passed the keyed check, smoke, DOM identity and listfuzz built with its own mask. One DOM-identity failure on the fit-best hybrid did not reproduce in 11 reruns; its log had been overwritten, so gate logs are now per entry.

## Final (results/pairs/last1, last2; every start at load < 3, range 1.82-2.99)

Weighted geomean vs vanillajs: first build (cdomb) 1.036 / 1.033, last build 1.007 / 0.998, react-hooks 1.466 / 1.457.
First paint 1.005 vs vanillajs (first build 1.80); compressed 5.8 KB (first 7.7, vanillajs 2.5).
Stop: every operation is within noise of vanillajs, and create10k is below it in all four passes since #4 (0.987-0.993; script 19.0-19.2 ms against 20.7-20.9).

DOM calls per operation (tools/domcount.mjs, one measured click after the harness's warm-ups):
| operation | vanillajs | cdom first | cdom last | floor |
| --- | ---: | ---: | ---: | --- |
| run1k, replace1k, append (per row) | 9 | 10 | 4 | 4: clone, 2 text writes, insert |
| create10k | 90,008 | 100,004 | 40,002 | 40,000 |
| update (100 rows) | 403 | 500 | 500 | 100 text writes, plus reaching the nodes (400 without cached references) |
| select | 12 | 7 | 7 | 2 className writes |
| swap | 8 | 4 | 4 | 2 insertBefore |
| remove | 17 | 9 | 9 | 1 remove |
| clear | 6 | 1 | 1 | 1 textContent |
cdom's select and remove counts include the event path the shim computes (which cell was hit), and swap's the parentNode of each moved row.

What is irreducible: rendering (style, layout, paint) is over 90% of every total and identical for identical DOM, so totals can move only by script; fragment appends and three clear strategies changed nothing measurable.
cdom's remaining script over the floor is the keyed library's own work: the model and keyed diff in wasm (about 0.15 ms per 1k rows), one reference and one expando per row so rows can be moved, removed and hit by events, and the event path.
