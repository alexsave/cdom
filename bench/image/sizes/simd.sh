#!/bin/bash
# The image module's bytes with SIMD (docs/image-simd.md, "Size"): bench/image/sizes/app.c, stripped, raw and brotli -q 11 (node's
# zlib, the same encoder), at LEVEL oz and o2, for CDOM_IMAGE_SIMD none, simd128 and simde, with both formats (and no image code at -Oz).
# Usage: bench/image/sizes/simd.sh      (WASM_CC picks the clang)
set -euo pipefail
cd "$(dirname "$0")"
printf 'build\traw\tbrotli\n'
for v in "none none oz" "full none oz" "full simd128 oz" "full simde oz" "jpeg none oz" "jpeg simde oz" "full none o2" "full simd128 o2" "full simde o2"; do
  set -- $v
  make -s V="$1" SIMD="$2" LEVEL="$3" all > /dev/null
  d="out-$1-js"; [ "$2" = none ] || d="$d-$2"; d="$d-$3"
  node -e 'const fs=require("fs"),z=require("zlib");const b=fs.readFileSync(process.argv[1]);const c=z.brotliCompressSync(b,{params:{[z.constants.BROTLI_PARAM_QUALITY]:11,[z.constants.BROTLI_PARAM_LGWIN]:24,[z.constants.BROTLI_PARAM_SIZE_HINT]:b.length}});console.log(process.argv[2]+"\t"+b.length+"\t"+c.length)' "$d/app.wasm" "$1 $2 $3"
done
