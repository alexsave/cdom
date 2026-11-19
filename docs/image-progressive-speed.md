# Progressive JPEG: a faster decoder and a smaller coefficient store

The image module resizes the bench's 12 MP progressive JPEG (`bench/image/inputs.py`, p12p.jpg) in 130.2 ms with SIMD on Alex's Mac, against the browser's 122.5 ms (B1, `createImageBitmap`).
Baseline JPEGs already beat the browser.
This branch makes the progressive path faster and smaller without touching the baseline JPEG or PNG paths.
The doc is in the order the work was done: the profile and its explanation, the predictions (committed before any new decoder code), then the build, the tests, the measurements, and each prediction marked.

## Where the numbers come from

Every number here is from a Linux x86-64 VM, not Alex's arm64 Mac, so they are indicative.
The cdom manager re-measures on the Mac.

| thing | value |
| --- | --- |
| CPU | Intel Xeon, model string "Intel(R) Xeon(R) Processor @ 2.10GHz", 4 cores |
| OS | Ubuntu 24.04.5, kernel 6.18.44 (a cloud VM) |
| Chrome | Google Chrome for Testing 153.0.8010.12, `--headless=new --no-sandbox` |
| clang for wasm | Ubuntu clang 20.1.2 with its wasm-ld 20.1.2 (see below) |
| clang for the native tests | Ubuntu clang 18.1.3 |
| node | v22.23.5 |
| inputs | `bench/image/inputs.py` with Pillow 13.0.0: p12p.jpg is 3,550,927 bytes, as in docs/image-simd-hand.md |

The toolchain the brief names (LLVM 23 from apt.llvm.org) could not be installed: the session's network policy answers 403 for apt.llvm.org.
Ubuntu's clang-20 and lld-20 build the image bench and the image tests as they are (the wasm-ld 22 requirement is `cd_tmpz`'s `__rodata_start` in src/cdom.c, which the image bench does not reach in a way that changes its numbers).
Main and this branch are both built with the same clang 20 here, so every comparison is like with like.

