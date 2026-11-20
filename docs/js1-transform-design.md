# The JavaScript 1 transform: the app's wasm2js output for the frames tier

The frames tier (docs/frames-design.md) serves Netscape 2 to 4 and IE 3 and 4, and loads the app's IE 9 build, which none of them can parse.
docs/descent.md measured what each of those engines lacks.
Alex asked for the choice between a WebAssembly interpreter written in JavaScript 1.0 and a transform of the wasm2js output to be made by measurement.
This document is the transform's side (branch js1-transform); the interpreter is another job's (branch js1-interp).
It was written before the code, with the predictions in section 2; the results were added after measuring, and the predictions were not edited.

## 1. What it is

A build step, `node tools/legacy/build.mjs app-js1 <in.wasm> <out.wasm.js1.js>`, next to app and app-ie9.
It takes the same wasm2js output and the same memory pass (tools/legacy/memory.mjs) as app-ie9, on a JavaScript 1.0 copy of words.js's memory (the same layout: one 32-bit word a key), and rewrites the result down to JavaScript 1.0.
The frames tier's boot loads it (url + ".js1.js") instead of url + ".ie9.js".

Two targets:

- A: JavaScript 1.1 as tools/legacy/js11.mjs enforces it (Netscape 3 and 4, IE 4).
- B: JavaScript 1.0 as docs/descent.md measured it in Netscape 2.02 (tools/legacy/js10.mjs, the gate written from that list).

## 2. Predictions

Written before any measuring, each with what would refute it.
Timings are headless Chrome on a Linux x86-64 VM, under LEGACY=frames (the transformed file) and LEGACY=frames with the IE 9 build in its place, and indicative only: the repo manager measures the real browsers.

- T1, size: the transformed tests/frames.c (frames.wasm.js1.js), minified, is between 0.8 and 1.3 times the size of frames.wasm.ie9.js, and gzipped (gzip -9) between 0.9 and 1.3 times.
  Refuted outside either range.
- T2, CPU loop: tests/js1cpu.c's loop of 100,000 iterations of integer operations (multiply, shift, xor, a load and a store) takes between 0.5 and 1.5 times as long in the transformed file as in the IE 9 build (the median of 7 runs each).
  Refuted outside that range.
- T3, calls: 1,000 runs of a 20-deep C recursion take between 0.5 and 1.5 times as long in the transformed file as in the IE 9 build.
  Refuted outside that range.
- T4, boot and click: tests/frames.c's boot (navigation to the first draw) and one click on its button (to the redraw) each take between 0.67 and 1.5 times as long with the transformed file as with the IE 9 build.
  Refuted outside that range.
- T5, call depth: C calls stay JavaScript calls, one for one.
  tests/frames.c, driven through all of tools/frames.mjs, reaches at most 15 JavaScript frames from a browser entry, shim and C together, and js1cpu's 20-deep recursion reaches 20 C frames plus at most 8 others.
  Refuted if either is deeper.
  With Netscape 2 and 3 dying between depth 100 and 300 (docs/descent.md), that means a C program recursing up to about 90 deep runs there, and one recursing past about 290 does not.
- T6, target B: the transformed files of tests/frames.c and tests/js1cpu.c pass the JavaScript 1.0 gate (tools/legacy/js10.mjs), not only the 1.1 one.
  Refuted if any construct the transform leaves cannot be lowered below 1.1.
- T7, correctness: LEGACY=frames tools/frames.mjs passes on the transformed file, and tests/js1cpu.c's exports and tests/memforms.c's give the same answers in the transformed file, in a context with no Math.imul, clz32, trunc or fround and no charCodeAt, push or typed arrays, as in WebAssembly.
  Refuted by any failing check or differing answer.
