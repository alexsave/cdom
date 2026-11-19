#!/bin/bash
# The spike's emscripten-lowering build (bench/image/sjlj.mjs): the bench app with guard.c and rt.c in place of
# cdom_image_sjlj.c, from the objects make -C bench/image built for the js page. Writes out/app.wasm.
set -e
top="$(cd "$(dirname "$0")/../../.." && pwd)"
cd "$(dirname "$0")"
mkdir -p out
. "$top/tools/cdomflags.sh"
CC=/opt/homebrew/opt/llvm/bin/clang
B="$top/bench/image/build-js/app.wasm.cdom/image"
"$CC" -Oz "${CDOM_CFLAGS[@]}" -mllvm -enable-emscripten-sjlj -c -o out/guard.o guard.c
"$CC" -Oz "${CDOM_CFLAGS[@]}" -c -o out/rt.o rt.c
"$CC" -Oz "${CDOM_CFLAGS[@]}" "${CDOM_LDFLAGS[@]}" -Wl,--export-table -o out/app.linked "$top/bench/image/app.c" "$top/src/cdom.c" "$top/src/mini.c" "$B"/own/*.o "$B"/vendor/*.o "$B"/miniz/*.o out/guard.o out/rt.o
"$CDOM_LITS" out/app.linked out/app.wasm out/app.wasm.lits

