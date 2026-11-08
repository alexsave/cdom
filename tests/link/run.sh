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
C=(--target=wasm32 -O2 -Wall -Wextra -Werror -c)
if ! "$CC" "${C[@]}" -o "$out/lit_ok.o" "$here/lit_ok.c" 2> "$out/lit_ok.log"; then
  echo "FAIL CD_LIT given a literal does not compile:"; cat "$out/lit_ok.log"; fail=1
elif "$CC" "${C[@]}" -o "$out/lit_var.o" "$here/lit_var.c" 2> "$out/lit_var.log"; then
  echo "FAIL CD_LIT accepted a variable"; fail=1
elif grep -q "lit_var.c:3:.*error: expected ')'" "$out/lit_var.log"; then echo "ok   CD_LIT given a variable does not compile, a literal does"
else echo "FAIL CD_LIT given a variable failed for another reason:"; cat "$out/lit_var.log"; fail=1; fi
node "$here/run.mjs" "$out" || fail=1
exit $fail
