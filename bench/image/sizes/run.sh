#!/bin/bash
# The image module's bytes (docs/image-design.md, "Size"): bench/image/sizes/app.c at cdom.mk's -Oz, stripped, raw and
# brotli -q 11, with no image code, with both formats (each way out of a libjpeg error) and with one format each,
# and at each optimization level (oz, o2, o3, mix: third_party at -O2, the rest at -Oz).
set -euo pipefail
cd "$(dirname "$0")"
printf 'build\traw\tbrotli\n'
for v in "none js oz" "full js oz" "full eh oz" "jpeg js oz" "png js oz" "none js o2" "full js o2" "none js o3" "full js o3" "none js mix" "full js mix"; do
  set -- $v
  make -s V="$1" SJLJ="$2" LEVEL="$3" all > /dev/null
  f="out-$1-$2-$3/app.wasm"
  printf '%s %s %s\t%s\t%s\n' "$1" "$2" "$3" "$(wc -c < "$f" | tr -d ' ')" "$(brotli -q 11 -c "$f" | wc -c | tr -d ' ')"
done
