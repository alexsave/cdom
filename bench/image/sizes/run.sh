#!/bin/bash
# The image module's bytes (docs/image-design.md, "Size"): bench/image/sizes/app.c at cdom.mk's -Oz, stripped, raw and
# brotli -q 11, with no image code, with both formats (each way out of a libjpeg error) and with one format each, and
# (the last three lines) with the hand-written wasm SIMD loops, CDOM_IMAGE_SIMD=1 (docs/image-simd-hand.md).
set -euo pipefail
cd "$(dirname "$0")"
printf 'build\traw\tbrotli\n'
for v in "none js 0" "full js 0" "full eh 0" "jpeg js 0" "png js 0" "full js 1" "jpeg js 1" "png js 1"; do
  set -- $v
  if [ "${SIZES_EH:-1}" = 0 ] && [ "$2" = eh ]; then continue; fi   # SIZES_EH=0: a clang without clang 23's wasm EH lowering
  make -s V="$1" SJLJ="$2" SIMD="$3" all > /dev/null
  f="out-$1-$2$([ "$3" = 1 ] && echo -simd || true)/app.wasm"
  printf '%s %s%s\t%s\t%s\n' "$1" "$2" "$([ "$3" = 1 ] && echo " simd" || true)" "$(wc -c < "$f" | tr -d ' ')" "$(brotli -q 11 -c "$f" | wc -c | tr -d ' ')"
done
