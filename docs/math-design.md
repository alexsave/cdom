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

## How the bench works

Everything is in bench/math/; bench/math/run.sh runs it all on one machine and bench/math/mac.sh on Alex's Mac.

### Building the arms

- Each arm is one wasm module: bench.c (the timed loops and the entry points) and special.c (the special-value test), linked with the arm's libm and compat/compat.c.
  The browser's arms are bench.c alone, every function an import.
- The flags are cdom.mk's (CDOM_CFLAGS, CDOM_LDFLAGS, CDOM_OPT -Oz), with clang running wasm-opt after the link as it does for a cdom app when binaryen is on the PATH.
  Two flags are added to every arm: -ffp-contract=off (no fused multiply-add the source did not write; wasm has none) and -fno-math-errno (wasm's default anyway).
- compat/ stands in for the libc the vendored sources expect: a math.h, fenv.h (one rounding mode, no flags: what wasm has), errno.h, and the few BSD and musl internal headers.
  compat.c gives each arm errno, the functions that are one wasm instruction (sqrt, fabs, floor, ceil, trunc, rint, copysign, fmin, fmax) and round, all weak.
- CORE-MATH needs fma, which wasm lacks, so its arm links musl's fma.c and fmaf.c (exact, in software); it uses __int128, so in wasm it also links three 128-bit helpers (compat/int128.c, since cdom links no compiler-rt).
- The native twin of an arm is the same sources and flags built for the host: -nostdinc with compat/ and clang's own headers, __FP_FAST_FMA undefined (so musl keeps the paths it takes in wasm on an arm64 Mac too), and every global symbol renamed to <arm>_<name> (a header made from llvm-nm over the arm's wasm objects), so one program, build/native/bench, links every arm and glibc's libm side by side.

### Inputs

- One generator: splitmix64, seeded from MATH_SEED (gen.h); the stream of each (purpose, entry, domain) starts at its own offset of the seed, so it is the same whatever order the bench asks for it in.
- 4096 timed inputs per function and domain; 1,000,000 accuracy inputs; 100,000 wasm-equals-native inputs.
- The domains (gen.h): sin, cos, tan have small (|x| < pi/4), typical (|x| < 100) and large (|x| < 1e6); every other function has typical only:
  asin and acos uniform on [-1, 1]; atan and atan2 uniform on [-100, 100]; exp on [-700, 700] (float [-87, 88]); exp2 on [-1000, 1000] (float [-125, 127]); expm1 on [-10, 10]; log, log2, log10 and sqrt log-uniform over 2^-1000 to 2^1000 (float 2^-120 to 2^120); log1p on [-0.9999, 10]; pow with x log-uniform over 2^-10 to 2^10 and y on [-50, 50] (float 2^-4 to 2^4, [-20, 20]); sinh, cosh, tanh on [-20, 20]; cbrt log-uniform with a random sign; hypot both on [-1000, 1000]; fmod x on [-1e6, 1e6] and y on [0.5, 100].
  A float entry draws the same and rounds to float.

### Timing

- ns per call: one run is reps passes over the 4096 inputs, each call's result added to a sum; the host times the run (performance.now() in a browser and node, clock_gettime natively) and divides by reps times 4096.
- Warm-up: every cell (arm, function, domain) runs with doubling reps until a run takes a quarter of MS, which sets its reps so a run lasts about MS (10 ms).
- Then 9 rounds; each round runs every (function, domain) once per arm, the arms in an order that rotates by one each round.
  Each arm's sum is printed at the end of the run (the # sum lines), so no call can be dropped as dead code.
- The table cells are the median of the 9 rounds and the spread, half of max minus min as a percentage of the median.
- Chrome runs headless through tools/cdp.mjs, the page served cross-origin isolated (performance.now() to 5 us rather than 100 us); Safari through tools/safari.mjs on the same page; node runs the same glue.js in process.
- The browser's arms: js1 passes Math.sin (and the rest) as the import itself, which V8 calls without a JS frame for the Math functions it knows; exp2 is 2 ** x and fmod is x % y, which have no Math function, and each float import is an arrow function with Math.fround, so those are a JS call per value.
  js1w wraps every import in an arrow function, as a shim that adapts its arguments would.
  jsn makes one import call per pass, whose loop (a function of its own per import, so V8's feedback stays monomorphic) fills a Float64Array view of C memory with Math.sin of another view; the C loop then sums that array.

### Accuracy

- build/native/accuracy: every native arm (each bit for bit its wasm module, which the tests check) against MPFR at 160 bits, on 1,000,000 inputs per function and domain.
  The error of a result is |got - exact| over the ulp of the exact value's binade (no smaller than the least normal's ulp); a wrong zero, infinity or NaN counts as 1e9.
- V8's Math.* (the arm v8): tools/v8dump.mjs evaluates the js1 module in node on the same inputs and writes the results, which the sweep reads; node's V8 is Chrome's Math.* code (both are V8's port of fdlibm), though node 22's V8 is older than Chrome's.
- tools/oracle.py checks the reference itself: the first 200 inputs of every function and domain against mpmath at 300 bits, MPFR's correctly rounded result and CORE-MATH's both.

### Size

- build/size/<arm>-{empty,sin,all}.wasm: a module exporting one function that calls nothing, one that calls sin, and one that exports every function the arm has (double and float), each linked with the arm and run through wasm-opt -O3; the bytes an arm adds are a module's size minus the empty one's, raw and brotli at quality 11 (sizes.sh).

### Tests

- node bench/math/test.mjs runs both.
- The special-value test (special.c), 415 cases: each arm's results for Annex F's special inputs (signed zeros, infinities, NaN, the overflow and underflow edges, pow's whole table), the least subnormals, and a few exact values (cbrt(-8) = -2, hypot(3, 4) = 5, log2(2^k) = k, exp2(-1074) is the least subnormal).
  A rounded constant (pi, pi/2, pi/4, 3pi/4) may be either neighbour.
  It runs in every arm's wasm module (the browser's through its import, with node's Math.*), in every native twin and in glibc.
  Known deviations are listed per arm and must still deviate: the browser's Math.pow gives NaN for pow(1, NaN), pow(1, inf) and pow(-1, inf), which C gives as 1 (ECMAScript's exponentiation says NaN on purpose); glibc's cbrt is not exact on cbrt(27) and cbrt(2^-1074).
