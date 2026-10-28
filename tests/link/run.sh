#!/bin/bash
# The link-level tests: one heap whoever brings it, mini.c next to an app's libc, and CD_LIT
# refusing a variable. Builds into tests/out/link and runs tests/link/run.mjs.
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
src="$here/../../src"
out="$here/../out/link"
CC="${WASM_CC:-/opt/homebrew/opt/llvm/bin/clang}"
mkdir -p "$out"
F=(--target=wasm32 -O2 -nostdlib -mbulk-memory -Wall -Wextra -Werror -Wl,--no-entry)
fail=0
"$CC" "${F[@]}" -o "$out/alloc_cdom.wasm" "$here/alloc.c" "$src/cdom.c" "$src/mini.c" || fail=1
"$CC" "${F[@]}" -o "$out/alloc_app.wasm" "$here/alloc.c" "$here/app_alloc.c" "$src/cdom.c" "$src/mini.c" || fail=1
"$CC" "${F[@]}" -o "$out/alloc_half.wasm" "$here/alloc.c" "$here/half_alloc.c" "$src/cdom.c" "$src/mini.c" || fail=1
if "$CC" "${F[@]}" -o "$out/own_libc.wasm" "$here/alloc.c" "$here/own_libc.c" "$src/cdom.c" "$src/mini.c" 2> "$out/own_libc.log"; then :; else
  echo "FAIL an app's own libc does not link next to mini.c:"; cat "$out/own_libc.log"; fail=1; fi
if "$CC" "${F[@]}" -c -o "$out/lit_var.o" "$here/lit_var.c" 2> "$out/lit_var.log"; then
  echo "FAIL CD_LIT accepted a variable"; fail=1
else echo "ok   CD_LIT given a variable does not compile ($(grep -c error: "$out/lit_var.log") error)"; fi
node "$here/run.mjs" "$out" || fail=1
exit $fail
