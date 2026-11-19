#!/bin/bash
# The image module's bytes (docs/image-design.md, "Size"): bench/image/sizes/app.c at cdom.mk's -Oz, stripped, raw and
# brotli -q 11, with no image code, with both formats (each way out of a libjpeg error) and with one format each.
set -euo pipefail
cd "$(dirname "$0")"
printf 'build\traw\tbrotli\n'
for v in "none js" "full js" "full eh" "jpeg js" "png js"; do
  set -- $v
  make -s V="$1" SJLJ="$2" all > /dev/null
  f="out-$1-$2/app.wasm"
  printf '%s %s\t%s\t%s\n' "$1" "$2" "$(wc -c < "$f" | tr -d ' ')" "$(brotli -q 11 -c "$f" | wc -c | tr -d ' ')"
done
