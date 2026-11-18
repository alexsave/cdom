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
# cdom.mk's flag set, the one apps build with (tools/cdomflags.sh), and its gate: no build script of
# cdom's spells a flag of that set itself.
. "$here/../tools/cdomflags.sh"
cdom_flags_gate
W=(-Wall -Wextra -Werror)
wasm() { # wasm <opt> <out.wasm> <sources and extra flags...>: a cdom module, as cdom.mk builds an app's
  local opt="$1" o="$2"; shift 2
  "$CC" "$opt" "${CDOM_CFLAGS[@]}" "${W[@]}" "${CDOM_LDFLAGS[@]}" -o "$o" "$@"
}
# shim/cdom.min.js, which apps inline, must be what cdom.js makes now (tools/minshim.mjs).
node "$here/../tools/minshim.mjs" check
wasm -O2 "$here/out/listfuzz.wasm" "$here/listfuzz.c" "$here/../src/cdom.c" "$here/../src/mini.c"
wasm -O2 "$here/out/unit.wasm" "$here/unit.c" "$here/../src/cdom.c" "$here/../src/cdom_net.c" "$here/../src/cdom_store.c" "$here/../src/cdom_audio.c" "$here/../src/cdom_random.c" "$here/../src/cdom_url.c" "$here/../src/cdom_locale.c" "$here/../src/cdom_share.c" "$here/../src/cdom_fonts.c" "$here/../src/mini.c"
wasm -Oz "$here/out/click.wasm" -Wl,--strip-all "$here/../demo/click.c" "$here/../src/cdom.c" "$here/../src/cdom_audio.c" "$here/../src/mini.c"
# Every page inlines the unminified shim at its /*CDOM_SHIM*/ line (tools/page.awk), so the tests run the
# readable shim/cdom.js that tools/mutate.py mutates.
shim="$here/../shim/cdom.js"
page() { awk -v shim="$shim" -f "$here/../tools/page.awk" "$1" > "$2.tmp" && mv "$2.tmp" "$2"; }
page "$here/unit.html" "$here/out/unit.html"
page "$here/../demo/click.html" "$here/out/click.html"
# The fuzz page and the legacy page go through the boot, so LEGACY=1 runs them on the wasm2js build.
page "$here/listfuzz.html" "$here/out/listfuzz.shim.html"
node "$legacy" page "$here/out/listfuzz.shim.html" "$here/out/listfuzz.html"
node "$legacy" shim "$here/out/cdom.legacy.js"
node "$legacy" app "$here/out/listfuzz.wasm" "$here/out/listfuzz.wasm.js"
wasm -O2 "$here/out/canvas.wasm" "$here/canvas.c" "$here/../src/cdom.c" "$here/../src/mini.c"
page "$here/canvas.html" "$here/out/canvas.html"
wasm -O2 "$here/out/host.wasm" "$here/host.c" "$here/../src/cdom.c" "$here/../src/mini.c"
page "$here/host.html" "$here/out/host.html"
wasm -O2 "$here/out/legacy.wasm" "$here/legacy.c" "$here/../src/cdom.c" "$here/../src/mini.c"
node "$legacy" app "$here/out/legacy.wasm" "$here/out/legacy.wasm.js"
page "$here/legacy.html" "$here/out/legacy.shim.html"
node "$legacy" page "$here/out/legacy.shim.html" "$here/out/legacy.html"
