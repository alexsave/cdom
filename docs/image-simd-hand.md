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
  4.
     RGB to RGBA expansion (5.1% of the PNG);
  5.
     YCbCr to RGBA (0.9 to 2.9%).
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

- T1: no input gets slower with SIMD (every ratio under 1.00).
  Falsifier: any median ratio over 1.00.
- T2: the loops not touched do not move: decode_mcu and tinfl_decompress self time within 5% of the scalar build's.
  Falsifier: outside 5%.
- T3: B1 and B2 (the browser's own arms, which do not run cdom code) have a median within 5% of the scalar session's on every input (a control of the machine's noise).
  Falsifier: outside 5%.

**Size** (bench/image/sizes/run.sh's way: `-Oz`, stripped; `brotli -q 11`):

- Z1: SIMD adds 3 to 8 KB raw and 1.5 to 4 KB brotli to an app that links the image module (both formats).
  Falsifier: outside either range.
- Z2: the dispatch (`jsimd_wasm.c`) and the loops' scalar twins add nothing to a scalar build beyond 300 bytes raw.
  Falsifier: more than 300 bytes raw, or any difference in a scalar build's output bytes against origin/image's build of the same module (the scalar default's loops are the same code, moved).

**Memory** (the C arm's wasm memory after one run on a fresh page):

- M1: the same for every input as the scalar build's, to the 0.1 MiB the bench prints (the scaler's row buffer grows by 4 bytes).
  Falsifier: any input differing by 0.1 MiB or more.

**Where this will land against the browser** (a prediction about shape, not a number on this VM; the manager re-measures on the Mac):

- C1: the SIMD module stays slower than B1 and B2 on p12.png on the Mac, since 48% of its time is inflate, which no SIMD here touches.
  Falsifier (on the Mac): C within B1's time.

## Results, build 1: the profile of the first SIMD code, against the predictions

The first working SIMD code (commit 2916260), profiled as the scalar one was: `bench/image/profile.mjs`, 8 timed runs after a warm-up, twice (two profiles per input and build, interleaved scalar and SIMD), mean of the two, self time per run in ms; `bench/image/profcmp.py` makes the table (raw profiles in `bench/image/results/profile-v1/`).
A SIMD helper that V8 did not inline into its caller (`parts4`, `col4`, `unfilter_paeth`) is counted with the loop it belongs to.
The whole runs are measured once, on the final code, in the section after next.

| loop | input | scalar ms | SIMD ms | speedup | predicted | verdict |
| --- | --- | --- | --- | --- | --- | --- |
| L1 jpeg_idct_4x4 | p12.jpg | 9.2 | 6.0 | 1.53x | 2.5x (2.0 to 3.5) | refuted (under 2.0x) |
| L1 | p12p.jpg | 9.9 | 6.4 | 1.53x | | refuted |
| L2 jpeg_idct_2x2 | p12.jpg | 9.1 | 7.2 | 1.27x | 2.0x (1.5 to 3.0) | refuted (under 1.5x) |
| L2 | p12p.jpg | 9.7 | 7.5 | 1.28x | | refuted |
| L2 | p48.jpg | 18.8 | 16.1 | 1.17x | | refuted |
| L3 ycc to RGBA | p12.jpg | 3.0 | 1.1 | 2.77x | 3.0x (2.0 to 5.0) | confirmed |
| L3 | p12p.jpg | 2.4 | 1.0 | 2.40x | | confirmed |
| L3 | p48.jpg | 2.1 | 1.1 | 1.83x | | refuted on this input (under 2.0x; 1 ms of self time, a few samples) |
| L4 scaler (row, across, down) | p12.jpg | 16.6 | 18.2 | 0.91x | 1.4x (1.2 to 1.8) | refuted (slower) |
| L4 | p12p.jpg | 15.7 | 17.2 | 0.91x | | refuted |
| L4 | p48.jpg | 14.6 | 18.2 | 0.80x | | refuted |
| L4 | p12.png | 65.5 | 75.1 | 0.87x | | refuted |
| L5 PNG unfilter (Paeth) | p12.png | 106.5 | 81.9 | 1.30x | 1.8x (1.4 to 2.5) | refuted (under 1.4x) |
| L6 RGB to RGBA | p12.png | 1.6 | 0.4 | 3.4x | 4.0x (2.5 to 6.0) | confirmed |
| T2 decode_mcu, decode_mcu_AC_*, tinfl (not touched) | p12.jpg, p12p.jpg, p48.jpg, p12.png | 65.3, 205.2, 219.2, 173.2 | 68.1, 211.0, 232.2, 178.2 | +4.3%, +2.8%, +5.9%, +2.9% | within 5% | refuted on p48.jpg (5.9%), held on the other three |

What the first build shows, and what it was:

- L4 was slower because of a store-to-load stall.
  The across loop added into the sums with a 16-byte read-modify-write at a 12-byte stride (three sums of an output pixel and the first of the next), so the load for an output pixel overlapped the previous pixel's store by one word, which a store cannot forward.
- L1 lost half its gain to calls: `parts4` and `col4` were not inlined (`static inline` at `-Oz`), 3 of the 6 ms.
- L2 is a loop of about 48 ns a block in scalar, 38 in SIMD: it reads five rows of eight coefficients and eight table words, and the C returns early on a column of zeros.
  With four lanes and five columns to make, there is little to win; this is the loop the profile picked that SIMD helps least.
- L5: the Paeth predictor of a pixel depends on the pixel before it, so one pixel is one dependent chain of about 11 vector ops whatever the width of the vector; the scalar code runs three channels of that chain in parallel on an out-of-order core.

Build 2 changes what these say: the helpers always inlined, the across loop keeping the output pixel's sum in a register (one store a pixel, no load), and a shorter Paeth chain (a minimum in place of two compares and an and; the next pixel's left value kept in 16-bit lanes).
The predictions above are not changed; build 2 is measured against them the same way, below.

## Results, build 3 (the final code): the loops

Build 2 (always-inlined helpers, the across loop's sum in a register, a shorter Paeth chain; `bench/image/results/profile-v2/`) measured L1 2.05x and 2.14x, L2 1.29x, 1.40x and 1.28x, L3 3.19x, 2.00x and 3.17x, L4 1.15x, 0.95x, 1.05x and 1.23x, L5 1.48x, L6 2.07x.
A look at the profile with V8's wasm inlining off (`--js-flags=--no-wasm-inlining`, a diagnostic only) then showed where the rest of the scaler was: `emit`'s 64-bit division, 7.4 ms of the 15 on p12.jpg, which no vector loop of the first two builds touched.
Build 3 divides in doubles (exact: the argument is in the comment of `cd_image_scaler_div_simd`, and the test puts sums at both sides of every quotient's edge) and is the final code.
Three profiles per input and build, interleaved, mean self time per run in ms (`bench/image/results/profile-v3/`, `profile-v3.txt`):

| loop | input | scalar ms | SIMD ms | speedup | predicted | verdict |
| --- | --- | --- | --- | --- | --- | --- |
| L1 jpeg_idct_4x4 | p12.jpg | 9.3 | 4.4 | 2.11x | 2.5x (2.0 to 3.5) | in the range |
| L1 | p12p.jpg | 8.6 | 4.5 | 1.92x | | under the 2.0x falsifier by 0.08: refuted on this input |
| L2 jpeg_idct_2x2 | p12.jpg | 9.1 | 6.3 | 1.45x | 2.0x (1.5 to 3.0) | refuted (under 1.5x) |
| L2 | p12p.jpg | 8.6 | 7.0 | 1.22x | | refuted |
| L2 | p48.jpg | 17.5 | 14.9 | 1.17x | | refuted |
| L3 ycc to RGBA | p12.jpg | 2.5 | 0.8 | 3.26x | 3.0x (2.0 to 5.0) | confirmed |
| L3 | p12p.jpg | 2.4 | 0.7 | 3.65x | | confirmed |
| L3 | p48.jpg | 2.7 | 0.8 | 3.24x | | confirmed |
| L4 scaler (row, across, down, divide) | p12.jpg | 14.7 | 11.0 | 1.34x | 1.4x (1.2 to 1.8) | confirmed |
| L4 | p12p.jpg | 15.1 | 10.5 | 1.43x | | confirmed |
| L4 | p48.jpg | 14.6 | 11.3 | 1.29x | | confirmed |
| L4 | p12.png | 65.3 | 47.6 | 1.37x | | confirmed |
| L5 PNG unfilter (Paeth, 3 bytes a pixel) | p12.png | 103.4 | 69.1 | 1.50x | 1.8x (1.4 to 2.5) | in the range (the point 1.8x is missed) |
| L6 RGB to RGBA | p12.png | 1.3 | 0.6 | 2.35x | 4.0x (2.5 to 6.0) | refuted (under 2.5x; 0.7 ms of self time, a few samples) |
| T2 decode_mcu, decode_mcu_AC_*, tinfl (untouched) | p12.jpg, p12p.jpg, p48.jpg, p12.png | 62.4, 191.0, 215.8, 169.3 | 59.8, 189.1, 213.5, 170.3 | -4.2%, -1.0%, -1.1%, +0.6% | within 5% | confirmed |

Reading it:

- L2 is the loop SIMD helps least, as the first build showed: about 48 ns a block in scalar and 33 in SIMD.
  A 2x2 output needs five of eight columns and five of eight rows of coefficients, so four lanes make a half-empty second vector, and the C returns early on a column of zeros, which on this grainy photo at a quarter scale is often.
  The loop stays SIMD since it is faster, 17 to 45%, but it is the one that did not earn its code.
- L4: `down` is bound by the memory it reads and writes (1.07x on the PNG, build 2 profiled with V8's inlining off), and the control of `across` (a 64-bit loop of 4 to 6 operations a source pixel) is scalar in both.
- L5: Paeth's chain is one dependent chain of about 8 vector operations a pixel, against the out-of-order scalar core running the three channels' chains side by side; 1.5x is what a one-pixel chain gives.

## Results: the whole runs

`cd_image_resize` on each input to a 900 box in headless Chrome 153, `bench/image/run.mjs 9 900 C-js,C-simd,B1,B2`: median of 9 interleaved rounds (each run a freshly loaded page; the arms' order rotates each round); two sessions, the second with the arms in another order and as a replicate (`bench/image/results/final/session1.tsv`, `session2.tsv`).
The load gate was 4 (the load was under 1.6).
Median ms, with min to max:

| input | C scalar, session 1 | C SIMD, session 1 | ratio | session 2 ratio | 18 rounds pooled ratio | predicted | verdict |
| --- | --- | --- | --- | --- | --- | --- | --- |
| p12.jpg | 93.8 (86.0 to 115.7) | 77.8 (73.5 to 89.6) | 0.83 | 0.88 | 0.86 | 0.85 (0.80 to 0.92) | confirmed |
| p12p.jpg | 256.6 (246.3 to 274.0) | 244.4 (236.4 to 293.9) | 0.95 | 0.95 | 0.95 | 0.94 (0.90 to 0.97) | confirmed |
| p48.jpg | 242.4 (233.1 to 267.5) | 230.0 (218.9 to 268.6) | 0.95 | 0.97 | 0.95 | 0.94 (0.90 to 0.97) | confirmed |
| p12.png | 311.9 (297.1 to 338.5) | 266.8 (249.5 to 282.0) | 0.86 | 0.86 | 0.86 | 0.78 (0.70 to 0.86) | confirmed at the edge of the range (0.855, 0.857, 0.856); the point 0.78 is missed |

The browser's arms, medians of the same 9 rounds, session 1 then session 2:

| input | B1 | B2 |
| --- | --- | --- |
| p12.jpg | 116.7, 107.7 | 113.0, 106.3 |
| p12p.jpg | 227.2, 237.0 | 238.2, 229.0 |
| p48.jpg | 403.5, 404.5 | 407.4, 397.7 |
| p12.png | 180.9, 176.5 | 198.5, 187.3 |

- T1 (no input slower): confirmed, every ratio under 1.00, in both sessions.
- T3 (B1 and B2 within 5% of themselves between sessions): refuted: p12.jpg B1 differs 7.7% and B2 5.9%, p12.png B2 5.6%; the rest are within 5%.
  The machine's session-to-session noise is up to 8%, which is the size of the SIMD gain on the two progressive and 48 MP inputs (5%); the replicate is why two sessions are shown, and why the pooled column is there.
  The ratios of the C arms agree between sessions to within 0.05.
- Z1, Z2 (size, below): confirmed.
- M1 (memory): the wasm memory is the same to the byte for every input (6,422,528; 42,729,472; 17,956,864; 17,891,328) in all 36 runs of each arm.
- C1 (on the Mac): not checkable here; on this VM, p12.png C-SIMD is 266.8 ms against B1 180.9 and B2 198.5, in the direction predicted.
- X1 (bit-exact): confirmed, below.

## Results: sizes

`bench/image/sizes/run.sh` (the app of `bench/image/sizes/app.c` at `-Oz`, stripped, `brotli -q 11`; clang 18.1.3, so the "none" row is 3,144 raw where the design doc's Mac numbers are 2,696):

| build | raw | brotli |
| --- | --- | --- |
| no image code | 3,144 | 1,503 |
| origin/image, both formats (this toolchain) | 91,587 | 29,738 |
| scalar (CDOM_IMAGE_SIMD=0, this branch), both formats | 91,721 | 29,618 |
| SIMD (CDOM_IMAGE_SIMD=1), both formats | 97,105 | 31,763 |
| scalar, JPEG only | 79,770 | 24,916 |
| SIMD, JPEG only | 83,642 | 26,380 |
| scalar, PNG only | 16,027 | 6,797 |
| SIMD, PNG only | 18,039 | 7,652 |

- Z1: SIMD adds 5,384 raw and 2,145 brotli to the scalar build (5,518 and 2,025 to origin/image's): inside both ranges, confirmed.
- Z2: the scalar build is 134 bytes raw larger than origin/image's (JPEG half 24, PNG half 138; the loops are functions of a file of their own now) and 120 bytes smaller in brotli: under the 300 bytes, confirmed; and its output is the same bytes (below).

## Results: correctness (X1)

- `tests/image/simd.sh` (node): the SIMD loops against their scalar counterparts, one seed (`SEED` in `tests/image/simd.c`), buffers at the end of memory so that a read or a write past a buffer traps:
  jsimd_idct_4x4 and 2x2 against libjpeg's own jpeg_idct_4x4 and 2x2, 65,000 blocks each, extremes included; YCbCr to RGBA against libjpeg's own conversion, 6,000 random rows (widths 1 to 64 fifty times each, then up to 1,500) and every one of the 16,777,216 (y, cb, cr); unfilter, every filter, 1 to 8 bytes a pixel, widths 0 to 64 (24,840 cases); RGB to RGBA 4,200; the scaler's across 2,500, down 1,704 and divide 2,130.
  0 differing bytes in all, and no trap.
- `bench/image/equal.mjs`: the four bench inputs at boxes 900, 300, 1500, 2500 and 4000, scalar against SIMD, SHA-256 of the RGBA block: 20 of 20 equal.
  At the boxes over 1500 the JPEG decodes at 5/8 and beyond, so libjpeg's C 8x8 IDCT and fancy upsampling run in both builds: the dispatch answers no for them, as it must.
- `bench/image/equal.mjs` is itself mutation-checked, on one (input, box 900) each, with the faults of `simd_mutants.py` built into the bench page: 8 of 11 make it print DIFFER (idct4-const, idct2-const, ycc-order, across-weight, down-halves, div-trunc-round on p12.jpg; paeth-min, sub3-store on p12.png).
  Three it does not see, and why: div-cap (no pixel of p12.jpg at 900 has a quotient of 255), rgba-alpha (the scaler ignores alpha), up-saturate (p12.png has no Up row: 3,016 Paeth and 8 Sub).
  The loop test catches all three; the two checks are not the same check.
- `tools/image.mjs` (Chrome), the 65 cases of `cases.tsv` (the big inputs included) and the error paths, with the SIMD build as one more build (`wasm_js_simd`, `wasm_js_cap_simd`): every image equal to the oracle's exact integer image to the byte.
- `tests/image/run.sh` as a whole, with `IMAGE_EH=0`: third_party equal to the archives, 9,182 native checks (ASan and UBSan, the scalar default) with 0 failures, every wasm build, the SIMD loop test.
  A fault planted in the scalar Avg loop that moved to `cdom_image_loops.c` fails the native tests on four PNG cases, so the move kept the existing tests' hold on it.

## The design

- **The switch.** `CDOM_IMAGE_SIMD=0` (the default, scalar) or `1`, in cdom.mk like `CDOM_IMAGE_SJLJ`.
  With 1, the image module's files and libjpeg-turbo's get `-msimd128`, and `src/jsimd/jsimd_wasm.c` is built in.
  Nothing detects SIMD at run time: a module with SIMD instructions does not instantiate in a browser without them (Chrome 91, Firefox 89, Safari 16.4), so the app chooses.
- **libjpeg-turbo's loops, through its own dispatch, with its sources untouched.**
  The decoder's files include `../simd/jsimd.h`, `../simd/jsimddct.h` and `../simd/jsimdconst.h` when `WITH_SIMD` is defined, ask `jsimd_set_*(cinfo)` and use the routine they name.
  `src/jsimd/simd/` holds those three headers (cdom.mk puts it on the include path, so `../simd/` resolves to its sibling), `src/jsimd/jsimd_wasm.c` the three routines (`jsimd_idct_4x4`, `jsimd_idct_2x2`, `jsimd_color_deconvert`), and every other `jsimd_set_*` is a constant 0, so libjpeg's C runs for it and the branch naming the missing routine is dead code.
  `WITH_SIMD` comes from the one place that defines it, the generated `jconfig.h` and `jconfigint.h` (`tools/third_party.py`: a documented edit of generated files, not of libjpeg's sources): their `WITH_SIMD` line is `#ifdef CDOM_JSIMD`, which cdom.mk defines with `CDOM_IMAGE_SIMD=1`.
  `WITH_SIMD` also changes libjpeg's types (`MULTIPLIER` to short, `DCTELEM` to short, the allocation alignment to 32); cdom.mk gives `-DMULTIPLIER=int -DALIGN_SIZE=8`, so the scalar code that stays is the code it was (`DCTELEM` is only the forward DCT's and the fast and float IDCTs', neither of which is built).
- **Exactness.** The IDCTs use the C's own 32-bit integer arithmetic lane for lane, so the results are the same on every input, not only on a valid file's: a corrupt file's huge coefficients make the C's products wrap, and the SIMD wraps the same way.
  The C's DC-only column shortcut in pass 1 is a lane select (it differs from the general path once a dequantized DC is over 2^17).
  Its zero-row shortcut in pass 2 is not reproduced: the two paths differ only above bit 12 of the result, which the range limit's 10-bit mask drops (the test has blocks of one column and of one row of huge terms, and passes; a first version that did reproduce it showed that no mutant of it could be caught, which is how that came out).
  The range limit is the C's table, `table[x & 1023]`, done as arithmetic: `clamp(((x + 512) & 1023) - 384, 0, 255)`.
  The colour conversion is `build_ycc_rgb_table`'s own four constants in 32-bit lanes, tested against every (y, cb, cr).
- **cdom's own loops** (`src/cdom_image_loops.c`): the scalar bodies are the module's code, moved whole; the SIMD bodies are beside them.
  `cd_image_unfilter` (Up 16 bytes at a time; Sub as a prefix sum of the pixels; Avg and Paeth a pixel at a time, in 16-bit lanes; 3 and 4 bytes a pixel, the others stay scalar), `cd_image_rgb_to_rgba` (4 pixels a shuffle), and the scaler's `across` (the output pixel's sum in a register), `down` (`i64x2.extmul` of the weight into the 64-bit sums) and `div`.
  `div` divides in doubles: x = 2a + t is under 2^41 and d = 2t under 2^33, both exact; the quotient is under 256; a correctly rounded division is within 2^-45 of the true quotient, and the true quotient is, when not whole, at least 2^-33 from a whole number, so truncating the double gives the integer quotient.
  It needs a buffer of dw x 3 bytes after the sums (`cd_image_scaler_init` allocates it in the same block, in SIMD builds only).
- **Not done, and why.**
  The entropy decoders (60 to 80% of the JPEG runs and 48% of the PNG's: serial, nothing for SIMD).
  The h2v1 and h2v2 fancy upsamplers (the bench never reaches them, see the profile; the dispatch answers no, libjpeg's C runs, and `equal.mjs` at 1500 to 4000 checks the mixed case).
  The 8x8 islow IDCT (the same: a box at the full size only).
  The 1x1 IDCT (1.5%, trivial).
  Sub, Avg and Paeth for 1, 2, 6 and 8 bytes a pixel (palette, grey, grey with alpha: they stay scalar; Up is SIMD for all).

## The tests, and their mutants

`tests/image/simd.sh` builds one module with both bodies of each loop and runs it in node (`tests/image/simd.mjs`); `tests/image/run.sh` runs it as its step 5.
`tests/image/simd_mutants.sh` builds each of `tests/image/simd_mutants.py`'s faults into a copy of the sources, runs the same test, and requires a failure on the line of the loop the mutant names: 35 of 35 are caught, each on its own loop's assertion (the failing case's number and what differed, or a trap).
Along the way, seven planted faults did not fail the test at first.
Two did not build (an unused variable).
Five were equivalent mutants, faults no input can show: a colour constant off by one, and two roundings by one (the exhaustive colour test passes on them, so no (y, cb, cr) tells them from the right code), the Paeth tie "pa < pb for pa <= pb" (pa = pb with pa at most pc means a = b = c), and dropping the C's zero-row shortcut of the 4x4 (the range limit's mask drops what the shortcut changes).
They were replaced by faults that do change an output (a constant of 1.419 for 1.402, a rounding of 30000 for 32768, Paeth's other tie), and the shortcut was taken out of the code.

| mutant | loop whose assertion fails | what it breaks |
| --- | --- | --- |
| idct4-const | idct4 | a rounding constant of the 4x4 IDCT is off by one |
| idct4-pass1-round | idct4 | the first pass of the 4x4 rounds with half the right constant |
| idct4-lane | idct4 | two output pixels of the 4x4 swapped |
| idct4-dc-column-shortcut | idct4 | the 4x4 skips the C's DC-only column shortcut for the first row (they differ when the C wraps) |
| idct4-range-mask | idct4 | the range limit does not wrap at 10 bits as the C's table does |
| idct4-quant-low-half | idct4 | the right half of the block is dequantized with the left half's table entries |
| idct2-const | idct2 | a constant of the 2x2 IDCT is off by one |
| idct2-lane | idct2 | a pixel of the 2x2 goes to the wrong place |
| idct2-sign | idct2 | the 2x2's second pixel takes the wrong sign on a row |
| ycc-const | ycc | the red constant is 1.4191 and not 1.4020 in the low half of a vector |
| ycc-green-round | ycc | the green channel of the high 4 lanes of 8 rounds with 30000, not 32768 |
| ycc-order | ycc random | pixels 4 to 7 and 8 to 11 of 16 swapped |
| ycc-alpha | ycc random | alpha is 254 |
| ycc-tail | ycc random | the pixels past the last 16 round the red channel with 30000 |
| ycc-read-past | ycc random | the loop reads and writes 16 pixels when only 15 are left (a trap at the top of memory) |
| up-saturate | unfilter | Up adds with saturation, not modulo 256 |
| sub4-carry | unfilter | Sub (4 bytes) carries the wrong pixel to the next 4 |
| sub3-store | unfilter | Sub (3 bytes) stores its last pixel's bytes from the wrong lane |
| avg-rounds-up | unfilter | Avg rounds the half up (the floor is the filter) |
| paeth-tie | unfilter | Paeth takes the up-left byte on a tie of b and c (the order of the tie is the filter) |
| paeth-upleft | unfilter | Paeth never moves the up-left pixel along |
| paeth-min | unfilter | Paeth takes a when pa is at most the larger of pb and pc |
| rgba-alpha | rgb to rgba | the last pixel of 4 gets the red byte of the first as its alpha |
| rgba-read-past | rgb to rgba | the loop reads a byte past the row (a trap at the top of memory) |
| across-store-lane | scaler across | a pixel's blue sum is stored from its alpha lane |
| across-no-reset | scaler across | a finished pixel's sum carries into the next |
| across-weight | scaler across | every weight one too big |
| down-halves | scaler down | the two halves of 4 sums take each other's products |
| down-big-weight | scaler down | a weight over 32 bits is cut to 32 bits |
| down-read-past | scaler down | the loop reads and writes 4 sums when 3 are left (a trap at the top of memory) |
| div-round | scaler divide | the divide does not add the half (t) before dividing |
| div-halves | scaler divide | the high half of the second sum is read as the first's |
| div-cap | scaler divide | the quotient is capped at 254, not 255 |
| div-trunc-round | scaler divide | the quotient is rounded to nearest, not truncated |
| div-read-past | scaler divide | the loop reads and writes two sums when one is left (a trap at the top of memory) |

## The Mac: predictions (written before measuring on it)

The branch was rebased onto main (after #49) as `image-simd`, and every number from here on is from Alex's Mac: an Apple Silicon arm64 machine, Homebrew clang 23.1.5, headless Chrome through `tools/cdp.mjs`.
The scalar numbers on main for the same bench (`bench/image/results/2026-11-19-box900.tsv`) were C-js 58.9, 139.1, 170.0 and 241.6 ms on p12.jpg, p12p.jpg, p48.jpg and p12.png, against 93.8, 256.6, 242.4 and 311.9 on the x86 VM.
So the Mac runs the JPEGs 1.4 to 1.8 times faster than the VM but the PNG only 1.3 times faster.

What should move between the two machines, and which way:

- The 64-bit integer division that build 3 replaced with a double division is cheap on Apple's cores (a few cycles) and costly on the VM's Xeon (tens of cycles), so the scaler's gain (L4) should be smaller on the Mac.
- Apple's cores run scalar code very wide, and a NEON integer operation has a latency of 2 to 3 cycles against 1 for a scalar one, so a loop bound by one dependent chain (Paeth, L5) should gain less, and may lose.
- The IDCTs (L1, L2) and YCbCr to RGBA (L3) are throughput loops with no chain from one block to the next; the Mac has more vector units than the VM, so they should keep their gain or grow it.
- The entropy decoders are untouched, so the whole-run ratio is bounded as on the VM.

Whole runs (`bench/image/run.mjs 9 900 C-js,C-simd,B1,B2`, median of 9 interleaved rounds, ratio C-simd over C-js):

| id | input | x86 ratio | Mac prediction | range | falsifier |
| --- | --- | --- | --- | --- | --- |
| W1 | p12.jpg | 0.86 | 0.90 | 0.84 to 0.96 | outside the range |
| W2 | p12p.jpg | 0.95 | 0.97 | 0.93 to 1.00 | outside the range |
| W3 | p48.jpg | 0.95 | 0.96 | 0.92 to 1.00 | outside the range |
| W4 | p12.png | 0.86 | 0.93 | 0.85 to 1.00 | outside the range |

- WT: no input slower with SIMD (every median ratio at most 1.00), the least sure of these on p12.png.
  Falsifier: a median ratio over 1.00.
- WM: the wasm memory after one run is the same for C-js and C-simd on every input, to the byte.
  Falsifier: any difference.
- WB: B1 and B2 are within 5% of their medians in main's run on every input (a check of the machine, not of the code).
  Falsifier: outside 5%.
- WC: against the browser, the SIMD build stays slower than B1 on p12p.jpg and p12.png and faster on p12.jpg and p48.jpg, as the scalar one is on main.
  Falsifier: any of the four the other way.
- Safari (scalar against SIMD on p12.jpg), if it can be driven: 0.92, range 0.80 to 1.05 (JavaScriptCore's wasm SIMD is newer than V8's and less tuned, so the widest range).
