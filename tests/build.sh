#!/bin/bash
# Builds the keyed-diff fuzz page into tests/out (served by tools/fuzz.mjs).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
CC="${WASM_CC:-/opt/homebrew/opt/llvm/bin/clang}"
mkdir -p "$here/out"
"$CC" --target=wasm32 -O2 -nostdlib -mbulk-memory -Wall -Wextra -Werror ${XFLAGS:-} -Wl,--no-entry \
  -o "$here/out/listfuzz.wasm" "$here/listfuzz.c" "$here/../src/cdom.c" "$here/../src/mini.c"
cp "$here/listfuzz.html" "$here/out/"
cp "${SHIM:-$here/../shim/cdom.js}" "$here/out/cdom.js"
