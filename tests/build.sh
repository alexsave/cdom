#!/bin/bash
# Builds the keyed-diff fuzz page and the unit page into tests/out (served by tools/fuzz.mjs, tools/unit.mjs).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
CC="${WASM_CC:-/opt/homebrew/opt/llvm/bin/clang}"
mkdir -p "$here/out"
"$CC" --target=wasm32 -O2 -nostdlib -mbulk-memory -Wall -Wextra -Werror -Wl,--no-entry \
  -o "$here/out/listfuzz.wasm" "$here/listfuzz.c" "$here/../src/cdom.c" "$here/../src/mini.c"
"$CC" --target=wasm32 -O2 -nostdlib -mbulk-memory -Wall -Wextra -Werror -Wl,--no-entry -Wl,--export-memory \
  -o "$here/out/unit.wasm" "$here/unit.c" "$here/../src/cdom.c" "$here/../src/cdom_net.c" "$here/../src/mini.c"
cp "$here/listfuzz.html" "$here/unit.html" "$here/../shim/cdom.js" "$here/out/"
