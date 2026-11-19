#!/bin/bash
# The differential test of the image module's wasm SIMD loops (tests/image/simd.c, simd.mjs): builds one wasm module
# that holds both each SIMD loop and its scalar counterpart (libjpeg's own jidctred.c and jdcolor.c without SIMD,
# cdom_image_loops.c's scalar bodies), and runs it in node. Usage: tests/image/simd.sh [OUTDIR]
# SIMD_MUTANT=<name> (tests/image/simd_mutants.sh) builds with one planted fault; the run must then fail.
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"; top="$(cd "$here/../.." && pwd)"
out="${1:-$top/tests/out/image-simd}"
mkdir -p "$out"
LJT="$top/third_party/libjpeg-turbo-3.2.1/src"
CC="${WASM_CC:-clang}"
base=(--target=wasm32 -std=c23 -nostdlib -mbulk-memory -msimd128 -O2 -isystem "$top/src/libc" -I"$top/src" -DNO_GETENV -DNO_PUTENV -DMULTIPLIER=int)
src="$top/src"
if [ -n "${SIMD_MUTANT:-}" ]; then   # a copy of the sources with one fault planted (simd_mutants.sh), in place of the real ones
  src="$out/mutant_src"; rm -rf "$src"; mkdir -p "$src"; cp -r "$top/src/." "$src/"
  python3 -I "$here/simd_mutants.py" "$src" "$SIMD_MUTANT" || exit 2
  base=(--target=wasm32 -std=c23 -nostdlib -mbulk-memory -msimd128 -O2 -isystem "$src/libc" -I"$src" -DNO_GETENV -DNO_PUTENV -DMULTIPLIER=int)
fi
o="$out/obj"; rm -rf "$o"; mkdir -p "$o"
cc() { "$CC" "${base[@]}" "$@" || exit 1; }
cc -Wall -Wextra -Werror -isystem "$LJT" -I"$src/jsimd" -I"$here" -c -o "$o/simd.o" "$here/simd.c"
cc -Wall -Wextra -Werror -isystem "$LJT" -DCDOM_IMAGE_SIMD=1 -c -o "$o/loops.o" "$src/cdom_image_loops.c"
cc -Wall -Wextra -Werror -isystem "$LJT" -DCDOM_JSIMD -DALIGN_SIZE=8 -c -o "$o/jsimd.o" "$src/jsimd/jsimd_wasm.c"
for f in wrapper/jidctred-8 wrapper/jdcolor-8 wrapper/jutils-8; do cc -w -I"$LJT" -c -o "$o/$(basename "$f").o" "$LJT/$f.c"; done
"$CC" --target=wasm32 -nostdlib -Wl,--no-entry -Wl,--initial-memory=16777216 -Wl,--stack-first -Wl,-z,stack-size=65536 -o "$out/simd.wasm" "$o"/*.o || exit 1
node "$here/simd.mjs" "$out/simd.wasm"
