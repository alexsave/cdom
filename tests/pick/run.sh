#!/bin/bash
# The SIMD pick (cdom.mk's APP_SIMD; README.md, "Starting an app"): builds tests/pick/app.mk's app (bench/image's, with
# APP_SIMD := 1) into tests/out/pick as four pages, bundled and separate, each with the readable shim (shim/cdom.js, which
# tools/mutate.py mutates) and with the minified one apps inline, writes bench/image's inputs beside them, and runs
# tools/pick.mjs. Usage: tests/pick/run.sh
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"; top="$(cd "$here/../.." && pwd)"
out="$top/tests/out/pick"
mkdir -p "$out"
cd "$out" # cdom.mk bundles the stylesheets under src/ where make runs: there is none here
mk() { make -s --no-print-directory -f "$here/app.mk" OUT="$out" "$@" all; }
mk PAGE=bundled CDOM_SHIM="$top/shim/cdom.js"
mk PAGE=separate APP_WASM_SEPARATE=1 CDOM_SHIM="$top/shim/cdom.js"
mk PAGE=bundled-min
mk PAGE=separate-min APP_WASM_SEPARATE=1
[ -f "$out/inputs/p12.png" ] || python3 -I "$top/bench/image/inputs.py" "$out/inputs" > /dev/null
node "$top/tools/pick.mjs" "$out"
