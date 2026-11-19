#!/bin/bash
# The link-level tests: one heap whoever brings it, mini.c next to an app's libc, the program's
# string.h (src/libc) under cdom's flags and freestanding ones, a non-string refused as a string argument, the I/O
# contract (contract.c), and cdom.mk's rules (app.mk). Builds into tests/out/link and runs tests/link/run.mjs, then the name gate
# (tools/edges.mjs), the seed gate (tools/seeds.mjs) and the string-names gate (tools/strings.mjs).
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
src="$here/../../src"
out="$here/../out/link"
if [ "$(uname -s)" = Darwin ]; then CC="${WASM_CC:-/opt/homebrew/opt/llvm/bin/clang}"; else CC="${WASM_CC:-clang}"; fi
mkdir -p "$out"
# cdom.mk bundles the stylesheets under src/ where make runs: tests/link/app.mk runs here, where there is none.
cd "$out" || exit 1
. "$here/../../tools/cdomflags.sh" # CDOM_CFLAGS and CDOM_LDFLAGS, cdom.mk's set for apps
F=(-O2 "${CDOM_CFLAGS[@]}" -Wall -Wextra -Werror "${CDOM_LDFLAGS[@]}")
fail=0
# link out.wasm sources...: a cdom module as an app links it, then cdlits (the step cdom.mk's APP_WASM rule has).
link() {
  local o="$1"; shift
  "$CC" "${F[@]}" -o "$o.linked" "$@" && "$CDOM_LITS" "$o.linked" "$o" "$o.lits"
  local rc=$?; rm -f "$o.linked"; return $rc
}
link "$out/alloc_cdom.wasm" "$here/alloc.c" "$src/cdom.c" "$src/mini.c" || fail=1
link "$out/alloc_app.wasm" "$here/alloc.c" "$here/app_alloc.c" "$src/cdom.c" "$src/mini.c" || fail=1
link "$out/alloc_half.wasm" "$here/alloc.c" "$here/half_alloc.c" "$src/cdom.c" "$src/mini.c" || fail=1
if link "$out/own_libc.wasm" "$here/alloc.c" "$here/own_libc.c" "$src/cdom.c" "$src/mini.c" 2> "$out/own_libc.log"; then :; else
  echo "FAIL an app's own libc does not link next to mini.c:"; cat "$out/own_libc.log"; fail=1; fi
if link "$out/on_only.wasm" "$here/on_only.c" "$src/cdom.c" "$src/mini.c" 2> "$out/on_only.log"; then :; else
  echo "FAIL an app that uses cd_on alone, with no cd_on_event, does not link:"; cat "$out/on_only.log"; fail=1; fi
if link "$out/ro_range.wasm" "$here/ro_range.c" "$src/cdom.c" "$src/mini.c" 2> "$out/ro_range.log"; then :; else
  echo "FAIL the read-only range probe does not link:"; cat "$out/ro_range.log"; fail=1; fi
# The render pass is linked only into an app that asks for a render (cdom.c, renders: the volatile
# pointer). dirty_pass.c built without and with a cd_dirty call: at -Oz the second is 586 bytes larger
# (the pass, the queue, its table and cd_dirty); with the pass linked into every app the gap is 230. 330
# splits them.
Z=(-Oz "${CDOM_CFLAGS[@]}" -Wall -Wextra -Werror "${CDOM_LDFLAGS[@]}" -Wl,--strip-all)
if "$CC" "${Z[@]}" -o "$out/dirty_none.wasm" "$here/dirty_pass.c" "$src/cdom.c" "$src/mini.c" &&
   "$CC" "${Z[@]}" -DASKS -o "$out/dirty_asks.wasm" "$here/dirty_pass.c" "$src/cdom.c" "$src/mini.c"; then
  none=$(wc -c < "$out/dirty_none.wasm"); asks=$(wc -c < "$out/dirty_asks.wasm")
  if [ $((asks - none)) -lt 330 ]; then
    echo "FAIL an app that never calls cd_dirty carries the render pass: $none bytes without cd_dirty, $asks with (a gap under 330)"; fail=1
  fi
else echo "FAIL dirty_pass.c does not build"; fail=1; fi
# The views of shared state are linked only into an app that uses cd_state (cdom.c, shared state: the
# volatile pointers views_pass and views_gone). state_pass.c renders and hides a region, and with -DWATCH
# its render is a view: at -Oz the second is 1,456 bytes larger (the views' pass, their takedown,
# cd_watch and cd_changed). With the pass linked into every app that renders the gap is 523, and with the
# takedown linked into every app that hides a region it is 1,277. 1,370 splits them.
if "$CC" "${Z[@]}" -o "$out/state_none.wasm" "$here/state_pass.c" "$src/cdom.c" "$src/mini.c" &&
   "$CC" "${Z[@]}" -DWATCH -o "$out/state_watch.wasm" "$here/state_pass.c" "$src/cdom.c" "$src/mini.c"; then
  none=$(wc -c < "$out/state_none.wasm"); watch=$(wc -c < "$out/state_watch.wasm")
  if [ $((watch - none)) -lt 1370 ]; then
    echo "FAIL an app that never uses cd_state carries the views' pass or takedown: $none bytes without cd_state, $watch with (a gap under 1370)"; fail=1
  fi
else echo "FAIL state_pass.c does not build"; fail=1; fi
# cdom_ui (src/cdom_ui.c) costs an app that links it and calls none of it nothing: dirty_pass.c built with
# and without it is the same bytes.
if "$CC" "${Z[@]}" -o "$out/ui_off.wasm" "$here/dirty_pass.c" "$src/cdom.c" "$src/mini.c" &&
   "$CC" "${Z[@]}" -o "$out/ui_on.wasm" "$here/dirty_pass.c" "$src/cdom.c" "$src/cdom_ui.c" "$src/mini.c"; then
  if cmp -s "$out/ui_off.wasm" "$out/ui_on.wasm"; then echo "ok   cdom_ui linked and not called adds no byte ($(wc -c < "$out/ui_off.wasm" | tr -d ' ') bytes either way)"
  else echo "FAIL cdom_ui linked and not called changes the module: $(wc -c < "$out/ui_off.wasm" | tr -d ' ') bytes without it, $(wc -c < "$out/ui_on.wasm" | tr -d ' ') with"; fail=1; fi
else echo "FAIL dirty_pass.c does not build with cdom_ui.c"; fail=1; fi
# The callbacks-only profile (src/cdom_callbacks_only.h): cd_on and the _cb forms compile under it, and a
# token form or the listening it replaces does not.
PROF=(-O2 "${CDOM_CFLAGS[@]}" -Wall -Wextra -Werror)
if "$CC" "${PROF[@]}" -c -o "$out/callbacks_only.o" "$here/callbacks_only.c" 2> "$out/callbacks_only.log"; then
  echo "ok   the callbacks-only profile takes cd_on, cd_timer_cb, cd_http_cb, cd_store_get_cb and cd_file_read_cb"
