#!/bin/bash
# Builds the benchmark's six modules, out/bench-<X>.wasm and their string tables (.lits), one per entry
# X in A B C CL E L (ENTRY_X; docs/shared-state-design.md, "The entries"), from the same sources with the
# flags an app gets (-Oz, cdom.mk's set through tools/cdomflags.sh), linking cdom.c, mini.c and cdom_ui.c,
# and for E and L the prototype ../lib/cd_state.c. ENTRIES="A L" builds some.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"; top="$here/../../.."
CC="${WASM_CC:-/opt/homebrew/opt/llvm/bin/clang}"
. "$top/tools/cdomflags.sh"
mkdir -p "$here/out"
src=(main.c model.c ops.c live.c entry.c)
for x in ${ENTRIES:-A B C CL E L}; do
  files=("${src[@]/#/$here/src/}")
  case "$x" in E|L) files+=("$here/../lib/cd_state.c");; esac
  "$CC" -Oz "${CDOM_CFLAGS[@]}" -Wall -Wextra -Werror "-DENTRY_$x" -I"$here/src" -I"$here/../lib" "${CDOM_LDFLAGS[@]}" \
    -o "$here/out/bench-$x.linked.wasm" "${files[@]}" "$top/src/cdom.c" "$top/src/mini.c" "$top/src/cdom_ui.c"
  "$CDOM_LITS" "$here/out/bench-$x.linked.wasm" "$here/out/bench-$x.wasm" "$here/out/bench-$x.wasm.lits"
  rm "$here/out/bench-$x.linked.wasm"
done
ls -l "$here/out" | awk 'NR > 1 {print $5, $9}'
