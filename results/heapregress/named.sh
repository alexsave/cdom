#!/bin/bash
# A module of the given sources at -Oz through wasm-opt with its names kept; prints each function's size.
# Usage: results/heapregress/named.sh out.wasm [clang flags and sources...]
set -euo pipefail
here="$(cd "$(dirname "$0")/../.." && pwd)"
o="$1"; shift
. "$here/tools/cdomflags.sh"
ld="$(mktemp -d)"; ln -s "$(command -v wasm-ld)" "$ld/wasm-ld"
PATH="$ld:/usr/bin:/bin" /opt/homebrew/opt/llvm/bin/clang -Oz "${CDOM_CFLAGS[@]}" "${CDOM_LDFLAGS[@]}" -o "$o.linked" "$@"
wasm-opt -Oz -g "$o.linked" -o "$o"; rm "$o.linked"
wasm-objdump -x "$o" | sed -n '/^Code\[/,/^[A-Z][a-z]*\[/p' | grep "size=" | sed -E 's/ - func\[[0-9]+\] size=([0-9]+) <(.*)>.*/\2 \1/' | sort