else echo "FAIL the callbacks-only profile refused cd_on or a _cb form:"; cat "$out/callbacks_only.log"; fail=1; fi
for u in LISTEN HTTP TIMER STORE FILE; do
  if "$CC" "${PROF[@]}" -DUSE_$u -c -o "$out/callbacks_only_$u.o" "$here/callbacks_only.c" 2> "$out/callbacks_only_$u.log"; then
    echo "FAIL the callbacks-only profile let a poisoned call through (USE_$u)"; fail=1
  elif ! grep -q 'poisoned' "$out/callbacks_only_$u.log"; then
    echo "FAIL the callbacks-only profile refused USE_$u for another reason:"; cat "$out/callbacks_only_$u.log"; fail=1
  else echo "ok   the callbacks-only profile refuses USE_$u ($(grep -o "attempt to use a poisoned identifier" "$out/callbacks_only_$u.log" | head -1))"; fi
done
# The _try allocations' path is linked only into an app that calls them: alloc_try_size.c built with
# cd_alloc and with cd_alloc_try differs by the path itself, 311 bytes at -Oz now (90 with malloc sharing it). Under 150 the path is
# in every app (malloc sharing it); over 400 it has grown past its budget. Z is the -Oz set above.
if "$CC" "${Z[@]}" -o "$out/try_none.wasm" "$here/alloc_try_size.c" "$src/cdom.c" "$src/mini.c" &&
   "$CC" "${Z[@]}" -DTRY -o "$out/try_asks.wasm" "$here/alloc_try_size.c" "$src/cdom.c" "$src/mini.c"; then
  none=$(wc -c < "$out/try_none.wasm"); asks=$(wc -c < "$out/try_asks.wasm"); gap=$((asks - none))
  if [ $gap -lt 150 ]; then echo "FAIL the _try path is in an app that never calls it: $none bytes without cd_alloc_try, $asks with (a gap of $gap, under 150)"; fail=1
  elif [ $gap -gt 400 ]; then echo "FAIL calling cd_alloc_try costs $gap bytes, past its budget of 400 ($none without, $asks with)"; fail=1
  else echo "ok   the _try path costs only an app that calls it: $gap bytes (150 to 400)"; fi
else echo "FAIL alloc_try_size.c does not build"; fail=1; fi
# realloc calls the allocator's body (heap_alloc) rather than inlining a copy of it: realloc_size.c built
# with cd_alloc and with cd_realloc differs by realloc's own code (the block's room, the grow at the top, the
# copy), 255 bytes at -Oz now; a copy of the allocator in it makes the gap 478. Over 350 it has one again.
if "$CC" "${Z[@]}" -o "$out/realloc_none.wasm" "$here/realloc_size.c" "$src/cdom.c" "$src/mini.c" &&
   "$CC" "${Z[@]}" -DREALLOC -o "$out/realloc_asks.wasm" "$here/realloc_size.c" "$src/cdom.c" "$src/mini.c"; then
  none=$(wc -c < "$out/realloc_none.wasm"); asks=$(wc -c < "$out/realloc_asks.wasm"); gap=$((asks - none))
  if [ $gap -gt 350 ]; then echo "FAIL realloc carries a copy of the allocator: $none bytes with cd_alloc, $asks with cd_realloc (a gap of $gap, over 350)"; fail=1
  else echo "ok   realloc shares the allocator's body: cd_realloc costs $gap bytes over cd_alloc (at most 350)"; fi
else echo "FAIL realloc_size.c does not build"; fail=1; fi
# string.h (and stdio.h, below): a file that includes it builds under cdom's flags and under freestanding ones (-ffreestanding, where
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
  # stdio.h: snprintf and vsnprintf compile and link with mini.c alone (run.mjs runs their cases), and a
  # program that does not call them carries none of their code (the string.h one above links mini.c too).
  local p="$out/libc_printf_$name.wasm"; rm -f "$p"
  if ! "$CC" "$@" "${CDOM_LDFLAGS[@]}" -o "$p" "$here/libc_printf.c" "$src/mini.c" 2> "$log"; then
    echo "FAIL stdio.h ($name flags): snprintf does not compile or link with mini.c alone:"; cat "$log"; fail=1; return; fi
  # The string.h program again, against mini.c cut before its snprintf: the two modules are the same bytes.
  local cut="$out/mini_no_printf.c" w0="$out/libc_header_${name}_no_printf.wasm"
  sed '/^\/\* snprintf and vsnprintf/,$d' "$src/mini.c" > "$cut"
  if cmp -s "$cut" "$src/mini.c" || grep -q "int v*snprintf" "$cut"; then echo "FAIL stdio.h: mini.c has no '/* snprintf and vsnprintf' block to cut"; fail=1; return; fi
  if ! "$CC" "$@" "${CDOM_LDFLAGS[@]}" -o "$w0" "$o" "$cut" 2> "$log"; then echo "FAIL stdio.h ($name flags): mini.c without snprintf does not build:"; cat "$log"; fail=1; return; fi
  if ! cmp -s "$w" "$w0"; then
    echo "FAIL stdio.h ($name flags): a program that never calls snprintf is $(wc -c < "$w" | tr -d ' ') bytes, $(wc -c < "$w0" | tr -d ' ') without it in mini.c"; fail=1; return; fi
  echo "ok   stdio.h ($name flags): snprintf links with mini.c alone, and a program that does not call it carries none of it"
  # stdlib.h: the heap, abs and labs, and stdio.h's FILE in a prototype, compile and link with cdom.c and mini.c
  # (run.mjs runs libc_stdlib); a FILE cannot be made (libc_file.c, which declares one, does not compile).
  # The file is built with the case's flags; cdom.c, which it links to for the heap, with cdom's (as an app's library is).
  local s="$out/libc_stdlib_$name.wasm" so="$out/libc_stdlib_$name.o"; rm -f "$s" "$so"
  if ! "$CC" "$@" -c -o "$so" "$here/libc_stdlib.c" 2> "$log" ||
     ! "$CC" "${LIBC_CDOM[@]}" "${CDOM_LDFLAGS[@]}" -o "$s" "$so" "$src/cdom.c" "$src/mini.c" 2> "$log"; then
    echo "FAIL stdlib.h ($name flags): a file that uses the heap, abs and labs, and FILE does not compile or link:"; cat "$log"; fail=1; return; fi
  if "$CC" "$@" -c -o "$out/libc_file_$name.o" "$here/libc_file.c" 2> "$log"; then
    echo "FAIL stdio.h ($name flags): a FILE could be declared, so FILE is not the incomplete type"; fail=1; return
  elif ! grep -q "never completed" "$log"; then echo "FAIL stdio.h ($name flags): libc_file.c failed for another reason:"; cat "$log"; fail=1; return; fi
  echo "ok   stdlib.h ($name flags): malloc, calloc, realloc, free, abs and labs compile and link with cdom.c and mini.c; FILE names a prototype's parameter and nothing more"
}
libc_case cdom "${LIBC_CDOM[@]}"
libc_case freestanding "${LIBC_FREESTANDING[@]}"
C=(-O2 "${CDOM_CFLAGS[@]}" -Wall -Wextra -Werror -c)
# A string argument is plain C, and a thing that is not a string is a compile error: str_ok.c, the control,
# compiles, and each of the others fails with its own message.
if ! "$CC" "${C[@]}" -o "$out/str_ok.o" "$here/str_ok.c" 2> "$out/str_ok.log"; then
  echo "FAIL the string control file does not compile:"; cat "$out/str_ok.log"; fail=1