- The wasm-equals-native check: each arm's wasm module and its native twin hash their results on 100,000 inputs per function and domain (NaNs as one value); every hash must match.
- tools/mutate.py breaks each thing a test is for, in a private copy of bench/math, and checks that the test fails with its own message.
- 13 mutants, all caught (results/2026-11-20-final-mutants.txt; the tests at the final commit pass, results/2026-11-20-final-tests.txt): musl's sin(-0) made +0, msun's exp(inf) made 0, msun's atan2(+0, -0) and atan2(-0, -0) given the wrong sign of pi, CORE-MATH's log(-1) made 0, ours' pow(-1, inf) made 0 and fmod(x, inf) made -x, the glue's exp2 made Math.exp, a browser deviation unlisted, a glibc deviation unlisted, a listed deviation that no longer deviates, compat.c's fmax bug put back, and the native twins built with fused multiply-add contraction.
  The mutation check found a bug in the test itself: the 1-ulp allowance for a rounded pi compared the bit patterns as signed integers, so -pi passed for pi (msun's atan2 mutant was missed); it now requires the same sign, and the msun-atan2-sign mutant keeps it so.
  The measured run's tests.txt is from before that fix and before the cases' names were made to print as written; the final tests pass with both.

## Results (the Linux VM)

Every number here is from one run on this Linux x86-64 cloud VM, not Alex's Mac: Intel Xeon Processor @ 2.30GHz, 4 cores (avx2, fma), Linux 6.18; clang 23.1.6 (the LLVM-23.1.6-Linux-X64 release tarball; Ubuntu's clang 18 is too old for cdom.mk) for wasm and native alike; wasm-opt 130; headless Chrome for Testing 153.0.8010.12 (Playwright's build, CHROME_ARGS=--no-sandbox); node 22.23.5 (V8 12.4); glibc 2.39 (Ubuntu); MPFR 4.2 and mpmath 1.3 for the references.
The run's files are bench/math/results/2026-11-20-vm-*: env.txt, tests.txt, sizes.tsv, native.tsv, native-O2.tsv, node.tsv, chrome.tsv, accuracy.tsv, oracle.txt and tables.md (every table, the domains and float ones included).
9 rounds, arms interleaved and rotated each round, each timed run about 10 ms; the load average was under 1 throughout (uptime is in each file).
A cell is the median of the 9 rounds, ±spread (half of max minus min, as a percentage of the median); on this shared VM a spread of 20% to 60% shows up in some cells, so a difference of less than about 1.3x between two cells is not to be read as a ranking.
The Mac re-measures with bench/math/mac.sh.

### THE TABLE: ns per call in Chrome, typical domain, double

musl, msun, core and ours are C in wasm; js1 is Math.* through one import per value, js1w the same wrapped in an arrow function, jsn one import per 4096 values.

| function | musl | msun | core | ours | js1 | js1w | jsn |
|---|---|---|---|---|---|---|---|
| sqrt | 1.6 ±3% | 1.6 ±22% | 1.6 ±3% | 1.6 ±3% | 8.5 ±45% | 10.4 ±25% | 2.1 ±4% |
| sin | 14.5 ±7% | 12.9 ±12% | 321.7 ±24% | 17.9 ±12% | 21.9 ±11% | 23.9 ±2% | 16.9 ±8% |
| cos | 14.8 ±12% | 12.7 ±16% | 311.5 ±25% | 18.5 ±12% | 21.8 ±4% | 24.0 ±3% | 16.8 ±2% |
| tan | 15.8 ±9% | 14.7 ±11% | 430.7 ±9% | 19.9 ±11% | 27.6 ±14% | 29.8 ±5% | 22.4 ±3% |
| asin | 6.6 ±5% | 7.7 ±8% | 54.2 ±5% | 12.7 ±4% | 23.7 ±6% | 26.9 ±4% | 17.4 ±4% |
| acos | 7.4 ±5% | 5.7 ±13% | 52.6 ±5% | 13.1 ±6% | 26.4 ±40% | 28.7 ±5% | 19.3 ±5% |
| atan | 6.8 ±5% | 6.8 ±6% | 33.9 ±8% | 11.1 ±30% | 20.6 ±7% | 22.6 ±9% | 15.2 ±5% |
| atan2 | 20.9 ±14% | 18.8 ±3% | 47.2 ±5% | 24.0 ±25% | 31.1 ±37% | 32.7 ±20% | 21.9 ±7% |
| exp | 5.3 ±22% | 8.1 ±23% | 32.0 ±3% | 9.7 ±5% | 14.1 ±17% | 16.1 ±6% | 8.0 ±2% |
| exp2 | 7.1 ±20% | 3.8 ±38% | 31.0 ±9% | 11.7 ±9% | 25.7 ±16% | 29.7 ±16% | 15.5 ±14% |
| expm1 | 11.1 ±14% | 10.9 ±8% | 31.9 ±14% | 11.4 ±2% | 17.8 ±6% | 20.8 ±7% | 10.2 ±19% |
| log | 4.0 ±60% | 7.1 ±4% | 120.2 ±21% | 8.4 ±57% | 13.7 ±49% | 15.6 ±58% | 6.2 ±51% |
| log2 | 5.1 ±14% | 7.9 ±5% | 28.2 ±4% | 10.4 ±18% | 14.6 ±38% | 16.6 ±16% | 8.2 ±25% |
| log10 | 7.9 ±35% | 8.5 ±5% | 162.5 ±26% | 10.8 ±31% | 14.1 ±29% | 16.1 ±48% | 8.1 ±25% |
| log1p | 7.8 ±7% | 9.2 ±6% | 26.1 ±4% | 9.7 ±8% | 17.2 ±8% | 19.6 ±3% | 10.4 ±7% |
| pow | 12.8 ±3% | 44.1 ±3% | 449.7 ±4% | 50.5 ±2% | 23.3 ±3% | 26.2 ±5% | 12.5 ±4% |
| sinh | 14.1 ±15% | 15.2 ±6% | 50.9 ±10% | 27.3 ±3% | 19.5 ±10% | 21.1 ±15% | 11.1 ±3% |
| cosh | 6.0 ±23% | 24.0 ±4% | 50.4 ±12% | 17.6 ±9% | 16.3 ±4% | 19.1 ±7% | 9.3 ±11% |
| tanh | 16.0 ±30% | 16.9 ±20% | 47.0 ±3% | 16.7 ±8% | 24.4 ±11% | 28.0 ±38% | 13.5 ±29% |
| cbrt | 6.5 ±27% | 6.3 ±6% | 52.1 ±4% | 15.8 ±5% | 21.1 ±3% | 23.6 ±4% | 15.8 ±5% |
| hypot | 9.2 ±4% | 10.8 ±6% | 65.6 ±3% | 10.4 ±3% | 20.2 ±4% | 22.7 ±15% | 18.6 ±3% |
| fmod | 62.0 ±3% | 67.2 ±7% | - | 10.8 ±6% | 21.1 ±30% | 19.9 ±6% | 9.0 ±5% |

### Chrome, typical domain, float

| function | musl | msun | core | ours | js1 | js1w | jsn |
|---|---|---|---|---|---|---|---|
| sqrtf | 0.8 ±6% | 0.8 ±7% | 0.8 ±6% | 0.8 ±14% | 15.0 ±41% | 14.8 ±43% | 2.1 ±15% |
| sinf | 11.3 ±8% | 13.3 ±7% | 10.9 ±9% | 18.0 ±6% | 27.0 ±5% | 26.7 ±4% | 18.7 ±3% |
| cosf | 11.7 ±5% | 12.1 ±5% | 11.1 ±9% | 19.3 ±16% | 28.3 ±21% | 27.2 ±21% | 18.8 ±2% |
| tanf | 11.7 ±35% | 10.6 ±33% | 13.9 ±23% | 21.6 ±32% | 33.1 ±44% | 33.7 ±32% | 25.1 ±28% |
| asinf | 6.1 ±4% | 4.6 ±4% | 7.2 ±2% | 14.2 ±9% | 31.0 ±5% | 29.7 ±14% | 19.2 ±7% |
| acosf | 5.8 ±9% | 5.3 ±13% | 7.5 ±5% | 14.4 ±4% | 32.8 ±5% | 32.2 ±4% | 20.9 ±2% |
| atanf | 5.2 ±26% | 4.8 ±30% | 7.5 ±28% | 12.4 ±5% | 27.3 ±4% | 26.8 ±24% | 17.5 ±24% |
| atan2f | 20.8 ±5% | 17.4 ±4% | 10.1 ±7% | 25.8 ±10% | 37.5 ±4% | 36.4 ±5% | 23.9 ±5% |
| expf | 3.1 ±6% | 11.4 ±2% | 4.4 ±6% | 10.9 ±4% | 21.8 ±7% | 20.7 ±4% | 9.0 ±5% |
| exp2f | 2.9 ±39% | 3.7 ±35% | 4.3 ±30% | 12.9 ±29% | 29.2 ±38% | 28.0 ±39% | 13.0 ±34% |
| expm1f | 9.9 ±10% | 9.8 ±27% | 11.0 ±5% | 12.8 ±14% | 24.7 ±7% | 23.7 ±41% | 11.8 ±18% |
| logf | 3.3 ±37% | 5.7 ±49% | 4.1 ±53% | 9.4 ±58% | 19.7 ±31% | 18.1 ±40% | 7.1 ±17% |
| log2f | 3.3 ±4% | 6.1 ±11% | 4.0 ±6% | 11.8 ±6% | 20.7 ±8% | 20.2 ±9% | 9.4 ±3% |
| log10f | 6.3 ±5% | 6.7 ±25% | 4.1 ±21% | 12.4 ±21% | 20.5 ±7% | 19.9 ±11% | 9.4 ±15% |
| log1pf | 6.5 ±3% | 9.3 ±6% | 5.3 ±5% | 10.7 ±14% | 23.9 ±8% | 22.6 ±5% | 11.8 ±3% |
| powf | 7.1 ±13% | 41.4 ±5% | 53.0 ±6% | 52.3 ±4% | 31.8 ±15% | 31.2 ±14% | 14.0 ±4% |
| sinhf | 12.4 ±33% | 12.0 ±48% | 13.6 ±4% | 29.5 ±2% | 31.5 ±6% | 31.4 ±33% | 12.9 ±24% |
| coshf | 5.4 ±46% | 12.9 ±17% | 13.6 ±11% | 19.8 ±30% | 30.7 ±37% | 28.6 ±20% | 10.5 ±27% |
| tanhf | 11.9 ±4% | 9.5 ±24% | 5.3 ±26% | 18.1 ±34% | 34.9 ±30% | 34.0 ±26% | 14.6 ±17% |
| cbrtf | 4.8 ±2% | 4.8 ±3% | 5.9 ±5% | 16.6 ±12% | 32.3 ±8% | 31.4 ±12% | 17.4 ±4% |
| hypotf | 8.9 ±14% | 8.6 ±5% | 10.6 ±5% | 12.3 ±6% | 26.2 ±39% | 25.6 ±40% | 19.3 ±38% |
| fmodf | 60.7 ±4% | 62.6 ±6% | - | 12.2 ±5% | 23.2 ±10% | 23.0 ±9% | 9.3 ±4% |

### Chrome, sin cos tan in the small (|x| < pi/4) and large (|x| < 1e6) domains, double

| function | musl | msun | core | ours | js1 | js1w | jsn |
|---|---|---|---|---|---|---|---|
| sin | 3.2 ±2% | 4.5 ±19% | 319.3 ±28% | 4.5 ±6% | 21.8 ±15% | 23.8 ±20% | 16.1 ±12% |
| cos | 3.7 ±26% | 4.5 ±56% | 313.4 ±25% | 4.8 ±31% | 21.9 ±32% | 23.9 ±25% | 16.0 ±7% |
| tan | 7.7 ±16% | 9.5 ±13% | 438.6 ±11% | 10.6 ±14% | 27.5 ±6% | 29.4 ±5% | 22.4 ±4% |

| function | musl | msun | core | ours | js1 | js1w | jsn |
|---|---|---|---|---|---|---|---|
| sin | 15.5 ±5% | 15.5 ±6% | 316.0 ±5% | 18.2 ±4% | 33.8 ±5% | 36.5 ±3% | 27.3 ±6% |
| cos | 15.9 ±12% | 15.6 ±4% | 307.4 ±15% | 19.4 ±19% | 33.9 ±30% | 36.3 ±29% | 27.4 ±12% |
| tan | 17.5 ±6% | 17.1 ±14% | 429.0 ±7% | 19.9 ±12% | 42.0 ±6% | 44.1 ±8% | 34.0 ±5% |

### node, typical domain, double

| function | musl | msun | core | ours | js1 | js1w | jsn |
|---|---|---|---|---|---|---|---|
| sqrt | 1.6 ±20% | 1.6 ±7% | 1.6 ±8% | 1.6 ±4% | 2.1 ±13% | 12.8 ±26% | 2.1 ±8% |
| sin | 15.1 ±6% | 12.9 ±4% | 298.4 ±7% | 13.3 ±5% | 20.1 ±8% | 33.9 ±9% | 19.0 ±4% |
| cos | 13.9 ±14% | 11.9 ±14% | 281.9 ±4% | 13.6 ±6% | 19.7 ±4% | 32.5 ±18% | 18.8 ±8% |
| tan | 16.3 ±3% | 14.0 ±2% | 406.2 ±9% | 20.4 ±8% | 19.5 ±8% | 32.4 ±6% | 18.9 ±4% |
| asin | 9.4 ±3% | 7.8 ±1% | 50.6 ±12% | 13.7 ±15% | 10.6 ±5% | 20.9 ±9% | 9.5 ±9% |
| acos | 9.4 ±34% | 6.2 ±79% | 50.6 ±29% | 14.4 ±23% | 10.1 ±6% | 20.2 ±5% | 12.7 ±129% |
| atan | 6.8 ±2% | 6.9 ±5% | 32.9 ±18% | 11.4 ±16% | 7.9 ±3% | 19.6 ±8% | 8.0 ±4% |
| atan2 | 22.0 ±5% | 20.9 ±6% | 47.2 ±6% | 26.4 ±6% | 22.2 ±4% | 38.8 ±4% | 20.4 ±7% |
| exp | 5.0 ±10% | 8.9 ±3% | 30.5 ±34% | 8.7 ±31% | 6.5 ±37% | 17.8 ±41% | 6.6 ±35% |
| exp2 | 6.0 ±30% | 3.9 ±49% | 30.7 ±28% | 10.7 ±41% | 44.6 ±29% | 47.0 ±41% | 36.7 ±18% |
| expm1 | 11.7 ±3% | 11.1 ±2% | 30.6 ±2% | 11.6 ±4% | 20.5 ±5% | 22.5 ±2% | 10.6 ±3% |
| log | 4.1 ±12% | 7.2 ±7% | 108.9 ±24% | 8.7 ±37% | 6.7 ±39% | 16.7 ±10% | 7.3 ±11% |
| log2 | 4.9 ±34% | 8.1 ±4% | 26.1 ±3% | 11.2 ±5% | 14.5 ±12% | 15.7 ±5% | 7.5 ±15% |
| log10 | 8.3 ±3% | 8.6 ±20% | 146.9 ±31% | 11.7 ±28% | 21.8 ±19% | 23.4 ±6% | 13.0 ±3% |
| log1p | 8.7 ±6% | 10.5 ±4% | 24.9 ±3% | 10.4 ±3% | 18.4 ±5% | 20.5 ±3% | 8.9 ±3% |
| pow | 12.8 ±32% | 44.7 ±22% | 402.2 ±27% | 49.3 ±7% | 40.7 ±16% | 52.3 ±20% | 38.6 ±10% |
| sinh | 14.9 ±5% | 16.2 ±16% | 49.4 ±5% | 23.0 ±23% | 23.9 ±10% | 27.0 ±11% | 13.6 ±10% |
| cosh | 6.1 ±57% | 11.3 ±30% | 48.9 ±28% | 17.5 ±36% | 17.8 ±53% | 19.9 ±41% | 8.5 ±68% |
| tanh | 16.3 ±37% | 17.7 ±17% | 43.2 ±2% | 16.8 ±22% | 25.3 ±4% | 28.6 ±11% | 14.2 ±28% |
| cbrt | 7.1 ±30% | 6.7 ±12% | 50.7 ±5% | 16.0 ±5% | 16.4 ±7% | 16.9 ±28% | 6.4 ±5% |
| hypot | 13.6 ±25% | 10.4 ±3% | 68.3 ±3% | 12.7 ±6% | 26.9 ±10% | 28.2 ±7% | 22.8 ±9% |
| fmod | 61.5 ±5% | 67.8 ±5% | - | 11.5 ±7% | 17.7 ±13% | 19.6 ±7% | 9.0 ±4% |

### Native, typical domain, double: the twins at cdom's -Oz, and at -O2

The native twins are the same C with the same flags (-Oz); glibc is Ubuntu's libm.
At -Oz, clang's x86-64 code keeps a false dependency that -O2 breaks (sqrtsd writes a register it never clears, so each sqrt waits for the one before: native sqrt is 4.7 ns at -Oz and 1.6 ns at -O2), so the -O2 table is the fairer native number and the one the recommendation reads.
That -O2 build is a decision beyond the brief.

| function | musl | msun | core | ours | glibc |
|---|---|---|---|---|---|
| sqrt | 4.7 ±3% | 4.7 ±3% | 4.6 ±3% | 4.7 ±3% | 4.6 ±3% |
| sin | 15.0 ±3% | 12.9 ±3% | 51.9 ±4% | 15.6 ±30% | 10.7 ±6% |
| cos | 14.9 ±7% | 13.2 ±4% | 48.8 ±3% | 15.6 ±4% | 9.8 ±7% |
| tan | 11.5 ±38% | 10.9 ±36% | 62.6 ±15% | 16.2 ±35% | 15.1 ±19% |
| asin | 5.3 ±6% | 5.3 ±8% | 11.7 ±3% | 12.9 ±4% | 7.9 ±16% |
| acos | 5.2 ±5% | 5.7 ±3% | 11.4 ±2% | 12.5 ±3% | 8.5 ±4% |
| atan | 5.8 ±3% | 6.5 ±3% | 7.4 ±33% | 8.3 ±3% | 5.6 ±3% |
| atan2 | 18.4 ±10% | 18.8 ±10% | 11.1 ±33% | 20.9 ±20% | 20.4 ±19% |
| exp | 4.2 ±4% | 5.3 ±19% | 5.3 ±33% | 7.1 ±34% | 5.8 ±15% |
| exp2 | 4.4 ±12% | 2.7 ±44% | 5.6 ±31% | 8.1 ±39% | 4.9 ±9% |
| expm1 | 7.4 ±24% | 8.8 ±18% | 6.0 ±14% | 9.4 ±2% | 7.7 ±2% |
| log | 3.4 ±5% | 5.3 ±7% | 12.0 ±12% | 6.7 ±6% | 3.1 ±5% |
| log2 | 5.5 ±3% | 6.0 ±9% | 5.8 ±4% | 8.9 ±6% | 3.1 ±3% |
| log10 | 6.2 ±9% | 6.6 ±4% | 16.5 ±3% | 9.2 ±5% | 6.8 ±4% |
| log1p | 5.5 ±4% | 7.4 ±6% | 6.9 ±4% | 7.6 ±4% | 8.0 ±8% |
| pow | 14.5 ±3% | 37.5 ±3% | 48.9 ±4% | 42.9 ±4% | 9.5 ±4% |
| sinh | 10.3 ±28% | 12.1 ±25% | 10.3 ±35% | 19.6 ±29% | 12.7 ±32% |
| cosh | 4.1 ±3% | 6.8 ±28% | 9.2 ±8% | 14.1 ±3% | 4.3 ±5% |
| tanh | 11.8 ±3% | 13.4 ±41% | 8.7 ±38% | 12.4 ±5% | 11.9 ±13% |
| cbrt | 5.6 ±10% | 5.4 ±5% | 22.0 ±7% | 13.9 ±22% | 10.7 ±16% |
| hypot | 8.4 ±3% | 9.0 ±6% | 8.7 ±22% | 8.3 ±13% | 7.9 ±4% |
| fmod | 55.2 ±59% | 61.4 ±5% | - | 8.3 ±4% | 6.7 ±5% |

At -O2:

| function | musl | msun | core | ours | glibc |
|---|---|---|---|---|---|
| sqrt | 1.6 ±17% | 1.6 ±13% | 1.6 ±2% | 1.6 ±2% | 1.6 ±2% |
| sin | 14.5 ±13% | 13.1 ±11% | 52.6 ±9% | 12.8 ±5% | 8.5 ±8% |
| cos | 14.9 ±18% | 13.4 ±17% | 49.9 ±8% | 13.0 ±6% | 8.6 ±9% |
| tan | 11.9 ±12% | 10.5 ±5% | 63.7 ±15% | 13.4 ±3% | 14.6 ±14% |
| asin | 4.8 ±9% | 5.0 ±6% | 11.8 ±6% | 8.8 ±2% | 6.0 ±7% |
| acos | 5.1 ±9% | 5.2 ±6% | 11.3 ±5% | 7.8 ±3% | 8.0 ±3% |
| atan | 5.7 ±5% | 5.9 ±5% | 7.3 ±10% | 7.0 ±9% | 5.0 ±13% |
| atan2 | 17.1 ±14% | 17.4 ±23% | 11.3 ±48% | 16.7 ±38% | 18.7 ±22% |
| exp | 4.4 ±14% | 5.5 ±28% | 5.4 ±40% | 6.1 ±12% | 5.7 ±6% |
| exp2 | 4.0 ±21% | 4.0 ±15% | 5.4 ±39% | 7.9 ±15% | 3.9 ±10% |
| expm1 | 7.7 ±3% | 8.8 ±2% | 6.1 ±4% | 9.1 ±2% | 8.0 ±3% |
| log | 3.9 ±5% | 5.3 ±8% | 12.8 ±3% | 6.2 ±2% | 3.4 ±3% |
| log2 | 4.5 ±4% | 6.2 ±7% | 6.0 ±3% | 8.7 ±6% | 3.1 ±2% |
| log10 | 6.2 ±12% | 6.5 ±12% | 17.3 ±7% | 9.2 ±6% | 6.8 ±6% |
| log1p | 5.7 ±12% | 7.5 ±7% | 7.1 ±11% | 7.0 ±9% | 7.9 ±6% |
| pow | 17.5 ±2% | 37.4 ±10% | 49.4 ±3% | 43.1 ±2% | 10.2 ±4% |
| sinh | 10.9 ±6% | 12.3 ±8% | 10.4 ±3% | 20.3 ±6% | 12.2 ±6% |
| cosh | 4.3 ±37% | 6.9 ±32% | 9.7 ±19% | 13.9 ±12% | 5.3 ±31% |
| tanh | 11.3 ±3% | 13.0 ±3% | 8.1 ±39% | 12.7 ±15% | 11.7 ±4% |
| cbrt | 5.1 ±3% | 4.9 ±5% | 22.6 ±1% | 12.9 ±3% | 10.9 ±2% |
| hypot | 4.9 ±12% | 8.7 ±19% | 8.9 ±5% | 8.7 ±7% | 7.5 ±9% |
| fmod | 55.5 ±12% | 59.2 ±16% | - | 8.6 ±21% | 7.0 ±30% |

### Accuracy: max ulp, 1,000,000 inputs per function and domain, against MPFR at 160 bits

v8 is V8's Math.* (node 22's), on the same inputs.
The oracle (tools/oracle.py) found MPFR's correctly rounded result and CORE-MATH's both right on all 11,200 sampled inputs.
tables.md also has the mean error and the count of results that are not correctly rounded.

