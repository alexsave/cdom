# Roads not taken

Experiments that were measured and lost.
Each branch was deleted on 2026-11-26, and its tip is kept at the tag named here.

## The batched op list (rig at tag `archive/direct-calls`)

C wrote a stream of operations that the shim interpreted; one wasm import per DOM operation won, and is what cdom ships.
In eight load-gated sessions the list took 1.029 times the direct calls' total (95% interval 1.023 to 1.035).
The cause was the list's one big `apply()` being optimised inside the measured click: split into a function per operation, the list matched direct calls (0.999).
The method is in [PERF_LOG.md](../PERF_LOG.md), "Proof: why direct calls win", and the interval table and probe summaries are in [results/proof/](../results/proof/).

## A JavaScript 1.0 interpreter for Netscape 2 (tag `archive/js1-interp`, PR #105)

A WebAssembly interpreter written in JavaScript 1.0.
The compiler, `tools/js1c`, won: in the real browsers the interpreter ran the integer loop 3 to 4 times slower than the compiler on Netscape 2 and 3, memory work about 6 times slower, and 2 to 3 times slower on Netscape 4 and IE 4 ([descent.md](descent.md), "The three code paths in the real browsers").

## A wasm2js rewrite for Netscape 2 (tag `archive/js1-transform`, PR #103)

wasm2js's output rewritten into JavaScript 1.0.
Its integer loop was the fastest of the three, but it kept each C call a JavaScript call, so it died before depth 100 on Netscape 2 and 3.
The compiler keeps its own stack, ran the loop within 1.1 to 1.6 times the rewrite's time on Netscape 2, and reached depth 5,000 (same section of [descent.md](descent.md)).

## Stripping the shim per app (tag `archive/shim-strip-exp`)

Cut the shim down to what each app's module imports.
It halved the starter page (8,660 to 4,284 bytes, brotli) and took 11% off a hello world with a server (18,683 to 16,667).
Decision: ship the whole shim.
A real app needs nearly all of it, and a toy app's shim is already small enough that the saving matters little to its load.
The method and numbers are in [RESULTS.md](../RESULTS.md), "Stripping the shim per app".

## libjpeg-turbo's NEON through SIMDe (tag `archive/simde-mac`)

Compiled libjpeg-turbo's Arm NEON code to wasm SIMD with SIMDe.
The hand-written wasm SIMD loops won on every input on the Mac: 0.876, 0.931, 0.940 and 0.940 of scalar time, against SIMDe's 0.944, 0.960, 0.951 and 0.991.
SIMDe helped only the JPEGs ([image-simde.md](image-simde.md); the loops that ship are in [image-simd-hand.md](image-simd-hand.md)).

## A ready-made picture in Netscape 2 (tag `archive/ns2-paint`)

Netscape 2 showing a picture of a modern page was dropped for painting the page itself ([descent.md](descent.md), "Netscape 2 paints itself"): a picture needs only an `img` tag.