else
  for bad in id:'incompatible integer to pointer conversion' int:'incompatible integer to pointer conversion' cmp:'invalid operands to binary expression'; do
    name="${bad%%:*}" why="${bad#*:}"
    if "$CC" "${C[@]}" -o "$out/str_$name.o" "$here/str_$name.c" 2> "$out/str_$name.log"; then echo "FAIL str_$name.c compiles"; fail=1
    elif ! grep -qF -- "$why" "$out/str_$name.log"; then echo "FAIL str_$name.c failed for another reason:"; cat "$out/str_$name.log"; fail=1
    else echo "ok   str_$name.c does not compile ($why), the control does"; fi
  done
fi
# The I/O contract (cdom.h, top): contract.c compiles only while every declaration it names is unchanged.
if "$CC" "${LIBC_CDOM[@]}" -fsyntax-only "$here/contract.c" 2> "$out/contract.log"; then
  echo "ok   the I/O contract: every declaration external adapters build on has its type, and every code its value"
else echo "FAIL the I/O contract changed in place:"; cat "$out/contract.log"; fail=1; fi
# cdom.mk as an app uses it (tests/link/app.mk), with its one include before APP_* are set (first), after
# them (last), and the old two includes (twice): the APP_WASM rule builds a module that exports its memory
# and cd_main's entry, with the modules APP_MODULES names, and a second make finds it up to date; the
# APP_PAGE rule (tools/cdpage) puts the shim where the page's one /*CDOM_SHIM*/ line was, and refuses a
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
# APP_MODULES file links cdom_file.c, and an app without it carries none of it: none of its two exports
# (its imports go with the calls an app makes; this app makes none).
filebits() { wasm-dis "$1" | grep -oE '\(export "cd_file_(room|got)"' | sort -u | wc -l | tr -d ' '; }
if ! mk "$mo/mk_app.wasm" PAGE= APP_MODULES='random file' -B 2> "$mo/mk_file.log"; then echo "FAIL cdom.mk ($inc) did not build with APP_MODULES file:"; cat "$mo/mk_file.log"; fail=1
elif [ "$(filebits "$mo/mk_app.wasm")" != 2 ]; then echo "FAIL cdom.mk ($inc): APP_MODULES file did not link cdom_file.c ($(filebits "$mo/mk_app.wasm") of its 2 exports)"; fail=1
elif ! mk "$mo/mk_app.wasm" PAGE= -B 2> "$mo/mk_file.log" || [ "$(filebits "$mo/mk_app.wasm")" != 0 ]; then echo "FAIL cdom.mk ($inc): an app without APP_MODULES file carries some of cdom_file"; fail=1
else echo "ok   cdom.mk ($inc): APP_MODULES file links cdom_file.c (its 2 exports), and an app without it has none of them"; fi
# A module of several files and a part of its own (app.mk's PARTS module, tests/link/parts): its two own files
# and the part's two link, the part built with its -w, -I and -D after the app's -Wall -Wextra -Werror, and the
# module's link flags at the link (parts_lib_b is exported only by them); the objects are kept, and a second make
# finds it up to date.
partbits() { wasm-dis "$1" | grep -oE '\(export "parts_(own_a|own_b|lib_a|lib_b)"' | sort -u | wc -l | tr -d ' '; }
pk() { mk "$mo/mk_app.wasm" PAGE= PARTS=1 "$@"; }
if ! pk APP_MODULES='random parts' -B 2> "$mo/mk_parts.log"; then echo "FAIL cdom.mk ($inc): a module with a part does not build:"; cat "$mo/mk_parts.log"; fail=1
elif [ "$(partbits "$mo/mk_app.wasm")" != 4 ]; then echo "FAIL cdom.mk ($inc): the module's files, its part's or its link flags are missing ($(partbits "$mo/mk_app.wasm") of 4 exports)"; fail=1
elif ! [ -f "$mo/mk_app.wasm.cdom/parts/lib/lib_a.cdom.o" ]; then echo "FAIL cdom.mk ($inc): make deleted a part's object after the link (an intermediate file), so every build compiles the part again"; fail=1
elif ! pk APP_MODULES='random parts' -q; then echo "FAIL cdom.mk ($inc): a second make builds the module with a part again"; fail=1
elif ! pk -B 2> "$mo/mk_parts.log" || [ "$(partbits "$mo/mk_app.wasm")" != 0 ]; then echo "FAIL cdom.mk ($inc): an app without the parts module carries some of it"; fail=1
else echo "ok   cdom.mk ($inc): a module's two own files and its part's two link, the part with its own -w, -I and -D under the app's -Werror, the link with the module's flags; up to date after; none of it without the module"; fi
# The part's flags are the part's alone: an app file that includes its private header does not compile, and the
# module's own files keep the app's -Werror (an unused variable in own_a.c stops the build; the same in the part does not).
if pk APP_MODULES='random parts' EXTRA_SRC="$here/parts/app_sees.c" -B > "$mo/mk_parts_sees.log" 2>&1; then echo "FAIL cdom.mk ($inc): an app file sees a module part's -I"; fail=1
elif ! grep -q "'parts_private.h' file not found" "$mo/mk_parts_sees.log"; then echo "FAIL cdom.mk ($inc): app_sees.c failed for another reason:"; cat "$mo/mk_parts_sees.log"; fail=1
else echo "ok   cdom.mk ($inc): a module part's -I does not reach the app's files"; fi
if pk APP_MODULES='random parts' EXTRA_CFLAGS=-DPARTS_WARN -B > "$mo/mk_parts_warn.log" 2>&1; then echo "FAIL cdom.mk ($inc): a module's own file built with a warning under -Werror"; fail=1
elif ! grep -q "own_a.c.*unused_own" "$mo/mk_parts_warn.log" || grep -q "unused_lib" "$mo/mk_parts_warn.log"; then echo "FAIL cdom.mk ($inc): the warning was not own_a.c's alone:"; cat "$mo/mk_parts_warn.log"; fail=1
else echo "ok   cdom.mk ($inc): a module's own files keep the app's -Werror, and its part's -w is the part's alone"; fi
if pk APP_MODULES='random parts' CDOM_PARTS_LIB_SRC="$here/parts/lib/lib_a.c $here/parts/dup/lib_a.c" -B > "$mo/mk_parts_dup.log" 2>&1; then echo "FAIL cdom.mk ($inc) took a part that names two files of one base name"; fail=1
elif ! grep -q 'part lib of module parts names two files called lib_a.c' "$mo/mk_parts_dup.log"; then echo "FAIL cdom.mk ($inc) refused the doubled part for another reason:"; cat "$mo/mk_parts_dup.log"; fail=1
else echo "ok   cdom.mk ($inc) stops at a part that names two files of one base name"; fi
if [ "$(mk PAGE= 2>&1)" != app-goal ]; then echo "FAIL cdom.mk ($inc) took the default goal from the app's first rule"; fail=1
else echo "ok   cdom.mk ($inc) leaves the app's first rule its default goal"; fi
if mk "$mo/mk_app.wasm" PAGE= APP_MODULES='random bogus' -B > "$mo/mk_bogus.log" 2>&1; then echo "FAIL cdom.mk ($inc) took APP_MODULES naming no module"; fail=1
elif ! grep -q 'APP_MODULES names bogus, not a cdom module' "$mo/mk_bogus.log"; then echo "FAIL cdom.mk ($inc) refused APP_MODULES for another reason:"; cat "$mo/mk_bogus.log"; fail=1
else echo "ok   cdom.mk ($inc) stops at an APP_MODULES name that is no module"; fi
want="$mo/ok.want"
{ sed -n '1,3p' "$here/page/ok.html"; cat "$mo/mk_app.wasm.lits"; echo "</script>"; echo "<script>"
  cat "$src/../shim/cdom.min.js"; sed -n '5,$p' "$here/page/ok.html"; } > "$want"