| function | domain | musl | msun | core | ours | glibc | v8 |
|---|---|---|---|---|---|---|---|
| sqrt | typical | 0.500 | 0.500 | 0.500 | 0.500 | 0.500 | 0.500 |
| sin | small | 0.718 | 0.718 | 0.500 | 0.714 | 0.515 | 0.714 |
| sin | typical | 0.765 | 0.765 | 0.500 | 0.765 | 0.515 | 0.827 |
| sin | large | 0.764 | 0.764 | 0.500 | 0.757 | 0.515 | 0.820 |
| cos | small | 0.747 | 0.747 | 0.500 | 0.744 | 0.503 | 0.814 |
| cos | typical | 0.765 | 0.765 | 0.500 | 0.763 | 0.515 | 0.817 |
| cos | large | 0.758 | 0.758 | 0.500 | 0.754 | 0.515 | 0.826 |
| tan | small | 0.789 | 0.789 | 0.500 | 0.794 | 0.551 | 0.789 |
| tan | typical | 0.832 | 0.832 | 0.500 | 0.865 | 0.553 | 0.832 |
| tan | large | 0.869 | 0.869 | 0.500 | 0.896 | 0.560 | 0.869 |
| asin | typical | 0.872 | 0.872 | 0.500 | 0.756 | 0.514 | 0.872 |
| acos | typical | 0.870 | 0.870 | 0.500 | 0.762 | 0.519 | 0.870 |
| atan | typical | 0.747 | 0.747 | 0.500 | 0.614 | 0.510 | 0.747 |
| atan2 | typical | 1.488 | 1.488 | 0.500 | 0.721 | 0.522 | 1.488 |
| exp | typical | 0.507 | 0.869 | 0.500 | 0.662 | 0.507 | 0.869 |
| exp2 | typical | 0.507 | 0.502 | 0.500 | 0.692 | 0.507 | 0.966 |
| expm1 | typical | 0.809 | 0.809 | 0.500 | 0.727 | 0.816 | 0.809 |
| log | typical | 0.502 | 0.757 | 0.500 | 0.753 | 0.502 | 0.757 |
| log2 | typical | 0.505 | 0.639 | 0.500 | 0.639 | 0.502 | 0.639 |
| log10 | typical | 0.564 | 0.564 | 0.500 | 0.565 | 1.497 | 1.673 |
| log1p | typical | 0.839 | 0.839 | 0.500 | 0.839 | 0.839 | 0.839 |
| pow | typical | 0.507 | 0.828 | 0.500 | 0.734 | 0.505 | 0.989 |
| sinh | typical | 1.737 | 1.737 | 0.500 | 0.703 | 1.737 | 1.737 |
| cosh | typical | 1.019 | 1.313 | 0.500 | 0.656 | 1.019 | 1.313 |
| tanh | typical | 2.042 | 2.042 | 0.500 | 0.919 | 2.042 | 2.042 |
| cbrt | typical | 0.666 | 0.666 | 0.500 | 0.500 | 3.268 | 0.666 |
| hypot | typical | 0.979 | 0.935 | 0.500 | 0.500 | 0.562 | 1.929 |
| fmod | typical | 0.000 | 0.000 | - | 0.000 | 0.000 | 0.000 |

