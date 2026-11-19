# The image module with hand-written wasm SIMD

This is part 2 of the image module's SIMD work: the hot loops the profile picks, written by hand with `<wasm_simd128.h>` intrinsics.
Part 1 (branch `image-simd-neon`) is the autovectorizer and libjpeg-turbo's NEON through SIMDe; this branch is the comparison, and uses no SIMDe.
The doc is written in the order the work was done: the profile, then the predictions (committed before any SIMD code), then the build, the tests, the measurements, and each prediction marked.

## Where the numbers come from

Every number here is from a Linux x86-64 VM, not Alex's arm64 Mac, so they are indicative.
The manager re-measures the final numbers on the Mac.

| thing | value |
| --- | --- |
| CPU | Intel Xeon (model string "Intel(R) Xeon(R) Processor @ 2.10GHz"), 4 cores, 1 thread each, AVX-512 |
| OS | Ubuntu 24.04.5, kernel 6.18.44 (a cloud VM) |
| Chrome | Google Chrome for Testing 153.0.8010.12, `--headless=new --no-sandbox` |
| clang (wasm and native) | Ubuntu clang 18.1.3 (1ubuntu1), with wasm-ld from the same package; no newer one was needed (cdom.mk builds with it as is) |
| node | v22.23.5 |
| perf | linux-tools 6.8.0-1007-azure's perf on a 6.18 kernel, event `cpu-clock` (the VM has no hardware counters) |
| inputs | `bench/image/inputs.py` run with Pillow 13.0.0 (the Mac's Pillow may encode other bytes: sizes below) |

The inputs on this machine: p12.jpg 3,808,235 bytes, p12p.jpg 3,550,927, p48.jpg 15,318,435, p12.png 15,180,358.
The skew to expect against the Mac: V8 turns a 128-bit wasm SIMD op into SSE or AVX instructions here and into NEON instructions there, a few of the ops (the 64-bit multiplies, the shuffles) differ in cost between the two, so a per-loop gain here is a guess at the Mac's, not a copy of it.
The numbers are one machine's, noisy: the load was under 0.3 for the profiles; a measurement round reports medians with the spread, arms interleaved.

## The profile (before any SIMD code)

The bench's C arm, scalar (`make -C bench/image page`, cdom.mk's `-Oz`, no `-msimd128`), box 900.
Chrome: `bench/image/profile.mjs`, the CDP Profiler at a 100 us sampling interval, wasm function names from the name section, one warm-up run and then 8 timed runs; the share is of the sampled time that is not idle, program or GC.
Native: `bench/image/native.sh`, the same files built for x86-64 at `-O2 -g`, scalar (no SIMD in libjpeg-turbo either), `perf record -F 5000 -e cpu-clock`, 20 runs.
`-fno-inline` builds (a scratch directory, not kept) split the PNG row loop into its functions.

### p12.jpg (4032 x 3024 baseline 4:2:0, decoded at 2/8: luma 2x2 IDCT, chroma 4x4 IDCT; chroma is already full size, so no upsampling)

| function | Chrome share | native share |
| --- | --- | --- |
| decode_mcu (Huffman) | 60.6% | 64.3% |
| cd_image_scaler_row | 15.4% | 14.4% |
| jpeg_idct_4x4 | 9.0% | 8.8% |
| jpeg_idct_2x2 | 7.6% | 7.8% |
| decompress_onepass (the coefficient controller) | 3.1% | 1.7% |
| ycc_extrgbx_convert | 2.9% | 1.9% |

### p12p.jpg (the same pixels, progressive)

| function | Chrome share | native share |
| --- | --- | --- |
| decode_mcu_AC_refine | 64.2% | 61.4% |
| decode_mcu_AC_first | 10.6% | 8.2% |
| consume_data | 6.4% | 4.5% |
| cd_image_scaler_row | 5.0% | 5.4% |
| jpeg_idct_4x4 | 3.5% | 3.3% |
| jpeg_idct_2x2 | 3.2% | 3.6% |
| decode_mcu_DC_refine + DC_first | 4.2% | 4.2% |
| ycc_extrgbx_convert | 0.9% | 0.6% |

### p48.jpg (8064 x 6048, decoded at 1/8: luma 1x1, chroma 2x2)

| function | Chrome share | native share |
| --- | --- | --- |
| decode_mcu | 80.5% | 83.2% |
| jpeg_idct_2x2 | 6.1% | 5.9% |
| cd_image_scaler_row | 5.5% | 5.2% |
| decompress_onepass | 5.4% | 2.2% |
| jpeg_idct_1x1 | (inlined, in decompress_onepass) | 1.5% |
| ycc_extrgbx_convert | 1.0% | 0.7% |

### p12.png (4032 x 3024 RGB, 8-bit, 3,016 of 3,024 rows filtered Paeth, 8 Sub)

| function | Chrome share | native share |
| --- | --- | --- |
| tinfl_decompress | 47.7% | 33.6% |
| unfilter (Paeth, bpp 3) | 30.2% | in cd_image_png_resize, 50.4% with to_rgba |
| cd_image_scaler_row (with emit) | 17.9% | 15.3% |
| to_rgba (RGB to RGBA) | 5.1% | in the 50.4% |

### What the profile picks, and what it does not

- **Not picked: the entropy decoders** (decode_mcu, decode_mcu_AC_refine, tinfl_decompress): 60 to 80% of every JPEG and 48% of the PNG.
  Huffman decoding is a serial bit-stream walk with a data-dependent length at every step; wasm SIMD has no gather, no variable shift per lane that helps, and nothing here that the vector unit can do.
  libjpeg-turbo's own SIMD does not touch the decoder's Huffman loop either (its Huffman SIMD is for encoding).
  Inflate is the same kind of loop.
  So the largest part of every input is out of reach, and Amdahl's law bounds the whole-run gain: on p12.jpg at most 40% of the time can move, on p48.jpg 20%, on p12.png 52%.
