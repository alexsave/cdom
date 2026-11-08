#!/bin/bash
# Builds the test pages into tests/out (served by tools/fuzz.mjs, canvas.mjs and host.mjs).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
CC="${WASM_CC:-/opt/homebrew/opt/llvm/bin/clang}"
legacy="$here/../tools/legacy/build.mjs"
[ -d "$here/../tools/legacy/node_modules" ] || (cd "$here/../tools/legacy" && npm ci --no-audit --no-fund)
mkdir -p "$here/out"
"$CC" --target=wasm32 -O2 -nostdlib -mbulk-memory -Wall -Wextra -Werror -Wl,--no-entry \
  -o "$here/out/listfuzz.wasm" "$here/listfuzz.c" "$here/../src/cdom.c" "$here/../src/mini.c"
cp "$here/../shim/cdom.js" "$here/out/"
node "$legacy" page "$here/listfuzz.html" "$here/out/listfuzz.html"
node "$legacy" shim "$here/out/cdom.legacy.js"
node "$legacy" app "$here/out/listfuzz.wasm" "$here/out/listfuzz.wasm.js"
"$CC" --target=wasm32 -O2 -nostdlib -mbulk-memory -Wall -Wextra -Werror -Wl,--no-entry \
  -o "$here/out/canvas.wasm" "$here/canvas.c" "$here/../src/cdom.c" "$here/../src/mini.c"
cp "$here/canvas.html" "$here/out/"
"$CC" --target=wasm32 -O2 -nostdlib -mbulk-memory -Wall -Wextra -Werror -Wl,--no-entry \
  -o "$here/out/host.wasm" "$here/host.c" "$here/../src/cdom.c" "$here/../src/mini.c"
cp "$here/host.html" "$here/out/"
"$CC" --target=wasm32 -O2 -nostdlib -mbulk-memory -Wall -Wextra -Werror -Wl,--no-entry \
  -o "$here/out/legacy.wasm" "$here/legacy.c" "$here/../src/cdom.c" "$here/../src/mini.c"
node "$legacy" app "$here/out/legacy.wasm" "$here/out/legacy.wasm.js"
node "$legacy" page "$here/legacy.html" "$here/out/legacy.html"
