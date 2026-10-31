#!/bin/bash
# Builds the keyed-diff fuzz page, the unit page and the click demo into tests/out (served by
# tools/fuzz.mjs and tools/unit.mjs, which taps the demo too).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
CC="${WASM_CC:-/opt/homebrew/opt/llvm/bin/clang}"
mkdir -p "$here/out"
"$CC" --target=wasm32 -O2 -nostdlib -mbulk-memory -Wall -Wextra -Werror -Wl,--no-entry \
  -o "$here/out/listfuzz.wasm" "$here/listfuzz.c" "$here/../src/cdom.c" "$here/../src/mini.c"
"$CC" --target=wasm32 -O2 -nostdlib -mbulk-memory -Wall -Wextra -Werror -Wl,--no-entry -Wl,--export-memory \
  -o "$here/out/unit.wasm" "$here/unit.c" "$here/../src/cdom.c" "$here/../src/cdom_net.c" "$here/../src/cdom_store.c" "$here/../src/cdom_audio.c" "$here/../src/cdom_random.c" "$here/../src/mini.c"
"$CC" --target=wasm32 -Oz -nostdlib -mbulk-memory -Wall -Wextra -Werror -Wl,--no-entry -Wl,--strip-all \
  -o "$here/out/click.wasm" "$here/../demo/click.c" "$here/../src/cdom.c" "$here/../src/cdom_audio.c" "$here/../src/mini.c"
cp "$here/listfuzz.html" "$here/unit.html" "$here/../demo/click.html" "$here/../shim/cdom.js" "$here/out/"
