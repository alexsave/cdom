#!/bin/bash
# Builds bench/plainc/out/micro-{base,ro}.wasm from micro.c and src/cdom.c (ro: -DCDOM_RO=1).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"; top="$here/../.."
CC="${WASM_CC:-/opt/homebrew/opt/llvm/bin/clang}"
. "$top/tools/cdomflags.sh"
mkdir -p "$here/out"
b() { local o="$1"; shift; "$CC" -Oz "${CDOM_CFLAGS[@]}" -Wall -Wextra -Werror "${CDOM_LDFLAGS[@]}" "$@" -o "$here/out/micro-$o.wasm" "$here/micro.c" "$top/src/cdom.c" "$top/src/mini.c"; wc -c "$here/out/micro-$o.wasm"; }
b base -DCDOM_RO=0
b ro -DCDOM_RO=1
