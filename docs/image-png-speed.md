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
