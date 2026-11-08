#!/bin/bash
# The link-level tests: one heap whoever brings it, mini.c next to an app's libc, the program's
# string.h (src/libc) under cdom's flags and freestanding ones, and CD_LIT refusing a variable. Builds into
# tests/out/link and runs tests/link/run.mjs.
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
if "$CC" "${F[@]}" -o "$out/on_only.wasm" "$here/on_only.c" "$src/cdom.c" "$src/mini.c" 2> "$out/on_only.log"; then :; else
  echo "FAIL an app that uses cd_on alone, with no cd_on_event, does not link:"; cat "$out/on_only.log"; fail=1; fi
# string.h: a file that includes it builds under cdom's flags and under freestanding ones (-ffreestanding, where
# clang emits a call for a memcpy it does not know), and links with mini.c alone; memcpy, memmove and
# memset are memory.copy and memory.fill in place, never an undefined symbol. libc_addr takes their
# address, and links to mini.c's real ones.
NM="$(dirname "$CC")/llvm-nm"
OD="$(dirname "$CC")/llvm-objdump"
LIBC_CDOM=(--target=wasm32 -O2 -nostdlib -mbulk-memory -Wall -Wextra -Werror)
LIBC_FREESTANDING=(--target=wasm32 -Oz -nostdlib -ffreestanding -mbulk-memory -mno-nontrapping-fptoint -std=c11 -Wall -Wextra -Werror -Wshadow -Wvla -Wstrict-prototypes)
libc_case() { # name, then the flags
  local name="$1"; shift
  local o="$out/libc_header_$name.o" w="$out/libc_header_$name.wasm" a="$out/libc_addr_$name.wasm" log="$out/libc_$name.log"
  rm -f "$o" "$w" "$a"
  if ! "$CC" "$@" -isystem "$src/libc" -c -o "$o" "$here/libc_header.c" 2> "$log"; then
    echo "FAIL string.h ($name flags): a file that includes it does not compile:"; cat "$log"; fail=1; return; fi
  local undef; undef="$("$NM" -u "$o" | awk '{print $NF}' | grep -E '^(memcpy|memmove|memset)$' | tr '\n' ' ')"
  if [ -n "$undef" ]; then echo "FAIL string.h ($name flags): undefined $undef(a call, not an instruction)"; fail=1; return; fi
  local ins; ins="$("$OD" -d "$o" | grep -oE 'memory\.(copy|fill)' | sort -u | tr '\n' ' ')"
  if [ "$ins" != "memory.copy memory.fill " ]; then echo "FAIL string.h ($name flags): the code has [$ins], not memory.copy and memory.fill"; fail=1; return; fi
  if ! "$CC" "$@" -Wl,--no-entry -o "$w" "$o" "$src/mini.c" 2> "$log"; then
    echo "FAIL string.h ($name flags): does not link with mini.c alone:"; cat "$log"; fail=1; return; fi
  ins="$(wasm-dis "$w" | grep -oE 'memory\.(copy|fill)' | sort -u | tr '\n' ' ')"
  if [ "$ins" != "memory.copy memory.fill " ]; then echo "FAIL string.h ($name flags): the linked program has [$ins], not memory.copy and memory.fill"; fail=1; return; fi
  if ! "$CC" "$@" -isystem "$src/libc" -Wl,--no-entry -o "$a" "$here/libc_addr.c" "$src/mini.c" 2> "$log"; then
    echo "FAIL string.h ($name flags): taking memcpy's address does not link with mini.c:"; cat "$log"; fail=1; return; fi
  echo "ok   string.h ($name flags): compiles, links with mini.c alone, copy and fill are memory.copy and memory.fill"
}
libc_case cdom "${LIBC_CDOM[@]}"
libc_case freestanding "${LIBC_FREESTANDING[@]}"
C=(--target=wasm32 -O2 -Wall -Wextra -Werror -c)
if ! "$CC" "${C[@]}" -o "$out/lit_ok.o" "$here/lit_ok.c" 2> "$out/lit_ok.log"; then
  echo "FAIL CD_LIT given a literal does not compile:"; cat "$out/lit_ok.log"; fail=1
elif "$CC" "${C[@]}" -o "$out/lit_var.o" "$here/lit_var.c" 2> "$out/lit_var.log"; then
  echo "FAIL CD_LIT accepted a variable"; fail=1
elif grep -q "lit_var.c:3:.*error: expected ')'" "$out/lit_var.log"; then echo "ok   CD_LIT given a variable does not compile, a literal does"
else echo "FAIL CD_LIT given a variable failed for another reason:"; cat "$out/lit_var.log"; fail=1; fi
node "$here/run.mjs" "$out" || fail=1
exit $fail
