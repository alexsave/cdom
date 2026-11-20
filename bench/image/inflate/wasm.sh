#!/bin/bash
# Builds bench/image/inflate/wasm.c into OUT/*.wasm with cdom.mk's wasm flags (clang --target=wasm32, cdom's libc
# headers, bulk memory), at LEVEL (default -Oz, cdom.mk's): base.wasm (tinfl and cdom's inflate, scalar),
# simd.wasm (the same, cdom's inflate with -msimd128), and when given ZLIB=<zlib-1.3.1 dir> and
# ZNG=<zlib-ng source dir>:<a native CMake build dir with WITH_OPTIM=OFF, for zconf-ng.h and zlib-ng.h>,
# zlib.wasm and zng.wasm (generic C; zlib-ng's own functable, no runtime CPU detection).
# Usage: bench/image/inflate/wasm.sh OUT
set -euo pipefail
top="$(cd "$(dirname "$0")/../../.." && pwd)"
out="$1"; mkdir -p "$out"
CC="${WASM_CC:-clang}"
L="${LEVEL:--Oz}"
F=(--target=wasm32 -std=c23 -nostdlib -mbulk-memory -isystem "$top/src/libc" -I"$top/src" -isystem "$top/third_party/miniz-3.1.2" "$L")
LD=(-Wl,--no-entry -Wl,--export-memory -Wl,--stack-first -Wl,-z,stack-size=65536 -Wl,--strip-all)
o="$(mktemp -d)"
"$CC" "${F[@]}" -w -I"$top/third_party/miniz-3.1.2" -include "$top/src/cdom_image_miniz.h" -c -o "$o/tinfl.o" "$top/third_party/miniz-3.1.2/tinfl.c"
"$CC" "${F[@]}" -c -o "$o/inflate.o" "$top/src/cdom_image_inflate.c"
"$CC" "${F[@]}" -msimd128 -c -o "$o/inflate_simd.o" "$top/src/cdom_image_inflate.c"
"$CC" "${F[@]}" -c -o "$o/wasm.o" "$top/bench/image/inflate/wasm.c"
"$CC" "${F[@]}" "${LD[@]}" -o "$out/base.wasm" "$o/wasm.o" "$o/tinfl.o" "$o/inflate.o"
"$CC" "${F[@]}" "${LD[@]}" -o "$out/simd.wasm" "$o/wasm.o" "$o/tinfl.o" "$o/inflate_simd.o"
if [ -n "${ZLIB:-}" ]; then
  mkdir -p "$o/z"
  for f in adler32 crc32 inflate inftrees inffast zutil; do "$CC" "${F[@]}" -w -DZ_SOLO -c -o "$o/z/$f.o" "$ZLIB/$f.c"; done
  "$CC" "${F[@]}" -DWITH_ZLIB -DZ_SOLO -I"$ZLIB" -c -o "$o/wasm_z.o" "$top/bench/image/inflate/wasm.c"
  "$CC" "${F[@]}" "${LD[@]}" -o "$out/zlib.wasm" "$o/wasm_z.o" "$o/tinfl.o" "$o/inflate.o" "$o/z"/*.o
fi
if [ -n "${ZNG:-}" ]; then
  src="${ZNG%%:*}"; cfg="${ZNG##*:}"; mkdir -p "$o/n" "$o/inc/sys"
  printf 'typedef long long off_t;\n' > "$o/inc/sys/types.h"   # the headers zlib-ng includes that cdom's libc has not
  : > "$o/inc/unistd.h"
  printf "#include <stdlib.h>\nstatic inline long long llabs(long long v) { return v < 0 ? -v : v; }\nvoid *aligned_alloc(size_t a, size_t n);\n" > "$o/inc/zng_llabs.h"
  D=(-isystem "$o/inc" -include "$o/inc/zng_llabs.h" -DHAVE_ALIGNED_ALLOC -DDISABLE_RUNTIME_CPU_DETECTION -DHAVE_BUILTIN_CTZ -DHAVE_BUILTIN_CTZLL -DZLIBNG_NATIVE_API -DNO_MEDIUM_STRATEGY -DNO_QUICK_STRATEGY -I"$cfg" -I"$src")
  for f in adler32 inflate inftrees zutil functable crc32 crc32_braid_comb arch/generic/adler32_c arch/generic/adler32_fold_c arch/generic/chunkset_c arch/generic/compare256_c arch/generic/crc32_braid_c arch/generic/crc32_fold_c arch/generic/slide_hash_c; do
    "$CC" "${F[@]}" -w "${D[@]}" -c -o "$o/n/$(basename $f).o" "$src/$f.c"
  done
  "$CC" "${F[@]}" -DWITH_ZNG "${D[@]}" -c -o "$o/wasm_n.o" "$top/bench/image/inflate/wasm.c"
  "$CC" "${F[@]}" "${LD[@]}" -Wl,--gc-sections -o "$out/zng.wasm" "$o/wasm_n.o" "$o/tinfl.o" "$o/inflate.o" "$o/n"/*.o
fi
rm -rf "$o"
ls -l "$out"/*.wasm