| function | domain | musl | msun | core | ours | glibc | v8 |
|---|---|---|---|---|---|---|---|
| sqrtf | typical | 0.500 | 0.500 | 0.500 | 0.500 | 0.500 | 0.500 |
| sinf | small | 0.500 | 0.500 | 0.500 | 0.500 | 0.560 | 0.500 |
| sinf | typical | 0.501 | 0.501 | 0.500 | 0.500 | 0.560 | 0.500 |
| sinf | large | 0.501 | 0.501 | 0.500 | 0.500 | 0.559 | 0.500 |
| cosf | small | 0.501 | 0.501 | 0.500 | 0.500 | 0.501 | 0.500 |
| cosf | typical | 0.501 | 0.501 | 0.500 | 0.500 | 0.559 | 0.500 |
| cosf | large | 0.501 | 0.501 | 0.500 | 0.500 | 0.561 | 0.500 |
| tanf | small | 0.798 | 0.798 | 0.500 | 0.500 | 0.819 | 0.500 |
| tanf | typical | 0.799 | 0.799 | 0.500 | 0.500 | 1.008 | 0.500 |
| tanf | large | 0.799 | 0.799 | 0.500 | 0.500 | 0.904 | 0.500 |
| asinf | typical | 0.718 | 0.650 | 0.500 | 0.500 | 0.863 | 0.500 |
| acosf | typical | 0.899 | 0.891 | 0.500 | 0.500 | 0.876 | 0.500 |
| atanf | typical | 0.826 | 0.826 | 0.500 | 0.500 | 0.826 | 0.500 |
| atan2f | typical | 1.401 | 1.401 | 0.500 | 0.500 | 1.409 | 0.500 |
| expf | typical | 0.502 | 0.872 | 0.500 | 0.500 | 0.502 | 0.500 |
| exp2f | typical | 0.501 | 0.501 | 0.500 | 0.500 | 0.501 | 0.500 |
| expm1f | typical | 0.778 | 0.778 | 0.500 | 0.500 | 0.778 | 0.500 |
| logf | typical | 0.796 | 0.800 | 0.500 | 0.500 | 0.796 | 0.500 |
| log2f | typical | 0.724 | 0.803 | 0.500 | 0.500 | 0.724 | 0.500 |
| log10f | typical | 0.808 | 0.808 | 0.500 | 0.500 | 1.881 | 0.500 |
| log1pf | typical | 0.781 | 0.781 | 0.500 | 0.500 | 0.781 | 0.500 |
| powf | typical | 0.503 | 0.824 | 0.500 | 0.500 | 0.503 | 0.500 |
| sinhf | typical | 1.678 | 1.678 | 0.500 | 0.500 | 1.678 | 0.500 |
| coshf | typical | 1.019 | 1.317 | 0.500 | 0.500 | 1.019 | 0.500 |
| tanhf | typical | 2.075 | 2.075 | 0.500 | 0.500 | 2.075 | 0.500 |
| cbrtf | typical | 0.500 | 0.500 | 0.500 | 0.500 | 0.933 | 0.500 |
| hypotf | typical | 0.838 | 0.912 | 0.500 | 0.500 | 0.500 | 0.500 |
| fmodf | typical | 0.000 | 0.000 | - | 0.000 | 0.000 | 0.000 |

