# The PNG path's speed: inflate and the row pipeline

The goal (Alex): cdom's image module resizes the 12 MP PNG of the bench (`bench/image/inputs.py`, `p12.png`, 4032 x 3024 RGB, 8-bit, nearly all rows Paeth) in 242 ms on the Mac against the browser's 131 ms (createImageBitmap, B1).
"We are writing in C. How could the browser be much faster? We should at least match."
This branch (`png-fast`) works on the inflate side and the pipeline around it, keeping the low memory peak: rows stream, nothing holds the whole decoded image.
The three hot loops of the other side (the Paeth unfilter, the scaler, RGB to RGBA) are the parallel branch `image-simd`'s; their bodies are not edited here.

The doc is in the order the work was done: the machine, the predictions (committed before any measurement), then the measurements, each prediction marked.

## The machine

Every number here is from a Linux x86-64 cloud VM, not Alex's arm64 Mac, so the numbers are indicative; the cdom manager re-measures on the Mac.

| thing | value |
| --- | --- |
| CPU | "Intel(R) Xeon(R) Processor @ 2.30GHz", 4 cores |
| OS | Ubuntu 24.04, kernel 6.18.44 (a cloud VM) |
| Chrome | Google Chrome for Testing 153.0.8010.12, headless, through tools/cdp.mjs |
| clang | Ubuntu clang 18.1.3 (1ubuntu1), wasm-ld from the same package; native builds with the same clang |
| node | v22 (only to drive Chrome) |

The skew to expect against the Mac: V8 lowers wasm to x86 here and to arm64 there; the Mac's cores are faster per clock and have wider load units, so absolute times here are larger, and ratios move by a few tens of percent between the two.
The image-simd-hand branch measured this bench on a VM of the same kind: p12.png scalar C 311.9 ms, B1 180.9 ms, B2 198.5 ms, and tinfl's self time 170 ms of the C arm's.

## Predictions (written before any measurement)

Numbers are predicted with a point and a range; the falsifier of each is a measurement outside its range, unless it says otherwise.
"The stream" is p12.png's zlib stream (its IDAT payloads joined): 36,581,376 bytes out (3024 rows of 1 + 4032 x 3).

### P1. The gap, split (step 1)

