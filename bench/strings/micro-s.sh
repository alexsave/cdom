#!/bin/bash
# Builds out/micro-s.wasm and its table out/micro-s.wasm.lits from micro-s.c on this branch's cdom.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"; top="$here/../.."
CC="${WASM_CC:-/opt/homebrew/opt/llvm/bin/clang}"
. "$top/tools/cdomflags.sh"
mkdir -p "$here/out"
"$CC" -Oz "${CDOM_CFLAGS[@]}" -Wall -Wextra -Werror "${CDOM_LDFLAGS[@]}" -o "$here/out/micro-s.linked.wasm" \
  "$here/micro-s.c" "$top/src/cdom.c" "$top/src/mini.c"
"$CDOM_LITS" "$here/out/micro-s.linked.wasm" "$here/out/micro-s.wasm" "$here/out/micro-s.wasm.lits"
rm "$here/out/micro-s.linked.wasm"
