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

## What was built (step 2)

All four candidates are in `src/cdom_image_jpegp.c` (about 700 lines), with no edit to the vendored sources.

- **The link-level swap.**
  cdom.mk no longer compiles `jdphuff.c`; cdom's `jinit_phuff_decoder` takes its place.
  `jdcoefct.c` is compiled through `src/cdom_image_ljt_coef.c` (it includes the vendored file with `jinit_d_coef_controller` renamed `cdom_ljt_d_coef_controller`, which is all `wrapper/jdcoefct-8.c` does besides the 8-bit setting), and cdom's `jinit_d_coef_controller` hands it every file that is not progressive.
  Baseline JPEG therefore runs libjpeg's own code, as before; `tools/third_party.py check` passes unchanged and the README of libjpeg-turbo says which two files are built differently and why.
- **(a) the entropy decoder.**
  A 64-bit bit buffer with libjpeg's byte rules (0xFF 0x00, fill bytes, a marker ends the data and zeros follow), refilled eight bytes at a time when none is 0xFF.
  A 10-bit lookahead table built by libjpeg's own canonical rule (`maxcode`), so a code longer than 10 bits, and a bad code (17 bits, a warning, symbol 0), decode exactly as `jpeg_huff_decode` decodes them.
  First AC scans: one lookup gives the run, the value and the bits of both when they fit in 10 bits; EOB runs skip whole blocks at once.
  Refinement: one lookup gives the run and the new coefficient's sign; the zero to stop at is found in the block's 64-bit nonzero mask (branch-free for runs up to 3), the correction bits of every nonzero coefficient passed are taken in one shift and spread to their positions with bit operations (branch-free up to 4), and the stored values change once per block.
  The bit buffer lives in locals in the two AC loops and goes back to its struct only around calls; the first bit taken past a scan's data sets `insufficient_data` (and the warning) at the same bit as `jpeg_fill_bit_buffer` would.
  Restart markers go through libjpeg's own `read_restart_marker`.
- **(b1) the store.**
  Per block: the coefficients the component's IDCT reads, in zigzag order, and a 64-bit nonzero mask; at size 8 the 64 values only (the mask is made from them when a refinement needs it), so a store is never larger than libjpeg's.
  One libjpeg virtual array per component, with an access window of a few rows, so `realize_virt_arrays` applies the budget exactly as it did to libjpeg's arrays (a first version asked for a window of the whole array, which `realize_virt_arrays` holds in memory whatever the budget; the oracle's over-budget case caught it, and the mutant `jpegp-window-all` keeps it caught).
- **(b2) skipped scans.**
  As in step 2's plan; a scan's touched range is Ss to Se + 15 (a corrupt first scan can write that far, and 63 at most), and the walk gives up (nothing skipped) on any marker or scan header it does not follow as libjpeg does, including libjpeg's own quirk in matching component ids.
  At start of each scan the scan libjpeg read is checked against the walk; a scan the walk did not see after one was skipped stops the decode (`JERR_NOTIMPL`, CORRUPT), which the fuzz runs never reached.
  With the standard script it skips every luma AC scan at 1/8 (p48p.jpg at a 900 box: the 4 luma AC scans of its 10; chroma, at 2x2, needs its AC), and nothing at 2/8.
- **(c)** one call per scan instead of one per block, the DC blocks' offsets in an MCU computed once per scan, and no whole-image `jzero_far` beyond the smaller store.

## Correctness (step 3)

