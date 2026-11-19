# SIMD pick: handoff (paused 2026-11-19 for a quiet Mac)

Branch simd-pick, based on main 2bf7c2a (after #55 and the CI commits).

## Done and committed

- cdom.mk: `APP_SIMD := 1` builds `$(APP_WASM)` (scalar, the same bytes as without it) and `app.simd.wasm` (the same bytes as `CDOM_IMAGE_SIMD=1` makes), each with its own objects under `<module>.cdom/`.
- A part's SIMD build is its `_SIMD_SRC` and `_SIMD_CFLAGS` (image: OWN and VENDOR); `CDOM_IMAGE_SIMD=1` still works and gives the same bytes as main.
- The twin's `.lits` must equal APP_WASM's, or the twin is removed and make stops (`cdom_mk_lits_same`).
- Refused: APP_SIMD with CDOM_IMAGE_SIMD=1, a value not 0 or 1, an app with no module that has a SIMD build.
- tools/cdpage.c: `--simd-wasm FILE` / `--simd-wasm-url URL` write `["scalar","simd"]` at `/*CDOM_WASM*/`; a pair with no marker is refused.
- shim/cdom.js start(): an array url is picked by `WebAssembly.validate` of the 29-byte probe (tools/simd-probe.wat; `wasm-as -all` makes the same bytes).
  Shim cost: +138 bytes minified, +64 bytes brotli (18,320 to 18,458; 6,896 to 6,960).
- shim/boot.js: the no-WebAssembly fallback loads `url[0] + ".js"` for a pair.
- tests/pick/run.sh and tools/pick.mjs: four pages (bundled and separate, readable and minified shim); real Chrome gets app.simd.wasm only, validate stubbed false gets app.wasm only; the two picks give identical pixels on bench/image's 4 inputs at 5 boxes.
  All 33 checks passed on the first commit.
- bench/image/resize.mjs: equal.mjs's resize-and-hash, shared with tools/pick.mjs.
- Page cost, bench/image's app bundled: 46,367 to 71,069 bytes brotli (+24.7 KB); app.wasm 28.9 KB brotli, app.simd.wasm 30.8 KB.

## Done in this commit, not yet run

- tests/build.sh builds tests/out/legacy.simd.wasm (-msimd128, lits compared); tests/legacy.html starts the pair; tools/legacy.mjs expects legacy.simd.wasm on wasm and neither .wasm on LEGACY=1.
  Both runs (plain and LEGACY=1) passed before the pause.
- tests/link/run.sh: the APP_SIMD block at its end (byte-identity, pair in the page, refusals, lits mismatch via tests/link/parts/lib/lib_a.c `-DPARTS_SIMD_LIT`).
  It was started and stopped by the pause: run `tests/link/run.sh` first and fix what fails.

## Next

1. Run tests/link/run.sh; fix.
2. Mutants in tools/mutate.py (PICK = ['tests/pick/run.sh']): choice inverted (`url[+WebAssembly.validate` to `url[+!WebAssembly.validate`), probe byte broken (253 to 254), cdpage writing the bundled pair in the wrong order, the lits check removed from cdom.mk (tests/link), boot.js fallback taking url[1] (LEGACY=1 tools/legacy.mjs).
   Note mutate.py works in a copy of the tracked tree: tests/pick/run.sh makes bench inputs itself (2 s).
3. Add tests/pick/run.sh to .github/workflows/ci.yml; `gh pr checks` must be green on the head.
4. Byte-identity against main for apps without APP_SIMD (one-time: bench build-js and build-simd were already identical; also tests/image/wasm.mk js/eh, tests/link/app.mk).
5. README ("Starting an app", "Building an app", the image module item) and docs/image-simd-hand.md: the API, the shim and page costs.
6. Paeth (coordinator's extra task): the 3-byte Paeth SIMD path is 0.86x on the Mac; profile on the Mac, hypothesis first, keep the faster of SIMD-Paeth-fixed and scalar Paeth in the SIMD build.
   Timed: ask the coordinator "need quiet CPU" and wait for "quiet now" before timing.
7. Open the PR against main; delete this handoff file in the last commit (its content goes into the PR body).
