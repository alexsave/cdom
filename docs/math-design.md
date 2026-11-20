# A maths library for cdom: the bench

cdom's libc (src/libc/) is stdio.h, stdlib.h and string.h: there is no math.h and no libm.
WebAssembly has sqrt, floor, ceil, trunc, nearest, abs, min, max and copysign as instructions, and nothing else.
This doc measures several ways cdom could get sin, cos, tan, asin, acos, atan, atan2, exp, exp2, expm1, log, log2, log10, log1p, pow, sinh, cosh, tanh, cbrt, hypot and fmod (sqrt as the control), and recommends one.
It is a bench and a recommendation, not a change to cdom: everything is under bench/math/, and src/libc/ is untouched.

## The arms

| arm | what | licence |
|---|---|---|
| musl | musl 1.2.6's src/math, the files these functions need | MIT |
| msun | FreeBSD msun at release/15.1.0 (fdlibm's descendant) | BSD-style |
| core | CORE-MATH (correctly rounded), as vendored by the core-math-sys 1.4.0 crate | MIT |
| ours | short functions written here: range reduction and minimax polynomials (bench/math/ours/) | cdom's |
| js1 | the browser's Math.* through one import call per value | - |
| js1w | js1 with each import wrapped in an arrow function, as a shim that adapts an argument would be | - |
| jsn | the browser's Math.* through one import call per 4096 values (a loop over a Float64Array view of C memory) | - |
| glibc | glibc 2.39's libm, called natively (reference only: LGPL, measured, never shipped) | LGPL |

Every arm but glibc is built for wasm32 with cdom's flags (cdom.mk's CDOM_CFLAGS, at its default CDOM_OPT -Oz, through clang's wasm-opt as on the Mac) and also natively, with the same clang and the same flags but the target, so each wasm number has a native twin.
The vendored sources, their versions, URLs, licences and hashes are in bench/math/third_party/README.md.

## Predictions

Written before any measurement, and not edited after it.
One disclosure: while making the arms link, a smoke run in node printed ns per call for six entries (sin, sinf, exp, pow, fmod, fmodf) of musl, msun and core, unwarmed and once.
Those prints are not results and are not used below; the predictions on CORE-MATH's speed (P7) were written having seen them.

Numbers are for this x86-64 VM in headless Chrome, typical domain, double, unless a prediction says otherwise.
A ratio is the median of the arm over the median of the other arm.

P1. The per-call import (js1) is slower than musl in wasm for every function but fmod, by 1.3x to 3x (the call into JS and back, 8 to 25 ns, on top of V8's own function).
fmod is the exception: musl's fmod is a bit-by-bit loop, and JS's % is a C call inside V8, so js1 fmod is faster than musl fmod.
Falsifier: any function other than fmod where js1's median is at or below musl's; or fmod where it is above; or a ratio outside 1.3x to 3x for more than 4 of the other 20.

P2. The batched import (jsn) is faster than musl in wasm (ratio under 1) for at least 12 of the 21 functions: V8's Math.* is fdlibm compiled natively, called from optimized JS with no crossing per value.
Falsifier: fewer than 12 functions with jsn under musl.

P3. The per-call crossing (js1 minus jsn, per value) is 8 to 25 ns in Chrome, about the same for every function; js1w adds 5 to 30 ns on top of js1.
Falsifier: a median crossing outside 8 to 25 ns, or js1w not slower than js1 for most functions.

P4. The wasm tax: musl in Chrome over musl's native twin is 1.2x to 2.0x, median over the 21 functions; no function is under 1.0x or over 3.0x.
Falsifier: a median outside 1.2x to 2.0x, or any function outside 1.0x to 3.0x.

P5. Our own functions against musl in wasm: within 0.7x to 1.4x of musl's median for at least 15 of the 21; pow is the slowest relative to musl, at 1.5x to 3x (no fma, so its double-double steps split by hand).
Falsifier: fewer than 15 inside 0.7x to 1.4x, or pow outside 1.5x to 3x.

P6. Our own functions' accuracy (max ulp over 1,000,000 inputs per domain, against MPFR): under 1 ulp for at least 15 of the 21 double functions in every domain they have; the misses are among tan, atan2, pow, sinh, cosh and tanh, and none is over 2 ulp.
fmod is exact (0 ulp).
Falsifier: fewer than 15 under 1 ulp, a miss outside that list of six, or any over 2 ulp.

P7. CORE-MATH in wasm is slower than musl in wasm by 3x or more (median over its 20 double functions): it leans on fma, which wasm lacks, so every fma is a software call (musl's fma.c).
Its float functions are within 0.7x to 2x of musl's floats (they compute in double and need few fma).
Falsifier: a double median under 3x, or a float median outside 0.7x to 2x.

P8. CORE-MATH's max error is at most 0.5 ulp in every function and domain (it is correctly rounded), and it matches mpmath on every sampled input.
Falsifier: any max over 0.5 ulp (beyond the 1e-9 of the measure itself), or any mpmath mismatch.

P9. musl's max error is under 1 ulp for at least 17 of the 21 double functions, and under 2.5 ulp for all; msun's the same.
Falsifier: fewer than 17 under 1 ulp, or any over 2.5 ulp, for either.

P10. Every arm's wasm build gives bit-identical results to its native twin on 100,000 inputs per function and domain (NaNs counted as one value).
Falsifier: any mismatch.

P11. The size musl's sin adds to a module, after wasm-opt -O3, is 4 to 8 KB of wasm and 2 to 4 KB brotli (sin, __sin, __cos, __rem_pio2 and __rem_pio2_large, whose table of 2/pi is most of it).
Falsifier: outside either range.

P12. sqrt, the control, ties: musl, msun, core and ours are within 10% of each other in Chrome, double and float.
Falsifier: any of the four more than 10% from the median of the four.

P13. glibc natively is faster than musl's native twin for sin, cos, exp, log and pow, by 1.2x to 2.5x (glibc picks its FMA builds on this CPU, which has FMA).
Falsifier: any of the five outside 1.2x to 2.5x.

P14. node and Chrome are the same V8: each wasm arm's median in node is within 15% of Chrome's for at least 90% of the cells.
Falsifier: fewer than 90% of cells within 15%.
