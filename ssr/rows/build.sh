#!/bin/bash
# Builds the rows SSR demo: the native server (app + cdom.c + cdom_html.c + httpd.c), app.wasm
# from the same app sources, the shim, the benchmark CSS, and /csr/ (the plain client-rendered
# page from the same build).
#   SSR_APP unset: the hand-written bench/cdom-keyed/src/app.c into ssr/rows/out (port 8701)
#   SSR_APP=tpl:   the templated bench/cdom-tpl (rows.cdt compiled by cdtc) into ssr/rows-tpl/out (port 8704)
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
top="$here/../.."
CC="${WASM_CC:-/opt/homebrew/opt/llvm/bin/clang}"
case "${SSR_APP:-}" in
  "") out="$here/out"; app=("$top/bench/cdom-keyed/src/app.c"); inc=() ;;
  tpl)
    out="$top/ssr/rows-tpl/out"
    mkdir -p "$out/gen"
    nice -n 10 cc -O2 -Wall -Wextra -Werror -o "$out/gen/cdtc" "$top/tools/cdtc.c"
    "$out/gen/cdtc" "$top/bench/cdom-tpl/rows.cdt" -o "$out/gen/rows"
    app=("$top/bench/cdom-tpl/app.c" "$out/gen/rows.c"); inc=(-I "$top/src" -I "$out/gen") ;;
  *) echo "SSR_APP must be unset or tpl" >&2; exit 2 ;;
esac
mkdir -p "$out/csr" "$out/css/bootstrap/dist/css" "$out/css/bootstrap/dist/fonts"
nice -n 10 cc -O2 -Wall -Wextra -Werror ${inc[@]+"${inc[@]}"} -o "$out/server" \
  "$here/server.c" "${app[@]}" "$top/src/cdom.c" "$top/src/cdom_html.c" "$top/src/httpd.c"
nice -n 10 "$CC" --target=wasm32 -O2 -nostdlib -mbulk-memory -Wall -Wextra -Werror ${inc[@]+"${inc[@]}"} \
  -Wl,--no-entry -Wl,--strip-all -o "$out/app.wasm" "${app[@]}" "$top/src/cdom.c" "$top/src/mini.c"
cp "$top/shim/cdom.js" "$out/cdom.js"
cp "$out/app.wasm" "$out/csr/"
# the benchmark page inlines the shim at /*CDOM_SHIM*/ (bench/build.sh minifies it; here it stays readable)
sed -e "/\/\*CDOM_SHIM\*\//{r $top/shim/cdom.js" -e "d;}" "$top/bench/cdom-keyed/index.html" > "$out/csr/index.html"
css="$top/jfb/css"
cp "$css/currentStyle.css" "$css/main.css" "$out/css/"
cp "$css/bootstrap/dist/css/bootstrap.min.css" "$out/css/bootstrap/dist/css/"
cp "$css/bootstrap/dist/fonts/"glyphicons-halflings-regular.* "$out/css/bootstrap/dist/fonts/"