if ! mk "$mo/ok.html" PAGE="$here/page/ok.html" APP_WASM_SEPARATE=1 2> "$mo/mk_page.log"; then
  echo "FAIL cdom_page ($inc) refused a page whose marker is alone on its line:"; cat "$mo/mk_page.log"; fail=1
elif ! cmp -s "$want" "$mo/ok.html"; then echo "FAIL cdom_page ($inc): the page is not itself with the table and the shim at its marker line"; fail=1
else echo "ok   cdom_page ($inc) inlines the table, a script of its own, and shim/cdom.min.js at the page's /*CDOM_SHIM*/ line, every other line kept (APP_WASM_SEPARATE: the page's own URL)"; fi
# The refused pages, with the module separate: each is refused for its own fault, which bundling would
# otherwise mask (a page whose start call went with its shim line, or that takes a second module, has no
# bundled module to start either).
for bad in inline:'must be alone on its line' none:'no /\*CDOM_SHIM\*/ line' two:'a second /\*CDOM_SHIM\*/' two_wasm:'a second /\*CDOM_WASM\*/' noscript:'with no <script tag before it'; do
  name="${bad%%:*}" why="${bad#*:}"
  if mk "$mo/$name.html" PAGE="$here/page/$name.html" APP_WASM_SEPARATE=1 2> "$mo/mk_$name.log"; then
    echo "FAIL cdom_page ($inc) took the $name page"; fail=1
  elif [ -e "$mo/$name.html" ] || compgen -G "$mo/$name.html.tmp*" > /dev/null; then # the page, or the tool's temporary file
    echo "FAIL cdom_page ($inc) refused the $name page but left a file"; fail=1
  elif ! grep -q "$name.html.*$why" "$mo/mk_$name.log"; then
    echo "FAIL cdom_page ($inc) refused the $name page for another reason:"; cat "$mo/mk_$name.log"; fail=1
  else echo "ok   cdom_page ($inc) refuses the $name page ($(sed 's/.*: //' "$mo/mk_$name.log" | head -1)) and writes nothing"; fi
done
# The table is a script of its own before the shim and before the module, whatever the page's script tag
# carries (it is written again as it was), and the table is the module's own.
if ! mk "$mo/attrs.html" PAGE="$here/page/attrs.html" APP_WASM_SEPARATE=1 -B 2> "$mo/mk_attrs.log"; then
  echo "FAIL cdom_page ($inc) refused a page whose script tag has attributes:"; cat "$mo/mk_attrs.log"; fail=1
elif ! mk "$mo/marker.html" PAGE="$here/page/marker.html" -B 2> "$mo/mk_marker0.log"; then
  echo "FAIL cdom_page ($inc) refused the marker page:"; cat "$mo/mk_marker0.log"; fail=1
elif ! node -e 'const fs = require("fs"), [a, m, lits, shim] = process.argv.slice(1);
    const tag = "<script type=\"text/javascript\" data-x=\"a b\">", table = fs.readFileSync(lits, "utf8");
    const pa = fs.readFileSync(a, "utf8"), pm = fs.readFileSync(m, "utf8"), sh = fs.readFileSync(shim, "utf8").slice(0, 200);
    const ok = pa.includes(table + "</script>\n" + tag + "\n" + sh) && pa.indexOf(tag) < pa.indexOf(table) &&
      pm.includes(table + "</script>\n<script>\n" + sh) && pm.indexOf(table) < pm.indexOf(sh) && pm.indexOf(sh) < pm.indexOf("cdom.start(");
    process.exit(ok ? 0 : 1)' "$mo/attrs.html" "$mo/marker.html" "$mo/mk_app.wasm.lits" "$src/../shim/cdom.min.js"; then
  echo "FAIL cdom_page ($inc): the table is not a script of its own before the shim and the module, with the page's script tag written again"; fail=1
