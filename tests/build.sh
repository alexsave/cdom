#!/bin/bash
# Builds the test pages into tests/out (served by tools/fuzz.mjs, canvas.mjs and host.mjs).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
CC="${WASM_CC:-/opt/homebrew/opt/llvm/bin/clang}"
mkdir -p "$here/out"
"$CC" --target=wasm32 -O2 -nostdlib -mbulk-memory -Wall -Wextra -Werror -Wl,--no-entry \
  -o "$here/out/listfuzz.wasm" "$here/listfuzz.c" "$here/../src/cdom.c" "$here/../src/mini.c"
cp "$here/listfuzz.html" "$here/../shim/cdom.js" "$here/out/"
"$CC" --target=wasm32 -O2 -nostdlib -mbulk-memory -Wall -Wextra -Werror -Wl,--no-entry \
  -o "$here/out/canvas.wasm" "$here/canvas.c" "$here/../src/cdom.c" "$here/../src/mini.c"
cp "$here/canvas.html" "$here/out/"
"$CC" --target=wasm32 -O2 -nostdlib -mbulk-memory -Wall -Wextra -Werror -Wl,--no-entry \
  -o "$here/out/host.wasm" "$here/host.c" "$here/../src/cdom.c" "$here/../src/mini.c"
cp "$here/host.html" "$here/out/"
