#!/bin/bash
# Builds the cd_state prototype's checks: out/test.wasm (as is) and out/test-check.wasm (-DCD_STATE_CHECK),
# each with its string table (.lits), from test.c and ../lib/cd_state.c, with the flags an app gets.
# LIB=path/to/cd_state.c builds against another copy of the library (mutate.sh's mutants); OUT another dir.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"; top="$here/../../.."
CC="${WASM_CC:-/opt/homebrew/opt/llvm/bin/clang}"
. "$top/tools/cdomflags.sh"
lib="${LIB:-$here/../lib/cd_state.c}"
out="${OUT:-$here/out}"
mkdir -p "$out"
for v in test test-check; do
  def=(); [ "$v" = test-check ] && def=(-DCD_STATE_CHECK)
  "$CC" -Oz "${CDOM_CFLAGS[@]}" -Wall -Wextra -Werror ${def[@]+"${def[@]}"} -I"$here/../lib" "${CDOM_LDFLAGS[@]}" \
    -o "$out/$v.linked.wasm" "$here/test.c" "$lib" "$top/src/cdom.c" "$top/src/mini.c" "$top/src/cdom_ui.c"
  "$CDOM_LITS" "$out/$v.linked.wasm" "$out/$v.wasm" "$out/$v.wasm.lits"
  rm "$out/$v.linked.wasm"
done