else echo "ok   cdom_page ($inc) writes the table as a script of its own before the shim and the module, the page's script tag (attributes too) written again"; fi
# Bundling, the default: an app's own page that names its module by URL is refused with the one line that
# says what to do, and nothing is written (never a rewrite of the app's own call).
rm -f "$mo/ok.html"
line="$here/page/ok.html: this page loads mk_app.wasm; set APP_WASM_SEPARATE := 1, or start with the bundled module (the /*CDOM_WASM*/ marker)"
if mk "$mo/ok.html" PAGE="$here/page/ok.html" 2> "$mo/mk_url.log"; then echo "FAIL APP_PAGE ($inc) bundled into a page that loads its module by URL"; fail=1
elif [ -e "$mo/ok.html" ] || compgen -G "$mo/ok.html.tmp*" > /dev/null; then echo "FAIL APP_PAGE ($inc) refused a page that loads its module by URL but left a file"; fail=1
elif ! grep -qxF "$line" "$mo/mk_url.log"; then echo "FAIL APP_PAGE ($inc) refused a page that loads its module by URL with another message:"; cat "$mo/mk_url.log"; fail=1
else echo "ok   APP_PAGE ($inc), bundling, refuses a page of the app's own that loads mk_app.wasm, with the line that says what to do"; fi
# The default page (no APP_PAGE_SRC), bundled: shim/page.html with the title, the shim, and the module as
# a data: URL whose bytes are mk_app.wasm's; the page is made again when the module is newer. With
# APP_WASM_SEPARATE the start call names the module beside the page (or APP_WASM_URL) and the page does
# not depend on it. APP_TITLE is escaped for HTML.
tpl="$src/../shim/page.html"
want_page() { # title, then the start call's first argument: the page with an empty <style>
  { sed -n '1,5p' "$tpl"; echo "<title>$1</title>"; sed -n '7p' "$tpl"; sed -n '9,13p' "$tpl"
    cat "$mo/mk_app.wasm.lits"; echo "</script>"; echo "<script>"
    cat "$src/../shim/cdom.min.js"
    echo "cdom.start($2, \"app\");"; sed -n '16,$p' "$tpl"; } > "$mo/index.want"
}
b64() { base64 < "$1" | tr -d '\n\r'; }
if ! mk "$mo/index.html" PAGE= 2> "$mo/mk_default.log"; then echo "FAIL the default page ($inc) does not build:"; cat "$mo/mk_default.log"; fail=1
elif want_page mk_app "\"data:application/wasm;base64,$(b64 "$mo/mk_app.wasm")\""; ! cmp -s "$mo/index.want" "$mo/index.html"; then
  echo "FAIL the default page ($inc), bundled, is not shim/page.html with the title, the shim and the module:"; diff "$mo/index.want" "$mo/index.html" | cut -c1-200 | head; fail=1
elif ! node -e 'const [p, w] = process.argv.slice(1), fs = require("fs");
    const m = /cdom\.start\("data:application\/wasm;base64,([A-Za-z0-9+\/=]+)", "app"\)/.exec(fs.readFileSync(p, "utf8"));
    process.exit(m && Buffer.from(m[1], "base64").equals(fs.readFileSync(w)) ? 0 : 1)' "$mo/index.html" "$mo/mk_app.wasm"; then
  echo "FAIL the default page ($inc): its data: URL does not decode to mk_app.wasm"; fail=1
elif touch -t 203001010000 "$mo/mk_app.wasm"; mk -q "$mo/index.html" PAGE=; then echo "FAIL the default page ($inc), bundled, is up to date with a newer module"; fail=1
else echo "ok   the default page ($inc), bundled: shim/page.html titled mk_app, the shim inlined, mk_app.wasm's bytes as its data: URL, made again with the module"; fi
touch "$mo/mk_app.wasm"
if ! mk "$mo/index.html" PAGE= APP_WASM_SEPARATE=1 APP_TITLE='a<b & c>' -B 2> "$mo/mk_sep.log"; then echo "FAIL the default page ($inc), APP_WASM_SEPARATE, does not build:"; cat "$mo/mk_sep.log"; fail=1
elif want_page 'a&lt;b &amp; c&gt;' '"mk_app.wasm"'; ! cmp -s "$mo/index.want" "$mo/index.html"; then
  echo "FAIL the default page ($inc), APP_WASM_SEPARATE: not shim/page.html naming mk_app.wasm, titled from APP_TITLE:"; diff "$mo/index.want" "$mo/index.html" | cut -c1-200 | head; fail=1
elif touch -t 203001010000 "$mo/mk_app.wasm"; mk -q "$mo/index.html" PAGE= APP_WASM_SEPARATE=1; then echo "FAIL the default page ($inc), APP_WASM_SEPARATE, is up to date with a newer module (its table comes from the module)"; fail=1
elif ! mk "$mo/index.html" PAGE= APP_WASM_SEPARATE=1 APP_WASM_URL='/w/"x".wasm' -B 2> "$mo/mk_sep.log" || ! grep -qxF 'cdom.start("/w/\"x\".wasm", "app");' "$mo/index.html"; then
  echo "FAIL the default page ($inc), APP_WASM_SEPARATE: APP_WASM_URL is not its start call's URL"; fail=1
else echo "ok   the default page ($inc), APP_WASM_SEPARATE: names mk_app.wasm (or APP_WASM_URL, as a JS string), made again with the module for its table, <title> APP_TITLE escaped"; fi
# An app's own page with the /*CDOM_WASM*/ marker: the module bundled there, or, separate, its URL.
for sep in '' 1; do
  if [ -n "$sep" ]; then arg='"mk_app.wasm"'; else arg="\"data:application/wasm;base64,$(b64 "$mo/mk_app.wasm")\""; fi
  if ! mk "$mo/marker.html" PAGE="$here/page/marker.html" APP_WASM_SEPARATE=$sep -B 2> "$mo/mk_marker.log"; then
    echo "FAIL APP_PAGE ($inc) refused a page with /*CDOM_WASM*/ (separate: ${sep:-no}):"; cat "$mo/mk_marker.log"; fail=1
  elif ! grep -qxF "cdom.start($arg, \"app\", null, { seed: 1 });" "$mo/marker.html"; then
    echo "FAIL APP_PAGE ($inc): the page's /*CDOM_WASM*/ is not the module (separate: ${sep:-no})"; fail=1
  else echo "ok   APP_PAGE ($inc) fills an app's own /*CDOM_WASM*/ with $([ -n "$sep" ] && echo 'the URL (APP_WASM_SEPARATE)' || echo 'the bundled module'), the rest of the call kept"; fi
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
# The toolchain check: a WASM_CC that cannot build a wasm32 module (Apple's clang, which finds no wasm-ld
# on a PATH without Homebrew's; a clang that is not there) stops make with the install command, before
# the compiler runs, and leaves no module.
for cc in /usr/bin/clang "$out/no-such-clang"; do
  [ -x "$cc" ] || [ "$cc" = "$out/no-such-clang" ] || continue
  rm -rf "$out/mk-cc"
  if env PATH=/usr/bin:/bin make -s --no-print-directory -f "$here/app.mk" INCLUDE=first OUT="$out/mk-cc" PAGE= WASM_CC="$cc" "$out/mk-cc/mk_app.wasm" > "$out/mk_cc.log" 2>&1; then
    echo "FAIL cdom.mk built a module with WASM_CC=$cc, which cannot build wasm32"; fail=1
  elif ! grep -qxF "cdom.mk: install them with: brew install llvm lld   (Debian or Ubuntu: apt install clang lld)" "$out/mk_cc.log" \
    || grep -q "linker command failed\|error:" "$out/mk_cc.log" || [ -e "$out/mk-cc/mk_app.wasm" ]; then
    echo "FAIL cdom.mk with WASM_CC=$cc, which cannot build wasm32, did not stop at the toolchain check:"; cat "$out/mk_cc.log"; fail=1
  else echo "ok   cdom.mk with WASM_CC=$cc, which cannot build wasm32, stops before compiling: $(grep 'install them' "$out/mk_cc.log")"; fi
