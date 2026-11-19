# The image module with wasm SIMD, part 1: autovectorization and libjpeg-turbo's NEON through SIMDe

The image module (docs/image-design.md) ships scalar wasm.
This note measures two ways to use wasm `simd128` without writing an intrinsic by hand:

- **autovec**: the module's files compiled with `-msimd128`, so clang's vectorizers may use `v128`.
- **simde**: libjpeg-turbo 3.2.1's own Arm NEON intrinsics code (`simd/arm/`) compiled for wasm32, with SIMDe (https://github.com/simd-everywhere/simde, v0.8.2) mapping each NEON intrinsic to `simd128`, wired through libjpeg-turbo's `jsimd` dispatch so the decoder calls it.
  Decode side only.

A parallel job (branch image-simd-hand) writes wasm SIMD loops by hand; this branch is its comparison and writes none.

The make variable is `CDOM_IMAGE_SIMD`: `none` (the default, scalar, exactly the build of the image branch), `simd128` (autovec) or `simde` (NEON through SIMDe, which needs `-msimd128` too, so it includes autovec).

## Predictions (written before any build or measurement)

Every number below was written before anything was built or timed on this branch.
The machine is a Linux x86-64 VM, not the Mac; the bands are for that machine.
"ratio" is an arm's median ms divided by the scalar arm's median ms for the same input in the same interleaved rounds.
The arms are at cdom.mk's `-Oz` unless a name says `-O2`.

**What the module decodes** (read from `src/cdom_image_jpeg.c` and its box 900): `p12.jpg` and `p12p.jpg` (4032 x 3024) are decoded at scale 2/8 (1008 x 756), so libjpeg runs `jpeg_idct_2x2`, fancy h2v2 upsampling and YCbCr to RGBA on a quarter of the pixels.
`p48.jpg` (8064 x 6048) is decoded at 1/8 (1008 x 756): the DC-only 1x1 IDCT, which has no NEON version, then upsampling and colour conversion.
Huffman decoding (`decode_mcu`, scalar in both builds) is therefore the largest part of the decode.

### Instruction counts (static, `llvm-objdump -d` of the linked module, mnemonics that start with `v128.` or a lane shape `i8x16.` `i16x8.` `i32x4.` `i64x2.` `f32x4.` `f64x2.`)

- V1: the scalar module has 0 such instructions. Falsifier: any.
- V2: the autovec `-Oz` module has fewer than 100.
  `-Oz` makes clang's loop vectorizer refuse any loop that needs a scalar epilogue, so almost nothing vectorizes. Falsifier: 100 or more.