### Size: bytes an arm adds to an empty module (320 bytes, 226 brotli), after wasm-opt -O3

| arm | sin | sin, brotli | all | all, brotli |
|---|---|---|---|---|
| musl | 6644 | 4316 | 38846 | 22283 |
| msun | 6150 | 4265 | 35194 | 16492 |
| core | 25874 | 17179 | 265059 | 97026 |
| ours | 978 | 502 | 13935 | 5152 |
| js1 | 16 | 18 | 1433 | 492 |

| function | musl | msun | core | ours |
|---|---|---|---|---|
| sin | 4316 (6644) | 4265 (6150) | 17179 (25874) | 502 (978) |
| cos | 4295 (6572) | 4183 (6094) | 17186 (25791) | 507 (992) |
| tan | 4353 (6686) | 4295 (6297) | 19383 (28445) | 568 (769) |
| asin | 358 (457) | 378 (610) | 4837 (6103) | 450 (659) |
| acos | 361 (446) | 385 (747) | 4326 (5580) | 424 (655) |
| atan | 425 (572) | 450 (630) | 4033 (5265) | 489 (644) |
| atan2 | 652 (1022) | 755 (1300) | 6240 (10557) | 689 (1145) |
| exp | 2465 (2749) | 478 (648) | 4032 (5268) | 355 (448) |
| exp2 | 2491 (2755) | 2717 (4556) | 3959 (5134) | 400 (549) |
| expm1 | 506 (717) | 554 (846) | 5458 (6994) | 430 (674) |
| log | 4113 (4851) | 441 (627) | 13650 (26076) | 375 (420) |
| log2 | 2388 (2826) | 391 (456) | 4380 (6227) | 411 (545) |
| log10 | 395 (472) | 374 (481) | 13949 (26350) | 449 (570) |
| log1p | 375 (501) | 515 (785) | 4914 (7302) | 365 (499) |
| pow | 5000 (7623) | 1335 (2222) | 26454 (55425) | 1112 (1927) |
| sinh | 2954 (3639) | 1076 (1759) | 5019 (6778) | 764 (1380) |
| cosh | 2923 (3678) | 1070 (1742) | 4358 (6021) | 875 (1790) |
| tanh | 604 (976) | 670 (1116) | 4258 (5844) | 701 (1451) |
| cbrt | 214 (244) | 234 (258) | 1231 (2009) | 339 (460) |
| hypot | 270 (386) | 338 (513) | 1624 (2927) | 252 (469) |
| fmod | 283 (488) | 507 (851) | - | 277 (410) |