done
# The stylesheets (docs/page-tool-design.md): cdom.mk writes every .css file under src/ where make runs
# into the page's <style>, in byte order of its path. The fixture apps of tests/link/apps, built with
# make -C, CDOM this checkout and OUT under tests/out, so nothing is written into the tree.
apps="$here/apps"
top="$(cd "$here/../.." && pwd)"
ak() { local app="$1"; shift; make -s --no-print-directory -C "$app" CDOM="$top" "$@"; }
order="10.css 9.css Z.css a.css a/b/deep.css a/x.css b/00.css"
sheets() { # dir files...: the files as the page holds them: the comments that fill their lines left out,
  # </style escaped, a newline after a last line without one
  local d="$1" f; shift
  for f in "$@"; do perl -0pe 's{^[ \t]*/\*.*?\*/[ \t\r]*(\n|\z)}{}gms; s{</(style)}{<\\/$1}gi' "$d/$f"; [ -n "$(tail -c1 "$d/$f")" ] && echo; done
}
heads() { grep -oE '/\* [^ ]+\.css \*/$' "$1" | sed -E 's|^/\* (.*) \*/$|\1|' | tr '\n' ' ' | sed 's/ $//'; }
# Discovery: the default page holds the seven stylesheets of tests/link/apps/css/src, in byte order of
# their paths, and nothing else of that tree.
mo="$out/apps/css-default"; rm -rf "$mo"
sheets "$apps/css/src" $order > "$out/apps.css.want"
if ! ak "$apps/css" OUT="$mo" 2> "$out/apps-css.log"; then echo "FAIL the css app does not build:"; cat "$out/apps-css.log"; fail=1
else
  { sed -n '1,5p' "$tpl"; echo "<title>css</title>"; sed -n '7p' "$tpl"; cat "$out/apps.css.want"; sed -n '9,13p' "$tpl"
    cat "$mo/app.wasm.lits"; echo "</script>"; echo "<script>"; cat "$src/../shim/cdom.min.js"
    echo "cdom.start(\"data:application/wasm;base64,$(base64 < "$mo/app.wasm" | tr -d '\n\r')\", \"app\");"; sed -n '16,$p' "$tpl"; } > "$mo/index.want"
  if ! cmp -s "$mo/index.want" "$mo/index.html"; then echo "FAIL discovery: the css app's default page is not shim/page.html with its seven stylesheets in byte order:"; diff "$mo/index.want" "$mo/index.html" | cut -c1-160 | head; fail=1
  else echo "ok   discovery: the default page holds src/'s seven stylesheets in byte order of their paths ($order), and none of its dot-names, .css.bak, .CSS or linked folder"; fi
fi
# Rebuilding: in a copy of the app, make -q says up to date after a build, and not after an edit, an added
# file (with an old time), a removed one, or a rename that moves a file in the order, each of which is
# then in the page as it should be; a touch alone rebuilds nothing, and the module is never rebuilt. make
# compares whole seconds here, so a step waits one out before it changes a file.
rb="$out/apps/css-rebuild"; rm -rf "$rb"; mkdir -p "$out/apps"; cp -R "$apps/css" "$rb"
rk() { ak "$rb" OUT=build "$@"; }
step() { # name, then what the page must hold after it ("same" for a step that must rebuild nothing)
  local name="$1" want="$2"
  if [ "$want" = same ]; then
    if rk -q; then echo "ok   rebuilding: $name, and make -q is still up to date"; else echo "FAIL rebuilding: $name made the page out of date"; fail=1; fi
  elif rk -q; then echo "FAIL rebuilding: $name, and make -q says the page is up to date"; fail=1
  elif ! rk 2> "$rb.log"; then echo "FAIL rebuilding: $name, and make fails:"; cat "$rb.log"; fail=1
  elif [ "$(heads "$rb/build/index.html")" != "$want" ]; then echo "FAIL rebuilding: $name: the page holds [$(heads "$rb/build/index.html")], not [$want]"; fail=1
  elif ! rk -q; then echo "FAIL rebuilding: $name: a second make is not up to date"; fail=1
  else echo "ok   rebuilding: $name: make -q is out of date, make puts the page right ($want)"; fi
  if ! rk -q build/app.wasm; then echo "FAIL rebuilding: $name made the module out of date"; fail=1; fi
}
if ! rk 2> "$rb.log" || ! { sleep 1; rk -q; }; then echo "FAIL rebuilding: the copy does not build, or a second make is not up to date:"; cat "$rb.log"; fail=1
else
  sleep 1; printf '#probe { --edited: "yes"; }\n' >> "$rb/src/a.css"; step "a.css edited" "$order"
  if ! grep -q -- '--edited' "$rb/build/index.html"; then echo "FAIL rebuilding: the edit is not in the page"; fail=1; fi
  sleep 1; printf '.n-new { order: 1; } /* a/new.css */\n' > "$rb/src/a/new.css"; touch -t 200001010000 "$rb/src/a/new.css"
  step "a/new.css added, dated 2000" "10.css 9.css Z.css a.css a/b/deep.css a/new.css a/x.css b/00.css"
  sleep 1; rm "$rb/src/a/new.css"; step "a/new.css removed" "$order"
  sleep 1; mv "$rb/src/10.css" "$rb/src/zz.css"; step "10.css renamed zz.css (last now)" "9.css Z.css a.css a/b/deep.css a/x.css b/00.css 10.css"
  sleep 1; touch "$rb/src/a.css" "$rb/src/zz.css"; step "two files touched" same