The skew to expect against the Mac: the Chrome on this VM is slow at the browser arms relative to the Mac (main's SIMD build and B1 are about even here, against 130.2 and 122.5 ms there), so the ratios between cdom builds carry over better than cdom against the browser does.
Every timing is a median of interleaved rounds with the spread (min and max) beside it; the load was under 0.5 for all of them.

## Step 1: the profile, and why progressive is slow

### The scan script of p12p.jpg

Pillow's `progressive=True` writes libjpeg's standard script (`jpeg_simple_progression` for YCbCr), ten scans (`bench/image/scans.py` prints it):

| scan | components | Ss..Se | Ah | Al | entropy bytes |
| --- | --- | --- | --- | --- | --- |
| 0 | Y, Cb, Cr | 0..0 (DC) | 0 | 1 | 126,021 |
| 1 | Y | 1..5 | 0 | 2 | 245,010 |
| 2 | Cr | 1..63 | 0 | 1 | 22,667 |
| 3 | Cb | 1..63 | 0 | 1 | 20,945 |
| 4 | Y | 6..63 | 0 | 2 | 869,957 |
| 5 | Y | 1..63 | 2 | 1 | 838,190 |
| 6 | Y, Cb, Cr | 0..0 (DC) | 1 | 0 | 35,866 |
| 7 | Cr | 1..63 | 1 | 0 | 104,738 |
| 8 | Cb | 1..63 | 1 | 0 | 103,379 |
| 9 | Y | 1..63 | 1 | 0 | 1,183,479 |

Scans 1 and 4 are the luma "first" scans (spectral selection with two bits held back), 5 and 9 refine luma one bit each, 2, 3, 7 and 8 do the same for chroma with one bit held back, and 0 and 6 are the DC.

### Time per scan

Measured in Chrome by cutting the file after each scan (`bench/image/scans.py p12p.jpg inputs` writes p12p-s0.jpg to p12p-s9.jpg, each the file to the end of that scan and an EOI) and timing each cut file as the bench times a file (`bench/image/run.mjs`, 7 interleaved rounds, box 900).
The time of scan k is the median of cut k less the median of cut k-1; cut 0 also holds everything that is not a scan: allocating and zeroing the coefficient buffer, the IDCT, colour conversion and the scaler over the whole image.
Main's build, SIMD and scalar (`bench/image/results/progressive/perscan-main.log`):

| cut | SIMD median ms (min to max) | scan time, SIMD | scalar median ms (min to max) | scan time, scalar |
| --- | --- | --- | --- | --- |
| s0 (DC first, and the fixed part) | 51.1 (46.1 to 60.9) | | 52.8 (50.8 to 62.2) | |
| s1 (Y 1..5 first) | 69.3 (57.5 to 81.6) | 18.2 | 65.4 (63.5 to 71.6) | 12.6 |
| s2 (Cr 1..63 first) | 62.5 (60.1 to 91.0) | noise | 66.4 (63.1 to 68.7) | 1.0 |
| s3 (Cb 1..63 first) | 63.0 (58.8 to 83.6) | noise | 69.4 (64.0 to 83.7) | 3.0 |
| s4 (Y 6..63 first) | 81.3 (78.7 to 102.9) | 18.3 | 85.1 (81.8 to 103.5) | 15.7 |
| s5 (Y 1..63 refine, Al 1) | 137.8 (132.4 to 166.4) | 56.5 | 150.6 (144.3 to 192.2) | 65.5 |
| s6 (DC refine) | 146.0 (141.6 to 173.2) | 8.2 | 152.1 (149.1 to 155.9) | 1.5 |
| s7 (Cr refine) | 162.3 (153.7 to 173.1) | 16.3 | 166.7 (161.0 to 221.7) | 14.6 |
| s8 (Cb refine) | 163.7 (156.4 to 190.9) | 1.4 | 176.0 (172.4 to 225.0) | 9.3 |
| s9 (Y 1..63 refine, Al 0) | 256.6 (245.5 to 302.5) | 92.9 | 273.5 (256.9 to 311.9) | 97.5 |

The small scans are within the noise of a 7-round median (s2 is faster than s1 in the SIMD column); the shape is not.
The four refinement scans of AC coefficients (5, 7, 8 and 9) are about 170 ms of the 257, and the two luma ones alone about 150.
The four first scans of AC coefficients cost about 35 ms, the DC scans under 10.

### Time per function

`bench/image/profile.mjs build-mainsimd p12p.jpg 8`, twice (`bench/image/results/progressive/profile-main-simd-p12p.txt`), main's SIMD build, self time per run:

| function | share | ms per run |
| --- | --- | --- |
| decode_mcu_AC_refine | 62.6%, 62.9% | 164.6, 161.8 |
| decode_mcu_AC_first | 11.9%, 11.9% | 31.4, 30.7 |
| consume_data (the coefficient controller's input loop) | 5.5%, 5.6% | 14.4, 14.3 |
| cd_image_scaler_row | 4.0%, 4.2% | 10.6, 10.7 |
| jpeg_fill_bit_buffer | 3.5%, 3.7% | 9.3, 9.6 |
| jsimd_idct_2x2 + jsimd_idct_4x4 | 4.6%, 4.4% | 12.1, 11.4 |
| decode_mcu_DC_refine + decode_mcu_DC_first | 4.7%, 4.9% | 12.2, 12.5 |
| access_virt_barray + jzero_far | 1.8%, 1.3% | 4.7, 3.4 |

### Why libjpeg-turbo's progressive Huffman decoder (jdphuff.c) is slower than its baseline one (jdhuff.c)

- **Refinement walks every position of every block.**
  `decode_mcu_AC_refine` steps k from Ss to Se (63 positions for p12p's refinement scans) in every block, through `jpeg_natural_order[k]`, to find which coefficients are already nonzero (each takes a correction bit) and to count the zero ones a run skips.
  A block inside an EOB run has no new coefficient at all, only the correction bits of its nonzero ones, and still costs the full walk.
  For luma that is 190,512 blocks x 63 positions x 2 scans, 24 million steps, each a table load, a coefficient load, a compare and a branch that the data decides.
- **Bits one at a time.**
  Each correction bit and each sign bit is its own `CHECK_BIT_BUFFER` and `GET_BITS(1)`.
- **A 32-bit bit buffer in wasm.**
  `bit_buf_type` is `size_t`, 32 bits in wasm32, so `MIN_GET_BITS` is 25 and the buffer is refilled through the out-of-line `jpeg_fill_bit_buffer` every three bytes or so (3.5% of the time on its own).
  Its byte loop checks each byte for 0xFF, and the refill is a call.
- **No fast path.**
  jdhuff.c has `decode_mcu_fast`, which decodes a whole MCU with the marker checks hoisted out (when enough bytes are left) and the symbol and value from one lookahead; jdphuff.c has only the careful path, with its state copied in and out for suspension at every MCU (a source that can suspend, which cdom's never does).
- **Every coefficient is decoded several times.**
  A luma AC coefficient of p12p is touched by three scans (a first scan and two refinements), each a full pass over the image's blocks, and every block of every scan costs a call through `decode_mcu` from `consume_data`, which rebuilds the MCU's block pointers and calls `access_virt_barray` per row (consume_data and access_virt_barray, 5 to 7%).
- **The whole-image coefficient buffer.**
  128 bytes for every block of every component, pre-zeroed, live for the whole decode: 36.6 MB for p12p.jpg, nearly all of its 40.8 MiB of wasm memory.

### Which coefficients the scaled IDCTs read

Read from the code (`jidctred.c`, `jidctint.c`, `jddctmgr.c` for which IDCT each size picks, and cdom's `src/jsimd/jsimd_wasm.c` for the SIMD 2x2 and 4x4); positions are (row, column) of the natural-order 8x8 block.
The IDCT size of a component is its `DCT_scaled_size`: for a 4:2:0 file at scale n/8, luma is n and chroma 2n (at most 8).

| size | IDCT | coefficients read | count |
| --- | --- | --- | --- |
| 1 | jpeg_idct_1x1 | (0, 0) | 1 |
| 2 | jpeg_idct_2x2 (and jsimd_idct_2x2) | rows and columns 0, 1, 3, 5, 7 | 25 |
| 3 | jpeg_idct_3x3 | rows and columns 0 to 2 | 9 |
| 4 | jpeg_idct_4x4 (and jsimd_idct_4x4) | rows and columns 0, 1, 2, 3, 5, 6, 7 (never row or column 4) | 49 |
| 5 | jpeg_idct_5x5 | rows and columns 0 to 4 | 25 |
| 6 | jpeg_idct_6x6 | rows and columns 0 to 5 | 36 |
| 7 | jpeg_idct_7x7 | rows and columns 0 to 6 | 49 |
| 8 | jpeg_idct_islow | all | 64 |

Details that decide the table:

- `jpeg_idct_2x2`'s first pass skips columns 2, 4 and 6 (`if (ctr == DCTSIZE - 2 || ctr == DCTSIZE - 4 || ctr == DCTSIZE - 6) continue`) and reads rows 0, 1, 3, 5 and 7 of the others; its second pass reads work-array columns 0, 1, 3, 5 and 7.
- `jpeg_idct_4x4` skips column 4 in the first pass and reads rows 0 to 3 and 5 to 7; the second pass reads work-array columns 0 to 3 and 5 to 7.
- cdom's SIMD 2x2 and 4x4 load whole rows (all eight columns) of the rows above, but the lanes of the columns the C skips go only into work-array columns the second pass never reads (the 4x4's `T4`, the 2x2's lanes of columns 2, 4 and 6), and its zero tests are per column, so their output does not depend on those coefficients either.
  The tests check this directly (below): a coefficient outside the table changed to any value changes no output byte, for every size, scalar and SIMD.
- The zero shortcuts of the C (`if (inptr[DCTSIZE * 1] == 0 && ...)`) test only coefficients in the table, so the shortcut taken does not depend on the others.

So at 2/8, the bench's scale, luma needs 25 of 64 coefficients and chroma 49; at 1/8, luma needs only its DC and chroma (2x2) 25.

## Step 2: the candidates, and the predictions

Written before any of the new code, and not edited after the measurements.

### What is built

All of it is cdom's own code in one new file, `src/cdom_image_jpegp.c`, which replaces libjpeg-turbo's progressive path at link level with no edit to the vendored sources: cdom's `jinit_phuff_decoder` (jdphuff.c is no longer compiled) and cdom's `jinit_d_coef_controller`, which takes a progressive file and hands every other file to libjpeg's own controller (built as before, under another name).

- **(a) a faster progressive entropy decoder**: a 64-bit bit buffer, a longer lookahead table, a combined table for the first AC scans (symbol and value in one lookup when they fit), EOB runs skipped in bulk, and refinement driven by a 64-bit mask of each block's nonzero coefficients: the correction bits of all the nonzero coefficients a step passes are read at once, the zero to stop at is found with bit operations, and only the coefficients the IDCT reads are updated.
  It must be bit-exact with jdphuff.c on every input, corrupt ones included: the same warnings' effects (`insufficient_data` at the same bit), the same restart handling (libjpeg's own `read_restart_marker`), the same writes outside the band that corrupt data makes jdphuff.c do.
- **(b1) a coefficient store that keeps only what the IDCT reads**: per block, the coefficients of the table above for that component's IDCT size, in zigzag order, and the 64-bit nonzero mask (the refinement scans need to know which coefficients are nonzero at every position of their band, read or not).
  At size 8 every coefficient is kept and the mask is left out (it is made from the values), so a full-size decode holds no more than libjpeg's 128 bytes a block.
  When no AC coefficient of a component is decoded (1/8 with a standard script), the mask is left out too.
  The store is one libjpeg virtual array per component, so libjpeg's own budget check (`realize_virt_arrays`, `jpeg_mem_available`) still decides TOO_BIG exactly as before, only for fewer bytes.
- **(b2) whole scans skipped**: before the first scan is decoded, the markers of the rest of the file are walked (the file is in memory) to know every scan's band.
  For each component, the coefficients that must be decoded are the closure of the ones its IDCT reads under "a scan that touches one decoded coefficient makes every coefficient it touches decoded"; a scan whose touched range (Ss to Se, widened to what a corrupt stream can write) holds no decoded coefficient is skipped, its data jumped over to the next marker that is not a restart marker.
  Bit-exact because a skipped scan writes only coefficients no IDCT reads and no decoded scan's history depends on; when the walk finds anything it does not fully understand, nothing is skipped.
  For the standard script this skips every luma AC scan at 1/8 and nothing at 2/8 or larger (luma's band 1..63 holds coefficients the 2x2 IDCT reads).
- **(c) what the profile also points at**: one call per scan instead of one per block (consume_data and access_virt_barray), and no pre-zeroing beyond the smaller store.

### Predictions

The bench inputs added for this branch (`bench/image/inputs.py`, from the same scene and grain, no new random draws): p24p.jpg (6000 x 4000, progressive, 4:2:0), p12p444.jpg (p12's pixels, progressive, 4:4:4), p12pmany.jpg (p12.jpg's coefficients through `jpegtran` with a 26-scan script: every band split, three refinement levels), and p48p.jpg (p48.jpg's coefficients, progressive with jpegtran's standard script; it is decoded at 1/8 for a 900 box).
"Main" is origin/main's build, "new" this branch's; ratios are new over main, so under 1 is faster.
Whole runs are `cd_image_resize` at box 900, median of 9 interleaved rounds.

**Correctness**

- X1: every tests/image case passes against its exact oracle (the existing ones, with the expected statuses).
  Falsifier: any failure.
- X2: on the generated progressive corpus (spectral selection only, successive approximation, many scans, 500 scans, 4:4:4, 4:2:0, grey, restart intervals, a truncated scan, corrupt Huffman codes), the new decode equals libjpeg-turbo's own (main's code, built beside it in the same binary) to the byte at every scale 1/8 to 8/8, under ASan and UBSan, and with the same status.
  Falsifier: any differing byte or status.
- X3: a fuzz run of the differential target (new against libjpeg's own, every scale) finds no difference, crash or leak.
  Falsifier: any finding.

**Time** (whole runs, SIMD builds, this VM)

| id | input | predicted new SIMD / main SIMD | range | falsifier |
| --- | --- | --- | --- | --- |
| T1 | p12p.jpg | 0.45 | 0.35 to 0.60 | outside the range |
| T2 | p24p.jpg | 0.45 | 0.35 to 0.60 | outside the range |
| T3 | p12p444.jpg | 0.45 | 0.35 to 0.60 | outside the range |
| T4 | p12pmany.jpg | 0.40 | 0.30 to 0.55 | outside the range |
| T5 | p48p.jpg | main refuses it (TOO_BIG: 146 MB of coefficients); new decodes it in less than 0.6 of the time of the baseline p48.jpg | | TOO_BIG, or 0.6 or more |

- T6: on p12p.jpg, new SIMD is faster than B1 on this VM by at least 35% (new / B1 at most 0.65).
  Falsifier: over 0.65.
- T7: the refinement scans: the per-scan method above on the new build gives scan 9 (luma refine, Al 0) at most 35% of main's 92.9 ms.
  Falsifier: over 35%.
- T8: no change for baseline JPEG and PNG: p12.jpg and p12.png medians within 5% of main's (the code they run is the same).
  Falsifier: outside 5% on either.
- T9: new scalar on p12p.jpg is at most 0.55 of main scalar (the decoder has no SIMD of its own).
  Falsifier: over 0.55.

**Memory** (the C arm's wasm memory after one run on a fresh page, as the bench prints it)

| id | input | main | predicted new | falsifier |
| --- | --- | --- | --- | --- |
| M1 | p12p.jpg | 40.8 MiB | 26.1 MiB (coefficients 36.6 MB to 21.1 MB) | outside 24.5 to 27.5 |
| M2 | p24p.jpg | about 73 MiB | 45 MiB (72.0 MB to 41.6 MB of coefficients) | outside 41 to 49 |
| M3 | p12p444.jpg | about 74 MiB | 36 MiB (73.2 MB to 33.1 MB) | outside 33 to 40 |
| M4 | p48p.jpg | refused | 40 MiB (23.6 MB of coefficients, and the 15 MB file) | outside 36 to 44 |
| M5 | p12.jpg, p12.png | 6.1 MiB, as main's | the same | any change of 0.1 MiB or more |

**Size** (bench/image/sizes' way, `-Oz`, stripped; `brotli -q 11`)

- Z1: the new file adds 4 to 9 KB raw and 2 to 4 KB brotli to an app with the image module, less what jdphuff.c was (about 3 KB raw).
  Falsifier: a net change outside 1 to 7 KB raw.
