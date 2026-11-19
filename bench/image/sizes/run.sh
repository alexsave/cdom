#!/bin/bash
# The image module's bytes (docs/image-design.md, "Size"): bench/image/sizes/app.c at cdom.mk's -Oz, stripped, raw and
# brotli -q 11, with no image code, with both formats (each way out of a libjpeg error) and with one format each,
# at each optimization level (oz, o2, o3, mix: third_party at -O2, the rest at -Oz), and (the last three lines) with the
# hand-written wasm SIMD loops, CDOM_IMAGE_SIMD=1 (docs/image-simd-hand.md).
set -euo pipefail
cd "$(dirname "$0")"
printf 'build\traw\tbrotli\n'
for v in "none js oz 0" "full js oz 0" "full eh oz 0" "jpeg js oz 0" "png js oz 0" "none js o2 0" "full js o2 0" "none js o3 0" "full js o3 0" "none js mix 0" "full js mix 0" "full js oz 1" "jpeg js oz 1" "png js oz 1"; do
  set -- $v
  if [ "${SIZES_EH:-1}" = 0 ] && [ "$2" = eh ]; then continue; fi   # SIZES_EH=0: a clang without clang 23's wasm EH lowering
  s=""; [ "$4" = 1 ] && s="-simd"
  make -s V="$1" SJLJ="$2" LEVEL="$3" SIMD="$4" all > /dev/null
  f="out-$1-$2-$3$s/app.wasm"
  printf '%s %s %s%s\t%s\t%s\n' "$1" "$2" "$3" "$s" "$(wc -c < "$f" | tr -d ' ')" "$(brotli -q 11 -c "$f" | wc -c | tr -d ' ')"
done
