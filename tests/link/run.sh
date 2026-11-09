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
if ! mk "$mo/ok.html" PAGE="$here/page/ok.html" APP_WASM_SEPARATE=1 2> "$mo/mk_page.log"; then
  echo "FAIL cdom_page ($inc) refused a page whose marker is alone on its line:"; cat "$mo/mk_page.log"; fail=1
elif ! cmp -s "$want" "$mo/ok.html"; then echo "FAIL cdom_page ($inc): the page is not itself with the shim at its marker line"; fail=1
else echo "ok   cdom_page ($inc) inlines shim/cdom.min.js at the page's /*CDOM_SHIM*/ line, every other line kept (APP_WASM_SEPARATE: the page's own URL)"; fi
# The refused pages, with the module separate: each is refused for its own fault, which bundling would
# otherwise mask (a page whose start call went with its shim line, or that takes a second module, has no
# bundled module to start either).
for bad in inline:'must be alone on its line' none:'no /\*CDOM_SHIM\*/ line' two:'a second /\*CDOM_SHIM\*/' two_wasm:'a second /\*CDOM_WASM\*/'; do
  name="${bad%%:*}" why="${bad#*:}"
  if mk "$mo/$name.html" PAGE="$here/page/$name.html" APP_WASM_SEPARATE=1 2> "$mo/mk_$name.log"; then
    echo "FAIL cdom_page ($inc) took the $name page"; fail=1
  elif [ -e "$mo/$name.html" ] || [ -e "$mo/$name.html.tmp" ]; then
    echo "FAIL cdom_page ($inc) refused the $name page but left a file"; fail=1
  elif ! grep -q "$name.html.*$why" "$mo/mk_$name.log"; then
    echo "FAIL cdom_page ($inc) refused the $name page for another reason:"; cat "$mo/mk_$name.log"; fail=1
  else echo "ok   cdom_page ($inc) refuses the $name page ($(sed 's/.*: //' "$mo/mk_$name.log" | head -1)) and writes nothing"; fi
