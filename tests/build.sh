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
  "$CC" "$opt" "${CDOM_CFLAGS[@]}" "${W[@]}" "${CDOM_LDFLAGS[@]}" -o "$o.linked" "$@"
  "$CDOM_LITS" "$o.linked" "$o" "$o.lits" # the string table out of the module, into o.lits, as cdom.mk does
  rm -f "$o.linked"
}
# shim/cdom.min.js, which apps inline, must be what cdom.js makes now (tools/minshim.mjs).
node "$here/../tools/minshim.mjs" check
wasm -O2 "$here/out/listfuzz.wasm" "$here/listfuzz.c" "$here/../src/cdom.c" "$here/../src/mini.c"
wasm -O2 "$here/out/unit.wasm" "$here/unit.c" "$here/../src/cdom.c" "$here/../src/cdom_net.c" "$here/../src/cdom_store.c" "$here/../src/cdom_audio.c" "$here/../src/cdom_random.c" "$here/../src/cdom_url.c" "$here/../src/cdom_locale.c" "$here/../src/cdom_share.c" "$here/../src/cdom_fonts.c" "$here/../src/cdom_ui.c" "$here/../src/mini.c"
wasm -Oz "$here/out/click.wasm" -Wl,--strip-all "$here/../demo/click.c" "$here/../src/cdom.c" "$here/../src/cdom_audio.c" "$here/../src/mini.c"
# Every page inlines the unminified shim at its /*CDOM_SHIM*/ line (tools/cdpage), so the tests run the
# readable shim/cdom.js that tools/mutate.py mutates.
shim="$here/../shim/cdom.js"
# page <in.html> <out.html> <module.wasm>: the string table of that module goes before the shim.
page() { "$CDOM_PAGE_TOOL" --page "$1" --out "$2" --shim "$shim" --lits "$3.lits"; }
page "$here/unit.html" "$here/out/unit.html" "$here/out/unit.wasm"
# The same cases in cd_state's check build (-DCD_STATE_CHECK), whose trap cases unit.mjs opens on unit_check.html.
wasm -O2 "$here/out/unit_check.wasm" -DCD_STATE_CHECK "$here/unit.c" "$here/../src/cdom.c" "$here/../src/cdom_net.c" "$here/../src/cdom_store.c" "$here/../src/cdom_audio.c" "$here/../src/cdom_random.c" "$here/../src/cdom_url.c" "$here/../src/cdom_locale.c" "$here/../src/cdom_share.c" "$here/../src/cdom_fonts.c" "$here/../src/cdom_ui.c" "$here/../src/mini.c"
page "$here/unit.html" "$here/out/unit_check.html" "$here/out/unit_check.wasm"
page "$here/../demo/click.html" "$here/out/click.html" "$here/out/click.wasm"
# The fuzz page and the legacy page go through the boot, so LEGACY=1 runs them on the wasm2js build.
page "$here/listfuzz.html" "$here/out/listfuzz.shim.html" "$here/out/listfuzz.wasm"
node "$legacy" page "$here/out/listfuzz.shim.html" "$here/out/listfuzz.html"
node "$legacy" shim "$here/out/cdom.legacy.js"
node "$legacy" app "$here/out/listfuzz.wasm" "$here/out/listfuzz.wasm.js"
# The _try allocations at a small cap, and on an app's own allocator (tools/alloc.mjs, in Node).
wasm -O2 "$here/out/alloc_try.wasm" -DCDOM_HEAP_PAGES_MAX=40 "$here/alloc_try.c" "$here/../src/cdom.c" "$here/../src/mini.c"
wasm -O2 "$here/out/alloc_own.wasm" -DOWN_ALLOC "$here/alloc_try.c" "$here/../src/cdom.c" "$here/../src/mini.c"
wasm -O2 "$here/out/canvas.wasm" "$here/canvas.c" "$here/../src/cdom.c" "$here/../src/mini.c"
page "$here/canvas.html" "$here/out/canvas.html" "$here/out/canvas.wasm"
wasm -O2 "$here/out/host.wasm" "$here/host.c" "$here/../src/cdom.c" "$here/../src/mini.c"
page "$here/host.html" "$here/out/host.html" "$here/out/host.wasm"
wasm -O2 "$here/out/legacy.wasm" "$here/legacy.c" "$here/../src/cdom.c" "$here/../src/mini.c"
node "$legacy" app "$here/out/legacy.wasm" "$here/out/legacy.wasm.js"
page "$here/legacy.html" "$here/out/legacy.shim.html" "$here/out/legacy.wasm"
node "$legacy" page "$here/out/legacy.shim.html" "$here/out/legacy.html"
