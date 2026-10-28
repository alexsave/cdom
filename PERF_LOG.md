# cdom perf log (track 2: the plain-JS floor)

## API changes for track 1

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

## Proof: why direct calls win (2026-10-28)

Verdict: tiering of the op list's one big `apply()`, not GC and not allocation.
Splitting `apply()` into one small function per op (same op encoding, same batching, dispatch through a table) closes the gap completely; putting the switch back reopens it.

Arms (exp/cfg.sh; jfb entries kept for these numbers): `pf-list` (op list, mask 0), `pf-direct` (all ops direct, 0x7ffffffe), `pf-split` (op list, `apply` dispatching through a table of per-op functions, exp/gen_split.py), `pf-dswitch` (all ops direct, but every import routed into one big switch function, exp/gen_dswitch.py).
Sessions: exp/proof.sh, tools/pair.sh with LOADMAX=3 on port 8703, COUNT=15, order alternated per session; CIs over sessions with a t interval (exp/ci.mjs, results/proof/base_ci.txt).

Harness total, ratio of medians per session, 8 sessions (n=8), loads at start 1.40-2.93 (72 starts):
| ratio | 01_run1k | 07_create10k | 09_clear1k_x8 | weighted geomean |
| --- | ---: | ---: | ---: | ---: |
| list / direct | 1.035 [1.023, 1.047] | 1.007 [1.004, 1.011] | 1.050 [1.040, 1.060] | 1.029 [1.023, 1.035] |
| split / direct | 0.998 [0.993, 1.003] | 0.998 [0.994, 1.001] | 1.001 [0.989, 1.013] | 0.999 [0.996, 1.001] |
| list / split | 1.037 [1.025, 1.049] | 1.010 [1.006, 1.013] | 1.049 [1.034, 1.064] | 1.031 [1.025, 1.036] |

Harness script time, same sessions:
| ratio | 01_run1k | 07_create10k | 09_clear1k_x8 | weighted geomean |
| --- | ---: | ---: | ---: | ---: |
| list / direct | 1.049 [1.019, 1.079] | 1.111 [1.096, 1.126] | 1.058 [1.049, 1.067] | 1.072 [1.056, 1.089] |
| split / direct | 0.937 [0.905, 0.970] | 0.983 [0.974, 0.993] | 1.001 [0.989, 1.014] | 0.969 [0.957, 0.982] |
| list / split | 1.119 [1.090, 1.149] | 1.130 [1.116, 1.144] | 1.056 [1.040, 1.073] | 1.106 [1.092, 1.121] |

The gap is real (the list arm's CI excludes 1 on every benchmark) and the split arm removes all of it; the split op list is even a little faster than direct calls in script (0.969), as the micro said a list record is cheaper than a crossing.

Mechanism (V8 code log, `--log-code --log-deopt`, tools/clickprobe.mjs with marks around the measured click, 3 probes per arm on run1k):
- list: `apply` (minified W) is entered once per flush with a loop over thousands of ops, so it tiers up by OSR: Ignition, Sparkplug, Maglev, a wrong-map deopt, Maglev OSR, Maglev, and a TurboFan OSR compile that lands inside the measured click in 3 of 3 probes. The measured click runs its first part in Maglev code.
- direct: the per-op imports are called thousands of times and reach Maglev during the first warm-up click; nothing big is compiled during the measured click.
- split: the per-op functions reach Maglev during the first warm-up like direct; only the 3-line dispatch loop is OSR'd.
- Bytecode budget interrupts (the tiering checks) inside the measured click, 20 clicks per arm (tools/clickprobe.mjs, harness steps including its forced GC): create10k list 73.4 per click against direct 3.0, split 6.6, dswitch 4.1; run1k list 4.0, direct 1.05, dswitch 1.0. These are the "interrupts" the earlier note saw; they are tiering, not GC.

GC (same 20-click probes, events on the renderer main thread between the click and its frame's Commit): run1k and clear: no MinorGC, MajorGC or incremental marking in any arm. create10k: one MajorGC (1.6-2.0 ms) and about 124 incremental marking steps (1.8 ms) per click in every arm alike, none inside the click's script. The harness's own traces of the 8 sessions agree (tools/hbreak.mjs: 0 GC on run1k for every arm; create10k 3.5-3.7 ms in both list and direct). The earlier "incremental marking in the measured click" came from tools/onetrace.mjs, which does not force a GC before the click as the harness does.

Allocation per run1k click (HeapProfiler sampling at 16 bytes, both GC kinds included, 8 clicks each): list 93.0 KB (apply 35.5 KB, substring 2.6 KB), direct 111.2 KB (d13 clone 20.8 KB, d9 listen 14.3 KB). Direct allocates more, so allocation is not why it wins.

What was ruled out on the way: string representation (TextDecoder strings are external one-byte in both arms, %DebugPrint); decoding every string with the char loop in the list arm or with TextDecoder in the direct arm moved nothing in an exploratory pass (n=10, one session).

Not run (stopped early to report): the Chrome flag arms (exp/chrome/*.sh wrappers for --no-incremental-marking, --no-opt --no-maglev, --max-lazy, --min/max-semi-space-size 64 and 128 are written and verified to take effect: --no-opt makes a numeric loop 10x slower, semi64 takes MinorGC from 20 to 0 on a 40 MB allocation probe; Chrome 156 has no --always-turbofan), the dswitch harness sessions (exp/proof_rest.sh runs both), and Firefox (tools/ffbench.mjs is written, not run).
