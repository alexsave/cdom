#!/bin/bash
# Builds bench/strings/out/micro-{a,b,d}.wasm from micro.c and the real cdom.c of each design.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"; top="$here/../.."
CC="${WASM_CC:-/opt/homebrew/opt/llvm/bin/clang}"
. "$top/tools/cdomflags.sh"
mkdir -p "$here/out"
L=(-Wl,--export-if-defined=__start_cd_lits -Wl,--export-if-defined=__stop_cd_lits)
b() { local o="$1"; shift; "$CC" -Oz "${CDOM_CFLAGS[@]}" -Wall -Wextra -Werror "${CDOM_LDFLAGS[@]}" "${L[@]}" "$@" -o "$here/out/micro-$o.wasm" "$here/micro.c" "$top/src/cdom.c" "$top/src/mini.c"; }
b a
b b -DCDOM_STRB=1
b d -DCDOM_STRB=1 -DCDOM_LITD=1
b b2 -DCDOM_STRB=1 -DCDOM_TMP_ASCII=1
