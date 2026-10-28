# cdom perf log (track 2: the plain-JS floor)

## API changes for track 1

- `cdom.start(url, mount, env)`: `mount` may now be an element id (a string) as well as an element.
  With an id, the wasm is fetched, compiled and instantiated at once, and the app starts when the DOM is ready.
  Passing an element works as before.
- The repo root now has a package.json with one devDependency (terser 5.44.0); run `npm install` once at the root before bench/build.sh, which minifies the shim before inlining it.
- bench/build.sh builds -Oz by default; the cdom-oz variant became cdom-o2.
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
