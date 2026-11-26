#!/bin/bash
# heap-regress: the keyed entry of a checkout, built as bench/build.sh builds it but with
# results/heapregress/probe.c (that checkout's cdom.c plus cd_heap_probe) in place of src/cdom.c,
# as jfb entry NAME, for tools/heapregress_probe.mjs and the harness.
# SHIM=path inlines that shim instead of the checkout's. Usage: [SHIM=...] tools/heapregress_probe.sh <checkout> <name>
set -euo pipefail
src="$1"; name="$2"
top="$(cd "$(dirname "$0")/.." && pwd)"
jfb="${JFB_KEYED:-/Users/alex/Dev/cdom/jfb/frameworks/keyed}"
. "$src/tools/cdomflags.sh"
CC="${WASM_CC:-/opt/homebrew/opt/llvm/bin/clang}"
out="$jfb/$name"; mkdir -p "$out"
"$CC" -Oz "${CDOM_CFLAGS[@]}" "-DCDOM_SRC=\"$src/src/cdom.c\"" "${CDOM_LDFLAGS[@]}" -Wl,--strip-all \
  -o "$out/app.wasm.linked" "$src/bench/cdom-keyed/src/app.c" "$top/results/heapregress/probe.c" "$src/src/mini.c"
"$CDOM_LITS" "$out/app.wasm.linked" "$out/app.wasm" "$out/app.wasm.lits"
rm -f "$out/app.wasm.linked" "$out/cdom.js"
sed -e "s/cdom-\"keyed\"/$name-\"keyed\"/" "$src/bench/cdom-keyed/index.html" > "$out/index.named.html"
"$CDOM_PAGE_TOOL" --page "$out/index.named.html" --out "$out/index.src.html" --shim "${SHIM:-$src/shim/cdom.min.js}" --lits "$out/app.wasm.lits"
rm -f "$out/app.wasm.lits" "$out/index.named.html"
node "$src/tools/legacy/build.mjs" page "$out/index.src.html" "$out/index.html"
rm "$out/index.src.html"
cat > "$out/package.json" <<JSON
{
  "name": "js-framework-benchmark-$name",
  "version": "0.1.0",
  "js-framework-benchmark": { "frameworkVersion": "0.1.0", "language": "C", "issues": [1139] }
}
JSON
printf '{ "name": "js-framework-benchmark-%s", "version": "0.1.0", "lockfileVersion": 3, "requires": true, "packages": {} }\n' "$name" > "$out/package-lock.json"
printf '%s probe: %s bytes\n' "$name" "$(wc -c < "$out/app.wasm" | tr -d ' ')"
