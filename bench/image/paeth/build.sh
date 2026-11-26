#!/bin/bash
# The unfilter microbench's modules (bench.c): out/simd-lag2.wasm (the SIMD build as it ships), out/simd-lag1.wasm (the
# wavefront with a lag of 1), out/scalar.wasm (the scalar build). cdom.mk's compiler, flag set and image flags, at -Oz.
# Usage: bench/image/paeth/build.sh; then node bench/image/paeth/run.mjs
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"; top="$(cd "$here/../../.." && pwd)"
. "$top/tools/cdomflags.sh"
CC="$(cdom_mk_var WASM_CC)"
read -r -a OWNF <<< "$(cdom_mk_var CDOM_IMAGE_OWN_CFLAGS)"
mkdir -p "$here/out"
one() { # name, then flags
    local n="$1"; shift
    "$CC" -Oz "${CDOM_CFLAGS[@]}" "${OWNF[@]}" "$@" -Wall -Wextra -Werror "${CDOM_LDFLAGS[@]}" -o "$here/out/$n.wasm" \
      "$here/bench.c" "$top/src/cdom_image_loops.c"
}
one simd-lag2 -msimd128 -DCDOM_IMAGE_SIMD=1
one simd-lag1 -msimd128 -DCDOM_IMAGE_SIMD=1 -DCDOM_PAETH_LAG=1
one scalar -DCDOM_IMAGE_SIMD=0
echo "built $(ls "$here/out" | tr '\n' ' ')"