fi
# The first build: from an empty directory, with tools/cdpage not built yet (so the list cannot be written
# while make works out the prerequisites), one make writes the page with its stylesheets, and a second
# finds it up to date.
fb="$out/apps/css-first"; rm -rf "$fb"; rm -f "$CDOM_PAGE_TOOL"
if ! ak "$apps/css" OUT="$fb" 2> "$fb.log"; then echo "FAIL the first build fails:"; cat "$fb.log"; fail=1
elif [ "$(heads "$fb/index.html")" != "$order" ]; then echo "FAIL the first build: the page holds [$(heads "$fb/index.html")]"; fail=1
elif sleep 1; ! ak "$apps/css" OUT="$fb" -q; then echo "FAIL the first build: a second make is not up to date"; fail=1 # a second later: make compares whole seconds
elif [ ! -x "$CDOM_PAGE_TOOL" ]; then echo "FAIL the first build: make did not keep tools/cdpage, which it built as an intermediate file"; fail=1
else echo "ok   the first build, with tools/cdpage not built: one make writes the page with its stylesheets, a second is up to date, and the tool is kept"; fi
# An app's own page: with no /*CDOM_CSS*/ line and a .css file in src/, the build stops with the line that
# says what to do and writes nothing; a page that links a stylesheet kept outside src/ (static/) builds,
# keeps its <link>, and nothing is bundled.
nm="$out/apps/nomarker"; rm -rf "$nm"
line="page.html: src/ holds app.css, and this page has no /*CDOM_CSS*/ line (a <style> whose line is only the marker, before the shim's script); add it, or move the files out of src/"
if ak "$apps/nomarker" OUT="$nm" PAGE=page.html 2> "$nm.log"; then echo "FAIL an own page with no /*CDOM_CSS*/ took src/'s stylesheet"; fail=1
elif [ -e "$nm/index.html" ] || compgen -G "$nm/index.html.tmp*" > /dev/null; then echo "FAIL an own page with no /*CDOM_CSS*/ was refused but left a file"; fail=1
elif ! grep -qxF "$line" "$nm.log"; then echo "FAIL an own page with no /*CDOM_CSS*/ was refused with another message:"; cat "$nm.log"; fail=1
else echo "ok   an own page with no /*CDOM_CSS*/ and a stylesheet in src/ stops the build: $line"; fi
lk="$out/apps/linked"; rm -rf "$lk"
if ! ak "$apps/linked" OUT="$lk" PAGE=page.html 2> "$lk.log"; then echo "FAIL an own page that links static/app.css does not build:"; cat "$lk.log"; fail=1
elif ! grep -qxF '<link rel="stylesheet" href="static/app.css">' "$lk/index.html" || grep -q -- '--last' "$lk/index.html" || [ -s "$lk/index.html.csslist" ]; then
  echo "FAIL an own page that links static/app.css: the <link> is gone, or a stylesheet was bundled or listed"; fail=1
else echo "ok   an own page that links a stylesheet kept in static/ builds, keeps its <link>, and bundles and lists nothing"; fi
# In Chrome: the css app with css.html as its page, from disk (tests/link/css.mjs).
co="$out/apps/css-own"; rm -rf "$co"
if ! ak "$apps/css" OUT="$co" PAGE=css.html 2> "$co.log"; then echo "FAIL the css app with css.html does not build:"; cat "$co.log"; fail=1
else node "$here/css.mjs" "$co/index.html" || fail=1; fi
# The starter (make new, tools/new.sh), against rot: generated into a fresh directory (relative to where
# make was run, as a user types it), it is four files; make builds build/index.html, one file with the
# shim and the bundled module and no URL of a module, and a second make finds it up to date; opened from
# disk in headless Chrome it shows "cdom" centered (tools/starter.mjs). make new refuses a directory
# that is not empty.
st="$out/starter"
rm -rf "$st"; mkdir -p "$st"
if ! (cd "$st" && make -s --no-print-directory -C "$here/../.." new DIR=app > "$st/new.log" 2>&1); then
  echo "FAIL make new did not write the starter:"; cat "$st/new.log"; fail=1
elif [ "$(cd "$st/app" && find . -type f | sort | tr '\n' ' ')" != "./.gitignore ./Makefile ./src/app.c ./src/app.css " ]; then
  echo "FAIL make new wrote other files than Makefile, src/app.c, src/app.css and .gitignore: $(cd "$st/app" && find . -type f | sort | tr '\n' ' ')"; fail=1
elif ! nice -n 10 make -s --no-print-directory -C "$st/app" > "$st/make.log" 2>&1; then
  echo "FAIL the starter does not build:"; cat "$st/make.log"; fail=1
elif ! grep -qF "$(head -1 "$src/../shim/cdom.min.js")" "$st/app/build/index.html"; then echo "FAIL the starter's page has no shim"; fail=1
elif ! grep -qxF "cdom.start(\"data:application/wasm;base64,$(base64 < "$st/app/build/app.wasm" | tr -d '\n\r')\", \"app\");" "$st/app/build/index.html"; then
  echo "FAIL the starter's page does not start its bundled module"; fail=1