### What the numbers say

- musl is the fastest C in wasm for most functions: its exp, exp2, log, log2 and pow are table-driven (ARM's optimized-routines), 4 to 13 ns, and its pow is the only fast and accurate pow here (12.8 ns, 0.507 ulp).
  Its cost is bytes: sin, cos and tan each bring __rem_pio2_large and its table of 2/pi (4.3 KB brotli), exp and log their tables (2.4 to 4.1 KB), pow 5.0 KB, and all of them 22 KB.
  Its max error is under 1 ulp but for atan2 (1.49), sinh (1.74), cosh (1.02) and tanh (2.04).
- msun is fdlibm: the same sin, cos and tan kernels as musl (the same max error and the same count of results not correctly rounded, to the input), smaller exp and log (0.4 to 0.5 KB) but slower and less accurate (exp 0.87 ulp, pow 0.83 ulp and 44 ns).
- CORE-MATH is correctly rounded everywhere (0.500 ulp in every cell, and the oracle agrees), but in wasm its double functions are 2.3x to 35x slower than musl (median 7.1x): it is written for hardware fma, and wasm's fma is a call of musl's software fma.c each time.
  Natively, where glibc's fma is the CPU's instruction, the same code is 2.3x to 9.4x faster (the wasm tax of core at -O2, in tables.md).
  Its float functions need no fma and are as fast as musl's (median 1.19x) and correctly rounded, but it is the largest arm by far (97 KB brotli for all, 17 KB for sin).
- Ours is under 1 ulp in every function and every domain measured (max 0.919, tanh), with no tables; it is 1.63x musl's time in Chrome (median): 1.0x to 1.3x for sin, cos, tan, atan2, expm1, log1p, log10, tanh and hypot, 1.6x to 2.9x for asin, acos, atan, exp, exp2, log, log2, sinh, cosh and cbrt (where musl has tables, or rational approximations ours replaces with longer polynomials), and 3.9x for pow (50.5 ns: double-double steps, split by hand without fma); its fmod is 6x faster than musl's bit loop (10.8 ns against 62.0, both exact).
  Where musl carries a table (sin, cos, tan, exp, exp2, log, log2, pow, sinh, cosh) ours is 4x to 9x smaller (sin 0.5 KB brotli against 4.3 KB, pow 1.1 KB against 5.0 KB); elsewhere the two are within 0.2 KB of each other, ours the larger for asin, acos, atan, tanh, cbrt and log10; all of them: 5.2 KB against 22 KB.
  Where it falls short: sin, cos and tan reduce by pi/2 in four parts with a multiple below 2^20, and past that (|x| above 1.6e6) the result is garbage, not merely inaccurate (a spot check of 2,000 inputs per decade from 1.6e6 up: errors of 2e11 ulp at 1e9, and values outside [-1, 1] past 2^51; musl stays at 1 ulp); the bench's domains stop at 1e6, so the tables do not show it.
  A subnormal result of exp, exp2, expm1 or pow is rounded twice (no such input in the domains).
  The float functions are the double ones rounded to float: correctly rounded on every sampled input, at the double's cost (1.5x to 3x musl's float time).
  Ours was developed against this accuracy tool (the first version had expm1, atan2, exp2 and tanh over 1 ulp); P6 below was written before either.