| check | what | result |
| --- | --- | --- |
| X1 | tests/image/run.sh: the oracle's 66 cases natively under ASan and UBSan (9,182 checks), with the budget cases and peaks | pass |
| X1 | tools/image.mjs: every case in Chrome, CDOM_IMAGE_SJLJ=js, scalar and SIMD, with the capped heap and capped memory | pass |
| X1 | tools/image.mjs, CDOM_IMAGE_SJLJ=eh | not run: clang 20 cannot lower it (`__c_longjmp` already defined); it needs the LLVM 23 that could not be installed here, and this branch does not touch that file |
| X1 | tests/image/simd.sh: the SIMD loops, and the new IDCT read-table test (below) | pass |
| X2 | tests/image/prog.sh: 716 generated files (tests/image/progcorpus.py, seed 20261120) at every scale 1/8 to 8/8, 5,728 decodes, against libjpeg's own progressive path in the same binary, ASan and UBSan | 0 differences; 3,992 images, 1,728 CORRUPT and 8 TOO_BIG (the 501-scan file) with the same status on both sides; 14,079 scans skipped; peak held 721 MB in all against libjpeg's 1,050 MB |
| X3 | tests/image/prog.sh --fuzz: the differential target tests/image/fuzz_jpegp.c (5 scales a case), seeded with the corpus files under 100 KB | 240 s (5,534 inputs run), then 900 s (18,082 inputs run, 995 in the grown corpus): no difference, crash or leak; before those, a first run found one UBSan report (a Huffman table number over 3 indexed an array before libjpeg's own refusal), fixed |

The corpus: 7 sizes (1 x 1 to 333 x 517, odd and even) times grey, 4:4:4, 4:2:0, 4:2:2, 4:4:0 and 4:1:1, each with libjpeg's standard script (plain, optimized, restart every MCU row, restart every 3 MCUs) and five more scripts (spectral selection only, deep successive approximation down from Al 4, refinement bands that differ from the first bands, a scan per luma coefficient, bands outside the small IDCTs), cut copies (inside scans of every kind), corrupted copies (random, 0xFF and flipped bytes in the entropy data: bad codes and stray markers), a jpegtran transcode, 500 and 501 scans, a repeated scan, no EOI and DC only.

The IDCT read table is tested directly (tests/image/simd.c, `unread`): for every IDCT the decoder can pick (1x1 to 7x7 and islow in C, 2x2 and 4x4 in SIMD), 6,000 random blocks each: every coefficient outside `cd_image_idct_reads` set to random values changes no output byte, and every coefficient inside it changes the output in at least one case.

### The mutants

Each was applied alone, its test run, and the source restored (all 14 are in tools/mutate.py; on this VM tools/mutate.py itself cannot run, since its tests/build.sh step fails for reasons outside this branch, so a one-off loop over the same table ran them).

| mutant | what breaks | caught by |
| --- | --- | --- |
| jpegp-deposit | the second correction bit of a step goes to the wrong coefficient | prog.sh, FAIL progressive differential |
| jpegp-correct-sign | a correction moves a negative coefficient toward zero | prog.sh |
| jpegp-new-sign | a refinement's new coefficient takes the opposite sign | prog.sh |
| jpegp-refine-eobrun | a refinement's EOB run never ends | prog.sh |
| jpegp-first-run | the one-lookup path of a first scan loses the run's top bit | prog.sh |
| jpegp-dc-refine | a DC refinement sets the wrong bit | prog.sh |
| jpegp-insufficient | running past a scan's data does not stop the interval | prog.sh (the cut files) |
| jpegp-restart-eob | a restart leaves an EOB run running | prog.sh (the restart files) |
| jpegp-skip-all | every AC scan skipped, read or not | prog.sh |
| jpegp-no-closure | the skip ignores later decoded scans that touch a band | prog.sh |
| idct-reads-small | the 2x2 table drops row and column 1 | simd.sh, FAIL unread |
| idct-reads-large | the 2x2 table keeps row and column 2 | simd.sh, FAIL unread (the tightness check) |
| jpegp-window-all | the store's window is the whole array, past the budget | test_image, jpeg-prog444-over-budget: status OK, wanted TOO_BIG |
| jpegp-store-full | the store keeps all 64 coefficients at 2/8 | test_image, jpeg-p12-progressive: peak (its limit is now file + 26 MB, from 44) |

## Measurements (step 4)

Whole runs, `bench/image/run.mjs 9 900`, 9 interleaved rounds, box 900, median (min to max) in ms, and the C arm's wasm memory after one run (`bench/image/results/progressive/final.log`).
"main" is origin/main built here with the same clang 20 (bench/image/build-mainsimd, build-mainjs); B1 and B2 are the browser.

| input | main SIMD | new SIMD | new scalar | main scalar | B1 | B2 | wasm MiB main, new |
| --- | --- | --- | --- | --- | --- | --- | --- |
| p12p.jpg | 253.4 (241.0 to 256.3) | 164.7 (159.7 to 245.4) | 170.6 (166.0 to 215.8) | 266.6 (258.9 to 277.0) | 233.6 (222.2 to 289.0) | 233.8 (222.1 to 273.4) | 40.8, 26.1 |
| p24p.jpg | 479.3 (455.5 to 506.3) | 342.5 (303.1 to 392.1) | 327.0 (320.5 to 356.2) | 509.0 (496.5 to 537.6) | 446.5 (428.0 to 494.2) | 439.9 (408.9 to 482.7) | 77.7, 48.8 |
| p12p444.jpg | 396.2 (368.6 to 442.8) | 263.1 (236.1 to 274.7) | 263.5 (249.6 to 300.1) | 396.5 (391.2 to 433.3) | 330.0 (312.5 to 390.3) | 351.0 (317.6 to 377.4) | 77.2, 39.1 |
| p12pmany.jpg (26 scans) | 297.4 (281.7 to 362.4) | 179.6 (172.4 to 219.6) | 186.1 (179.0 to 222.1) | 307.2 (296.1 to 371.3) | 265.6 (246.5 to 272.2) | 267.2 (250.1 to 317.4) | 40.8, 26.1 |
| p48p.jpg (1/8) | TOO_BIG | 136.7 (131.1 to 155.3) | 145.7 (135.5 to 164.2) | TOO_BIG | 858.6 (843.1 to 1022.6) | 876.7 (846.2 to 931.8) | 16.0 (refused), 38.6 |
| p48.jpg (baseline) | 224.5 (212.9 to 263.1) | 228.8 (219.2 to 246.0) | 230.5 (220.3 to 279.6) | 234.6 (226.4 to 276.9) | 393.2 (379.4 to 427.3) | 405.8 (382.7 to 423.3) | 17.1, 17.1 |
| p12.jpg (baseline) | 76.0 (73.2 to 100.2) | 76.7 (73.5 to 93.3) | 88.1 (84.5 to 121.6) | 87.4 (84.6 to 94.3) | 108.3 (101.6 to 138.6) | 108.5 (103.5 to 147.5) | 6.1, 6.1 |
| p12.png | 267.3 (254.2 to 301.9) | 262.5 (254.1 to 287.2) | 309.8 (302.2 to 346.5) | 307.9 (301.9 to 372.7) | 170.3 (165.6 to 178.1) | 197.7 (180.8 to 222.7) | 17.1, 17.1 |

The new scalar build is as fast as the new SIMD build on progressive files (within the noise; on p24p.jpg it is faster): the SIMD loops are the IDCT, colour and scaler, now a small part of a progressive run, and the decoder has no SIMD of its own.

Per scan, the cut files again (7 rounds, main and new interleaved, `bench/image/results/progressive/perscan-new.log`), SIMD, the time of a scan as before:

| scan | main | new |
| --- | --- | --- |
| 1 (Y 1..5 first) | 12.2 | 3.7 |
| 4 (Y 6..63 first) | 17.4 | 12.2 |
| 5 (Y refine Al 1) | 60.6 | 40.7 |
| 7, 8 (chroma refine) | 18.2 | 17.7 |
| 9 (Y refine Al 0) | 82.7 | 43.3 |

What is left (`bench/image/results/progressive/profile-new-simd-p12p.txt`; the fuzzer ran on another core during it): the entropy decoding, all inlined into one function, is about 80% of the sampled time; scan 9 decodes 2.07 million refinement symbols (1.88 million of them new coefficients) and 2.77 million correction bits, about 20 ns a symbol in wasm.
The dependency from one symbol to the next (the lookup, the zero search, the count of correction bits, the shift) is what bounds it now.

### The predictions, marked

| id | predicted | measured | verdict |
| --- | --- | --- | --- |
| X1 | every case passes | native and wasm js-way pass; the eh-way not run (toolchain) | confirmed for what ran |
| X2 | 0 differing bytes or statuses | 0 of 5,728 | confirmed |
| X3 | the fuzz finds nothing | one UBSan report in the first run (fixed), then nothing | refuted by the first run, confirmed after the fix |
| T1 p12p.jpg | 0.45 (0.35 to 0.60) | 164.7 / 253.4 = 0.65 | refuted |
| T2 p24p.jpg | 0.45 (0.35 to 0.60) | 342.5 / 479.3 = 0.71 | refuted |
| T3 p12p444.jpg | 0.45 (0.35 to 0.60) | 263.1 / 396.2 = 0.66 | refuted |
| T4 p12pmany.jpg | 0.40 (0.30 to 0.55) | 179.6 / 297.4 = 0.60 | refuted |
| T5 p48p.jpg | main TOO_BIG; new under 0.6 of baseline p48.jpg | main TOO_BIG; 136.7 / 228.8 (new's own p48.jpg) = 0.597, 136.7 / 224.5 (main's) = 0.609 | confirmed against the same build's p48.jpg, by a hair; not against main's |
| T6 | new SIMD / B1 at most 0.65 on p12p.jpg | 164.7 / 233.6 = 0.71 | refuted (new is still 29% faster than B1) |
| T7 | scan 9 at most 35% of main's 92.9 ms | 43.3 ms, 47% (52% of main's 82.7 in the same run) | refuted |
| T8 | p12.jpg and p12.png within 5% of main | +0.9% and -1.8% (SIMD); +0.8% and +0.6% (scalar) | confirmed |
| T9 | new scalar / main scalar at most 0.55 | 170.6 / 266.6 = 0.64 | refuted |
| M1 p12p.jpg | 26.1 MiB (24.5 to 27.5) | 26.1 | confirmed |
| M2 p24p.jpg | 45 MiB (41 to 49) | 48.8 (main 77.7) | confirmed, at the top of the range |
| M3 p12p444.jpg | 36 MiB (33 to 40) | 39.1 (main 77.2) | confirmed |
| M4 p48p.jpg | 40 MiB (36 to 44) | 38.6 | confirmed |
| M5 p12.jpg, p12.png | unchanged | 6.1, 17.1 as main | confirmed |
| Z1 | a net 1 to 7 KB raw | +8.7 KB raw, +3.4 KB brotli (below) | refuted |

