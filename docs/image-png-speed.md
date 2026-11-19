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

The libraries, natively (bench/image/inflate/native.sh, native_round.sh; x86-64, clang 18 -O2, Rust 1.97 release at opt-level 2; 7 rounds of 3 runs each, arms interleaved; median, min to max; p12.png's stream, 15,177,529 bytes in 232 IDAT chunks):

| id | inflate | measured ms | predicted | verdict |
| --- | --- | --- | --- | --- |
| N1 | tinfl (miniz 3.1.2), as cdom called it | 88.2 (83.7 to 117.8) | 110 (80 to 150) | confirmed |
| N2 | zlib 1.3.1 (64 KB out buffer) | 98.9 (93.8 to 105.2) | 90 (65 to 130) | confirmed |
| N3 | zlib-ng 2.2.4, its x86 SIMD on (AVX2 chunkset and adler32) | 69.2 (67.9 to 74.8) | 60 (40 to 85) | confirmed |
| N4 | libdeflate 1.24 (one call, whole-output buffer) | 50.8 (47.9 to 63.7) | 45 (30 to 65) | confirmed |
| N5 | fdeflate 0.3.7 (streaming as the png crate does: 32 KB kept, the buffer compacted) | 39.0 (38.8 to 49.3) | 45 (30 to 70) | confirmed |
| | cdom's inflate (this branch, final) | 45.2 (43.6 to 51.0) | (P2) | |
| | cdom's inflate, fed the 232 IDAT payloads as pieces | 45.2 (43.7 to 54.9) | | |
| | png 0.18.0, the whole decode (inflate and unfilter, no scaling) | 97.3 (95.3 to 116.4) | not predicted | |

In wasm (Chrome 153, bench/image/inflate/run.mjs, 9 interleaved rounds, each run on a fresh page; the inflate with its rows handed on, nothing else):

| id | inflate | measured ms | predicted | verdict |
| --- | --- | --- | --- | --- |
| W1 | tinfl, as cdom called it | 117.7 (112.2 to 136.3) | 170 (130 to 220) | refuted (faster) |

- F1, the wasm factor W1/N1: 117.7 / 88.2 = 1.33; predicted 1.5 (1.2 to 2.0): confirmed.
- F2, the library factor N1/min(N4, N5): 88.2 / 39.0 = 2.26; predicted 2.4 (1.6 to 3.5): confirmed.
- G1 (F2 > F1): 2.26 against 1.33, confirmed: of the old C arm's inflate time, the library (tinfl against fdeflate) costs more than wasm does.

So the gap, on p12.png's inflate, split: the browser's fdeflate natively takes 39 ms; tinfl natively takes 2.26 times that (88 ms), and wasm adds 1.33 times again (118 ms).
Inflate is not the whole gap: the png crate's whole decode is 97 ms natively, so Chrome's B1 (about 155 ms here) spends about 60 ms more on its resize, its colour and its copies.

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
C-old is main (2d20ccd: tinfl, the RGBA row; bench/image/old.sh), C-new this branch scalar, C-new-simd this branch with the inflate (only) at -msimd128 (`make -C bench/image SIMD=1`).
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
