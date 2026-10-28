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
