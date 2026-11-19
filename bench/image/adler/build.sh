#!/bin/bash
# The adler32 check's cost (docs/image-design.md, A1): the bench app with cdom_image_png.c changed to stop reading at
# its last row (the zlib stream's end and checksum never read, as raceclub's PR did), from the objects make -C bench/image
# built for the js page. Writes out/app.wasm, for bench/image/sjlj.mjs's harness (adler.mjs).
set -e
top="$(cd "$(dirname "$0")/../../.." && pwd)"
cd "$(dirname "$0")"
mkdir -p out
. "$top/tools/cdomflags.sh"
CC=/opt/homebrew/opt/llvm/bin/clang
B="$top/bench/image/build-js/app.wasm.cdom/image"
sed 's/if (ts == TINFL_STATUS_DONE) { ended = 1; break; }/if (ts == TINFL_STATUS_DONE || done == h) { ended = 1; break; }/' "$top/src/cdom_image_png.c" > out/png_stop.c
grep -q 'done == h) { ended = 1' out/png_stop.c
read -r -a OWNF <<< "$(cdom_mk_var CDOM_IMAGE_OWN_CFLAGS)"
"$CC" -Oz "${CDOM_CFLAGS[@]}" "${OWNF[@]}" -I"$top/src" -c -o out/png_stop.o out/png_stop.c
"$CC" -Oz "${CDOM_CFLAGS[@]}" "${CDOM_LDFLAGS[@]}" -o out/app.linked "$top/bench/image/app.c" "$top/src/cdom.c" "$top/src/mini.c" \
  "$B"/own/cdom_image.cdom.o "$B"/own/cdom_image_jpeg.cdom.o out/png_stop.o "$B"/vendor/*.o "$B"/miniz/*.o "$B"/sjlj/*.o
"$CDOM_LITS" out/app.linked out/app.wasm out/app.wasm.lits
