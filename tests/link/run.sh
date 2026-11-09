#!/bin/bash
# The link-level tests: one heap whoever brings it, mini.c next to an app's libc, the program's
# string.h (src/libc) under cdom's flags and freestanding ones, CD_LIT refusing a variable, the I/O
# contract (contract.c), and cdom.mk's rules (app.mk). Builds into tests/out/link and runs tests/link/run.mjs, then the name gate
# (tools/edges.mjs) and the seed gate (tools/seeds.mjs).
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
src="$here/../../src"
out="$here/../out/link"
CC="${WASM_CC:-/opt/homebrew/opt/llvm/bin/clang}"
mkdir -p "$out"
. "$here/../../tools/cdomflags.sh" # CDOM_CFLAGS and CDOM_LDFLAGS, cdom.mk's set for apps
F=(-O2 "${CDOM_CFLAGS[@]}" -Wall -Wextra -Werror "${CDOM_LDFLAGS[@]}")
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
# cdom flags: cdom.mk's set, as apps compile. Freestanding ones: that set with what a strict external library
# adds to it for its own files (no builtins, no saturating float conversions, plain C11).
LIBC_CDOM=(-O2 "${CDOM_CFLAGS[@]}" -Wall -Wextra -Werror)
LIBC_FREESTANDING=(-Oz "${CDOM_CFLAGS[@]}" -ffreestanding -mno-nontrapping-fptoint -std=c11 -Wall -Wextra -Werror -Wshadow -Wvla -Wstrict-prototypes)
libc_case() { # name, then the flags
  local name="$1"; shift
  local o="$out/libc_header_$name.o" w="$out/libc_header_$name.wasm" a="$out/libc_addr_$name.wasm" log="$out/libc_$name.log"
  rm -f "$o" "$w" "$a"
  if ! "$CC" "$@" -c -o "$o" "$here/libc_header.c" 2> "$log"; then
    echo "FAIL string.h ($name flags): a file that includes it does not compile:"; cat "$log"; fail=1; return; fi
  local undef; undef="$("$NM" -u "$o" | awk '{print $NF}' | grep -E '^(memcpy|memmove|memset)$' | tr '\n' ' ')"
  if [ -n "$undef" ]; then echo "FAIL string.h ($name flags): undefined $undef(a call, not an instruction)"; fail=1; return; fi
  local ins; ins="$("$OD" -d "$o" | grep -oE 'memory\.(copy|fill)' | sort -u | tr '\n' ' ')"
  if [ "$ins" != "memory.copy memory.fill " ]; then echo "FAIL string.h ($name flags): the code has [$ins], not memory.copy and memory.fill"; fail=1; return; fi
  if ! "$CC" "$@" "${CDOM_LDFLAGS[@]}" -o "$w" "$o" "$src/mini.c" 2> "$log"; then
    echo "FAIL string.h ($name flags): does not link with mini.c alone:"; cat "$log"; fail=1; return; fi
  ins="$(wasm-dis "$w" | grep -oE 'memory\.(copy|fill)' | sort -u | tr '\n' ' ')"
  if [ "$ins" != "memory.copy memory.fill " ]; then echo "FAIL string.h ($name flags): the linked program has [$ins], not memory.copy and memory.fill"; fail=1; return; fi
  if ! "$CC" "$@" "${CDOM_LDFLAGS[@]}" -o "$a" "$here/libc_addr.c" "$src/mini.c" 2> "$log"; then
    echo "FAIL string.h ($name flags): taking memcpy's address does not link with mini.c:"; cat "$log"; fail=1; return; fi
  echo "ok   string.h ($name flags): compiles, links with mini.c alone, copy and fill are memory.copy and memory.fill"
}
libc_case cdom "${LIBC_CDOM[@]}"
libc_case freestanding "${LIBC_FREESTANDING[@]}"
C=(-O2 "${CDOM_CFLAGS[@]}" -Wall -Wextra -Werror -c)
if ! "$CC" "${C[@]}" -o "$out/lit_ok.o" "$here/lit_ok.c" 2> "$out/lit_ok.log"; then
  echo "FAIL CD_LIT given a literal does not compile:"; cat "$out/lit_ok.log"; fail=1
elif "$CC" "${C[@]}" -o "$out/lit_var.o" "$here/lit_var.c" 2> "$out/lit_var.log"; then
  echo "FAIL CD_LIT accepted a variable"; fail=1
elif grep -q "lit_var.c:3:.*error: expected ')'" "$out/lit_var.log"; then echo "ok   CD_LIT given a variable does not compile, a literal does"
else echo "FAIL CD_LIT given a variable failed for another reason:"; cat "$out/lit_var.log"; fail=1; fi
# The I/O contract (cdom.h, top): contract.c compiles only while every declaration it names is unchanged.
if "$CC" "${LIBC_CDOM[@]}" -fsyntax-only "$here/contract.c" 2> "$out/contract.log"; then
  echo "ok   the I/O contract: every declaration external adapters build on has its type, and every code its value"
else echo "FAIL the I/O contract changed in place:"; cat "$out/contract.log"; fail=1; fi
# cdom.mk as an app uses it (tests/link/app.mk), with its one include before APP_* are set (first), after
# them (last), and the old two includes (twice): the APP_WASM rule builds a module that exports its memory
# and cd_main's entry, with the modules APP_MODULES names, and a second make finds it up to date; the
# APP_PAGE rule (tools/page.awk) puts the shim where the page's one /*CDOM_SHIM*/ line was, and refuses a
# page with the marker beside other text, without it, or with it twice, leaving no output.
for inc in first last twice; do
mo="$out/mk-$inc"
mk() { make -s --no-print-directory -f "$here/app.mk" INCLUDE="$inc" OUT="$mo" "$@"; }
rm -rf "$mo"; mkdir -p "$mo"
if ! mk "$mo/mk_app.wasm" PAGE= 2> "$mo/mk_app.log"; then
  echo "FAIL cdom.mk's APP_WASM rule ($inc) does not build an app:"; cat "$mo/mk_app.log"; fail=1
