#!/bin/bash
# The bytes cd_realloc adds over cd_alloc (tests/link/realloc_size.c) at -Oz, for the checkout given.
src="${1:-$(cd "$(dirname "$0")/../.." && pwd)}"
here="$(cd "$(dirname "$0")/../.." && pwd)"
. "$src/tools/cdomflags.sh"
CC=/opt/homebrew/opt/llvm/bin/clang; d="$(mktemp -d)"
"$CC" -Oz "${CDOM_CFLAGS[@]}" "${CDOM_LDFLAGS[@]}" -Wl,--strip-all -o "$d/a.wasm" "$here/tests/link/realloc_size.c" "$src/src/cdom.c" "$src/src/mini.c"
"$CC" -Oz "${CDOM_CFLAGS[@]}" "${CDOM_LDFLAGS[@]}" -Wl,--strip-all -DREALLOC -o "$d/r.wasm" "$here/tests/link/realloc_size.c" "$src/src/cdom.c" "$src/src/mini.c"
a=$(wc -c < "$d/a.wasm"); r=$(wc -c < "$d/r.wasm"); echo "cd_alloc $a, cd_realloc $r, gap $((r - a))"