- **Picked, in order of the share they have across the four inputs:**
  1. the PNG unfilter, Paeth (30% of the PNG), and the other three filters (sub, up, avg) for completeness;
  2. cdom's area-average scaler (15.4% of p12.jpg, 17.9% of p12.png, 5% of the others);
  3. jpeg_idct_4x4 and jpeg_idct_2x2 (16.6% of p12.jpg, 6.7% of p12p.jpg, 6.1% of p48.jpg);
  4. RGB to RGBA expansion (5.1% of the PNG);
  5. YCbCr to RGBA (0.9 to 2.9%).
- **Not picked: the h2v1 and h2v2 fancy upsampling**, `sep_upsample` is under 0.2% in every profile.
  At the bench's scales (2/8 and 1/8) libjpeg gives the chroma planes a larger IDCT (4x4 against 2x2, 2x2 against 1x1) so the chroma is already full size and the upsampler does nothing; the fancy upsamplers only run at 5/8 and above, i.e. for a box that is more than 60% of the image.
  The dispatch answers "no" for them, and libjpeg's C runs, if a box ever reaches them.
- **Not picked: the 8x8 islow IDCT** (`jidctint`), for the same reason: the bench never decodes at full scale.
  The dispatch answers no for it too.
- **Not picked: the 1x1 IDCT** (1.5% natively on p48, trivial: a dequantize, a shift and a clamp per block).

## Predictions (written before any SIMD code)

The scalar number each ratio is against is the scalar build's, run in the same round or profile session.
A "ratio" below is SIMD time over scalar time, so under 1 is faster.

**Correctness**

- X1: every SIMD loop is bit-exact with its scalar counterpart on all four bench inputs and on every case of tests/image, and in a randomized differential test (one named seed) on widths 1 to 64, odd widths and edge values: 0 differing bytes.
  Falsifier: any differing byte.
  This includes inputs libjpeg's scalar code handles by wrapping (a corrupt file's huge coefficients): the SIMD IDCTs use the same 32-bit integer arithmetic and the same zero-column and zero-row shortcuts.

**Per loop, Chrome profiler, self time per run, scalar over SIMD** (so a speedup; over 1 is faster)

| id | loop | predicted | range | falsifier |
| --- | --- | --- | --- | --- |
| L1 | jpeg_idct_4x4 | 2.5x | 2.0 to 3.5 | under 2.0x |
| L2 | jpeg_idct_2x2 | 2.0x | 1.5 to 3.0 | under 1.5x |
| L3 | YCbCr to RGBA (ycc_extrgbx_convert) | 3.0x | 2.0 to 5.0 | under 2.0x |
| L4 | cd_image_scaler_row (across and down; emit's division is not touched) | 1.4x | 1.2 to 1.8 | under 1.2x |
| L5 | PNG unfilter, Paeth, 3 bytes a pixel | 1.8x | 1.4 to 2.5 | under 1.4x |
| L6 | RGB to RGBA expansion (to_rgba, case 2 without a colour key) | 4.0x | 2.5 to 6.0 | under 2.5x |

**Whole runs** (`cd_image_resize` in headless Chrome, median of 9 interleaved rounds, box 900), ratio SIMD over scalar:

| input | predicted ratio | range | falsifier |
| --- | --- | --- | --- |
| p12.jpg | 0.85 | 0.80 to 0.92 | outside the range |
| p12p.jpg | 0.94 | 0.90 to 0.97 | outside the range |
| p48.jpg | 0.94 | 0.90 to 0.97 | outside the range |
| p12.png | 0.78 | 0.70 to 0.86 | outside the range |

(From the shares: the picked loops are 17% to 33% of the time at the speedups above.)

- T1: no input gets slower with SIMD (every ratio under 1.00). Falsifier: any median ratio over 1.00.
- T2: the loops not touched do not move: decode_mcu and tinfl_decompress self time within 5% of the scalar build's. Falsifier: outside 5%.
- T3: B1 and B2 (the browser's own arms, which do not run cdom code) have a median within 5% of the scalar session's on every input (a control of the machine's noise). Falsifier: outside 5%.

**Size** (bench/image/sizes/run.sh's way: `-Oz`, stripped; `brotli -q 11`):

- Z1: SIMD adds 3 to 8 KB raw and 1.5 to 4 KB brotli to an app that links the image module (both formats). Falsifier: outside either range.
- Z2: the dispatch (`jsimd_wasm.c`) and the loops' scalar twins add nothing to a scalar build beyond 300 bytes raw. Falsifier: more than 300 bytes raw, or any difference in a scalar build's output bytes against origin/image's build of the same module (the scalar default's loops are the same code, moved).

**Memory** (the C arm's wasm memory after one run on a fresh page):

- M1: the same for every input as the scalar build's, to the 0.1 MiB the bench prints (the scaler's row buffer grows by 4 bytes). Falsifier: any input differing by 0.1 MiB or more.

**Where this will land against the browser** (a prediction about shape, not a number on this VM; the manager re-measures on the Mac):

- C1: the SIMD module stays slower than B1 and B2 on p12.png on the Mac, since 48% of its time is inflate, which no SIMD here touches. Falsifier (on the Mac): C within B1's time.

## The design

(Written after the predictions; see below once the code exists.)
