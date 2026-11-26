#!/bin/bash
# P9: every app in the tree built with -DCD_LITS_CHECK, then cdlits --check on it (each slot's spelling
# lexed and compared with the compiler's own bytes).
set -uo pipefail
top="$(cd "$(dirname "$0")/../.." && pwd)"
CC="${WASM_CC:-/opt/homebrew/opt/llvm/bin/clang}"
. "$top/tools/cdomflags.sh"
o="$top/bench/strings/out/litcheck"; mkdir -p "$o"
bad=0
for src in "$top"/tutorial/0*/src/app.c "$top/bench/cdom-keyed/src/app.c" "$top/demo/click.c" "$top/bench/strings/micro-s.c" "$top/bench/strings/out/synth.c"; do
    extra=(); case "$src" in */demo/click.c) extra=("$top"/src/cdom_audio.c "$top"/src/cdom_random.c);; esac
    n="$(echo "$src" | sed "s|$top/||; s|/|_|g")"
    if "$CC" -Oz "${CDOM_CFLAGS[@]}" -DCD_LITS_CHECK "${CDOM_LDFLAGS[@]}" -o "$o/$n.wasm" "$src" ${extra[@]+"${extra[@]}"} "$top/src/cdom.c" "$top/src/mini.c" 2> "$o/$n.err" \
      && "$CDOM_LITS" --check "$o/$n.wasm" > "$o/$n.out" 2>&1; then
        echo "ok   $n: $(cat "$o/$n.out" | tail -1)"
    else echo "FAIL $n: $(tail -2 "$o/$n.err" "$o/$n.out" 2>/dev/null)"; bad=1; fi
done
exit $bad
