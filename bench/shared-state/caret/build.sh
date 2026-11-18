#!/bin/bash
# Builds S14's three variants, out/caret-<v>.wasm and .lits for v in a c g (CARET_A, CARET_C, CARET_G),
# from caret.c and ../lib/cd_state.c with the flags an app gets.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"; top="$here/../../.."
CC="${WASM_CC:-/opt/homebrew/opt/llvm/bin/clang}"
. "$top/tools/cdomflags.sh"
mkdir -p "$here/out"
for v in a c g; do
  V="$(echo "$v" | tr a-z A-Z)"
  "$CC" -Oz "${CDOM_CFLAGS[@]}" -Wall -Wextra -Werror "-DCARET_$V" -I"$here/../lib" "${CDOM_LDFLAGS[@]}" \
    -o "$here/out/caret-$v.linked.wasm" "$here/caret.c" "$here/../lib/cd_state.c" "$top/src/cdom.c" "$top/src/mini.c" "$top/src/cdom_ui.c"
  "$CDOM_LITS" "$here/out/caret-$v.linked.wasm" "$here/out/caret-$v.wasm" "$here/out/caret-$v.wasm.lits"
  rm "$here/out/caret-$v.linked.wasm"
done