Why the time predictions missed: they assumed the refinement scans were slow mostly for the 63-position walk of every block, so that a mask would make them cheap.
On this grainy image the walk is not the main cost: refinement blocks are dense (about 11 new coefficients and 15 correction bits a luma block in scan 9, almost no EOB runs), so the work is the symbols themselves, and a symbol costs a dependent chain that a mask shortens but does not remove.
The first version of the new decoder, a direct port to a mask, was no faster than libjpeg's at all (natively 63 ms against 66 for scan 9); the speed came from keeping the bit buffer in registers and taking the branches out of the run search and the correction bits.

### Size

`bench/image/sizes`' way (`-Oz`, stripped; brotli -q 11), main and new built with the same clang 20:

| build | main raw | new raw | main brotli | new brotli |
| --- | --- | --- | --- | --- |
| no image code | 3,149 | 3,149 | 1,494 | 1,494 |
| both formats, scalar | 93,102 | 101,787 (+8,685) | 29,919 | 33,358 (+3,439) |
| JPEG only, scalar | 81,192 | 89,880 (+8,688) | 25,282 | 28,676 (+3,394) |
| both formats, SIMD | 98,539 | 107,256 (+8,717) | 31,892 | 35,419 (+3,527) |
| JPEG only, SIMD | 85,125 | 93,845 (+8,720) | 26,668 | 30,145 (+3,477) |

