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
