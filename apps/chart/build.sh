#!/bin/bash
# Builds the chart demo into apps/chart/out (gitignored): cdtc compiles chart.cdt into
# out/gen/chart.{h,c}; app.wasm from app.c + the generated C + cdom.c at -Oz; the shim minified
# (terser, npm install at the root) into out/cdom.min.js, which the server inlines into each
# page; and the native server (app + cdom.c + the C sink + httpd + the page shell).
# Usage: apps/chart/build.sh, then apps/chart/out/server (port 8705)
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
top="$here/../.."
CC="${WASM_CC:-/opt/homebrew/opt/llvm/bin/clang}"
OPT="${OPT:--Oz}"
out="$here/out"
gen="$out/gen"
mkdir -p "$gen"
nice -n 10 cc -O2 -Wall -Wextra -Werror -o "$gen/cdtc" "$top/tools/cdtc.c"
"$gen/cdtc" "$here/chart.cdt" -o "$gen/chart"
nice -n 10 "$CC" --target=wasm32 $OPT -nostdlib -mbulk-memory -Wall -Wextra -Werror \
  -Wl,--no-entry -Wl,--strip-all -I "$top/src" -I "$gen" -o "$out/app.wasm" \
  "$here/app.c" "$gen/chart.c" "$top/src/cdom.c" "$top/src/mini.c"
"$top/node_modules/.bin/terser" "$top/shim/cdom.js" -c passes=2 -m -o "$out/cdom.min.js"
nice -n 10 cc -O2 -Wall -Wextra -Werror -I "$top/src" -I "$gen" -o "$out/server" \
  "$here/server.c" "$here/app.c" "$gen/chart.c" "$top/src/cdom.c" "$top/src/cdom_html.c" \
  "$top/src/httpd.c" "$top/src/ssr_page.c"
printf 'chart %s: app.wasm %s bytes, %s gz\n' "$OPT" "$(wc -c < "$out/app.wasm" | tr -d ' ')" "$(gzip -9c "$out/app.wasm" | wc -c | tr -d ' ')"
