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
