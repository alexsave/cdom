#!/bin/bash
# tools/js1.sh in.wasm out.js [js1c flags...]: the JavaScript 1.0 build of a module (docs/js1-compile-design.md): binaryen
# lowers it to i32 and f64 alone (out.js.lowered.wasm, kept beside it for the measurements), tools/js1c compiles it, and
# tools/js10.mjs, the JavaScript 1.0 gate, checks what it wrote; the build fails on a refusal.
set -euo pipefail
top="$(cd "$(dirname "$0")/.." && pwd)"
in="$1" out="$2"; shift 2
make -s --no-print-directory -f "$top/cdom.mk" CDOM="$top" "$top/tools/out/js1c"
wasm-opt "$in" --enable-bulk-memory --enable-sign-ext --enable-nontrapping-float-to-int --enable-mutable-globals \
  --legalize-js-interface --remove-non-js-ops --flatten --i64-to-i32-lowering -O2 -o "$out.lowered.wasm"
"$top/tools/out/js1c" "$@" "$out.lowered.wasm" "$out.tmp"
node "$top/tools/js10.mjs" check "$out.tmp"
mv "$out.tmp" "$out"