done
# Bundling, the default: an app's own page that names its module by URL is refused with the one line that
# says what to do, and nothing is written (never a rewrite of the app's own call).
rm -f "$mo/ok.html"
line="$here/page/ok.html: this page loads mk_app.wasm; set APP_WASM_SEPARATE := 1, or start with the bundled module (the /*CDOM_WASM*/ marker)"
if mk "$mo/ok.html" PAGE="$here/page/ok.html" 2> "$mo/mk_url.log"; then echo "FAIL APP_PAGE ($inc) bundled into a page that loads its module by URL"; fail=1
elif [ -e "$mo/ok.html" ] || [ -e "$mo/ok.html.tmp" ]; then echo "FAIL APP_PAGE ($inc) refused a page that loads its module by URL but left a file"; fail=1
elif ! grep -qxF "$line" "$mo/mk_url.log"; then echo "FAIL APP_PAGE ($inc) refused a page that loads its module by URL with another message:"; cat "$mo/mk_url.log"; fail=1
else echo "ok   APP_PAGE ($inc), bundling, refuses a page of the app's own that loads mk_app.wasm, with the line that says what to do"; fi
# The default page (no APP_PAGE_SRC), bundled: shim/page.html with the title, the shim, and the module as
# a data: URL whose bytes are mk_app.wasm's; the page is made again when the module is newer. With
# APP_WASM_SEPARATE the start call names the module beside the page (or APP_WASM_URL) and the page does
# not depend on it. APP_TITLE is escaped for HTML.
tpl="$src/../shim/page.html"
want_page() { # title, then the start call's first argument
  { sed -n '1,5p' "$tpl"; echo "<title>$1</title>"; sed -n '7,10p' "$tpl"; cat "$src/../shim/cdom.min.js"
    echo "cdom.start($2, \"app\");"; sed -n '13,$p' "$tpl"; } > "$mo/index.want"
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
elif touch -t 203001010000 "$mo/mk_app.wasm"; ! mk -q "$mo/index.html" PAGE= APP_WASM_SEPARATE=1; then echo "FAIL the default page ($inc), APP_WASM_SEPARATE, depends on the module"; fail=1
elif ! mk "$mo/index.html" PAGE= APP_WASM_SEPARATE=1 APP_WASM_URL='/w/"x".wasm' -B 2> "$mo/mk_sep.log" || ! grep -qxF 'cdom.start("/w/\"x\".wasm", "app");' "$mo/index.html"; then
  echo "FAIL the default page ($inc), APP_WASM_SEPARATE: APP_WASM_URL is not its start call's URL"; fail=1
else echo "ok   the default page ($inc), APP_WASM_SEPARATE: names mk_app.wasm (or APP_WASM_URL, as a JS string), no dependency on it, <title> APP_TITLE escaped"; fi
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
  elif ! grep -qxF "cdom.mk: install them with: brew install llvm lld   (Debian or Ubuntu: apt install clang lld, and set WASM_CC := clang)" "$out/mk_cc.log" \
    || grep -q "linker command failed\|error:" "$out/mk_cc.log" || [ -e "$out/mk-cc/mk_app.wasm" ]; then
    echo "FAIL cdom.mk with WASM_CC=$cc, which cannot build wasm32, did not stop at the toolchain check:"; cat "$out/mk_cc.log"; fail=1
  else echo "ok   cdom.mk with WASM_CC=$cc, which cannot build wasm32, stops before compiling: $(grep 'install them' "$out/mk_cc.log")"; fi
done
# The starter (make new, tools/new.sh), against rot: generated into a fresh directory (relative to where
# make was run, as a user types it), it is three files; make builds build/index.html, one file with the
# shim and the bundled module and no URL of a module, and a second make finds it up to date; opened from
# disk in headless Chrome it shows "cdom" centered (tools/starter.mjs). make new refuses a directory
# that is not empty.
st="$out/starter"
rm -rf "$st"; mkdir -p "$st"
if ! (cd "$st" && make -s --no-print-directory -C "$here/../.." new DIR=app > "$st/new.log" 2>&1); then
  echo "FAIL make new did not write the starter:"; cat "$st/new.log"; fail=1
elif [ "$(cd "$st/app" && find . -type f | sort | tr '\n' ' ')" != "./.gitignore ./Makefile ./src/app.c " ]; then
  echo "FAIL make new wrote other files than Makefile, src/app.c and .gitignore: $(cd "$st/app" && find . -type f | sort | tr '\n' ' ')"; fail=1
elif ! nice -n 10 make -s --no-print-directory -C "$st/app" > "$st/make.log" 2>&1; then
  echo "FAIL the starter does not build:"; cat "$st/make.log"; fail=1
elif ! grep -qF "$(head -1 "$src/../shim/cdom.min.js")" "$st/app/build/index.html"; then echo "FAIL the starter's page has no shim"; fail=1
elif ! grep -qxF "cdom.start(\"data:application/wasm;base64,$(base64 < "$st/app/build/app.wasm" | tr -d '\n\r')\", \"app\");" "$st/app/build/index.html"; then
  echo "FAIL the starter's page does not start its bundled module"; fail=1
elif grep -qE "\.wasm['\"\`]" "$st/app/build/index.html"; then echo "FAIL the starter's page names a .wasm by URL: $(grep -oE ".{20}\.wasm['\"\`]" "$st/app/build/index.html" | head -1)"; fail=1
elif ! make -q -C "$st/app"; then echo "FAIL a second make in the starter builds again"; fail=1
else echo "ok   make new writes Makefile, src/app.c and .gitignore; make builds one page with the shim and the module bundled, then finds it up to date"; fi
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
node "$here/../../tools/seeds.mjs" || fail=1
exit $fail
