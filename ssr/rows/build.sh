#!/bin/bash
# Builds the rows SSR demo into ssr/rows/out: the native server (app.c + cdom.c +
# cdom_html.c + httpd.c), app.wasm from the same app.c, the shim, the benchmark CSS,
# and /csr/ (the plain client-rendered page from the same build). Port 8701.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
top="$here/../.."
CC="${WASM_CC:-/opt/homebrew/opt/llvm/bin/clang}"
out="$here/out"
mkdir -p "$out/csr" "$out/css/bootstrap/dist/css" "$out/css/bootstrap/dist/fonts"
app="$top/bench/cdom-keyed/src/app.c"
nice -n 10 cc -O2 -Wall -Wextra -Werror -o "$out/server" \
  "$here/server.c" "$app" "$top/src/cdom.c" "$top/src/cdom_html.c" "$top/src/httpd.c"
nice -n 10 "$CC" --target=wasm32 -O2 -nostdlib -mbulk-memory -Wall -Wextra -Werror \
  -Wl,--no-entry -Wl,--strip-all -o "$out/app.wasm" "$app" "$top/src/cdom.c" "$top/src/mini.c"
cp "$top/shim/cdom.js" "$out/cdom.js"
cp "$out/app.wasm" "$out/cdom.js" "$out/csr/"
cp "$top/bench/cdom-keyed/index.html" "$out/csr/index.html"
css="$top/jfb/css"
cp "$css/currentStyle.css" "$css/main.css" "$out/css/"
cp "$css/bootstrap/dist/css/bootstrap.min.css" "$out/css/bootstrap/dist/css/"
cp "$css/bootstrap/dist/fonts/"glyphicons-halflings-regular.* "$out/css/bootstrap/dist/fonts/"