### Does progressive now match the browser?

On this machine, yes: the new SIMD build decodes p12p.jpg in 164.7 ms against B1's 233.6 and B2's 233.8, 29% less time, and every progressive input of the set is faster than both browser arms (p48p.jpg, which main refused, 6.3 times faster than B1).
On the Mac main was 130.2 ms against B1's 122.5; if the ratio new / main measured here (0.65) carries over, p12p.jpg would take about 85 ms there, about 30% under B1, but the Mac's numbers are the manager's to take.
Memory for p12p.jpg is 26.1 MiB, from 40.8.

## Decisions the brief did not cover

- The toolchain: clang 20 from Ubuntu (apt.llvm.org is blocked here), for main's and this branch's builds alike; the eh-way wasm tests could not run with it.
- cdom's own decoder rather than a patch to jdphuff.c, swapped in at link level, so the vendored tree has no edit at all; libjpeg's progressive code is still vendored and is the reference the tests compare with.
- A file whose whole coefficients exceed the budget at full size but whose kept coefficients fit now decodes instead of TOO_BIG (p48p.jpg at a 900 box; the oracle's 4096 x 4096 4:4:4 at 2/8).
  The over-budget oracle case moved to 8/8, where the store is libjpeg's size and the answer is still TOO_BIG, and the 2/8 decode of the same file became a case that must succeed under a peak limit.
- The bench gained four inputs (no new random draws; `inputs.py` now needs jpegtran for two of them) and `bench/image/scans.py`; `build-main*/` (main's pages, built by hand for comparison) is ignored by git.
- The block-smoothing path of libjpeg (`do_block_smoothing`) is not in cdom's controller: cdom turns it off, as it always did; buffered-image mode, which cdom never uses, is left to libjpeg's controller (whose entropy decoder would then stop with JERR_NOT_COMPILED).