elif exports="$(wasm-dis "$mo/mk_app.wasm" | grep -oE '\(export "(memory|cd_start)"' | sort | tr -d '\n')"
  [ "$exports" != '(export "cd_start"(export "memory"' ]; then
  echo "FAIL cdom.mk's APP_WASM rule ($inc): the module exports no memory or no cd_start"; fail=1
elif ! mk -q "$mo/mk_app.wasm" PAGE=; then echo "FAIL cdom.mk's APP_WASM rule ($inc): a second make builds the module again"; fail=1
else echo "ok   cdom.mk's APP_WASM rule ($inc) builds an app from APP_SRC, CDOM_SRC and APP_MODULES, memory and entries exported, then finds it up to date"; fi
if [ "$(mk PAGE= 2>&1)" != app-goal ]; then echo "FAIL cdom.mk ($inc) took the default goal from the app's first rule"; fail=1
else echo "ok   cdom.mk ($inc) leaves the app's first rule its default goal"; fi
if mk "$mo/mk_app.wasm" PAGE= APP_MODULES='random bogus' -B > "$mo/mk_bogus.log" 2>&1; then echo "FAIL cdom.mk ($inc) took APP_MODULES naming no module"; fail=1
elif ! grep -q 'APP_MODULES names bogus, not a cdom module' "$mo/mk_bogus.log"; then echo "FAIL cdom.mk ($inc) refused APP_MODULES for another reason:"; cat "$mo/mk_bogus.log"; fail=1
else echo "ok   cdom.mk ($inc) stops at an APP_MODULES name that is no module"; fi
want="$mo/ok.want"
{ sed -n '1,3p' "$here/page/ok.html"; cat "$src/../shim/cdom.min.js"; sed -n '5,$p' "$here/page/ok.html"; } > "$want"
if ! mk "$mo/ok.html" PAGE="$here/page/ok.html" 2> "$mo/mk_page.log"; then
  echo "FAIL cdom_page ($inc) refused a page whose marker is alone on its line:"; cat "$mo/mk_page.log"; fail=1
elif ! cmp -s "$want" "$mo/ok.html"; then echo "FAIL cdom_page ($inc): the page is not itself with the shim at its marker line"; fail=1
else echo "ok   cdom_page ($inc) inlines shim/cdom.min.js at the page's /*CDOM_SHIM*/ line, every other line kept"; fi
for bad in inline:'must be alone on its line' none:'no /\*CDOM_SHIM\*/ line' two:'a second /\*CDOM_SHIM\*/'; do
  name="${bad%%:*}" why="${bad#*:}"
  if mk "$mo/$name.html" PAGE="$here/page/$name.html" 2> "$mo/mk_$name.log"; then
    echo "FAIL cdom_page ($inc) took the $name page"; fail=1
  elif [ -e "$mo/$name.html" ] || [ -e "$mo/$name.html.tmp" ]; then
    echo "FAIL cdom_page ($inc) refused the $name page but left a file"; fail=1
  elif ! grep -q "$name.html.*$why" "$mo/mk_$name.log"; then
    echo "FAIL cdom_page ($inc) refused the $name page for another reason:"; cat "$mo/mk_$name.log"; fail=1
  else echo "ok   cdom_page ($inc) refuses the $name page ($(sed 's/.*: //' "$mo/mk_$name.log" | head -1)) and writes nothing"; fi
done
done
# With the include first: a source APP_SRC names that is not there is what make names (not "no rule to
# make" the module), and a .wasm or .html that is not APP_WASM or APP_PAGE is not cdom.mk's to build.
mo="$out/mk-first"
mk() { make -s --no-print-directory -f "$here/app.mk" INCLUDE=first OUT="$mo" "$@"; }
if mk "$mo/mk_app.wasm" PAGE= EXTRA_SRC="$here/no_such_source.c" -B > "$mo/mk_missing.log" 2>&1; then echo "FAIL cdom.mk built an app whose APP_SRC names a file that is not there"; fail=1
elif ! grep -q "no_such_source.c.*needed by .*mk_app.wasm" "$mo/mk_missing.log"; then echo "FAIL cdom.mk: make does not name the missing source:"; cat "$mo/mk_missing.log"; fail=1
else echo "ok   cdom.mk: make names a missing APP_SRC file ($(sed 's/.*No rule to make target //' "$mo/mk_missing.log" | head -1))"; fi
if mk "$mo/other.wasm" "$mo/other.html" PAGE="$here/page/ok.html" > "$mo/mk_other.log" 2>&1 || [ -e "$mo/other.wasm" ] || [ -e "$mo/other.html" ]; then
  echo "FAIL cdom.mk's rules built a .wasm or .html that is not APP_WASM or APP_PAGE"; cat "$mo/mk_other.log"; fail=1
elif ! grep -q "No rule to make target .*other.wasm" "$mo/mk_other.log"; then echo "FAIL cdom.mk: other.wasm refused for another reason:"; cat "$mo/mk_other.log"; fail=1
else echo "ok   cdom.mk's rules leave a .wasm or .html that is not APP_WASM or APP_PAGE alone"; fi
node "$here/run.mjs" "$out" || fail=1
node "$here/../../tools/edges.mjs" || fail=1
node "$here/../../tools/seeds.mjs" || fail=1
exit $fail