- The browser's Math.* through an import is never faster than musl in Chrome 153 but for fmod (x % y): one call per value costs 1.4x to 3.6x musl's time, 5.4x for sqrt (a JS call against one wasm instruction).
  node 22's V8 is different: there, an import that is the Math function itself is called with no JS frame (js1 sin in the small domain is 3.8 ns in node, 21.8 in Chrome; js1w, the same wrapped in an arrow function, is 15.4 ns in node), so what an import costs depends on the engine's version.
  Batching (jsn) removes the crossing, 1.6 to 12 ns per value in Chrome (median 6.7), but V8's own functions (fdlibm compiled for the host) are still slower than musl in wasm for 16 of the 21.
  And the browser is not C: Math.pow(1, NaN), Math.pow(1, inf) and Math.pow(-1, inf) are NaN where C says 1 (the special-value test lists them), V8's log10 is off by 1.67 ulp and its hypot by 1.93.
- The wasm tax: musl in Chrome over musl natively at -O2 is 1.27x (median; 0.73x to 1.90x), so the C maths is about as fast in Chrome as natively; and CORE-MATH's 6x tax is fma alone.

## Verdicts

Each prediction above, unedited, against the numbers of this run (Chrome, typical domain, double unless it says so); python3 -I bench/math/tools/verdicts.py results/2026-11-20-vm prints them.

- P1, refuted: js1 is slower than musl for every function but fmod (as predicted), and fmod is faster (0.34x), but the ratio is outside 1.3x to 3x for 6 of the other 20 (asin 3.57x, acos 3.56x, atan 3.04x, exp2 3.60x, log 3.44x, cbrt 3.27x), more than the 4 allowed; sqrt is 5.41x; median 2.19x.
- P2, refuted: jsn is under musl for 5 of 21 (expm1 0.92x, pow 0.98x, sinh 0.78x, tanh 0.85x, fmod 0.14x), not 12; median 1.41x.
  V8's Math.* is fdlibm compiled for the host, and it is slower than musl's table-driven code compiled to wasm.
- P3, refuted: The crossing (js1 minus jsn) is 1.6 to 12.1 ns, median 6.7, under the 8 to 25 predicted; js1w is slower than js1 for 21 of 22, but by 2.2 ns (median), not 5 to 30.
  In Chrome 153 an import is a JS call whether it is Math.sin itself or an arrow function around it; node 22's V8 (below) is different.
- P4, refuted: musl in Chrome over its native twin at -Oz: median 1.26x, inside 1.2x to 2.0x, but 4 functions are under 1.0x (pow 0.88x, log2 0.93x, sin 0.97x, cos 0.99x).
  Against the -O2 twin (not predicted): median 1.27x, range 0.73x to 1.90x.
- P5, refuted: ours is inside 0.7x to 1.4x of musl for 9 of 21, not 15; pow is 3.94x, outside 1.5x to 3x; median 1.63x.
- P6, confirmed: ours is under 1 ulp for all 21 double functions in every domain (max 0.919, tanh); fmod is exact.
  Disclosure: ours was revised against the accuracy tool after P6 was written (its first version, on 200,000 inputs: expm1 1.81 ulp, tanh 2.10, sinh 1.84, cosh 1.25, atan2 1.20, exp2 1.02); the prediction was about the outcome and is unedited.
- P7, confirmed: CORE-MATH's double functions are 7.11x musl's time (median, 2.26x to 35.07x); its floats 1.19x (0.44x to 7.49x, powf the slowest), inside 0.7x to 2x.
- P8, confirmed: CORE-MATH's max error is 0.500 ulp in every cell (to the 4 decimals the sweep prints; no cell above), and the oracle found it right on all 11,200 sampled inputs, as MPFR.
- P9, confirmed: musl is under 1 ulp for 17 of 21 (over: atan2 1.488, sinh 1.737, cosh 1.019, tanh 2.042), all under 2.5; msun for 17 of 21 (atan2 1.488, sinh 1.737, cosh 1.313, tanh 2.042), all under 2.5.
- P10 confirmed, for the build in this PR: every arm's wasm module equals its native twin on all 222 (arm, function, domain) hashes of 100,000 inputs.
  Disclosure: the check's first run failed for CORE-MATH (atan2, hypot): compat.c's fmin and fmax, which wasm calls and the native twin takes from glibc, had a comparison backwards.
  That was a bug in the bench, not in a compiler or flag; it is fixed, and its mutant (compat-fmax below) is in the mutation check.
