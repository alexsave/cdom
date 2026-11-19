#!/bin/bash
# The differential test of the image module's wasm SIMD loops (tests/image/simd.c, simd.mjs): builds one wasm module
# that holds both each SIMD loop and its scalar counterpart (libjpeg's own jidctred.c and jdcolor.c without SIMD,
# cdom_image_loops.c's scalar bodies), and runs it in node. Usage: tests/image/simd.sh [OUTDIR]
# Its mutants are in tools/mutate.py (the SIMD section): each must make one loop's line FAIL.
# The compiler and the flag set are cdom.mk's (tools/cdomflags.sh), as an app builds with; the reference files (jidctred.c,
# jdcolor.c) are built scalar, with no SIMD flag, as the scalar module builds them.
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"; top="$(cd "$here/../.." && pwd)"
out="${1:-$top/tests/out/image-simd}"
mkdir -p "$out"
. "$top/tools/cdomflags.sh"
LJT="$top/third_party/libjpeg-turbo-3.2.1/src"
CC="$(cdom_mk_var WASM_CC)"
src="$top/src"
base=(-O2 "${CDOM_CFLAGS[@]}" -DNO_GETENV -DNO_PUTENV -DMULTIPLIER=int)
o="$out/obj"; rm -rf "$o"; mkdir -p "$o"
cc() { "$CC" "${base[@]}" "$@" || exit 1; }
cc -msimd128 -Wall -Wextra -Werror -idirafter "$LJT" -I"$src/jsimd" -I"$here" -c -o "$o/simd.o" "$here/simd.c"
cc -msimd128 -Wall -Wextra -Werror -idirafter "$LJT" -DCDOM_IMAGE_SIMD=1 -c -o "$o/loops.o" "$src/cdom_image_loops.c"
cc -msimd128 -Wall -Wextra -Werror -idirafter "$LJT" -DCDOM_JSIMD -DALIGN_SIZE=8 -c -o "$o/jsimd.o" "$src/jsimd/jsimd_wasm.c"
for f in wrapper/jidctred-8 wrapper/jidctint-8 wrapper/jdcolor-8 wrapper/jutils-8; do cc -w -I"$LJT" -c -o "$o/$(basename "$f").o" "$LJT/$f.c"; done
"$CC" "${CDOM_CFLAGS[@]}" "${CDOM_LDFLAGS[@]}" -Wl,--initial-memory=16777216 -o "$out/simd.wasm" "$o"/*.o || exit 1
node "$here/simd.mjs" "$out/simd.wasm"
