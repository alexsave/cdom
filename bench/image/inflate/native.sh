#!/bin/bash
# Builds bench/image/inflate/native.c, natively at -O2, with tinfl (third_party/miniz, as the module builds it) and
# cdom's inflate always, and zlib, zlib-ng and libdeflate when their build trees are given (none is vendored: the
# doc names their versions and sha256):
#   ZLIB=<zlib-1.3.1 dir with libz131.a>  ZNG=<zlib-ng source dir>:<its build dir>  LD=<libdeflate dir>:<its build dir>
# Usage: bench/image/inflate/native.sh <out binary>
set -euo pipefail
top="$(cd "$(dirname "$0")/../../.." && pwd)"
out="$1"; shift
D=(); L=()
if [ -n "${ZLIB:-}" ]; then D+=(-DHAVE_ZLIB -I"${ZLIB%%:*}"); L+=("${ZLIB##*:}"); fi
if [ -n "${ZNG:-}" ]; then D+=(-DHAVE_ZNG); L+=("${ZNG##*:}/libz-ng.a"); fi
if [ -n "${LD:-}" ]; then D+=(-DHAVE_LIBDEFLATE -I"${LD%%:*}"); L+=("${LD##*:}/libdeflate.a"); fi
CC="${CC:-clang}"
F=(-std=c2x -O2 -I"$top/src" -I"$top/tests/image" -isystem "$top/third_party/miniz-3.1.2")
o="$(mktemp -d)"
"$CC" "${F[@]}" -w -I"$top/third_party/miniz-3.1.2" -include "$top/src/cdom_image_miniz.h" -c -o "$o/tinfl.o" "$top/third_party/miniz-3.1.2/tinfl.c"
"$CC" "${F[@]}" -Wall -Wextra -c -o "$o/inflate.o" "$top/src/cdom_image_inflate.c"
"$CC" "${F[@]}" -c -o "$o/host_alloc.o" "$top/tests/image/host_alloc.c"
"$CC" "${F[@]}" "${D[@]}" -c -o "$o/native.o" "$top/bench/image/inflate/native.c"
if [ -n "${ZNG:-}" ]; then "$CC" -O2 -I"${ZNG%%:*}" -I"${ZNG##*:}" -c -o "$o/native_zng.o" "$top/bench/image/inflate/native_zng.c"; fi
"$CC" -o "$out" "$o"/*.o "${L[@]}"
rm -rf "$o"