- V3: the autovec `-O2` module has 200 to 4,000. Falsifier: outside.
- V4: the simde module has 5,000 to 30,000 (libjpeg-turbo's NEON files are about 3,500 lines of intrinsics, each NEON intrinsic becomes one to six wasm instructions). Falsifier: outside.

### Time (median of 9 interleaved rounds, box 900, headless Chrome)

- T1: autovec `-Oz` is within 3% of scalar on every input (ratio 0.97 to 1.03), as raceclub measured (83.5 vs 83.7 ms on 12 MP). Falsifier: a ratio outside 0.95 to 1.05 with the two arms' ranges not overlapping.
- T2: autovec `-O2` against scalar `-O2`: ratio 0.95 to 1.03 on the JPEGs and the PNG. Falsifier: outside 0.93 to 1.05.
- T3: simde on `p12.jpg` (scale 2/8): ratio 0.85 to 0.97. Falsifier: outside.
- T4: simde on `p12p.jpg`: ratio 0.88 to 0.98 (progressive Huffman is slower, so SIMD is a smaller share). Falsifier: outside.
- T5: simde on `p48.jpg` (1/8, DC-only IDCT): ratio 0.95 to 1.01. Falsifier: outside.
- T6: simde on `p12.png`: ratio 0.98 to 1.02 (no SIMD code runs; the PNG path and the scaler are cdom's, not libjpeg's). Falsifier: outside.
- T7: simde at `-Oz` is no more than 4% slower than simde at `-O2` on `p12.jpg`: SIMDe's wrappers are `static inline` and clang at `-Oz` still inlines them when they are small. Falsifier: more than 4% slower.
- T8: the SIMD gain is large only when libjpeg does the full-size work: `p12.jpg` with box 4032 (scale 8/8, `jpeg_idct_islow`, h2v2 fancy upsampling and colour conversion on all 12 MP): simde ratio 0.60 to 0.80. Falsifier: outside.
- T9: the browser arms B1 and B2 are unaffected by the build (they are the same page): within 5% of each other across the arms' pages. Falsifier: 5% or more.
- T10: the scalar C arm on this machine, `p12.jpg`, is 100 to 200 ms; `p12p.jpg` 200 to 380 ms; `p48.jpg` 190 to 380 ms; `p12.png` 250 to 450 ms. Falsifier: outside.

### The per-function time split of the JPEG decode (V8 CPU profile with wasm function names, `p12.jpg`, scalar)

- F1: `decode_mcu_slow` and `decode_mcu_fast` together (Huffman) are 40 to 60% of the time inside `cd_image_resize`. Falsifier: outside.
- F2: `jpeg_idct_2x2` is 5 to 12%, `h2v2_fancy_upsample` 5 to 12%, `ycc_rgba_convert` (the extrgbx path) 4 to 10%, the cdom scaler (`cd_image_scaler_row`) 8 to 20%. Falsifier: outside.
- F3: with simde, the three NEON functions (`jsimd_idct_2x2_neon`, `jsimd_h2v2_fancy_upsample_neon`, `jsimd_ycc_extrgbx_convert_neon`) each take 20 to 60% of the time their scalar functions took, and `decode_mcu_*` and the scaler are unchanged within 5%. Falsifier: outside.

### Size (the bench's C arm, linked, stripped, as bench/image/sizes/run.sh)

- Z1: autovec `-Oz` is within 2% of scalar, raw and brotli. Falsifier: outside.
- Z2: simde adds 25 to 70 KB raw and 8 to 24 KB brotli -q 11 over scalar (both formats, js). Falsifier: outside.
  It includes the ifast IDCT and the merged upsampler, which the module never selects (it sets `JDCT_ISLOW` and fancy upsampling) but libjpeg's dispatch reaches by switch.
- Z3: of that, the ifast IDCT and merged upsampling (code the module cannot reach) are 20 to 45% of the added bytes. Falsifier: outside.

### Memory (the C arm's wasm memory after one run on a fresh page)

- M1: identical to scalar to the byte on all four inputs, for both other arms (SIMD uses registers and the same buffers; the stack is the 64 KB the linker sets). Falsifier: any difference in `memory.buffer.byteLength`.

### Correctness

- C1: simde output is bit-exact with the scalar build on all four bench inputs (an FNV-1a hash of the RGBA the module returns, outside the timed part) and on every case of tests/image's oracle (`tools/image.mjs`). libjpeg-turbo's own test suite requires NEON and C to agree to the byte for islow, fancy upsampling, colour conversion and the scaled IDCTs. Falsifier: any difference.
- C2: the same for the `ifast` IDCT and merged upsampling, driven straight through the libjpeg API by a test of this branch (the module never selects them). Falsifier: any difference.
- C3: the autovec build is bit-exact too (a compiler transformation of integer code). Falsifier: any difference.

## Results

Every number below was measured after the predictions above were committed (commit a84b327); the predictions are not edited.

**Machine and tools.**
Linux x86-64 VM (Intel Xeon @ 2.30 GHz, 4 cores, load under 1.1 during runs), not the Mac: numbers are indicative, and the cdom manager re-measures on the Mac.
Headless Chrome for Testing 153.0.8010.12 (`--no-sandbox --headless=new`), clang 20.1.2 (Ubuntu noble-updates), wasm-ld 20.
The image's clang was 18.1.3: it builds the scalar and SIMD arms, but not the `eh` way out (its exception flags are newer), so clang-20 was installed from Ubuntu's archive (apt.llvm.org is not reachable from this VM); every number below is clang 20, every arm.
Each number is the median of 9 interleaved rounds (bench/image/run.mjs, arms rotated each round, a fresh page for each run), box 900, with min and max.
The inputs are bench/image/inputs.py's, made here with Pillow 10.2 (so their bytes may differ from the Mac's).
On this VM there is no Mac skew to subtract; the skew to expect is the reverse: wasm SIMD on x86 V8 against arm64 V8 (the manager's run will say).

### Time, ms, box 900 (median, min to max)

| input | scalar -Oz | simd128 -Oz | simde -Oz | scalar -O2 | simd128 -O2 | simde -O2 | B1 | B2 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| p12.jpg | 65.7 (62.8-79.5) | 66.5 (63.5-88.7) | 65.3 (61.5-81.3) | 66.2 (62.0-88.6) | 64.8 (62.0-85.3) | 62.9 (59.7-67.2) | 88.4 (80.8-96.8) | 86.8 (79.7-104.8) |
| p12p.jpg | 200.9 (190.7-214.4) | 202.3 (192.7-219.2) | 201.7 (189.9-217.3) | 196.5 (189.8-237.4) | 203.7 (188.9-218.7) | 198.2 (184.2-224.9) | 174.9 (165.4-181.8) | 173.8 (166.5-196.4) |
| p48.jpg | 176.8 (167.7-193.6) | 177.3 (169.9-195.6) | 173.6 (164.9-184.1) | 177.6 (167.3-193.7) | 184.8 (175.3-203.1) | 179.6 (172.7-197.3) | 297.8 (291.2-331.5) | 315.4 (300.8-338.4) |
| p12.png | 244.6 (232.1-325.6) | 241.1 (235.5-313.5) | 243.7 (234.2-319.3) | 243.2 (230.2-349.8) | 259.9 (251.2-390.8) | 255.8 (242.4-354.6) | 134.0 (130.2-150.6) | 145.8 (142.4-151.6) |

Raw: bench/image/results/raw/2026-11-19T17-02-33-738Z.tsv (not committed, results/raw is ignored).
B1 and B2 always run on the scalar page of the same run (run.mjs), so they do not depend on the C arm.

Ratios to scalar at the same -O level: simd128 -Oz 1.012, 1.007, 1.003, 0.986; simde -Oz 0.994, 1.004, 0.982, 0.996; simd128 -O2 0.979, 1.037, 1.041, 1.069; simde -O2 0.950, 1.009, 1.011, 1.052 (p12.jpg, p12p.jpg, p48.jpg, p12.png).

Full-size decode (box 4032, p12.jpg scale 8/8 and p12p.jpg, 9 rounds; the output is 48 MB RGBA, and the cdom scaler passes it through):

| input | scalar -Oz | simd128 -Oz | simde -Oz | simde -O2 |
| --- | --- | --- | --- | --- |
| p12.jpg | 282.1 (266.1-301.3) | 285.5 (268.1-327.8) | 305.7 (292.4-325.5) | 293.0 (275.5-310.9) |
| p12p.jpg | 426.5 (398.6-456.9) | 416.4 (399.8-431.3) | 435.2 (410.2-467.3) | 418.0 (399.5-427.1) |

### Predictions, marked

| id | result | verdict |
| --- | --- | --- |
| V1 | scalar: 0 v128 instructions | confirmed |
| V2 | simd128 -Oz: 55 (of 43,153) | confirmed (< 100) |
| V3 | simd128 -O2: 1,549 (of 57,743) | confirmed (200 to 4,000) |
| V4 | simde -Oz: 8,976 (of 86,896); -O2: 11,711 | confirmed (5,000 to 30,000) |
| T1 | simd128 -Oz ratios 0.986 to 1.012 | confirmed; raceclub's no-gain result holds |
| T2 | simd128 -O2 against scalar -O2: 0.979, 1.037, 1.041, 1.069 | refuted for p12.png (1.069, outside 0.93 to 1.05); p12p and p48 are off the predicted band and inside the falsifier band |
| T3 | simde p12.jpg 0.994 | refuted (0.85 to 0.97): no gain |
| T4 | simde p12p.jpg 1.004 | refuted (0.88 to 0.98) |
| T5 | simde p48.jpg 0.982 | confirmed |
| T6 | simde p12.png 0.996 | confirmed |
| T7 | simde -Oz is 3.8% slower than simde -O2 on p12.jpg | confirmed, barely (4%) |
| T8 | simde at 8/8 on p12.jpg: ratio 1.084 | refuted (0.60 to 0.80): slower, not faster |
| T9 | B1 and B2 on another C arm's page | not measured: run.mjs always runs them on the scalar page |
| T10 | scalar: p12.jpg 65.7, p12p.jpg 200.9, p48.jpg 176.8, p12.png 244.6 | refuted for p12.jpg (below 100), p48.jpg (below 190) and p12.png (below 250); p12p.jpg confirmed |
| Z1 | simd128 -Oz raw 92,657 against 92,955 (-0.3%), brotli 29,828 against 29,879 | confirmed |
| Z2 | simde adds 129,068 raw (+139%) and 13,143 brotli (+44%) | brotli confirmed (8 to 24 KB), raw refuted (25 to 70 KB) |
| Z3 | ifast and merged functions are 82,788 of the 114,140 bytes of NEON and dispatch code, 64% of the added raw bytes (function sizes from the unstripped module) | refuted (20 to 45%); the merged upsampler has fourteen colour-order variants |
| M1 | wasm memory 6.1, 40.8, 17.1, 17.1 MiB for p12.jpg, p12p.jpg, p48.jpg, p12.png, equal in every arm | confirmed |
| C1 | every C arm made the same RGBA on each input (FNV-1a, 9 rounds x 6 arms); tests/image's 59 oracle cases equal to the byte in the simde module | confirmed |
| C2 | 168 runs (every JPEG case x box x ifast/merged option) equal, SIMD against libjpeg's C routines in the same module | confirmed |
| C3 | simd128 arms equal to scalar | confirmed |

Sizes, stripped (bench/image/sizes/simd.sh, both formats, js):

| build | -Oz raw | -Oz brotli -q 11 | -O2 raw | -O2 brotli |
| --- | --- | --- | --- | --- |
| no image code | 3,149 | 1,493 | | |
| scalar | 92,955 | 29,879 | 107,672 | 33,226 |
| simd128 | 92,657 | 29,828 | 125,777 | 36,706 |
| simde | 222,023 | 43,022 | 235,166 | 51,399 |
| JPEG only, scalar / simde | 81,170 / 210,225 | 25,245 / 38,266 | | |

(The scalar module here is 92,955 where docs/image-design.md has 82,563: that was measured with clang 18's Homebrew successor on the Mac, and the bench app here exports two more functions; the comparison is between rows.)

### The per-function split (bench/image/profile.mjs: V8's CPU profiler, 100 us, a fresh page for each of 9 runs, self time per run)

p12.jpg, box 900 (scale 2/8), ms per run, share of all sampled time (the sampled total includes `hash`, which run.mjs does outside the timed call):

| function | scalar | simde |
| --- | --- | --- |
| decode_mcu (Huffman) | 42.77 (54.8%) | 43.63 (56.8%) |
| cd_image_scaler_row | 12.26 (15.7%) | 13.81 (18.0%) |
| jpeg_idct_2x2 / jsimd_idct_2x2_neon | 6.95 (8.9%) | 5.22 (6.8%) |
| jpeg_idct_4x4 / jsimd_idct_4x4_neon | 6.16 (7.9%) | 3.48 (4.5%) |
| ycc_extrgbx_convert / jsimd_ycc_extrgbx_convert_neon | 1.69 (2.2%) | 2.18 (2.8%) |
| h2v2 fancy upsampling | not in the profile | not in the profile |
| total sampled | 78.12 | 76.85 |

p12.jpg, box 4032 (scale 8/8), 5 runs:

| function | scalar | simde |
| --- | --- | --- |
| cd_image_scaler_row (a 12 MP pass-through) | 183.32 | 178.91 |
| jpeg_idct_islow / jsimd_idct_islow_neon | 35.46 | 46.65 |
| ycc_extrgbx_convert / jsimd_ycc_extrgbx_convert_neon | 14.98 | 51.00 |
| h2v2_fancy_upsample / jsimd_h2v2_fancy_upsample_neon | 12.13 | 2.47 |
| decode_mcu | 45.12 | 45.64 |
| process_data_context_main | 15.87 | not listed |

- F1 confirmed (54.8%, 40 to 60).
- F2: `jpeg_idct_2x2` 8.9% and the scaler 15.7% confirmed; `h2v2_fancy_upsample` is absent (refuted: at scale 2/8 libjpeg upsamples by replication in its own int upsampler), `ycc_extrgbx_convert` is 2.2% (refuted, 4 to 10).
- F3 refuted: the NEON routines take 75% (2x2), 56% (4x4) and 129% (ycc) of their scalar functions' time at box 900 (predicted 20 to 60%); `decode_mcu` is within 2%, and the scaler is 13% higher in the profile, in cdom code that is identical in both arms (profiler noise).
- At 8/8 the NEON islow IDCT is 1.3 times slower than the scalar one, NEON colour conversion 3.4 times slower, and only h2v2 fancy upsampling is faster (4.9 times).

### What the numbers say

- On this VM, with clang 20 and V8 in Chrome 153, neither autovectorization nor libjpeg-turbo's NEON through SIMDe makes the module faster, at box 900 or at full size.
  The C arm is at 65.7 ms against the browser's 88 ms on p12.jpg (C wins there), and behind it on p12p.jpg, p48.jpg and p12.png as predicted in docs/image-design.md.
- Why the gain is small: Huffman decoding is 55% of a 12 MP decode to 900 px and no SIMD code touches it, and at scale 2/8 and 1/8 the SIMD-able work is a quarter or a sixty-fourth of the pixels.
- Why SIMDe's routines do not win even at 8/8: SIMDe maps intrinsics one by one, and those that have no wasm instruction (`vtrn`, `vst4`, widening shifts and the like) become shuffles and scalar lane code.
- With clang 18 the SIMDe build was far worse: the same routines were 2 to 3 times slower than scalar (single-round smoke runs, p12.jpg 189 ms against 85 ms, not part of the tables), because clang 18 left SIMDe's portable `vtrnq_*`, `vshll_n_*` as scalar loops through memory (a probe of each: 51 to 79 wasm instructions with a loop at clang 18, 8 instructions and no loop at clang 20).
  An app on clang 18 should not use `CDOM_IMAGE_SIMD=simde`.

### Things I decided that the brief did not cover

- `CDOM_IMAGE_SIMD` is a cdom.mk variable (none, simd128, simde), and applies to the image module's four parts only, not to cdom's core.
- libjpeg-turbo's `simd/jsimd.c` is not vendored: it holds the encoder's steps and needs its NEON files. The decoder's half is `src/cdom_image_jsimd.c`, which has jsimd.c's checks and assignments for the decode steps.
- SIMDe is the release's amalgamated `arm/neon.h` (one 4.9 MB header); the release tarball is the only archive github served through this VM's proxy (the source archive of the tag returned 403).
  The pinned sha256 is of what the release served; the pin is trust-on-first-use.
- ifast IDCT and merged upsampling are switched on in the SIMD build only (the module never selects them), which is 64% of the added bytes (Z3).
- Test hooks (`CDOM_IMAGE_SIMD_TEST`, wasm.mk's SIMD builds only): a switch that makes libjpeg run its C routines, and one that sets the ifast and merged options, so SIMD is compared with C in one module.
- The native (ASan/UBSan) tests run the scalar build only: SIMDe on the x86 host would test SSE, not wasm.
- The `eh` way is skipped (`IMAGE_EH=0`) on clang 20.
- tests/image/test_image.c gained `#include <sys/types.h>` (ssize_t on glibc), and the oracle was run with djpeg, cjpeg and jpegtran 3.2.1 built here from the same archive, Python 3.12 with Pillow 10.2 and numpy.
- The big-input tests (IMAGE_BIG=1) were not run on the SIMD build; the 12 MP and 48 MP bench inputs are checked by hash across arms instead.
