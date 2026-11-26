#!/bin/bash
# Builds the benchmark's eight modules, out/bench-<X>.wasm and their string tables (.lits), one per entry
# X in A B C CL E L EI LI (ENTRY_X; docs/shared-state-design.md, "The entries"), from the same sources with
# the flags an app gets (-Oz, cdom.mk's set through tools/cdomflags.sh), linking cdom.c, mini.c and
# cdom_ui.c. A to L are the study as it was measured: cdom's src/ as it was then (main at cfee569, put in
# out/main by git archive), and for E and L the prototype ../lib/cd_state.c. EI and LI are E and L on
# cdom's own cd_state: this tree's src/, built with -DCD_STATE_REAL and no prototype. ENTRIES="A L" builds some.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"; top="$here/../../.."
CC="${WASM_CC:-/opt/homebrew/opt/llvm/bin/clang}"
. "$top/tools/cdomflags.sh"
mkdir -p "$here/out"
rm -rf "$here/out/main" && mkdir -p "$here/out/main"
git -C "$top" archive cfee569 src | tar -x -C "$here/out/main"
src=(main.c model.c ops.c live.c entry.c)
for x in ${ENTRIES:-A B C CL E L EI LI}; do
    files=("${src[@]/#/$here/src/}")
    cdom="$here/out/main/src" def=("-DENTRY_$x")
    case "$x" in
        E|L) files+=("$here/../lib/cd_state.c");;
        EI|LI) cdom="$top/src" def=("-DENTRY_${x%I}" -DCD_STATE_REAL);;
    esac
    # cdom.h through cdom's own -I (the flags' -I src first, then the one for the tree this entry is on)
    "$CC" -Oz -I"$cdom" "${CDOM_CFLAGS[@]}" -Wall -Wextra -Werror "${def[@]}" -I"$here/src" -I"$here/../lib" "${CDOM_LDFLAGS[@]}" \
      -o "$here/out/bench-$x.linked.wasm" "${files[@]}" "$cdom/cdom.c" "$cdom/mini.c" "$cdom/cdom_ui.c"
    "$CDOM_LITS" "$here/out/bench-$x.linked.wasm" "$here/out/bench-$x.wasm" "$here/out/bench-$x.wasm.lits"
    rm "$here/out/bench-$x.linked.wasm"
done
ls -l "$here/out" | awk 'NR > 1 {print $5, $9}'