- P11, refuted: musl's sin adds 6,644 bytes (inside 4 to 8 KB) but 4,316 brotli, over the 2 to 4 KB predicted: __rem_pio2_large's table of 2/pi is digits, which brotli barely compresses.
- P12, confirmed: sqrt: 1.57 ns in all four C arms (0% apart); sqrtf 0.78 to 0.79 ns.
- P13, refuted: glibc natively against musl's native twin (-Oz): sin 1.40x and cos 1.52x and pow 1.53x faster, inside 1.2x to 2.5x, but exp is slower (0.72x) and log only 1.12x.
- P14, refuted: node's median is within 15% of Chrome's in 84% of the C arms' cells (187 of 222), and 64% counting the browser's arms (249 of 390), under the 90% predicted.
  The C arms differ by noise and by V8 version (node 22 has V8 12.4, Chrome 153 a later one); the browser's arms differ by design: node's V8 calls an import that is Math.sin itself with no JS frame (js1 sqrt 2.1 ns, sin small 3.8 ns in node, against 8.5 and 21.8 in Chrome).

## Recommendation for cdom

**Ours becomes src/libc/math.h, once it has a large-argument reduction for sin, cos and tan.**
Not musl, not CORE-MATH, and not the browser's Math.*.
The reasons, in the order cdom weighs them:

1. Bytes: cdom is sold on its bytes (a hello world is 9.5 KB brotli), and a module carries only the functions it calls.
   The functions a UI calls are sin, cos, atan2, exp, log, pow and hypot (animation, easing, colour, geometry); there musl's tables cost 4.3 KB brotli for sin (cos shares it), 5.0 KB for pow, 2.5 KB for exp, 4.1 KB for log: musl's sin alone would add 45% to a hello world.
   Ours is 0.5 KB for sin, 1.1 KB for pow, 0.4 KB for exp and for log, 5.2 KB for all 21 (musl 22 KB, msun 16 KB, CORE-MATH 97 KB).
2. Accuracy: Ours is under 1 ulp in every function and domain measured; musl is over 1 ulp in atan2, sinh, cosh and tanh (up to 2.04), msun the same, V8's Math.* in atan2, log10, sinh, cosh, tanh and hypot (up to 2.04).
3. Speed is close enough: ours is 1.63x musl's time (median), which on this VM is under 10 ns a call more for 17 of the 21 functions (pow 38 ns more, sinh 13, cosh 12; fmod 51 ns less): 10,000 calls a frame cost under 100 us more.
   The exception is pow, 50.5 ns against musl's 12.8: an app that calls pow per pixel feels it, and that is the one function where taking musl's (pow.c and its data, 5.0 KB brotli, MIT) is worth offering, as an opt-in, not the default.
   fmod goes the other way: ours is 6x faster than musl's (and exact).
4. The other arms lose on their own numbers: CORE-MATH is correctly rounded but 7x slower in wasm (no fma) and the largest; the browser's Math.* through imports is 1.4x to 3.6x slower than musl per call in Chrome 153, costs a JS crossing per value that depends on the engine's version, and is not C (Math.pow's edge cases differ from Annex F); batching hides the crossing but not V8's slower functions.

What must be done first, and is not in this PR (which changes no part of cdom):

- sin, cos and tan for |x| above 2^20 pi/2 (1.6e6): a Payne-Hanek reduction with 2/pi to about 1,200 bits (a table of about 150 bytes, against musl's 2.7 KB raw), so that every double gives a result; today ours gives garbage there (measured above).
- The tests of this bench as cdom's: the special-value table (special.c) and an accuracy sweep in CI.
- The Mac's numbers (mac.sh) in Chrome and Safari, since Safari's JSC and the arm64 Mac may rank the arms differently.
- Smaller points: errno is never set (cdom has no errno), subnormal results of exp, exp2, expm1 and pow are rounded twice, and the float functions are the double ones rounded (correctly rounded on every sampled input, at the double's cost).

If ours cannot get its reduction, musl is the second choice: proven, MIT, the fastest C here, at 4x to 9x the bytes per table-driven function; with ours' fmod (6x faster) in place of musl's.

## The Mac

bench/math/mac.sh runs the whole bench on Alex's Mac: it builds (Homebrew's llvm and lld, binaryen, brotli), runs the tests, measures the sizes, the native twins at -Oz and -O2 and the Mac's libm (the arm named applelibm), node, headless Chrome and then Safari through the same page, 9 interleaved rounds each, and writes results/<date>-mac-* and the tables; a second run the same day needs another name (mac.sh mac2).
The accuracy sweep is the same on every machine but for the host's libm, so it runs only with ACCURACY=1 (it needs brew install mpfr, and pip install mpmath for the oracle).
On the Mac the native twins keep musl's wasm paths (-U__FP_FAST_FMA), while CORE-MATH's __builtin_fma becomes the arm64 instruction: its native twin there is the fast one, and its wasm tax is the cost of having no fma.

## Decided here, beyond the brief

- Sources: musl.libc.org, GitHub's archives and gitlab.inria.fr answered 403 or nothing through this VM's proxy, so musl 1.2.6 came from Ubuntu's archive (upstream's tarball under its source package's name), FreeBSD msun file by file from raw.githubusercontent.com at release/15.1.0, and CORE-MATH as the core-math-sys 1.4.0 crate's vendored tree (upstream commit 85430b3e); third_party/README.md has the URLs and hashes.
- FreeBSD msun over fdlibm: it is fdlibm maintained (and fdlibm's netlib home was not reachable).
- One change to a vendored file: msun's nan_mix in double, not long double (third_party/README.md).
- CORE-MATH's fma is musl's software fma.c (the arm has to have one); its wasm build needs three 128-bit helpers, written here (compat/int128.c), as cdom links no compiler-rt.
- An extra browser arm, js1w (the import wrapped in an arrow function), to separate an import that is the Math function from one that is JS, which matters for how cdom's shim would expose maths.
- The float functions of ours are the double ones rounded to float; CORE-MATH's fmod does not exist, so its cells are "-".
- The native twins at cdom's -Oz (the twin), and also at -O2 (the fairer native number), because -Oz on x86-64 keeps false dependencies.
- The accuracy reference is MPFR at 160 bits (in C, linked only into the native sweep, never shipped), checked by mpmath at 300 bits on a sample; 1,000,000 inputs per function and domain.
- SLEEF (optional in the brief) is not here: its build is CMake and generated headers, and the time went to the rest; it is the next arm to add if wasm SIMD matters.
- The domains' bounds in gen.h, chosen here (the brief named only the trig ones).