elif grep -qE "\.wasm['\"\`]" "$st/app/build/index.html"; then echo "FAIL the starter's page names a .wasm by URL: $(grep -oE ".{20}\.wasm['\"\`]" "$st/app/build/index.html" | head -1)"; fail=1
elif ! make -q -C "$st/app"; then echo "FAIL a second make in the starter builds again"; fail=1
elif ! grep -qxF '  justify-content: center;' "$st/app/build/index.html" || grep -q 'cd_open("style")' "$st/app/src/app.c"; then
  echo "FAIL the starter's page does not hold src/app.css, or its app.c still builds a <style>"; fail=1
else echo "ok   make new writes Makefile, src/app.c, src/app.css and .gitignore; make builds one page with the stylesheet, the shim and the module bundled, then finds it up to date"; fi
node "$here/../../tools/starter.mjs" "$st/app/build/index.html" || fail=1
if make -s --no-print-directory -C "$here/../.." new DIR="$st/app" > "$st/again.log" 2>&1; then echo "FAIL make new wrote into a directory that is not empty"; fail=1
elif ! grep -q "app is not empty" "$st/again.log"; then echo "FAIL make new refused a directory for another reason:"; cat "$st/again.log"; fail=1
else echo "ok   make new refuses a directory that is not empty"; fi
# The generated CDOM is a default (?=): the app copied elsewhere builds against the cdom the environment
# names (here a link to this checkout, so the path in the build is the override's), as a second clone,
# CI or a moved folder does.
rm -rf "$st/moved" "$st/cdom-link"; ln -s "$here/../.." "$st/cdom-link"; mkdir -p "$st/moved"
cp -R "$st/app/Makefile" "$st/app/src" "$st/moved/"
if ! env CDOM="$st/cdom-link" make --no-print-directory -C "$st/moved" > "$st/moved.log" 2>&1; then
  echo "FAIL the starter copied elsewhere does not build with CDOM from the environment:"; cat "$st/moved.log"; fail=1
elif ! grep -qF "$st/cdom-link/src/cdom.c" "$st/moved.log" || ! grep -qF "$st/cdom-link/shim/page.html" "$st/moved.log"; then
  echo "FAIL the starter copied elsewhere ignored CDOM from the environment (built with the path make new wrote)"; fail=1
elif [ ! -s "$st/moved/build/index.html" ]; then echo "FAIL the starter copied elsewhere wrote no page"; fail=1
else echo "ok   the starter copied elsewhere builds with CDOM from the environment overriding the generated default"; fi
node "$here/run.mjs" "$out" || fail=1
node "$here/../../tools/edges.mjs" || fail=1
# The image module (APP_MODULES += image) costs an app nothing it does not call, and its way out of a libjpeg error
# reaches only the apps that link it: tests/image/wasm.c built by cdom.mk with CDOM_IMAGE_SJLJ eh has wasm exception
# handling (try, catch, throw), and with js the shim's two imports and no exception handling; the app of app.mk,
# with no image module and with it but no call, has neither.
im="$out/image-link"; mkdir -p "$im"
eh_count() { wasm-dis --all-features "$1" | grep -cE '\b(try|catch|catch_all|throw|rethrow|delegate)\b'; }
img_imports() { wasm-dis --all-features "$1" | grep -cE '\(import "cdom" "image_(call|throw)"'; }
if ! make -s --no-print-directory -f "$here/../image/wasm.mk" OUT="$im" SJLJ=eh "$im/wasm_eh.wasm" 2> "$im/eh.log" ||
   ! make -s --no-print-directory -f "$here/../image/wasm.mk" OUT="$im" SJLJ=js "$im/wasm_js.wasm" 2> "$im/js.log"; then
  echo "FAIL the image module's wasm test app does not build:"; cat "$im/eh.log" "$im/js.log"; fail=1
elif [ "$(eh_count "$im/wasm_eh.wasm")" = 0 ] || [ "$(img_imports "$im/wasm_eh.wasm")" != 0 ]; then
  echo "FAIL CDOM_IMAGE_SJLJ eh: $(eh_count "$im/wasm_eh.wasm") exception-handling instructions (wanted some) and $(img_imports "$im/wasm_eh.wasm") of the shim's imports (wanted none)"; fail=1
elif [ "$(eh_count "$im/wasm_js.wasm")" != 0 ] || [ "$(img_imports "$im/wasm_js.wasm")" != 2 ]; then
  echo "FAIL CDOM_IMAGE_SJLJ js: $(eh_count "$im/wasm_js.wasm") exception-handling instructions (wanted none) and $(img_imports "$im/wasm_js.wasm") of the shim's 2 imports"; fail=1
else
  mo="$out/mk-first"
  bad=""
  for mods in 'random' 'random image'; do
    for sj in js eh; do
      if ! make -s --no-print-directory -f "$here/app.mk" INCLUDE=first OUT="$mo" PAGE= APP_MODULES="$mods" CDOM_IMAGE_SJLJ=$sj -B "$mo/mk_app.wasm" 2> "$im/mk.log"; then bad="$bad [$mods, $sj: no build]"; cat "$im/mk.log"
      elif [ "$(eh_count "$mo/mk_app.wasm")" != 0 ] || [ "$(img_imports "$mo/mk_app.wasm")" != 0 ]; then bad="$bad [$mods, $sj]"; fi
    done
  done
  if [ -n "$bad" ]; then echo "FAIL an app that does not call the image module carries its exception handling or its imports:$bad"; fail=1
  else echo "ok   the image module's way out reaches only apps that call it: eh gives them exception handling, js the shim's 2 imports; an app without it, or with it and no call, has neither, either way"; fi
fi
# CDOM_IMAGE_SIMD=1 puts wasm SIMD instructions (which a browser without wasm SIMD refuses to instantiate) only in an app
# that calls the image module: tests/image/wasm.c built with it has some, built without it (the default) none, and the
# app of app.mk with CDOM_IMAGE_SIMD=1, with no image module and with it but no call, none.
simd_count() { wasm-dis --all-features "$1" | grep -cE '\b(v128|i8x16|i16x8|i32x4|i64x2|f32x4|f64x2)\.'; }
if ! make -s --no-print-directory -f "$here/../image/wasm.mk" OUT="$im" SJLJ=js SIMD=1 "$im/wasm_js_simd.wasm" 2> "$im/simd.log" ||
   ! make -s --no-print-directory -f "$here/../image/wasm.mk" OUT="$im" SJLJ=js "$im/wasm_js.wasm" 2> "$im/js.log"; then
  echo "FAIL the image module's wasm test app does not build with and without CDOM_IMAGE_SIMD:"; cat "$im/simd.log" "$im/js.log"; fail=1
elif [ "$(simd_count "$im/wasm_js_simd.wasm")" = 0 ] || [ "$(simd_count "$im/wasm_js.wasm")" != 0 ]; then
  echo "FAIL CDOM_IMAGE_SIMD: $(simd_count "$im/wasm_js_simd.wasm") SIMD instructions with it (wanted some), $(simd_count "$im/wasm_js.wasm") without it (wanted none)"; fail=1
else
  mo="$out/mk-first"
  bad=""
  for mods in 'random' 'random image'; do
    if ! make -s --no-print-directory -f "$here/app.mk" INCLUDE=first OUT="$mo" PAGE= APP_MODULES="$mods" CDOM_IMAGE_SIMD=1 -B "$mo/mk_app.wasm" 2> "$im/mk.log"; then bad="$bad [$mods: no build]"; cat "$im/mk.log"
    elif [ "$(simd_count "$mo/mk_app.wasm")" != 0 ]; then bad="$bad [$mods: $(simd_count "$mo/mk_app.wasm")]"; fi
  done
  if [ -n "$bad" ]; then echo "FAIL an app that does not call the image module has SIMD instructions with CDOM_IMAGE_SIMD=1:$bad"; fail=1
  else echo "ok   CDOM_IMAGE_SIMD=1 gives SIMD instructions to an app that calls the image module ($(simd_count "$im/wasm_js_simd.wasm")) and none without it; an app without the module, or with it and no call, has none"; fi
fi
node "$here/../../tools/seeds.mjs" || fail=1
node "$here/../../tools/strings.mjs" || fail=1
exit $fail