Native, x86-64, clang -O2 (the Rust one: cargo's release profile), each library inflating the stream with its own streaming API into a bounded buffer (libdeflate has no streaming API: one call into a whole-output buffer, a reference only), adler32 checked, median of 21 runs:

| id | inflate | predicted ms | range |
| --- | --- | --- | --- |
| N1 | tinfl (miniz 3.1.2), as cdom calls it (32 KB wrapping window) | 110 | 80 to 150 |
| N2 | zlib 1.3.1 | 90 | 65 to 130 |
| N3 | zlib-ng 2.2.4 (its x86 SIMD paths on) | 60 | 40 to 85 |
| N4 | libdeflate 1.24 | 45 | 30 to 65 |
| N5 | fdeflate (the `png` crate's inflate, which Chrome's Rust PNG decoder runs) | 45 | 30 to 70 |

(N5 is withdrawn: its harness, in Rust, was never in this repository, so png-fast-2 removed the arm and its numbers; F2 below is then N1/N4.)

In wasm (Chrome 153, the inflate alone in a test page, cdom.mk's flags at -Oz, scalar):

| id | inflate | predicted ms | range |
| --- | --- | --- | --- |
| W1 | tinfl, as cdom calls it | 170 | 130 to 220 |

- F1, the wasm factor W1/N1: 1.5 (1.2 to 2.0).
- F2, the library factor N1/min(N4, N5): 2.4 (1.6 to 3.5).
- G1: of the C arm's lead of the browser on p12.png, the inflate library (F2) explains more than the wasm factor (F1): F2 > F1.
  Falsifier: F2 <= F1.
- G2: what Chrome 153 runs for a PNG is Skia's Rust PNG codec (the `png` crate, whose inflate is `fdeflate`), not libpng and zlib.
  Falsifier: libpng's or zlib's inflate in the binary's PNG decode path, with no Rust codec.

### P2. The inflate candidates in wasm (step 2)

Chrome 153, a test page that inflates the stream through each candidate's streaming interface, with a bounded window and no whole-output buffer, then hands the bytes on in rows (the PNG path's own loop, minus the unfilter, colour and scaler), median of 9 interleaved rounds:

| id | candidate | predicted ms | range |
| --- | --- | --- | --- |
| C-a | tinfl as is (the baseline, = W1) | 170 | 130 to 220 |
| C-b | zlib 1.3.1 or zlib-ng's generic C, built for wasm32 | 130 | 95 to 170 |
| C-c | cdom's own inflate (64-bit bit buffer, two-level tables, wide copies, a fast loop and a checked tail), scalar | 75 | 55 to 100 |
| C-c-simd | the same with -msimd128 (16-byte copies, adler32 in simd128) | 65 | 45 to 90 |

- P2a: C-c is at least 1.8 times faster than C-a (C-a / C-c >= 1.8).
  Falsifier: under 1.8.
- P2b: adler32 over the 36.6 MB in wasm: scalar (blocked at 5552, unrolled) 12 ms (8 to 20); simd128 3 ms (1.5 to 6).
- P2c: C-c in wasm is within 1.6 times of the best native library (C-c / min(N4, N5) <= 1.6).
  Falsifier: over 1.6.

### P3. The pipeline (step 3)

The C arm on p12.png, the bench page, before and after each change, the rest the same:

- P3a: unfiltering each row in place in the inflate's output buffer (no copy of the row out of the window): 3 ms faster (1 to 8).
- P3b: RGB to RGBA fused into the scaler's read (measured as an upper bound: the C arm with to_rgba skipped, a wrong image, timing only): under 3 ms on p12.png, so it is not worth a second scaler body.
  Falsifier: 5 ms or more.

### P4. The bench (step 5)

bench/image/run.mjs, box 900, median of 9 interleaved rounds, Chrome 153 headless on this VM.
New inputs (inputs.py, the same seed, drawn after the four existing ones so those stay byte-equal): `p12a.png` (the same 12 MP scene as RGBA, a smooth alpha), `p12g.png` (the scene as 8-bit grey), `p24.png` (the scene at 5664 x 4248, about 24 MP, RGB).

| input | old C | new C scalar | new C simd128 | B1 | B2 |
| --- | --- | --- | --- | --- | --- |
| p12.png | 300 (270 to 340) | 205 (170 to 240) | 200 (165 to 235) | 180 (155 to 210) | 195 (165 to 225) |
| p12a.png | 360 (300 to 430) | 250 (200 to 300) | 245 (195 to 295) | 200 (160 to 250) | 215 (170 to 260) |
| p12g.png | 120 (90 to 160) | 80 (60 to 110) | 78 (58 to 108) | 90 (65 to 120) | 100 (70 to 130) |
| p24.png | 600 (520 to 700) | 410 (340 to 480) | 400 (330 to 470) | 360 (300 to 430) | 390 (320 to 460) |

- M1 (the question asked): on this VM, new C scalar does not yet match B1 on p12.png: new C / B1 = 1.13 (1.0 to 1.3), with the unfilter and the scaler (image-simd's) left as the larger part.
  Falsifier: under 1.0 (C matches) or over 1.3.
- M2: new C / old C on p12.png: 0.68 (0.58 to 0.78).
- M3: on the grey PNG (a third of the bytes per pixel, so the unfilter and inflate shrink and the scaler stays), new C matches or beats B1.
  Falsifier: new C slower than B1 on p12g.png.

Memory and size:

- M4: the wasm memory after one run of p12.png (its peak; the file's 14.5 MiB is in it): old 17.4 MiB; new within 1 MiB of old (the inflate's output buffer is bounded by 32 KB plus a few rows plus a fixed chunk, no whole-image buffer).
  Falsifier: new more than 1 MiB over old.
- M5: the module (bench page's app.wasm, stripped) changes by -2 to +3 KB brotli -q 11 when cdom's inflate replaces tinfl.
  Falsifier: outside that range.

## Results

A correction to the prediction text, not to a prediction: p12.png's stream makes 36,581,328 bytes (3024 rows of 1 + 12,096), not 36,581,376 as written above.

### Step 1: what the browser runs, and the gap split

What Chrome 153 runs for a PNG: Skia's Rust PNG codec.
The binary (`/opt/pw-browsers/chromium-1243`, Chrome for Testing 153.0.8010.12) holds the strings `SkPngRustCodec` and `rust_png::` (the codec's error messages), and of the image decoders still behind a switch only `kRustyJpegFeature`, `kRustyBmpFeature` and `kRustyIcoFeature`: the PNG one has no switch left, so the Rust codec is the one that runs.
That codec is the `png` crate, whose inflate is the `fdeflate` crate (Rust, with an adler32 from `simd-adler32`), not zlib: Chromium's zlib (with its SIMD chunk copy and adler32) is in the binary for other uses, not on this path.
(Inferred from the binary's strings, not a trace; `chromium.googlesource.com` is not reachable from this machine, so Chromium's own zlib could not be fetched and built, and zlib-ng stands in for it below.)
G2 (the Rust codec, not libpng and zlib): confirmed, on that evidence.

The libraries, natively (bench/image/inflate/native.sh, native_round.sh; x86-64, clang 18 -O2; 7 rounds of 3 runs each, arms interleaved; median, min to max; p12.png's stream, 15,177,529 bytes in 232 IDAT chunks):

| id | inflate | measured ms | predicted | verdict |
| --- | --- | --- | --- | --- |
| N1 | tinfl (miniz 3.1.2), as cdom called it | 88.2 (83.7 to 117.8) | 110 (80 to 150) | confirmed |
| N2 | zlib 1.3.1 (64 KB out buffer) | 98.9 (93.8 to 105.2) | 90 (65 to 130) | confirmed |
| N3 | zlib-ng 2.2.4, its x86 SIMD on (AVX2 chunkset and adler32) | 69.2 (67.9 to 74.8) | 60 (40 to 85) | confirmed |
| N4 | libdeflate 1.24 (one call, whole-output buffer) | 50.8 (47.9 to 63.7) | 45 (30 to 65) | confirmed |
| | cdom's inflate (this branch, final) | 45.2 (43.6 to 51.0) | (P2) | |
| | cdom's inflate, fed the 232 IDAT payloads as pieces | 45.2 (43.7 to 54.9) | | |

In wasm (Chrome 153, bench/image/inflate/run.mjs, 9 interleaved rounds, each run on a fresh page; the inflate with its rows handed on, nothing else):

| id | inflate | measured ms | predicted | verdict |
| --- | --- | --- | --- | --- |
| W1 | tinfl, as cdom called it | 117.7 (112.2 to 136.3) | 170 (130 to 220) | refuted (faster) |

- F1, the wasm factor W1/N1: 117.7 / 88.2 = 1.33; predicted 1.5 (1.2 to 2.0): confirmed.
- F2, the library factor N1/N4 (N5 withdrawn): 88.2 / 50.8 = 1.74; predicted 2.4 (1.6 to 3.5): confirmed.
- G1 (F2 > F1): 1.74 against 1.33, confirmed: of the old C arm's inflate time, the library (tinfl against libdeflate) costs more than wasm does.

So the gap, on p12.png's inflate, split: the fastest inflate measured here (libdeflate) natively takes 51 ms; tinfl natively takes 1.74 times that (88 ms), and wasm adds 1.33 times again (118 ms).
(png-fast-2 removed the fdeflate arm and the png crate's whole decode, and their numbers, with the Rust harness that measured them: it was never in this repository, so no one could run it again.)

### Step 2: the inflate candidates in wasm

The same harness (9 interleaved rounds, Chrome 153, -Oz as cdom.mk builds, each candidate through its streaming interface into a bounded buffer):

| id | candidate | measured ms | predicted | verdict |
| --- | --- | --- | --- | --- |
| C-a | tinfl, as cdom called it | 117.7 (112.2 to 136.3) | 170 (130 to 220) | refuted (faster) |
| C-b | zlib 1.3.1, generic C | 134.8 (131.7 to 141.9) | 130 (95 to 170) | confirmed |
| C-b | zlib-ng 2.2.4, generic C (no SIMD: its functable's C paths) | 99.4 (98.3 to 110.6) | 130 (95 to 170) | confirmed |
| C-c | cdom's inflate, scalar | 58.5 (55.7 to 67.6) | 75 (55 to 100) | confirmed |
| C-c-simd | cdom's inflate, -msimd128 (adler32 and 16-byte copies) | 54.9 (54.5 to 63.2) | 65 (45 to 90) | confirmed |
| | adler32 alone over 36.7 MB, scalar (SWAR, 16 bytes a turn) | 7.8 (6.7 to 10.7) | 12 (8 to 20) | refuted (faster) |
| | adler32 alone, simd128 | 6.2 (5.4 to 10.3) | 3 (1.5 to 6) | refuted (slower) |

- P2a (C-a / C-c >= 1.8): 117.7 / 58.5 = 2.01, confirmed.
- P2b: both refuted, as in the table: the scalar adler32 is a SWAR one (8 bytes a 64-bit word, two multiplies sum the bytes and their weights), faster than predicted, and the simd128 one gains only 1.6 ms over it.
- P2c (C-c / min(N4, N5) <= 1.6): 58.5 / 39.0 = 1.50, confirmed: cdom's inflate in wasm is 1.5 times the browser's own inflate natively.

No SIMD was written for zlib-ng's chunk copy or adler32 in wasm (the brief allowed it): zlib-ng's generic build is already 1.7 times slower than cdom's inflate in the same harness, more than SIMD in two of its helpers could make up, so the candidate stopped there.
(d): what the measurements pointed at instead is the literal path, see below.

Picked: (c), cdom's own inflate, `src/cdom_image_inflate.c`, 2.0 times tinfl in wasm, about 520 lines with its comments, no third-party code.
How it got there, each step measured natively and in wasm on p12.png (native ms, then wasm ms):

| step | native | wasm |
| --- | --- | --- |
| first version: 64-bit bit buffer refilled without a branch, 11-bit first level with two literals an entry, second levels, 8- and 16-byte match copies, byte-wise adler32 | 57.5 | 76.3 (1 round) |
| adler32 as SWAR (it was 22 ms of the 57 natively) | 51.7 | |
| one 16-bit store for an entry's literals | 51.8 | |
| entries with the bit count in their low 6 bits, up to three literal entries a turn looked up before the stores (fdeflate's way), the next entry looked up before a match's copy | 48.0 | 69.1 |
| BAD entries filled only for an incomplete code; the 12-bit first level tried and dropped (no faster, twice the table building: p12.png has 619 dynamic blocks) | | 62.1 (11 bits) against 63.7 (12) |
| each literal lookup shifts by the entry itself (the shift takes its count modulo 64, so no mask on the chain) | 46.0 | 55.9 |
| four literal entries a turn: no faster, dropped | 45.5 | 58.0 against 57.3 |
| -O2 instead of -Oz for the wasm | | 60.5 against 60.6: no difference, so the inflate stays at the app's level |

The stream it is fast on: p12.png's literals take 5.2 bits each, so two fit an 11-bit entry nearly always; 622,647 matches make 16.9 MB of the 36.6 (27 bytes each, almost all at distances of 16 or more: the grain tile repeats every 1024 pixels).

### Step 3: the pipeline

Rows now flow: the inflate writes into one buffer (the 32 KB window, two rows, and 256 KB more); when it is nearly full, the sink takes each row that is whole and at least 32 KB behind the end (no later match can read it), unfilters it where it lies, against the row above it (kept in the buffer), and hands it on; the inflate then moves what is left (the window, the row above, the part-made row) to the front.
An 8-bit RGB PNG with no colour key goes to the scaler as RGB: the scaler takes a pixel step (`cd_image_scaler.step`, 4 by default, 3 here), so no RGBA row is written and read between.

The C arm on p12.png, the bench page (bench/image/run.mjs), 9 interleaved rounds each, two sessions; the variants were built from this branch with one change each (in a scratch worktree, not kept):

| arm | session 1 (before the RGB read) | session 2 (after) |
| --- | --- | --- |
| unfilter in place, to_rgba, scaler on RGBA | 184.8 (177.2 to 218.1) | 201.6 (181.0 to 235.6) |
| each row copied out of the buffer first (as the old path did), then the same | 183.4 (179.7 to 202.6) | 196.2 (182.4 to 239.2) |
| to_rgba skipped (a wrong image, timing only: the bound for P3b) | 172.3 (168.5 to 198.1) | 175.0 (168.3 to 222.1) |
| the RGB read (this branch) | | 173.9 (170.0 to 200.0) |

- P3a (unfiltering in place is 1 to 8 ms faster): refuted; no difference shows (in place 1.4 and 5.4 ms slower, inside the spread of either).
  It stays: it is no slower, it saves the two rows the copy would need, and the row's bytes are read once less.
- P3b (to_rgba under 3 ms, so not worth a second scaler body; falsifier 5 ms or more): refuted; skipping it saves 12.5 and 26.6 ms in the two sessions.
  So the RGB read went in, with no second scaler body: the scaler's across loop steps by `s->step` instead of 4, one token of its body, and the PNG path sets 3 for RGB with no colour key.
  Measured: 201.6 to 173.9 ms in session 2, the same as the bound.
- Tried and dropped: the same for 8-bit grey (step 1, with G and B read at offset 0): grey gained 10 ms (141.9 to 131.9, 5 rounds) but RGB lost 9 (173.4 to 182.2), since the scaler's G and B offsets became variables; RGB is the common case, so grey keeps its RGBA row.
- RGBA keeps its to_rgba: it composites each pixel on white before the average, which an average of the raw pixels cannot replace; its bound is 32 ms on p12a.png (244.0 against 211.6, 5 rounds).

### Step 5: the bench

bench/image/run.mjs, box 900, 9 interleaved rounds, each run on a fresh page, Chrome 153 headless; median (min to max), ms.
C-old is main (2d20ccd: tinfl, the RGBA row; `bench/image/old.sh 2d20ccd`, whose default is now main's tip), C-new this branch scalar, C-new-simd this branch with the inflate (only) at -msimd128 (`make -C bench/image SIMD=1`).
The prediction is under each number; the verdict is confirmed when the median is in its range.

| input | C-old | C-new | C-new-simd | B1 | B2 |
| --- | --- | --- | --- | --- | --- |
| p12.png (12 MP RGB, 15.2 MB) | 256.8 (234.4 to 319.8) | 183.2 (168.7 to 227.9) | 173.2 (170.1 to 194.6) | 146.2 (137.5 to 159.3) | 157.4 (151.4 to 169.2) |
| predicted | 300 (270 to 340): refuted (faster) | 205 (170 to 240): confirmed | 200 (165 to 235): confirmed | 180 (155 to 210): refuted (faster) | 195 (165 to 225): refuted (faster) |
| p12a.png (12 MP RGBA, 17.4 MB) | 327.7 (314.8 to 352.8) | 251.6 (241.4 to 303.0) | 248.5 (239.9 to 275.6) | 161.3 (148.2 to 171.1) | 172.2 (160.7 to 194.7) |
| predicted | 360 (300 to 430): confirmed | 250 (200 to 300): confirmed | 245 (195 to 295): confirmed | 200 (160 to 250): confirmed | 215 (170 to 260): confirmed |
| p12g.png (12 MP grey, 6.0 MB) | 167.3 (162.0 to 197.0) | 145.7 (141.4 to 155.6) | 146.1 (140.3 to 170.7) | 93.2 (87.4 to 103.1) | 91.4 (87.5 to 104.4) |
| predicted | 120 (90 to 160): refuted (slower) | 80 (60 to 110): refuted (slower) | 78 (58 to 108): refuted (slower) | 90 (65 to 120): confirmed | 100 (70 to 130): confirmed |
| p24.png (24 MP RGB, 26.2 MB) | 449.4 (441.2 to 468.2) | 323.5 (312.3 to 373.6) | 318.5 (311.9 to 353.1) | 260.9 (249.7 to 293.4) | 297.6 (266.2 to 313.2) |
| predicted | 600 (520 to 700): refuted (faster) | 410 (340 to 480): refuted (faster) | 400 (330 to 470): refuted (faster) | 360 (300 to 430): refuted (faster) | 390 (320 to 460): refuted (faster) |

- M1 (new C scalar does not yet match B1 on p12.png: new C / B1 = 1.0 to 1.3): 183.2 / 146.2 = 1.25, confirmed.
  With the inflate at -msimd128: 173.2 / 146.2 = 1.18.
- M2 (new C / old C on p12.png 0.58 to 0.78): 183.2 / 256.8 = 0.71, confirmed.
- M3 (new C matches or beats B1 on the grey PNG): refuted, badly: 145.7 against 93.2.
  The grey file's inflate is a third of p12.png's; what is left is the scaler (12 MP, the same work as RGB) and to_rgba's generic grey branch (20 ms by the bound: 143.0 against 122.7, 5 rounds), both image-simd's or left as they are here (the grey read was tried and dropped, step 3).
- So, plainly: **on this machine, C does not match the browser yet.** It went from 1.76 times B1 to 1.25 times on p12.png (1.18 with the SIMD inflate), and from 2.03 to 1.56 on p12a.png, 1.80 to 1.56 on p12g.png, 1.72 to 1.24 on p24.png.
  The inflate is no longer the larger part: cdom's inflate takes 55 to 59 ms of p12.png's 173 to 183; the unfilter (Paeth) and the scaler, image-simd's loops, are most of the rest.

Memory (the wasm memory after one run on a fresh page, its peak, the file's bytes in it):

| input | file | C-old | C-new | C-new beyond the file |
| --- | --- | --- | --- | --- |
| p12.png | 14.5 MiB | 17.1 MiB | 17.3 MiB | 2.8 MiB |
| p12a.png | 16.6 MiB | 19.1 MiB | 19.4 MiB | 2.8 MiB |
| p12g.png | 5.7 MiB | 8.3 MiB | 8.5 MiB | 2.8 MiB |
| p24.png | 25.0 MiB | 27.6 MiB | 27.8 MiB | 2.8 MiB |

- M4 (new within 1 MiB of old): +0.2 to +0.3 MiB on each input, confirmed.
  What the PNG path holds besides the scaler: its state (12.3 KB, the tables in it), one block of the zero row, the RGBA row and the inflate's buffer (32 KB + two rows + 256 KB): 329 KB for p12.png, against main's 43 KB of tinfl and three rows; nothing scales with the image's height (`png-whole-image`, the mutant that adds a whole image to it, is caught by the p12 peak case).

Size (bench/image/sizes: `app.c` at -Oz, stripped, clang 18 here, so not the Mac's numbers in README.md; bytes raw, then brotli -q 11):

| build | main (tinfl) | this branch | this branch, inflate at -msimd128 |
| --- | --- | --- | --- |
| no image code | 3,144 / 1,503 | 3,144 / 1,503 | |
| the module, both formats | 91,587 / 29,738 | 90,332 / 29,507 | 90,339 / 29,460 |
| PNG only | 15,889 / 6,843 | 14,620 / 6,725 | 14,627 / 6,752 |

- M5 (the module changes by -2 to +3 KB brotli): -231 bytes (the module 28,235 to 28,004 brotli beyond the empty app), confirmed.

### With image-simd's loops: does C match then?

Not a prediction (none was written for it), and not on this branch: a scratch merge of `origin/image-simd` (3e76f8b) into this branch, built with its `SIMD=1` (its wasm SIMD unfilter, scaler and RGB to RGBA, and this branch's inflate at -msimd128), measured the same way (9 interleaved rounds, same session as the arms beside it).
The merge's conflicts were mechanical (cdom.mk's parts, the PNG row loop, the scaler's across loop); two ways for RGB: `comb`, this branch's RGB read (the scalar across loop with step 3) for RGB and image-simd's SIMD across for the rest; `comb-rgba`, image-simd's SIMD RGB to RGBA and SIMD across for every image.
Timing only: the merged builds' images were not compared with the oracle.

| input | this branch | this branch, inflate -msimd128 | image-simd alone (tinfl) | comb | comb-rgba | B1 | B2 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| p12.png | 176.4 (170.7 to 192.4) | 180.6 (168.3 to 266.3) | 198.3 (194.9 to 219.9) | 151.6 (148.2 to 160.1) | 138.1 (133.1 to 154.6) | 146.4 (138.8 to 158.4) | 153.4 (144.9 to 189.2) |
| p12a.png | 247.3 (241.9 to 282.1) | 242.2 (238.0 to 294.4) | 258.6 (255.4 to 289.8) | 179.8 (177.8 to 188.4) | 181.4 (178.5 to 188.3) | 153.7 (144.6 to 177.7) | 173.0 (161.5 to 193.4) |

- With both branches, **C matches the browser on p12.png on this machine: 138.1 ms against B1's 146.4 (0.94) and B2's 153.4.**
  On the RGBA PNG it does not yet: 180 against 154 (1.17).
- For the merge: with image-simd's SIMD across loop, the RGB read of this branch is slower (151.6) than RGB to RGBA in SIMD and the SIMD across (138.1), so the merged module should send RGB through image-simd's path (`step` 3 only where there is no SIMD across).
- The ratio on the Mac will differ (the skew in "The machine"); the cdom manager re-measures there.

### After main took image-simd (the merge into this branch)

image-simd landed on main while this branch was open; the merge (14eb11a) keeps both, as the scratch run above pointed: the scalar build uses this branch's RGB read (the across loop, now in `cdom_image_loops.c`, takes a step, inlined as a constant: 4 for RGBA, 3 for RGB), and the SIMD build (`CDOM_IMAGE_SIMD=1`) keeps image-simd's SIMD RGB to RGBA and across, and builds the inflate with its SIMD flags (`CDOM_IMAGE_INFLATE_CFLAGS` follows `CDOM_IMAGE_SIMD`).
The merged module, 5 interleaved rounds (same machine, same Chrome):

| input | merged, scalar | merged, SIMD | B1 | B2 |
| --- | --- | --- | --- | --- |
| p12.png | 170.9 (169.9 to 220.2) | 137.5 (135.0 to 167.7) | 150.5 (144.5 to 157.9) | 172.2 (149.9 to 173.3) |
| p12a.png | 245.1 (241.5 to 276.3) | 182.3 (181.8 to 190.5) | 160.8 (152.3 to 163.9) | 169.6 (162.8 to 185.3) |

So with the module as it now is on this branch, the SIMD build beats B1 on p12.png (0.91) on this machine; RGBA is 1.13 times B1.
The merged tree passes tests/image/run.sh (9,192 checks, inflate_diff, the SIMD differential test) and tools/image.mjs with IMAGE_EH=0 (26 checks, scalar and SIMD builds).

## Tests

- tests/image/run.sh (native, ASan and UBSan, clang 18 here with its compiler-rt installed): every case of the oracle equal to the byte (65 file cases, 14 scaler cases, 9,192 checks, 0 failures), and `inflate_diff`: 495 streams equal to zlib's output, 912 refused by both, 9,596 cuts CORRUPT, 900 damaged agreeing, the hand-made dynamic blocks (one one-bit distance code, accepted; an incomplete, an over-subscribed code and a missing end code, refused), a sink that stops (STOPPED) and one that drops the window (STOPPED), seed 20261119.
  The corpus: zlib at levels 0 to 9, every strategy (stored, fixed, Huffman only, RLE, filtered), windows of 9 to 15 bits, sync and full flushes; hand-made fixed and stored blocks with distance 1 runs, length 258, distances at 32,768 and one past the output; each stream read from pieces of 1 byte to 70 KB, each its own heap block, into the smallest buffer allowed.
- tools/image.mjs, the js builds (the plain one, the capped heap, the module whose memory.grow refuses): every case's status and bytes equal in Chrome, the errors, NO_ROOM.
  The eh builds need a clang newer than this machine's 18 (`-wasm-use-legacy-eh`), as on main; not run here.
- tests/image/fuzz.sh, 300 s each (clang 18's libFuzzer): fuzz_jpeg 2,817 runs, fuzz_png 36,010, and the new fuzz_inflate (cdom's inflate against zlib on any bytes) 337,392: no crash, leak or limit.
- tools/third_party.py check: ok.

Mutants (tools/mutate.py's list; its own build step, tests/build.sh, needs a wasm2js newer than Ubuntu's 108 here, so they ran through a scratch driver with the same list, the same test command and the same text to find): the image module's 40, all caught for their reason, the new ones:

| mutant | what it breaks | caught by |
| --- | --- | --- |
| png-prev-row-dropped | the row above is not kept in the buffer | png-rgb differs |
| png-rgb-key-fused | an RGB image with a colour key takes the RGB read | png-rgb-key differs |
| png-window-unfiltered | rows unfiltered 4 KB from the end, inside the window | png-rgb CORRUPT |
| img-step-rgba | the scaler steps 4 on RGB | png-rgb differs |
| png-idat-not-entered (now) | only the first IDAT is read | png-rgba CORRUPT |
| png-bad-stream (now) | the inflate's answer ignored | png-adler-mismatch OK |
| png-rows-kept, png-whole-image, png-no-plte (now) | as before, on the new code | as before |
| inflate-dist-past-start | a distance one past the output allowed | ASan, heap-buffer-overflow |
| inflate-copy-overlap | 16-byte copies at distances of 8 to 15 | ASan, memcpy-param-overlap |
| inflate-incomplete-code | incomplete codes allowed | inflate_diff, "an incomplete literal/length code" |
| inflate-lit2-too-long | two literals in an entry one bit too long | inflate_diff, zlib case 6 |
| inflate-subtable-size | second levels sized without the codes taken | inflate_diff, zlib case 97 |
| inflate-third-literal | the third literal's lookup off by a bit | inflate_diff, zlib case 2 |
| inflate-stored-nlen | a stored block's NLEN unchecked | inflate_diff, a damaged stored stream |
| inflate-sink-overdrops | a sink may drop the window | ASan in the greedy sink's test |
| inflate-sink-ignored | a sink's stop ignored | inflate_diff, "a sink that stops" |
| inflate-adler-unchecked | the adler32 not compared | png-adler-mismatch OK |
| inflate-adler-weight | one SWAR weight wrong | inflate_diff, "adler32 of 84 bytes" |
| inflate-reads-past-piece | the fast loop's input margin 4, not 32 | ASan, heap-buffer-overflow |

Gone with tinfl's loop: `png-no-checksum-end` and `png-stops-at-last-row` (the stream is now always read to its adler32 inside the inflate: inflate-adler-unchecked and the cut checks stand for them), `png-window-wrap` (no wrapping window; png-window-unfiltered stands for it).
fuzz_inflate, mutation-checked by hand: with inflate-third-literal it traps at once; with inflate-lit2-too-long it ran 120 s (491,652 runs) without finding it (a byte-flipped stream rarely makes two codes of exactly 12 bits), which inflate_diff catches.

## Decisions the brief did not cover

- miniz stays in third_party/ (tools/third_party.py still makes and checks it) as the inflate bench's tinfl baseline, but leaves the module's build: an app no longer ships it or owes its MIT notice (README.md, its README).
- The inflate is its own part of the module (`CDOM_IMAGE_INFLATE_CFLAGS`), so -msimd128 can be given to it alone; it gains 3.6 ms in the inflate bench and 0 to 10 ms on the page (inside the noise in the combined session), so the default stays scalar.
- One token of the scaler's body changed (`px += s->step`, a field init sets to 4), for the RGB read; the RGBA, grey and palette paths keep to_rgba.
- inputs.py: the three new inputs are made after the four old ones and draw nothing; with this machine's Python 3.11 Pillow does not load, so inputs.py ran with /usr/bin/python3 (3.13, Pillow 12.3); the four old files are byte-equal to before (sha256 checked).
- tests/image/test_image.c includes sys/types.h: glibc declares ssize_t only there, so the test did not build on Linux.
- Distances past the output so far are now CORRUPT (zlib refuses them); tinfl, in its wrapping mode, read old window bytes for them.

## png-fast-2: the review's gaps

PR #56's review found no wrong answer by reading, but seven gaps; this branch (png-fast-2, on top of png-fast) closes them.
The one measurement is the memory of small PNGs (item 3); its predictions follow, committed before any measurement.

### Predictions: the small-PNG memory (written before measuring)

The inflate's buffer is now `CD_INFLATE_WINDOW + 2 rows + min(PNG_CHUNK, h x (stride + 1))`, and at least the contract's floor (`CD_INFLATE_CAP_MIN`, 32 KB + 1314); the RGBA row is not allocated when the scaler reads RGB as it is (step 3, the scalar build).
Peaks are test_image.c's count (the most held at once, the file not counted), natively, scalar build.

- S1: a 16x16 RGBA PNG holds 300 KB (290 to 310) on png-fast, and 47 KB (42 to 52) on png-fast-2: the 32 KB window and 1.3 KB more instead of 288 KB, beside the decoder's tables (11.5 KB) and the scaler.
  Falsifier: png-fast-2 above 55 KB, or more than 8 KB above main's peak for the same file.
- S2: main (tinfl: a 32 KB dictionary, its 11 KB decompressor, two rows) holds 43 KB (38 to 48) for that file: png-fast-2 is within 6 KB of it, above or below.
  Falsifier: a difference over 8 KB.
- S3: p12.png (4032x3024 RGB) holds the same on png-fast-2 as on png-fast, less the RGBA row (16,128 bytes) in the scalar build; the SIMD build keeps the row and holds the same.
  Falsifier: any other difference over 1 KB.
- S4: p12.png's time does not change with this branch's formula (its h x (stride + 1) is far over PNG_CHUNK, so the buffer is the same).
  A smaller PNG_CHUNK costs time through more hand-overs to the rows (each a call and a memmove of the window and two rows, about 56 KB): at 64 KB, 560 hand-overs instead of 140, about 23 MB more moved, 1 to 4 ms more natively (of about 100) and 1 to 6 ms in wasm (of about 140); at 16 KB, 2240 hand-overs, 5 to 15 ms more natively.
  Falsifier: 64 KB costs more than 8 ms on either, or 16 KB costs under 3 ms natively.

### Results: the small-PNG memory

Measured after the predictions above were committed.
Peaks: tests/image/host_alloc.c's count around one `cd_image_resize` (the most held at once, the file not counted), natively, scalar, the same build for each tree (main 2bf7c2a, png-fast 67f9643, png-fast-2).

| file | main | png-fast | png-fast-2 |
| --- | --- | --- | --- |
| 16x16 RGBA (box 300) | 44,066 (43.0 KB) | 309,691 (302.4 KB) | 48,731 (47.6 KB) |
| 8000x3 RGB (box 300) | 134,274 (131.1 KB) | 423,835 (413.9 KB) | 201,694 (197.0 KB) |
| p12.png, 4032x3024 RGB (box 900) | 2,544,994 (2485 KB) | 2,822,651 (2756 KB) | 2,806,523 (2741 KB) |

- S1: png-fast 302.4 KB (predicted 290 to 310), png-fast-2 47.6 KB (predicted 42 to 52), 4.6 KB above main: confirmed.
- S2: main 43.0 KB (predicted 38 to 48), png-fast-2 within 6 KB of it: confirmed.
- S3: p12.png holds 16,128 bytes less on png-fast-2 than on png-fast, exactly the RGBA row: confirmed for the scalar build.
  The SIMD build is wasm only, so it was not measured here; it allocates the row as before (`rgb_read` is 0 there).
- The 8000x3 file was not predicted: png-fast-2 holds 66 KB more than main there (the zero row of 24 KB and the whole image of 72 KB in the buffer beside the window), and 217 KB less than png-fast.

p12.png's time with PNG_CHUNK at 256 KB (the default), 64 KB and 16 KB (`-DPNG_CHUNK=`), box 900, median of the rounds (min to max), ms.
Natively: bench/image/native.c, x86-64, clang 23.1.6 -O2, 7 interleaved rounds, each the mean of 3 runs; png-fast (67f9643) as a fourth arm.
In wasm: bench/image/run.mjs, Chrome 153 headless, 9 interleaved rounds, each run on a fresh page, the js page at cdom.mk's -Oz, scalar.

| PNG_CHUNK | native | wasm |
| --- | --- | --- |
| png-fast, 256 KB | 186.1 (175.7 to 206.9) | |
| 256 KB | 179.2 (175.7 to 199.6) | 219.8 (202.8 to 272.5) |
| 64 KB | 177.1 (172.5 to 187.5) | 228.3 (204.1 to 240.1) |
| 16 KB | 182.2 (177.4 to 194.8) | 298.5 (205.8 to 391.8) |

This machine is slower than PR #56's (its p12.png took about 100 ms natively there; here 180), so the predicted costs are read against these medians.
- S4, the formula: p12.png's buffer is the same on both branches, and its time did not change (179.2 against 186.1, inside the spread): confirmed.
- S4, 64 KB: natively 2.1 ms faster, not 1 to 4 ms slower (inside a 15 ms spread); in wasm 8.5 ms slower, just over the falsifier (8 ms): refuted, both ways, by numbers smaller than the noise.
- S4, 16 KB: natively 3.0 ms slower (the falsifier was under 3: not falsified, but inside the spread); in wasm 79 ms slower, far over the prediction, with a spread of 186 ms.
So 256 KB stays: a smaller chunk saves memory only on large images (a small one now takes the whole image, not the chunk) and costs time in wasm.

### png-fast-2: the tests and their mutants

Each is in tools/mutate.py and was run through it; each is caught by its own case's line unless the column says otherwise.

| mutant | what it breaks | caught by |
| --- | --- | --- |
| inflate-dist-past-start | the fast loop's distance check allows one more | inflate_diff, "fast loop, a distance one past the output" (its buffer has 'Z' bytes before it, so the bad match reads them and the stream's adler32 then matches) |
| inflate-dist-past-start-checked | the checked loop's, the same | inflate_diff, "checked loop, a distance one past the output" (the stream as pieces of one byte) |
| inflate-hlit-287 | HLIT 30 and 31 allowed | inflate_diff, "HLIT 30 (287 literal/length codes)" |
| inflate-hdist-31 | HDIST over 29 allowed | inflate_diff, "HDIST 30 (31 distance codes)" |
| inflate-repeat-first | a code 16 first repeats a 0 | inflate_diff, "a code 16 first" |
| inflate-repeat-past-end | a repeat may run one past HLIT + HDIST | inflate_diff, "a repeat one past HLIT + HDIST" |
| inflate-repeat-unchecked | no repeat check at all | UBSan, "index 316 out of bounds" (the lengths overrun the stack array before any case can answer: a sanitizer catch, not an assertion; the case of three 138-zero repeats stays as a guard) |
| inflate-block-type-3 | block type 3 read as dynamic | inflate_diff, "block type 3" |
| inflate-header-cm, -cinfo, -fcheck, -fdict | one header check gone each | inflate_diff, "header CM 7", "header CINFO 8", "header FCHECK wrong", "header FDICT set" |
| inflate-fixed-286 | fixed symbols 286 and 287 end the block | inflate_diff, "fixed literal/length symbol 286" |
| inflate-fixed-dist-30 | fixed distances 30 and 31 are distance 1 | inflate_diff, "fixed distance code 30" |
| inflate-fixed-dist-no-30-31 | 30 and 31 have no code (the fixed distance table does not build) | inflate_diff, zlib case 0 |
| inflate-lt-short, inflate-dt-short | CD_INFLATE_LT 2339, CD_INFLATE_DT 591 | inflate_diff, "the largest tables (2340 and 592 entries)" |
| inflate-stops-on-good | STOPPED on a good stream (the room check 2 KB too strict) | fuzz_inflate within its seeds (the old trap, CORRUPT only, ran 30 s on it without a find) |
| simd-adler-weight-16, simd-adler-step-16, simd-adler-no-nmax | the SIMD adler32's high weight, its 16 x s1 per block, its NMAX cap | simd.sh, "FAIL adler32" |
| png-cap-whole-chunk | a small PNG's buffer is the 256 KB chunk again | oracle png-small16-rgba, peak |
| png-cap-no-floor | the buffer under the inflate's floor | oracle png-small16-rgba, CORRUPT |
| png-rgba-row-on-rgb-read | the RGBA row allocated on the RGB read | oracle png-wide8000x3-rgb, peak |

zlib's examples/enough.c (zlib 1.3.1) confirms the table sizes: `enough 286 11 15` is 2340 and `enough 30 9 15` is 592 (592 is also the answer at root 6, which is why it looked like one); no constant changed.
miniz (tinfl) stays in third_party/ only for the inflate bench's baseline arms (bench/image/inflate/native.c and wasm.c, N1 and W1).

## The Mac: Chrome and Safari

Alex's arm64 Mac, nothing else heavy running, `bench/image/png-mac.sh`: 9 interleaved rounds at box 900, in headless Chrome and then in Safari (tools/safari.mjs, through safaridriver).
The arms: C-old (main at 8eb591b, before this branch, built by `bench/image/old.sh`), C-js (this branch, scalar), C-simd (this branch, SIMD), B1 and B2.
The Mac's earlier numbers (docs/image-simde.md, the clean rerun) had p12.png at 238.1 ms scalar, 206.8 ms SIMD with the Paeth fix, and B1 at 130.6 ms, in Chrome.

### Predictions (written before the run)

- M1: in Chrome, p12.png's C-simd is within 10% of B1 (at most 1.10 times B1's median). Refuted if it is slower than that.
- M2: in Chrome, p12.png's C-js is at least 25% faster than C-old. Refuted if C-js is over 0.75 times C-old.
- M3: the JPEG inputs (p12.jpg, p12p.jpg, p48.jpg) are unchanged: C-js within 5% of C-old in both browsers, since this branch does not touch the JPEG path. Refuted by a gap over 5% either way.
- M4: in Safari, p12.png's C-simd is faster than B1 (the one-round smoke run had 165 ms against 213). Refuted if B1's median is lower.
- M5: the wasm memory after p12.png is at most 0.5 MiB above C-old's in both browsers (the inflate's buffer is about 290 KB, against tinfl's). Refuted by more.

### Results (2026-11-19)

Chrome 157.0.8103.79 headless, then Safari 27.0.1; the 1-minute load was 2.1 to 3.2 throughout (macOS's mediaanalysisd held about one core, nothing of ours ran beside the bench), and 2 of the 360 runs waited for the load gate.
Medians of 9 rounds in ms, the wasm memory after the run in MiB; the per-run tables are results/2026-11-19-png-mac-chrome.tsv and -safari.tsv.

| input | browser | C-old | C-js | C-simd | B1 | B2 | wasm MiB, old to new |
| --- | --- | --- | --- | --- | --- | --- | --- |
| p12.jpg | Chrome | 58.9 | 59.3 | 51.8 | 63.3 | 70.7 | 6.1 to 6.1 |
| p12p.jpg | Chrome | 139.3 | 136.3 | 130.7 | 122.8 | 129.2 | 40.8 to 40.8 |
| p48.jpg | Chrome | 171.1 | 170.5 | 160.1 | 214.4 | 270.7 | 17.1 to 17.1 |
| p12.png | Chrome | 235.9 | 178.2 | 156.7 | 131.0 | 139.9 | 17.4 to 17.6 |
| p12.jpg | Safari | 63.0 | 62.0 | 54.0 | 85.0 | 76.0 | 6.1 to 6.1 |
| p12p.jpg | Safari | 141.0 | 142.0 | 134.0 | 160.0 | 151.0 | 40.8 to 40.8 |
| p48.jpg | Safari | 176.0 | 175.0 | 164.0 | 369.0 | 330.0 | 17.1 to 17.1 |
| p12.png | Safari | 305.0 | 229.0 | 160.0 | 203.0 | 196.0 | 17.4 to 17.6 |

- M1 refuted: Chrome's p12.png C-simd is 1.196 times B1 (156.7 against 131.0), not within 1.10. The gap to Chrome's decoder is 26 ms, down from 76 (206.8 against 130.6 in the clean rerun).
- M2 refuted, narrowly: C-js is 0.755 of C-old in Chrome (24.5% faster, the line was 25%). In Safari it is 0.751.
- M3 confirmed: the JPEG inputs' C-js is within 2.2% of C-old in both browsers.
- M4 confirmed: in Safari, p12.png's C-simd (160) beats both B1 (203) and B2 (196); so do all of C's JPEG arms, and p48.jpg by more than 2 times.
- M5 confirmed: the wasm memory after p12.png is 0.2 MiB above C-old's in both browsers.

So PNG now matches or beats the browser in Safari, and is still 20% behind Chrome's B1; that gap is open work, not this branch's.

## Paeth as a wavefront

Issue #73's step 1.
On Alex's Mac (Chrome 157) p12.png (4032 x 3024 RGB, 3,016 of its 3,024 rows Paeth) resizes to box 900 in 163.7 ms with the SIMD build, against 131.0 for createImageBitmap (B1).
Paeth is 70.8 ms of it, about 20 cycles a pixel: `unfilter_paeth` (src/cdom_image_loops.c) is latency-bound on the chain from one pixel to the next, one pixel in flight at a time.

The change: pixel (r+1, x) needs row r up to x (its b and c) and its own left pixel (a), so row r+1 can run behind row r with a chain of its own.
The SIMD build puts rows r and r+1 in one v128 of 16-bit lanes (row r in lanes 0 to 3, row r+1 in lanes 4 to 7) and rows r+2 and r+3 in a second, each row 2 pixels behind the one above it: four chains in flight.
The PNG sink hands over a run of up to 4 whole Paeth rows when they are all past the window, in the buffer it already has.

### The lag

A lagging row's b and c are its upper row's pixels, made a few steps earlier and read back from the row's bytes (where the upper row's step stored them).
With a lag of 1 pixel, row r+1's b at step t is the pixel row r stored at step t-1: the store, the load back (or, in place of memory, a shuffle from lanes 0 to 3 to 4 to 7) and the distance arithmetic on b sit between one step and the next, so the cross-row path is longer than the a chain and is the critical one.
With a lag of 2, b was stored two steps before it is read and c three (c is the previous step's b, kept in a register), so a whole step's latency (about 20 cycles) covers the store and the load, and the critical chain is a's alone, as in the one-row loop.
So the lag is 2.

### Predictions (written before the new loop)

The Mac re-measures; the numbers of this VM (x86-64) are indicative, so these are ratios.

- P1. Paeth's self time on p12.png in the SIMD build (bench/image/profile.mjs, a page linked without wasm-opt so it keeps its name section, as docs/image-simd-hand.md says) drops to 0.28 to 0.42 of today's.
  Falsifier: above 0.64 of today's (on the Mac that is Paeth above 45 ms), which means the wavefront failed.
- P2. The whole p12.png resize at box 900 (bench/image/run.mjs, 9 interleaved rounds, C-simd against main's C-simd built from origin/main, plus B1) drops by 0.25 to 0.33 of main's time.
  Falsifier: a drop under 0.15.
- P3. p12.jpg and p12p.jpg are unchanged within 3% (the JPEG path is not touched).
  Falsifier: a C-simd median more than 3% from main's on either.
- P4. The wasm memory after p12.png is unchanged within 0.1 MiB (the sink keeps the same buffer; a run is rows already in it).
  Falsifier: a change over 0.1 MiB.
- P5. (scalar) The same 4-row interleave in plain C (the compiler gets four independent chains) drops scalar Paeth to at most 0.6 of today's.
  Falsifier: above 0.85 of today's; then it is dropped from the scalar build.
- P6. (mine) P5 is refuted: the scalar Paeth already has bpp independent chains (byte i needs byte i - bpp only), so it is bound by its throughput (about 15 integer operations a byte), not by a chain; the interleave lands at 0.85 to 1.05 of today's in the unfilter microbench.
  Falsifier: under 0.85.
- P7. (mine) The lag: in the unfilter microbench (node, the same V8 as Chrome, 3,024 Paeth rows of 4,032 RGB pixels), the wavefront with a lag of 1 is at least 1.10 times the time of the lag of 2.
  Falsifier: lag 1 within 1.10 of lag 2, or faster (then the cross-row path was not the critical one, and the lag is not what keeps the wavefront fast).
- P8. (mine) The microbench of the wavefront on runs of 4 is 0.25 to 0.40 of the one-row loop's time (4 chains where there was 1, short of 0.25 by the extra loads and the two vectors' throughput); runs of 2 with a 2-row variant (one vector) are 0.50 to 0.65.
  Falsifier: runs of 4 above 0.50, or runs of 2 above 0.75.

### What was built (and how it differs from the plan above)

Every number in this part is from this doc's x86-64 cloud VM (Chrome for Testing 153 headless, node 22, LLVM 23.1.6 from its release tarball, since apt.llvm.org is blocked here), so they are indicative; the Mac re-measures.
The unfilter microbench is `bench/image/paeth/` (build.sh, then `node bench/image/paeth/run.mjs`): p12.png's own filtered rows, unfiltered in wasm by each loop the way the sink hands them over, interleaved rounds, medians; every arm must give the same bytes.

The layout went through three versions, each measured on the microbench at runs of 4 (main's one-row SIMD loop is 1.000):

| version | runs of 4 |
| --- | --- |
| the plan: two rows a vector in 16-bit lanes, b loaded back from the row above's stored bytes | 0.88 |
| the same, b moved into its lanes from the results of 2 steps before (registers), `paeth_v` forced inline (at -Oz clang had made it a call) | 0.67 |
| four rows in one vector of bytes, b from registers, the raw bytes loaded a step ahead (shipped) | 0.61 |

- Loading b back from memory costs a failed store-to-load forward: the 4-byte load of a 3-byte pixel spans two or three of the narrower stores before it.
- On this VM the 16-bit loop is bound by its instruction count, not its chain: about 75 x86 instructions a step (V8's code, `node --print-wasm-code`), and a version with the chain cut entirely took the same time.
- So the shipped loop puts all four rows in one vector of 8-bit lanes (row j in bytes 4j to 4j + 3) and does Paeth in bytes: with p = b - c and q = a - c each made as two saturated halves, pa and pb are their ORs and pc is the distance of the two halves' sums.
  When p and q share a sign the sum may saturate at 255, but then the true pc is pa + pb, and both compares (pa <= pc, pb <= pc) answer the same for 255 as for the true value; when the signs differ, pc is exact.
  About 30 wasm operations a step for four pixels, where the 16-bit layout needs about 50.
- For 3 bytes a pixel each row's fourth byte is the next pixel's raw byte: the loop stores 4 bytes with that byte put back as loaded, and loads the next step's raw bytes before the step's stores (after them, the load overlaps the store just made, another failed forward, and that one sits in the chain).
- On the Mac both layouts should be bound by the chain (8 NEON operations deep in each), so the difference there is the 8-bit loop's headroom: about 30 operations a step for 4 pipes, against about 50.
- The lag is 2 (b is a result of 2 steps before, so the shift into the lower row's lanes is a step off the chain); the microbench has lag 1 at 1.06 of lag 2.
- Runs of 2 and 3 use the same loop, with the rows past the run sharing the last row's bytes, stored before it so the live row's store lands last: runs of 2 at 0.754 (a 2 and a 1 at 0.835, two single rows 1.000), runs of 3 at 0.663, so short runs take the wavefront.
- The scalar build: four rows interleaved in plain C measured 1.11 times a row at a time (196.7 ms a row at a time, 217.6 interleaved), so the scalar build has none (P5 below), and its `cd_image_unfilter_run` is a row.
- The sink (`rows()` in src/cdom_image_png.c) passes `cd_image_unfilter_run` the rows that are whole and past the window from the next one (at most the rows left), and it takes a run of up to 4 Paeth rows from them, or the one row of another filter; then each row goes to the RGBA step and the scaler in order.
  What it keeps is unchanged (the row above the next run), and so is the buffer's formula: a run is rows already in it.

The microbench (9 rounds after one warm-up, ms, the medians):

| loop | ms | of the SIMD row loop |
| --- | --- | --- |
| scalar, a row at a time | 196.7 | 1.759 |
| SIMD, a row at a time (main) | 111.8 | 1.000 |
| SIMD wavefront, lag 2, runs of 4 | 68.6 | 0.613 |
| SIMD wavefront, lag 1, runs of 4 | 72.8 | 0.651 |
| SIMD wavefront, runs of 3 | 74.2 | 0.663 |
| SIMD wavefront, runs of 2 | 84.3 | 0.754 |
| SIMD wavefront, runs of 2 then 1 | 93.4 | 0.835 |

Chrome runs the loop faster than node's V8 does: in Chrome's profile the wavefront is 0.31 of main's Paeth, not 0.61.

### Results

The profile (bench/image/profile.mjs, p12.png at box 900, 12 runs after a warm-up, the SIMD pages linked without wasm-opt, so they keep their name sections), the first of two runs; the second gave Paeth 61.3 to 18.8 ms and the resize 185.4 to 142.3:

| function, main (cdc957e) | ms | share | | function, this branch | ms | share |
| --- | --- | --- | --- | --- | --- | --- |
| cd_inflate_zlib | 65.5 | 33.6% | | cd_inflate_zlib | 67.3 | 42.7% |
| cd_image_unfilter_simd (Paeth inlined) | 43.0 | 22.1% | | cd_image_scaler_row | 42.3 | 26.8% |
| cd_image_scaler_row | 40.9 | 21.0% | | cd_image_unfilter_run_simd (the wavefront) | 20.2 | 12.8% |
| unfilter_paeth | 19.3 | 9.9% | | build | 8.1 | 5.2% |
| build | 8.6 | 4.4% | | cd_image_adler32 | 6.3 | 4.0% |
| cd_image_adler32 | 5.3 | 2.7% | | rows | 5.0 | 3.2% |
| cd_image_scaler_div_simd | 4.3 | 2.2% | | cd_image_scaler_div_simd | 4.2 | 2.6% |
| rows | 4.1 | 2.1% | | window.arm | 2.5 | 1.6% |
| resize, mean | 192.1 | | | resize, mean | 154.4 | |

The bench (bench/image/run.mjs, 9 interleaved rounds at box 900, the 1-minute load 0.58 to 1.13; C-old and C-old-simd are main at cdc957e, built from a worktree as bench/image/old.sh does, plus its SIMD page; bench/image/results/2026-11-20-paeth-wave-chrome.tsv), medians in ms, the wasm memory after the run in MiB:

| input | C-old | C-old-simd | C-js | C-simd | B1 | B2 | wasm MiB, main to this |
| --- | --- | --- | --- | --- | --- | --- | --- |
| p12.jpg | 89.3 | 76.0 | 88.3 | 78.4 | 109.0 | 109.2 | 6.1 to 6.1 |
| p12p.jpg | 256.1 | 244.4 | 257.4 | 243.9 | 226.0 | 231.6 | 40.8 to 40.8 |
| p48.jpg | 244.2 | 238.6 | 241.4 | 230.1 | 393.2 | 398.5 | 17.1 to 17.1 |
| p12.png | 231.5 | 178.4 | 224.2 | 139.1 | 171.8 | 192.2 | 17.3 to 17.3 |

Per round, C-simd against main's C-simd: p12.png 0.770 (0.660 to 0.829), p12.jpg 1.017 (0.994 to 1.549), p12p.jpg 0.964, p48.jpg 0.973.

- P1 confirmed: Paeth's self time is 0.32 of main's (62.3 to 20.2 ms; the second run 0.31), inside 0.28 to 0.42.
- P2 missed its range, not falsified: p12.png's C-simd is 22% faster than main's (178.4 to 139.1 ms; per round 0.770), under the 25 to 33% predicted and over the 15% falsifier. The other stages stayed as they were (the inflate 65.5 ms, the scaler 40.9), so Paeth's 42 ms is 22% of this VM's whole run. C-simd is now under B1 here (139.1 against 171.8).
- P3 refuted, narrowly: p12.jpg's C-simd median is 3.2% over main's (78.4 against 76.0), over the 3% line; p12p.jpg is within it (0.2%). Per round the p12.jpg ratio is 1.017, and the JPEG path's code is untouched (the module's PNG part and its layout are what changed), so I read it as noise, but the line was 3%.
- P4 confirmed: the wasm memory after p12.png is the same bytes (18,153,472 for both SIMD pages, 18,087,936 for both scalar ones).
- P5 refuted: four rows interleaved in plain C are 1.11 times a row at a time, over the 0.85 falsifier; dropped from the scalar build.
- P6 refuted on its own range, on the side it predicted: 1.11 is over the 1.05 top (the interleave is slower, not the same); its falsifier (under 0.85) is not hit.
- P7 refuted: lag 1 is 1.06 times lag 2, under 1.10. On this VM the loop is bound by its instruction count, so a longer chain shows only a little; the Mac, bound by the chain, is where the lag should matter.
- P8 refuted: runs of 4 are 0.61 of the one-row loop in node (falsifier 0.50), and runs of 2 are 0.75 (falsifier 0.75, on the line). In Chrome the same loop is 0.31, inside P1's range: node 22's V8 makes slower code of it than Chrome 153's.

### Tests

- tests/image/simd.c, "paeth runs": `cd_image_unfilter_run_simd` and `_scalar` against `cd_image_unfilter_scalar` row by row, on blocks of rows whose filters are Paeth runs of 1 to 5 with 0 to 2 rows of None, Sub, Up and Avg between (and runs that meet, and one run of 5), 3 and 4 bytes a pixel, widths 1 to 9 pixels and 4031 and 4032 (and some rows not a whole number of pixels), a zero or a random row above the first, handed over a random number of rows at a time as the sink does; every byte compared, the rows each call answers checked against the run there, and the block's last byte is the last byte of memory (a read past the last row traps). 1,488 cases from the test's seed.
- tests/image/oracle.py: png-paeth-runs-rgb and -rgba (403 x 300, rows in Paeth runs of 1 to 5, each followed by a row of None, Sub, Up or Avg in turn, the first row Paeth), and png-paeth-runs-far-rgb (rows that repeat every 26, so the stream copies each row from near the window's far end); pixels from a formula, so the older inputs are unchanged; Pillow decodes each file to its pixels first; the peak limits are what main held for these files (576 and 578 KB, rounded up, and the file).
- tools/mutate.py, the mutants, each caught by the assertion named:

| mutant | what it breaks | caught by |
| --- | --- | --- |
| paeth-wave-lag-off-by-one | row 1 runs 1 pixel off the lag its lanes' b assume | simd.sh, FAIL paeth runs |
| paeth-wave-c-old | the lagging rows keep an old up-left pixel (c) | simd.sh, FAIL paeth runs |
| paeth-wave-tail-drop-last | a run of 4 leaves its last row's tail bytes filtered | simd.sh, FAIL paeth runs |
| paeth-wave-keep-off | 3 bytes a pixel: the next pixel's raw byte overwritten | simd.sh, FAIL paeth runs |
| paeth-wave-store-order | runs of 2 and 3: the rows past the run stored after the last row | simd.sh, FAIL paeth runs |
| paeth-run-not-broken | a row of another filter taken into the run | simd.sh, FAIL paeth runs |
| paeth-run-not-broken-png | the same, through the PNG path | tests/image/run.sh in Chrome, FAIL wasm_js_simd: png-paeth-runs-rgb |
| png-sink-keeps-row-short | the sink drops the row above the next run | the native tests, png-paeth-runs-rgb |
| png-sink-run-past-window | a run takes rows still inside the window | tests/image/run.sh in Chrome, FAIL wasm_js_simd: png-paeth-runs-far-rgb |
| png-sink-run-scaled-once | only a run's first row reaches the scaler | tests/image/run.sh in Chrome, FAIL wasm_js_simd: png-paeth-runs-rgb |

png-sink-run-past-window was missed by the first oracle case (no match there reads the rows near the window's far edge); png-paeth-runs-far-rgb was added for it, and catches it.

### Paeth as a wavefront: the Mac (written before the run)

Alex's arm64 Mac, `bench/image/png-mac.sh` (C-old is main's scalar page; 9 rounds, box 900, Chrome then Safari) and `bench/image/profile.mjs` on a page linked without wasm-opt.
Main on the Mac: Paeth 70.8 ms of a 163.7 ms profiled resize; the bench's p12.png C-simd 157.0 ms in Chrome (B1 131.1) and 161 in Safari (B1 205).

- W1: Paeth's self time in Chrome's profile is at most 30 ms (0.42 of 70.8). Falsifier: above 45 ms.
- W2: in Chrome, p12.png's C-simd median is under B1's. Falsifier: at or above it.
- W3: in Safari, p12.png's C-simd falls by at least 0.15 (to 137 ms or less). Falsifier: less than 0.10.
- W4: C-js (no scalar change) and the JPEG inputs are within 3% of main in both browsers.
- W5: the wasm memory after p12.png is unchanged.

### Paeth as a wavefront: the Mac's results (2026-11-19)

Chrome 157.0.8103.79 headless, then Safari 27.0.1; load 2.2 to 3.0; the per-run tables are bench/image/results/2026-11-19-wave-mac-chrome.tsv and -safari.tsv.
Chrome's profile (12 runs, the SIMD page without wasm-opt): resize 117.6 ms mean against 163.7 on main; cd_inflate_zlib 59.0 ms, cd_image_unfilter_run_simd 23.7, cd_image_scaler_row 22.5, build 4.5, cd_image_adler32 2.7.

| input | browser | C-old (main) | C-js | C-simd | B1 | B2 |
| --- | --- | --- | --- | --- | --- | --- |
| p12.jpg | Chrome | 58.8 | 58.8 | 51.8 | 63.3 | 69.8 |
| p12p.jpg | Chrome | 136.3 | 136.6 | 131.7 | 122.9 | 128.5 |
| p48.jpg | Chrome | 170.3 | 171.6 | 160.1 | 216.1 | 259.7 |
| p12.png | Chrome | 178.5 | 179.5 | 111.9 | 131.7 | 139.9 |
| p12.jpg | Safari | 66 | 67 | 55 | 87 | 79 |
| p12p.jpg | Safari | 144 | 143 | 136 | 167 | 154 |
| p48.jpg | Safari | 177 | 177 | 165 | 373 | 333 |
| p12.png | Safari | 234 | 232 | 118 | 207 | 196 |

- W1 confirmed: Paeth 23.7 ms in Chrome's profile (0.33 of 70.8).
- W2 confirmed: Chrome's p12.png C-simd is 111.9 ms against B1's 131.7, 15% under the browser's own decode.
- W3 confirmed: Safari's p12.png C-simd falls from 161 to 118 ms (0.27), against B1's 207.
- W4 confirmed: C-js and the JPEG inputs are within 1.5% of main in both browsers.
- W5 confirmed: 17.6 MiB after p12.png, as main.

So PNG now beats the browser in both: 0.85 of Chrome's createImageBitmap and 0.57 of Safari's.
The next hotspot is the inflate (59.0 ms, half the resize), then Paeth and the scaler at about 23 ms each.

## The inflate fast loop

Issue #73's step 2 (branch `inflate-fast`, from main at 7e7f01f, before PR #79's wavefront; the inflate does not depend on it, and every measurement below was taken before main, with #79, was merged into the branch).
On Alex's Mac with #79 (Chrome 157) p12.png at box 900 resizes in 117.6 ms in the SIMD build's profile, and cd_inflate_zlib is 59.0 ms of it.
The goal is more margin over createImageBitmap, mainly from the inflate.
The constraint: PR #74 (`chunked-read`) moves the inflate's state between symbols into cd_inflate and adds cd_inflate_put, so only the fast loop's body, its helpers and the table build change here; cd_inflate_zlib's driver, its signature, the callbacks and the state layout stay.

### The machine and the toolchain

The same kind of x86-64 cloud VM as above (4 cores, Intel Xeon at 2.30GHz), so the numbers are indicative and the Mac re-measures.
Chrome for Testing 153.0.8010.12 headless (CHROME_ARGS=--no-sandbox), node 22.23.5, LLVM 23.1.6 from its GitHub release tarball (apt.llvm.org answers 403 here), binaryen 130 (the release tarball, sha256 as in ci.yml), Python 3.11 with numpy and Pillow 13.0.0, zlib1g-dev 1.3, libjpeg-turbo-progs 2.1.5.

### What the profile says (measured before any change)

- Chrome's profile of main (bench/image/profile.mjs, p12.png, box 900, 12 runs, the SIMD page linked without wasm-opt): resize 163.6 ms mean; cd_inflate_zlib 55.6 ms (33.4%), cd_image_scaler_row 35.8, cd_image_unfilter_simd 35.6, unfilter_paeth 16.3, build 7.0, cd_image_adler32 5.2, rows 3.5, cd_image_scaler_div_simd 3.4.
- What the fast loop does on p12.png's stream (a counting copy of the inflate, natively): 4,598,952 turns of the loop; 4,331,546 start with a literal entry, 4,080,039 of them take a second literal entry and 3,855,594 a third; the entries hold one literal 5,004,946 times and two 7,262,233 times (19.5 MB of literals from 12.27 M lookups); 152,744 literal/length codes take the second level, no distance does; 622,646 matches make 17.1 MB, and 622,625 of them are at distances of 64 or more (4 at distance 1); their lengths: 8 or less 56.1%, 9 to 16 26.3%, 17 to 32 5.6%, 33 to 64 3.5%, over 64 8.4%; 618 ends of block, 19 symbols in the checked loop, 1,857 table builds (619 dynamic blocks, three tables each).
- The first level's size, counted the same way: 10 bits 12.60 M literal lookups (and 473 K second-level ones), 11 bits 12.27 M (153 K), 12 bits 12.03 M (52 K).
  So a 12-bit first level saves 2% of the lookups: the one-literal entries are mostly a literal before a length code or the odd one of a run, not a pair that did not fit.
  It would also need a larger `lt` than CD_INFLATE_LT (zlib's enough for 12 bits is over 2340), which is the state layout; not tried.
- V8's code for the loop (`node --print-wasm-code`, TurboFan, x64, the inflate harness bench/image/inflate/wasm.c with the inflate at -Oz -msimd128 and its name section):
  - a literal entry is 15 to 17 instructions, and the lookups form one chain: `shr r, cl` (the entry is the count), `and 0x7ff`, `lea` (the table's base and the index times 4), the load; about 9 cycles a lookup here;
  - the loop's head is about 16 instructions a turn: the two limits (`out <= out_lim && in <= in_lim`) become two compares, two `setcc` and an `and` whose result is stored to the stack and compared again, V8's stack check, and the bit buffer stored to the stack and loaded back before the next shift, which puts a store-to-load forward on the chain;
  - a turn of three literal entries is about 78 instructions for about 4.7 bytes;
  - a match is about 120 instructions besides the copy: the extra bits each take a mask made from two shifts, the distance check loads `o->total` from memory and adds the 64-bit count, and six values are stored to the stack and loaded back.
- Natively (callgrind on bench/image/inflate/native.c, clang 23 -O2, one run): cd_inflate_zlib's own code 322.9 M instructions (8.8 an output byte), build 56.3 M (30 K a build: the code reversal bit by bit 8.9 M, the pass that pairs literals about 30 M), adler32 80.3 M.
- Without the match copy (a wrong output, timing only), the wasm inflate in node is 4 to 8 ms faster of about 60: the copy is not where the time is.

### The changes to try

- C1, one limit: each turn takes at most 7 input bytes (one refill) and writes at least one byte, so the loop checks only `out` against a limit that is the lower of the buffer's and `out` plus the input's room over 8; it leaves earlier when the input binds and comes back after the driver's checks.
- C2, the bit count without masks: `bl -= e` (the whole entry: only its low 6 bits are right, which is all the refill needs), instead of summing the counts.
- C3, a literal entry is a non-negative int32 (the class's bits move to the top), so its test is a sign test.
- C4, the distance check against the lowest byte a match may read, so the loop keeps neither `out0` nor `o`.
- C5, the match copy: at distances of 16 or more, two 16-byte copies before the loop.
- C6, the table build: the reversed code made by an increment from the last (zlib's way), and the pairing pass without branches.

### Predictions (written before the change)

- Q1. cd_inflate_zlib's self time in Chrome's profile (as above, 12 runs) is 0.80 of main's (0.70 to 0.85), main measured in the same session.
  Falsifier: above 0.85 of main's (then the step failed).
- Q2. build's self time in the same profile is 0.60 of main's (0.45 to 0.75).
  Falsifier: above 0.80.
- Q3. Natively, callgrind: cd_inflate_zlib's own instructions fall from 322.9 M to 265 M (240 to 285), and build's from 56.3 M to 35 M (28 to 42).
  Falsifier: cd_inflate_zlib above 290 M, or build above 45 M.
- Q4. C1 is the largest of the fast loop's changes: on its own, at least 20 M of the instructions callgrind counts.
  Falsifier: under 20 M, or another change alone saves more.
- Q5. C5 alone moves the inflate bench in Chrome by under 2 ms (the lengths 17 to 32, 5.6% of the matches, are all it saves a turn of the copy loop for).
  Falsifier: a gain of 2 ms or more.
- Q6. The inflate bench in Chrome (bench/image/inflate/run.mjs, 9 rounds, the SIMD build): this branch at 0.85 of main's median (0.75 to 0.90).
  Falsifier: above 0.90.
- Q7. The image bench (bench/image/run.mjs, 9 rounds, box 900): p12.png's C-simd at 0.93 of main's C-simd (0.85 to 0.97); the JPEG inputs within 3% of main.
  Falsifier: p12.png's C-simd above 0.98 of main's, or a JPEG input more than 3% off.
- Q8. On the Mac (not measured here; for the cdom manager's run): cd_inflate_zlib at 50 ms or less in Chrome's profile, against 59.0.

### What was built

Measured after the predictions above were committed; the changes were made one at a time in a scratch copy, each counted and timed, and the ones kept went into src/cdom_image_inflate.c.
Three counts, each deterministic to 0.2%: callgrind's instructions natively (clang 23 -O2), and the instructions V8's TurboFan code runs for one inflate of p12.png (callgrind on node with `--no-liftoff`, three runs less one, halved; build and adler32 included), with node 22 (V8 12.4) and with node 27.0.0-alpha.0 (V8 15.6, the nearest to Chrome 153's V8 15.3; Chrome's own `--print-wasm-code` prints nothing, its release build has no disassembler).

| version | native, cd_inflate_zlib | native, build | V8 12.4 | V8 15.6 |
| --- | --- | --- | --- | --- |
| main | 322.9 M | 56.3 M | 590.6 M | 610.8 M |
| C1 alone (one limit, the exits as they were) | 313.1 M | 56.3 M | 571.8 M | |
| C5 alone (the 32-byte first copy) | 323.6 M | 56.3 M | 589.1 M | |
| C6 alone (the build) | 322.9 M | 50.7 M | 581.2 M | |
| C1 with the exits as returns, C2, C4 and one mask for the extra bits | 311.6 M | 56.3 M | 506.9 M | |
| the same and C6 | 311.6 M | 50.7 M | 500.4 M | |
| all of C1 to C6 but C3 | 321.1 M | 50.7 M | 496.8 M | 513.1 M |
| this branch: the same, the literal lookups indexed by a 32-bit shift | 328.0 M | 50.7 M | 514.7 M | 537.4 M |

The scalar build, V8 15.6: main 674.0 M, this branch 601.2 M.

What the instructions showed, change by change:

- C1 alone saved little in V8 because of how clang left the loop: `while (out <= out_lim && ...)` with `end` and `bad` flags set inside made clang keep the loop's condition in a local (to tell the exits apart after the loop), which V8 stored to the stack each turn.
  With the exits as returns (BAD returns 0, the end of the block saves the state and returns 1, only the limit leaves the loop), the loop's head is V8's stack check, `bl | 56` and one compare, and the bit buffer stays in a register.
- C2 (`bl -= e`) and C4 (the distance against the buffer's start, since a sink keeps the window: while nothing was dropped the output starts there, and once something was, no distance reaches past it) took two values out of the loop (`out0` and `o`), and with C1 cut V8 12.4's count by 14%.
- C3 was not done: the checked loop reads the same entries (`e >> 24` the count, `e >> 16` the base), and the checked loop is not this step's to change, so no bit of an entry can move.
- C5 is no gain in any count; it stays (the lengths 17 to 32 take one 32-byte step, no loop).
- C6: the pairing pass without branches and the codes reversed by increment took build from 56.3 M to 50.7 M natively; a second form of the pair (e plus a masked delta) was 52.5 M and was dropped.
- The 32-bit index: V8 15.6 puts `and 0x3f` before every 64-bit shift by a register (a tiny module shows it for `i64.shr_u` of a zero-extended i32, of an i64 and even of an `i64.and` with 63: then two ands), and not before a 32-bit shift; V8 12.4 puts none.
  So in Chrome each literal lookup's chain was `and 0x3f`, `shr r64, cl`, `and 0x7ff`, `lea`, the load.
  Each lookup now takes its index from the low 32 bits of the buffer before it, shifted by the entry's count (11 bits and 11 more fit in 32), and the 64-bit shift of the buffer runs beside the chain: the chain is `shr r32, cl`, `and 0x7ff`, `lea`, the load.
  It costs instructions (537.4 M against 513.1 M in V8 15.6) and gains time (below).
  The same for the distance's lookup gained nothing and was dropped; so were 64-bit entry variables (533.3 M, the mask stays).

Chrome 153, the inflate harness with each build of cdom_image_inflate.c, SIMD, ms of p12.png's stream:

| arm | fresh page each run, 9 rounds (median) | warm, 12 rounds of 6 runs (median), session A | session B |
| --- | --- | --- | --- |
| main | 65.1 | 65.1 | 63.8 |
| all of C1 to C6 but C3 (64-bit index) | 63.1 | 62.9 | 62.4 |
| this branch (32-bit index) | | 60.9 | 60.4 |

- The instruction counts fell 12 to 16% but the time only 3 to 7%: the loop is bound by the chain from one literal lookup to the next, about 8 cycles each on this VM (12.27 M lookups make about 37 ms of p12.png's inflate), not by its instructions.
- A run on a fresh page is in part Liftoff's code: in node, the first run of the inflate is 64 to 90 ms against 57 to 62 for the later ones, and Liftoff alone (`--liftoff-only`) is about 75 ms on main and 66 with these changes.
  So the bench's numbers (each run on a fresh page) have this tier's share too, where the instruction count matters more.
- Natively the changes are no faster: 54.0 ms on main, and three rounds of 7 runs gave main 50.2, 57.9, 56.7 and this branch 55.8, 51.6, 51.0 (noise of 15% on this VM).

### Results

The profile (bench/image/profile.mjs, p12.png at box 900, 12 runs after a warm-up, the SIMD pages linked without wasm-opt; main and this branch interleaved, two runs each; ms, then the share of the sampled time):

| function | main, run 1 | main, run 2 | this branch, run 1 | this branch, run 2 |
| --- | --- | --- | --- | --- |
| cd_inflate_zlib | 57.8 (33.1%) | 59.1 (33.9%) | 53.7 (31.4%) | 55.7 (31.5%) |
| cd_image_unfilter_simd | 39.3 (22.5%) | 38.2 (21.9%) | 39.2 (22.9%) | 38.6 (21.9%) |
| cd_image_scaler_row | 36.2 (20.7%) | 35.7 (20.5%) | 36.9 (21.6%) | 38.8 (22.0%) |
| unfilter_paeth | 16.6 (9.5%) | 16.8 (9.6%) | 17.0 (10.0%) | 17.7 (10.0%) |
| build | 7.0 (4.0%) | 7.5 (4.3%) | 6.9 (4.0%) | 6.7 (3.8%) |
| cd_image_adler32 | 5.7 (3.3%) | 5.1 (2.9%) | 5.1 (3.0%) | 6.0 (3.4%) |
| rows | 4.1 (2.3%) | 3.7 (2.1%) | 4.2 (2.5%) | 4.3 (2.4%) |
| cd_image_scaler_div_simd | 3.6 (2.1%) | 3.1 (1.8%) | 3.3 (1.9%) | 3.9 (2.2%) |
| resize, mean | 171.4 | 170.8 | 167.6 | 173.0 |

(An earlier pair, before the 32-bit index, gave cd_inflate_zlib 56.0 and 59.1 on main against 55.3 and 54.2, build 7.1 and 7.0 against 6.5 and 6.0.)

The inflate bench (bench/image/inflate/run.mjs, 9 rounds, each run on a fresh page; and the same builds warm, 10 rounds of 6 runs):

| build | main, fresh | this branch, fresh | ratio | main, warm | this branch, warm | ratio |
| --- | --- | --- | --- | --- | --- | --- |
| SIMD | 65.5 (60.5 to 77.6) | 61.3 (57.5 to 89.8) | 0.936 | 65.3 | 60.5 | 0.926 |
| scalar | 69.3 (64.1 to 82.3) | 62.3 (60.1 to 90.3) | 0.899 | 66.7 | 62.5 | 0.937 |

The image bench (bench/image/run.mjs, 9 interleaved rounds, box 900, each run on a fresh page, Chrome 153 headless; C-old is main's scalar page (7e7f01f, bench/image/old.sh), C-old-simd main's SIMD page; bench/image/results/2026-11-20-inflate-fast-chrome.tsv), medians (min to max) in ms, the wasm memory after the run in MiB:

| input | C-old | C-old-simd | C-js | C-simd | B1 | B2 | MiB |
| --- | --- | --- | --- | --- | --- | --- | --- |
| p12.jpg | 78.7 | 69.4 | 80.7 | 68.9 | 101.1 | 103.3 | 6.1 |
| p12.png | 195.7 | 161.3 | 209.4 | 158.9 | 165.4 | 178.8 | 17.3 |
| p12a.png | 291.3 | 211.0 | 287.1 | 209.8 | 176.6 | 195.3 | 19.4 |
| p12g.png | 163.3 | 143.8 | 154.8 | 143.8 | 105.2 | 101.8 | 8.5 |

(The C-js p12.png median is over C-old's on this run, with a spread of 181 to 248 ms; the scalar inflate alone is 0.90 to 0.94 of main's in the inflate bench.)

- Q1 refuted: cd_inflate_zlib's self time is 0.936 of main's (58.5 to 54.7 ms, the means of the two runs each), over the 0.85 falsifier: **the step failed** its gate.
- Q2 refuted: build is 0.94 of main's (7.25 to 6.8 ms), over the 0.80 falsifier.
- Q3 refuted: natively cd_inflate_zlib's own instructions rose to 328.0 M (the falsifier was above 290 M), and build's fell to 50.7 M (the falsifier was above 45 M). In V8 the fall was 12 to 16% (above).
  After these measurements the pairing pass lost its count tests (found equivalent by a mutant: an entry read there is never paired yet), which took build to 45.9 M natively, still over the falsifier; the profile and benches above are of the build before that.
- Q4 refuted: C1 alone saved 9.8 M natively (under 20 M; 18.8 M in V8 12.4); with the exits as returns it is most of the 83.7 M V8 12.4 lost from C1, C2, C4 and C7 together, but the falsifier's count was the native one.
- Q5 refuted, by a number the counts do not back: C5 alone was 59.5 ms against main's 65.5 in one fresh-page session (a gain over 2 ms), while its instruction counts are main's within 0.3% and the build with all the changes measured 63.5 in the same session; read it as noise, but the falsifier was hit.
- Q6 refuted: 0.936 of main's in the inflate bench (the falsifier was above 0.90).
- Q7 refuted: p12.png's C-simd is 0.985 of main's C-simd (161.3 to 158.9 ms), over 0.98; the JPEG part holds (p12.jpg C-simd 0.99 of main's, C-js 1.025).
- Q8 open: for the Mac's run. My expectation after this VM: about 0.93 of 59.0, near 55 ms, not 50; the chain is shorter there by the same `and` if V8's arm64 code masks the same way.

So, plainly: the fast loop runs 12 to 16% fewer instructions in V8 and is 6 to 7% faster in Chrome, short of the 15% the step wanted; the loop is now bound by the lookup chain, which the state's layout (an 11-bit first level, 32-bit entries, the checked loop's reading of them) fixes.
What would move it further is outside this step: a larger first level or wider entries (more literals a lookup, so fewer links in the chain) change the state's layout, and with PR #74 merged that is the next place to look.

## The small PNG's buffer (png-fast-2's 8000x3 note)

The inflate's buffer is now `CD_INFLATE_WINDOW + 2 rows + min(PNG_CHUNK, the whole image)`, at least CD_INFLATE_CAP_MIN.
An 8000x3 RGB PNG holds 201,694 bytes at its peak, against 134,274 on main before png-fast, since a small image still has the window and two rows beside the whole image.

### The inflate's rule for cap, checked

- The inflate hands the buffer to the sink only when fewer than FAST_OUT (290: the longest match, 258, and a 16-byte copy's overshoot, 32) bytes are left (the fast loop, the checked loop and stored blocks all test the same), and it stops when the sink leaves less than that.
  So a buffer of the whole image and 290 more is never handed over before the stream's end, if the stream makes exactly the image.
- But a stream may make more than the image: the sink drops the bytes past the last row, and zlib's rules (and this inflate's) take the stream to its end and its adler32.
  Those bytes come out of matches like any other, so the window must stay, and the last rows can be unfiltered and dropped only once they are 32 KB behind the end.
  A sink keeps at most the window and two rows less one byte (the row above the next and the part-made one) after a hand-over, so a buffer of the window, two rows and 290 never stops.
- So the buffer is the larger of the image and the window with two rows, plus 290, when that is under the chunked buffer: `min(window + 2 rows + PNG_CHUNK, max(image, window + 2 rows) + 290)`, never under CD_INFLATE_CAP_MIN.
  With the image and 290 alone, a stream with more than about 290 bytes past its last row would answer CORRUPT where main decodes it; a new oracle case has 40 KB of zeros past the image.
- 290 is `CD_INFLATE_CAP_MIN - CD_INFLATE_WINDOW - 1024`, which cdom_image_inflate.c's static assertion ties to its FAST_OUT, so the header does not change.

### Predictions (written before the change)

Peaks: tests/image/host_alloc.c's count around one cd_image_resize (the most held at once, the file not counted), natively, scalar.

- T1. The 8000x3 RGB PNG (box 300): its buffer goes from 152,773 to 81,060 bytes, so its peak from 201,694 to 129,981 exactly.
- T2. The 16x16 RGBA PNG: its buffer stays CD_INFLATE_CAP_MIN (34,082), so its peak stays 48,731.
- T3. p12.png: the same buffer as before (the chunked one), so the same peak.
- T4. The new RGB cases 291 pixels wide (rows of 874 bytes): 299 rows (261,326 bytes, just under PNG_CHUNK) and 300 rows (262,200, just over) each hold 34,226 and 34,170 bytes less than the formula before (their buffers 261,616 and 262,490); 340 rows (297,160 bytes, the first height where the image and 290 are over the chunked buffer of 296,660) hold the same as before.
- Falsifier of each: any other number.

### Results: the small PNG's buffer

Measured after the predictions above were committed, with the same counting heap (tests/image/host_alloc.c), natively, scalar, main 7e7f01f against this branch:

| file | main | this branch | the buffer, main to this branch |
| --- | --- | --- | --- |
| 16x16 RGBA (box 300) | 48,731 | 48,731 | 34,082 to 34,082 |
| 8000x3 RGB (box 300) | 201,694 | 129,981 | 152,773 to 81,060 |
| 291x299 RGB, 261,326 bytes of image (box 300) | 668,148 | 633,922 | 295,842 to 261,616 |
| 291x300 RGB, 262,200 bytes (box 300) | 670,130 | 635,960 | 296,660 to 262,490 |
| 291x339 RGB (box 300) | 629,342 | 629,258 | 296,660 to 296,576 |
| 291x340 RGB (box 300) | 628,106 | 628,106 | 296,660 to 296,660 |
| 300x20 RGB, 40 KB past the last row (box 300) | 101,211 | 83,481 | 52,590 to 34,860 |
| p12.png (box 900) | 2,806,523 | 2,806,523 | the chunked one |

- T1 confirmed: 129,981 exactly; under main before png-fast (134,274).
- T2 confirmed: 48,731.
- T3 confirmed: p12.png holds the same.
- T4 confirmed: 34,226 and 34,170 bytes less for 299 and 300 rows, the same for 340; 339 rows (not predicted) 84 less, its buffer the image and 290.
- The wasm memory after the bench's runs is unchanged (6.1, 17.3, 19.4 and 8.5 MiB on both).

The oracle's new cases (tests/image/oracle.py): png-chunk291x299-rgb, -300, -339 and -340, and png-past-last-row-rgb (300 x 20 RGB, its stream 40 KB of zeros longer than the image); each has a peak limit of the file and its measured peak rounded up to the KB, and png-small16-rgba's and png-wide8000x3-rgb's limits are tightened the same way (48 and 128 KB beyond the file's whole KB).

### inflate-fast: the tests and their mutants

The runs (this VM, LLVM 23.1.6):

- tests/image/run.sh with the big inputs, both CDOM_IMAGE_SJLJ ways, scalar and SIMD, on the branch after main (with #79) was merged in: 9,241 native checks with 0 failures (76 file cases, the peaks), inflate_diff, the four wasm builds (76 cases each, equal to the byte), simd.sh (paeth runs 1,488 cases, adler32 1,691 and the rest) and prog.sh (1,079 files, 0 differences); 4 min 40 s. Before the merge: the same, 9,226 checks, 73 cases.
- tests/image/inflate_diff at its default (400 cases): 502 streams equal to zlib's output, 929 refused by both, 9,596 cuts CORRUPT, 900 damaged agreeing; at 4000: 4,902, 9,129, 103,838 and 9,000 (seed 20261119).
- tests/image/fuzz.sh 600, fuzz_png and fuzz_inflate (FUZZ_CC clang 23): 58,547 and 523,326 runs, no crash, leak or limit.
- tests/image/simd.sh: in run.sh's runs above.

The mutants, each through tools/mutate.py (`-j 1`, IMG unless named), each caught by the text named:

| mutant | what it breaks | caught by |
| --- | --- | --- |
| png-cap-whole-chunk (new text) | a small PNG's buffer is the window, two rows and the chunk | png-small16-rgba: peak |
| png-cap-chunk-formula | main's formula back | png-chunk291x299-rgb: peak |
| png-cap-image-only | the image and the match room, no window for a longer stream | png-past-last-row-rgb: status CORRUPT |
| png-cap-no-match-room | no match room | png-past-last-row-rgb: status CORRUPT |
| png-cap-chunk-unbounded (IMGBIG) | no chunked cap: a big image's buffer is the whole image | by hand (its run takes 162 s, over mutate.py's 150): png-p12: peak 54,122 KB, limit 22,113 KB (and png-chunk291x340-rgb: peak) |
| png-cap-no-floor, png-rgba-row-on-rgb-read (rerun) | as before | png-small16-rgba: status CORRUPT; png-wide8000x3-rgb: peak |
| inflate-dist-past-start, -copy-overlap, -lit2-too-long, -third-literal, -fixed-286 (new text) | as before | as before |
| inflate-reads-past-piece, -subtable-size, -incomplete-code (rerun) | as before | as before |
| inflate-one-limit-gone | the fast loop without its input bound | AddressSanitizer |
| inflate-bl-unmasked | the bit count saved with the entries' high bits | FAIL inflate_diff |
| inflate-len-extra-mask | one extra bit too many in a length | FAIL inflate_diff |
| inflate-second-index-short | the second literal's index shifted by 3 bits of the count | FAIL inflate_diff |
| inflate-pair-first-class | a pair made with a first entry that is not a literal | FAIL inflate_diff |
| inflate-rev-carry | the reversed code's increment without its carry | mutate.py timed out (counted caught); by hand: png-rgb (and every PNG case) status CORRUPT, then inflate_diff runs out of memory and is killed |

Two mutants first tried were equivalent and were replaced: the fast loop's input bound over 2 instead of 8 (a turn takes at most 2 input bytes for each byte it writes, so over 2 still stays in the piece; the bound over 8 is conservative), and the pairing pass's count test on the second entry (an entry read there is never paired yet; the count tests went from the code).
The last restored check of the first batch (tools/unit.mjs in the real tree) timed out while a run.sh used Chrome beside it; alone it passes (227 passed, 10 s), and the second batch's restored checks passed.

## inflate-fast on the Mac: predictions (written before the run)

Written 2026-11-20 by the dispatcher, before any Mac run of this branch.
The run is bench/image/inflate-mac.sh: main's scalar and SIMD pages (C-old, C-old-simd), this branch's (C-js, C-simd) and the two browser arms, 9 interleaved rounds at box 900, in Chrome and then Safari, on p12.jpg, p12.png, p12a.png (RGBA) and p12g.png (grey).
The VM's results above are the base: the inflate's self time 0.936 of main's, p12.png's C-simd 0.985.
On the Mac the image module's times have run at about 0.7 to 0.85 of the VM's, and V8 on arm64 is a different code generator, so the ratios are taken as the VM's with more spread.

- R1: p12.png, p12a.png and p12g.png, C-simd against C-old-simd: 0.94 to 1.00 in Chrome, and 0.94 to 1.01 in Safari.
  Falsifier for shipping the fast loop: any of the three more than 1% slower than main's in either browser.
- R2: p12.jpg within 2% of main's in both browsers (the JPEG path is untouched).
- R3: the wasm memory after each input equal to main's, within 1 page (the small PNG's buffer only moves small images).
- R4: against the browser in Chrome (C-simd over B1): p12.png 0.80 to 0.90; p12a.png 0.95 to 1.15; p12g.png 1.05 to 1.30.
  That is, the RGBA and grey PNGs are not predicted to beat Chrome's decoder; this is the first Mac measurement of either.
- R5: against Safari (C-simd over the faster of B1 and B2): under 0.75 for all three PNGs.

## inflate-fast on the Mac: results

bench/image/inflate-mac.sh at 2:39am New York time on 2026-11-20 (Apple arm64; Chrome, then Safari 27.0.1), main at 637f664 (the chunked read) as C-old and C-old-simd, 9 interleaved rounds at box 900.
The tables are results/2026-11-20-inflate-mac-{chrome,safari}.{log,tsv}. Medians in ms:

| input | browser | C-old | C-old-simd | C-js | C-simd | B1 | B2 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| p12.jpg | Chrome | 59.3 | 52.3 | 59.4 | 52.2 | 62.4 | 69.2 |
| p12.png | Chrome | 177.9 | 111.2 | 175.8 | 107.7 | 131.0 | 137.9 |
| p12a.png | Chrome | 243.0 | 154.2 | 239.4 | 149.1 | 152.9 | 160.1 |
| p12g.png | Chrome | 153.4 | 136.4 | 152.7 | 134.1 | 77.2 | 84.4 |
| p12.jpg | Safari | 64 | 54 | 63 | 54 | 85 | 76 |
| p12.png | Safari | 228 | 114 | 228 | 114 | 206 | 196 |
| p12a.png | Safari | 282 | 158 | 281 | 159 | 235 | 201 |
| p12g.png | Safari | 158 | 128 | 157 | 128 | 124 | 102 |

The wasm memory after each input is main's (6.1, 17.6, 20.2 and 8.9 MiB).

| id | prediction | measured | verdict |
| --- | --- | --- | --- |
| R1 | the three PNGs, C-simd over C-old-simd: 0.94 to 1.00 in Chrome, 0.94 to 1.01 in Safari; falsifier more than 1% slower | Chrome 0.969, 0.967, 0.983; Safari 1.000, 1.006, 1.000 | confirmed: the fast loop ships (3% in Chrome, nothing either way in Safari) |
| R2 | p12.jpg within 2% of main's | Chrome 0.998, Safari 1.000 | confirmed |
| R3 | wasm memory as main's | equal | confirmed |
| R4 | over B1 in Chrome: p12.png 0.80 to 0.90, p12a.png 0.95 to 1.15, p12g.png 1.05 to 1.30 | 0.82, 0.975, 1.74 | p12.png and p12a.png confirmed; p12g.png refuted, far over |
| R5 | over the faster browser arm in Safari: under 0.75 for all three | 0.58, 0.79, 1.25 | refuted for p12a.png and p12g.png |

What it shows beyond this branch: cdom beats both browsers on RGB PNG and on JPEG, beats Chrome narrowly and Safari by 21% on RGBA, and loses on grey: Chrome's decoder takes 77 ms where cdom takes 134, and Safari's 102 where cdom takes 128.
The grey PNG is the next PNG work, profiled before anything changes (issue to follow).

## Grey PNG

The goal (Alex): cdom's grey PNG resize at least as fast as the browser's.
On Alex's Mac (Chrome 157, the SIMD page, box 900) p12g.png (4032 x 3024, 8-bit grey, colour type 0) takes 134.1 ms against B1's 77.2: 1.74 times, the one format where cdom loses.
The Mac's native profile (bench/image/native.c, scalar) has cd_image_unfilter_scalar at 42% of the self time, cd_image_scaler_across_scalar 32%, the inflate 13% and rows (grey to RGBA) 10%.
Chrome's profile of the SIMD page had one function at 63% (91 ms).
p12g.png's rows on this machine's inputs: 3,015 Paeth, 8 Sub, 1 Up; the Paeth rows come in 9 runs of 50 to 703 rows.

The two causes, as the code reads:

- src/cdom_image_loops.c: the wavefront (`wave_v`) is for 3 and 4 bytes a pixel; `cd_image_unfilter_run_simd` gives a 1-byte row to `cd_image_unfilter_simd`, which gives every filter but Up to `cd_image_unfilter_scalar`, whose Paeth has one chain from each byte to the next.
- src/cdom_image_png.c: every grey row goes through `to_rgba` (4 bytes a pixel) and the scaler's across loop runs on 4 channels, where a grey image has one; RGB has its own read (`cd_image_scaler_across_rgb`), grey has none.

### The machine

A Linux x86-64 cloud VM, not the Mac, so the numbers are ratios against main measured in the same session, arms interleaved; the dispatcher re-measures on the Mac.

| thing | value |
| --- | --- |
| CPU | "Intel(R) Xeon(R) Processor @ 2.10GHz", 4 cores |
| OS | Ubuntu 24.04, kernel 6.18.44 (a cloud VM) |
| Chrome | Google Chrome for Testing 153.0.8010.12, headless, CHROME_ARGS=--no-sandbox, through tools/cdp.mjs |
| clang | clang 23.1.6 (the LLVM-23.1.6-Linux-X64 release tarball, at /usr/lib/llvm-23; apt.llvm.org not tried, the earlier jobs found it refused), wasm-ld from it |
| binaryen | version 130, the release tarball, its sha256 as in ci.yml (on PATH for the bench pages, as on the Mac; not for the profile pages, which keep their name section) |
| node | v22 (to drive Chrome) |
| inputs | bench/image/inputs.py with /usr/bin/python3 (3.13, Pillow 12.3.0, numpy 2.5.3): p12g.png 6,017,051 bytes |

### The changes to try

- G1, a wavefront for 1 byte a pixel: 16 rows in one vector of bytes (row j in byte j), each row LAG (2) bytes behind the one above; row 0's b loaded from the row above the run, row j's b the result row j - 1 made LAG steps before, moved up a lane.
  The sink's run grows from 4 to 16 rows for 1 byte a pixel (the same buffer: p12g.png's hand-overs hold about 65 rows past the window).
  The same loop for 2 bytes a pixel (grey+alpha, 8-bit) with 8 rows, if it costs little.
- G2, a grey read through the scaler: one channel across, down and divided (`dw` sums, not `3 dw`), expanded to RGBA only as the output row is stored; 8-bit grey with no colour key read as it lies in the inflate's buffer (no RGBA row), and the other greys (1, 2 and 4 bits, a colour key, grey+alpha) made into a row of 1 byte a pixel by their own unpacking, then read the same way.
  Byte-exact by construction: for a grey pixel R = G = B, so the three sums of today's scaler are equal integers and give equal bytes.

16-bit PNGs are refused (UNSUPPORTED) by this decoder, so "16-bit greys" is out of this step; their refusal is kept and tested.

### Predictions (written before any measurement)

The whole resize: bench/image/run.mjs, box 900, 9 interleaved rounds, each run on a fresh page, medians; main's pages (C-old scalar, C-old-simd SIMD) built from origin/main as bench/image/inflate-mac.sh builds them.
The self times: bench/image/profile.mjs, 12 runs after a warm-up, the SIMD pages linked without wasm-opt.
The base I predict from (not measured yet): main's p12g.png C-simd on this VM about 144 ms (inflate-fast's table above: 143.8), of which the scalar 1-byte unfilter about 70 ms, cd_image_scaler_row about 35 ms (it is p12.png's, the same pixel count), the inflate about 22 ms (6.0 MB of file against p12.png's 15.2), rows (to_rgba) about 8 ms.

| id | prediction | falsifier |
| --- | --- | --- |
| H1 | G1 alone: p12g.png C-simd at 0.60 of main's (0.50 to 0.70) | above 0.75 |
| H2 | G1: the 1-byte wavefront's self time (in cd_image_unfilter_run_simd) 13 ms (8 to 20), against about 70 for the scalar Paeth it replaces in cd_image_unfilter_simd | above 25 ms |
| H3 | G2 alone: p12g.png C-simd at 0.80 of main's (0.72 to 0.88) | above 0.92 |
| H4 | G2: cd_image_scaler_row with the grey read 11 ms (6 to 16) against about 35; rows' self time under 3 ms (to_rgba gone for 8-bit grey) | the scaler above 20 ms |
| H5 | G1 and G2: p12g.png C-simd at 0.40 of main's (0.30 to 0.50) | above 0.70 (the brief's falsifier for the whole task) |
| H6 | G1 and G2: p12g.png C-simd under B1 on this VM, at 0.55 of B1 (0.42 to 0.70) | at or above B1 |
| H7 | the scalar build (C-js, which takes G2 only): p12g.png at 0.82 of C-old's (0.72 to 0.92) | above 0.95 |
| H8 | p12.jpg, p12.png and p12a.png, C-simd and C-js, within 2% of main's (per-round median ratio) | any of them more than 2% off |
| H9 | the wasm memory after p12g.png: equal to main's or lower (the RGBA row of 16,128 bytes goes), at most 1 page (64 KiB) lower | higher than main's |
| H10 | the image module grows by 0.4 to 1.5 KB brotli (q 11) in the SIMD page and 0.1 to 0.6 KB in the scalar one | outside those ranges |
| H11 | a run of 2 or 3 Paeth rows through the 16-row loop is slower than the scalar rows (16 lanes of work for 2), so the loop is used from some run length up; a microbench decides the length, predicted 3 (2 to 5) | the 16-row loop faster than the scalar Paeth at runs of 2 by more than 1.2 times |

### A third change, predicted before it was made

After G1 and G2 (measured below), the profile's order is the inflate (run_on, 21.8 ms; not this step's), cd_image_scaler_row (14.2 ms, the grey across loop inlined into it) and the wavefront (13.3 ms).
The grey across loop's inner loop runs 3 or 4 times an output pixel on p12g.png (4032 to 900), a branch whose count follows the output pixel's fraction.
G3: the inner loop runs a count fixed for the row (sw / dw - 1, every output pixel spans at least that many whole pixels), then one more pixel taken by a mask, not a branch; the row's last output pixel as before (so nothing past the row is read).

| id | prediction | falsifier |
| --- | --- | --- |
| H12 | G3: cd_image_scaler_row's self time on p12g.png at 0.65 of G2's (0.50 to 0.80) | above 0.90 |
| H13 | G3: p12g.png C-simd at 0.90 of G1 and G2's (0.82 to 0.96) | above 0.98 |

### What was built

- G1, `wave_n` in src/cdom_image_loops.c: one row a lane, 16 rows of 1 byte or 8 of 2, each 2 pixels behind the one above (LAG, as `wave_v`); a lane load and a lane store a row a step, b from the row above in lane 0 and from the upper lane's result of 2 steps before in the others.
  `cd_image_unfilter_run_simd` takes runs of up to 16 Paeth rows of 1 byte a pixel (grey of any depth, palette) and 8 of 2 (grey+alpha), and runs the wave from 3 rows; the sink (`rows()`) is unchanged, since it already hands over every row that is whole and out of the window.
- G2, the grey read: `cd_image_scaler.step` 1 runs `cd_image_scaler_across_grey` (one sum an output pixel), `down` and the divide over `dw` sums, and the store makes R, G and B of the one quotient.
  The PNG path reads 8-bit grey with no colour key as it lies in the inflate's buffer (`READ_AS_IS`, no row allocated), and grey with a key, 1, 2 and 4-bit grey and grey+alpha through `to_grey`, a row of a byte a pixel (`READ_GREY`), made with the same unpacking and compositing `to_rgba` had for them; `to_rgba` keeps RGB with a key, RGBA and palette.
- The unfilter microbench (bench/image/paeth/run.mjs) takes a grey PNG and then times the grey arms: node v22.23.5, p12g.png's own rows, 9 rounds after one warm-up, medians:

| loop | ms | of the scalar rows |
| --- | --- | --- |
| scalar, a row at a time | 72.6 | 1.000 |
| SIMD build, a row at a time (main: its 1-byte Paeth is the scalar one) | 73.3 | 1.010 |
| wave_n, runs of 16 | 25.1 | 0.347 |
| wave_n, lag 1, runs of 16 | 25.0 | 0.345 |
| wave_n, runs of 8 | 32.7 | 0.450 |
| wave_n, runs of 4 | 48.0 | 0.662 |
| wave_n, runs of 3 | 59.8 | 0.825 |
| wave_n, runs of 2 | 82.1 | 1.131 |

So the wave starts at 3 rows for 1 and 2 bytes a pixel (at 2 its 16 lanes of work for 2 rows lose to the scalar rows).
On this VM the loop is bound by its lane loads and stores (32 a step), not its chain: lag 1 is as fast as lag 2; the lag stays 2, as for `wave_v`, since the Mac is where the chain binds.

### Results

The profile (bench/image/profile.mjs, p12g.png at box 900, 12 runs after a warm-up, the SIMD pages linked without wasm-opt: main 59380c1 as build-prof0, this branch as build-prof-simd), ms per run, three runs of each (the first before the others, the second and third interleaved):

| function, main | run 1 | run 2 | run 3 | | function, this branch | run 1 | run 2 | run 3 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| cd_image_unfilter_scalar (the 1-byte Paeth) | 66.6 | 65.4 | 64.4 | | run_on (the inflate) | 21.8 | 24.9 | 23.8 |
| cd_image_scaler_row (across on RGBA inlined) | 45.9 | 45.6 | 43.2 | | cd_image_scaler_row (across_grey inlined) | 14.2 | 17.6 | 15.5 |
| rows (to_rgba inlined) | 24.7 | 23.0 | 23.2 | | cd_image_unfilter_run_simd (wave_n) | 13.3 | 17.2 | 15.8 |
| run_on (the inflate) | 23.4 | 23.1 | 22.8 | | build | 2.9 | 3.6 | 3.1 |
| build | 3.2 | 3.0 | 3.0 | | cd_image_adler32 | 2.0 | 2.3 | 1.6 |
| cd_image_adler32 | 1.8 | 2.0 | 1.7 | | cd_image_scaler_div_simd | 1.4 | 1.7 | 1.1 |
| resize, mean | 166.0 | 162.6 | 158.7 | | resize, mean | 57.0 | 68.5 | 62.7 |

(An earlier profile of main, before any change, had 67.7, 49.8, 26.5 and 24.4 ms for the first four, 174.8 resize; the profiles' text is bench/image/results/profile-grey/, and the G3 session's runs are 2026-11-20-png-grey-g3-chrome.tsv, the second 31-round session's -others2.)
rows is gone from the list (under 0.1 ms): the row loop now only hands rows to the scaler.
The inflate is the largest part left, then the scaler and the wave, near equal.

The bench (bench/image/run.mjs, 15 interleaved rounds, box 900, each run on a fresh page, Chrome 153 headless, the 1-minute load 1.0 to 1.1; C-old and C-old-simd are main at 59380c1, built in a scratch worktree as bench/image/inflate-mac.sh builds them; bench/image/results/2026-11-20-png-grey-chrome.tsv), medians (min to max) in ms, the wasm memory after the run:

| input | C-old | C-old-simd | C-js | C-simd | B1 | B2 | wasm bytes, main and this branch |
| --- | --- | --- | --- | --- | --- | --- | --- |
| p12.jpg | 84.8 | 71.8 | 89.0 | 77.1 | 106.7 | 106.6 | 6,422,528 |
| p12.png | 205.6 | 125.5 | 207.5 | 123.7 | 168.4 | 179.6 | 18,087,936 (scalar), 18,153,472 (SIMD) |
| p12a.png | 292.9 | 178.7 | 294.4 | 178.0 | 180.6 | 194.9 | 20,316,160 |
| p12g.png | 169.7 (131.5 to 218.6) | 147.7 (142.3 to 191.8) | 102.0 (81.6 to 117.5) | 52.1 (39.9 to 90.3) | 102.9 (98.5 to 122.3) | 107.3 (91.7 to 135.9) | 8,912,896 |

Per round, p12g.png: C-simd over C-old-simd 0.358 (0.265 to 0.629), C-js over C-old 0.578 (0.475 to 0.751), C-simd over B1 0.501 (0.371 to 0.773).
The changes one at a time (15 rounds, p12g.png only, the pages built from this branch with the other change left out; bench/image/results/2026-11-20-png-grey-split-chrome.tsv), per round over C-old-simd: G1 alone 0.803 (0.537 to 1.249), G2 alone 0.685 (0.480 to 1.015), both 0.358 (0.182 to 0.486); C-simd 55.5 ms against B1's 112.4.
The inputs this branch does not change, 31 interleaved rounds (bench/image/results/2026-11-20-png-grey-others-chrome.tsv), per round: C-simd over C-old-simd p12.jpg 0.971 (0.713 to 1.525), p12.png 1.001 (0.662 to 1.614), p12a.png 0.985 (0.663 to 1.400); C-js over C-old 0.977, 1.005 and 1.027.
After the scalar emit's grey loop was split from the RGB one (below), a second 31-round session gave C-js over C-old 1.018, 0.988 and 0.994, and the build before the split, in the same session, 0.999, 0.992 and 1.011 (it had been 1.027 on p12a.png in the first): the same code moves by 2% to 3% between sessions on this VM.

The module's sizes (the bench page's app.wasm, which is the image module and a small app; brotli -q 11):

| build | main | this branch | change |
| --- | --- | --- | --- |
| scalar, wasm | 95,152 | 95,642 | +490 |
| scalar, brotli | 34,361 | 34,554 | +193 |
| SIMD, wasm | 104,071 | 107,728 | +3,657 |
| SIMD, brotli | 37,063 | 38,106 | +1,043 |

| id | prediction | measured | verdict |
| --- | --- | --- | --- |
| H1 | G1 alone 0.60 of main (0.50 to 0.70), falsifier above 0.75 | 0.803 | refuted: the wave alone gains less in the bench than its 50 ms in the warm profile, and G2 gains more with it than alone (0.358 together) |
| H2 | the wave's self time 13 ms (8 to 20), falsifier above 25 | 13.3, 17.2, 15.8 (against 66.6, 65.4, 64.4) | confirmed |
| H3 | G2 alone 0.80 (0.72 to 0.88), falsifier above 0.92 | 0.685 | outside its range on the fast side; not falsified |
| H4 | cd_image_scaler_row 11 ms (6 to 16), falsifier above 20; rows under 3 ms | 14.2, 17.6, 15.5 (against 45.9, 45.6, 43.2); rows under 0.1 | confirmed in two runs of three; the third over the range, under the falsifier |
| H5 | G1 and G2 0.40 of main (0.30 to 0.50), falsifier above 0.70 (the task's) | 0.358 | confirmed: the task's falsifier is not hit |
| H6 | under B1, at 0.55 of B1 (0.42 to 0.70) | 0.501 (and 0.504 in the split session) | confirmed |
| H7 | C-js 0.82 of C-old (0.72 to 0.92), falsifier above 0.95 | 0.578 | outside its range on the fast side (the scalar build loses to_rgba and three of four channels too); not falsified |
| H8 | p12.jpg, p12.png, p12a.png within 2% of main | per round 0.971 to 1.027 in one session, 0.988 to 1.018 in the next | refuted by the letter (p12a.png's C-js 1.027 in the first session); the same build was 1.011 in the second, and this VM's per-round spread (0.66 to 1.6) is wider than the line, so the Mac decides |
| H9 | the wasm memory after p12g.png equal to main's or up to a page lower | equal to the byte (8,912,896) | confirmed (the RGBA row, 16,128 bytes, is no longer allocated; the heap's pages do not fall) |
| H10 | +0.4 to +1.5 KB brotli (SIMD), +0.1 to +0.6 KB (scalar) | +1,043 and +193 bytes | confirmed |
| H11 | the wave from some run length up, predicted 3 (2 to 5); falsifier: faster than the scalar rows at runs of 2 by more than 1.2 times | runs of 2 at 1.131 of the scalar rows (slower), runs of 3 at 0.825 | confirmed on the length (3); the text's "2 or 3 slower" was wrong for 3 |
| H12 | G3: cd_image_scaler_row at 0.65 of G2's (0.50 to 0.80), falsifier above 0.90 | 17.0 ms (12.7 in across_grey, no longer inlined, and 4.3), against 14.2: 1.20 | refuted |
| H13 | G3: p12g.png C-simd at 0.90 of G1 and G2's (0.82 to 0.96), falsifier above 0.98 | 1.048 (0.971 to 1.521), 15 rounds | refuted: G3 is not kept |

So on this VM the grey resize is 0.36 of main's in the SIMD build and 0.58 in the scalar one, and half of createImageBitmap's time; the Mac re-measures.
On the Mac's numbers (main 134.1 ms against B1's 77.2) the same ratio would be about 48 ms, but the Mac's chain-bound cores gain less from wider loops than this VM's port-bound ones in some loops and more in others, so that is not a prediction.

### Tests

- tests/image/simd.c: "paeth runs" now covers 1 to 4 bytes a pixel, with Paeth runs of 1 to 17 rows for 1 and 2 bytes (the sink's 16 and 8, and one past) mixed with None, Sub, Up and Avg rows, every length in some plan, widths 1 to 9 pixels and 4031 and 4032 (narrow ones run only the parts where rows start and end), handed over a random number of rows at a time: 2,976 cases.
  "scaler across grey": `cd_image_scaler_across_grey` against `cd_image_scaler_across_scalar` on the same pixels as RGBA, sw 1 to 70 at every dw edge and 400 wide rows, a canary word past the sums and the grey row at the top of memory (a read past it traps): 2,500 cases.
- tests/image/oracle.py: png-paeth-runs-grey, -grey-key, -grey-alpha, -grey1, -grey2 and -grey4 (403 x 300, Paeth runs of every length 1 to 17, each followed by a row of None, Sub, Up or Avg in turn, the first row Paeth), png-grey-w1, -w2, -w3, -w5, -w17 and -w33 (60 rows, a box of 4), png-paeth-runs-far-grey (rows that repeat every 80, so the stream copies each from near the window's far end), png-grey16 and png-grey-alpha16 (UNSUPPORTED); pixels from a formula, Pillow decodes each file first; main decodes every new case to the same bytes (its test_image on this oracle: 9,275 checks, 0 failures), and holds 1 KB more on each than the limits, which are this branch's peaks (main fails five of the seven).
