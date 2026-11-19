#!/bin/bash
# heap-regress: builds the keyed entry from a checkout as jfb entry NAME, and, into results/heapregress/NAME,
# the same module before cdlits twice: as shipped (clang runs wasm-opt -Oz after the link when it is on
# PATH), and through the same link and wasm-opt -Oz with the names kept (-g), for per-function sizes.
# Usage: tools/heapregress_build.sh <checkout> <name> [app.c, default the keyed app]
set -euo pipefail
src="$1"; name="$2"; app="${3:-$src/bench/cdom-keyed/src/app.c}"
top="$(cd "$(dirname "$0")/.." && pwd)"
out="$top/results/heapregress/$name"
mkdir -p "$out"
if [ -z "${3:-}" ]; then JFB_KEYED="${JFB_KEYED:-/Users/alex/Dev/cdom/jfb/frameworks/keyed}" NAME="$name" "$src/bench/build.sh"; fi
. "$src/tools/cdomflags.sh"
CC="${WASM_CC:-/opt/homebrew/opt/llvm/bin/clang}"
"$CC" -Oz "${CDOM_CFLAGS[@]}" "${CDOM_LDFLAGS[@]}" -Wl,--strip-all \
  -o "$out/app.stripped.wasm" "$app" "$src/src/cdom.c" "$src/src/mini.c"
# The names: no wasm-opt on PATH for the link (so the linker's name section survives), then wasm-opt -Oz by hand.
ld="$(mktemp -d)"; ln -s "$(command -v wasm-ld)" "$ld/wasm-ld"
PATH="$ld:/usr/bin:/bin" "$CC" -Oz "${CDOM_CFLAGS[@]}" "${CDOM_LDFLAGS[@]}" \
  -o "$out/app.linked.wasm" "$app" "$src/src/cdom.c" "$src/src/mini.c"
wasm-opt -Oz -g "$out/app.linked.wasm" -o "$out/app.named.wasm"
rm "$out/app.linked.wasm"
wasm-objdump -x "$out/app.named.wasm" | sed -n '/^Code\[/,/^[A-Z][a-z]*\[/p' | grep "size=" |
  sed -E 's/ - func\[[0-9]+\] size=([0-9]+) <(.*)>.*/\2 \1/; s/ - func\[([0-9]+)\] size=([0-9]+)$/func\1 \2/' | sort > "$out/funcs.txt"
printf '%s: stripped %s bytes, named code %s bytes\n' "$name" "$(wc -c < "$out/app.stripped.wasm" | tr -d ' ')" "$(awk '{s+=$2} END{print s}' "$out/funcs.txt")"
