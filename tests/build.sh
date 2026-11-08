#!/bin/bash
# Builds the keyed-diff fuzz page, the unit page and the click demo into tests/out (served by
# tools/fuzz.mjs and tools/unit.mjs, which taps the demo too) and the canvas and host pages (tools/canvas.mjs,
# tools/host.mjs).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
CC="${WASM_CC:-/opt/homebrew/opt/llvm/bin/clang}"
legacy="$here/../tools/legacy/build.mjs"
[ -d "$here/../tools/legacy/node_modules" ] || (cd "$here/../tools/legacy" && npm ci --no-audit --no-fund)
mkdir -p "$here/out"
# shim/cdom.min.js, which apps inline, must be what cdom.js makes now (tools/minshim.mjs).
node "$here/../tools/minshim.mjs" check
"$CC" --target=wasm32 -O2 -nostdlib -mbulk-memory -Wall -Wextra -Werror -Wl,--no-entry \
  -o "$here/out/listfuzz.wasm" "$here/listfuzz.c" "$here/../src/cdom.c" "$here/../src/mini.c"
"$CC" --target=wasm32 -O2 -nostdlib -mbulk-memory -Wall -Wextra -Werror -Wl,--no-entry -Wl,--export-memory \
  -o "$here/out/unit.wasm" "$here/unit.c" "$here/../src/cdom.c" "$here/../src/cdom_net.c" "$here/../src/cdom_store.c" "$here/../src/cdom_audio.c" "$here/../src/cdom_random.c" "$here/../src/cdom_url.c" "$here/../src/cdom_locale.c" "$here/../src/cdom_share.c" "$here/../src/cdom_fonts.c" "$here/../src/mini.c"
"$CC" --target=wasm32 -Oz -nostdlib -mbulk-memory -Wall -Wextra -Werror -Wl,--no-entry -Wl,--strip-all \
  -o "$here/out/click.wasm" "$here/../demo/click.c" "$here/../src/cdom.c" "$here/../src/cdom_audio.c" "$here/../src/mini.c"
cp "$here/unit.html" "$here/../demo/click.html" "$here/../shim/cdom.js" "$here/out/"
# The fuzz page and the legacy page go through the boot, so LEGACY=1 runs them on the wasm2js build.
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
