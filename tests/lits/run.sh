#!/bin/bash
# tools/cdlits and its literal macro, standing alone (docs/strings-design.md, "tools/cdlits.c" and the test
# plan's tests/lits rows): the lexer against clang's own bytes (the differential, torture.c), which call
# becomes a slot (kinds.c), the same slots at -O0, -Oz and -Oz -flto, the table text run in Node, the strip,
# every build error, and the compile errors. Builds into tests/out/lits; nothing is left in the tree.
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
top="$here/../.."
out="$here/../out/lits"
CC="${WASM_CC:-/opt/homebrew/opt/llvm/bin/clang}"
CDLITS="$top/tools/out/cdlits"
mkdir -p "$out" "$top/tools/out"
. "$top/tools/cdomflags.sh" # CDOM_CFLAGS and CDOM_LDFLAGS, cdom.mk's set for apps
F=("${CDOM_CFLAGS[@]}" -I"$here" -Wall -Wextra -Werror "${CDOM_LDFLAGS[@]}")
pass=0
fail=0
ok() { echo "ok   $*"; pass=$((pass + 1)); }
bad() { echo "FAIL $*"; fail=$((fail + 1)); }

# The tool: built like cdom.mk will build it (a temporary name, renamed).
if cc -O2 -Wall -Wextra -Werror -o "$CDLITS.tmp$$" "$top/tools/cdlits.c" 2> "$out/cc.log"; then
  mv "$CDLITS.tmp$$" "$CDLITS"; ok "tools/cdlits.c builds with -Wall -Wextra -Werror"
else
  bad "tools/cdlits.c does not build:"; cat "$out/cc.log"; echo "tests/lits: 0 passed, 1 failed"; exit 1
fi

# bw out.wasm flags... sources...: a module as an app links. The log goes to out.wasm.log.
bw() { local o="$1"; shift; "$CC" "${F[@]}" -o "$o" "$@" 2> "$o.log"; }
# strip_to linked out: the build step, to out.wasm and out.lits. The log goes to out.log.
strip_to() { "$CDLITS" "$1" "$2.wasm" "$2.lits" > "$2.log" 2>&1; }
# nodelog out base fn...: what the stripped module's strings are, as the Node stub sees them
nodelog() { local b="$1"; shift; node "$here/stub.mjs" run "$b.wasm" "$b.lits" "$@"; }
# same name what expected-file actual-file: one line saying whether two texts agree
same() { if [ "$(cat "$2")" == "$(cat "$3")" ]; then ok "$1"; else bad "$1:"; diff "$2" "$3" | head -20; fi; }
# expect_err name message cmd...: the command exits 1 and its output says the message
expect_err() {
  local name="$1" msg="$2"; shift 2
  local log="$out/err_$$.log" rc=0
  "$@" > "$log" 2>&1 || rc=$?
  if [ "$rc" -eq 1 ] && grep -qF -- "$msg" "$log"; then ok "error: $name"
  else bad "error: $name (exit $rc, wanted 1 and \"$msg\"):"; head -5 "$log"; fi
}

# ---- the differential: the lexer against the compiler's own bytes ----
for opt in -O0 -Oz; do
  for src in torture kinds table; do
    n="$out/chk_${src}_${opt#-}"
    if ! bw "$n.wasm" "$opt" -DCD_LITS_CHECK "$here/$src.c" "$here/stub.c"; then bad "differential: $src $opt does not build:"; cat "$n.wasm.log"; continue; fi
    if "$CDLITS" --check "$n.wasm" > "$n.log" 2>&1; then ok "differential: $src $opt, $(cat "$n.log")"
    else bad "differential: $src $opt:"; cat "$n.log"; fi
  done
done

# ---- kinds: which call is a slot, and what string each shows ----
cat > "$out/kinds.slots" <<'EOT'
"a" "b"
"class"
"danger-s"
"data-x"
"data-x"
"div"
""
"hi-macro"
"kept"
EOT
cat > "$out/kinds.expect" <<'EOT'
text:div
text:ab
text:hi-macro
text:Stack
text:from-a-pointer
text:name-one
text:mut
text:nut
text:const-array
text:danger
text:danger-s
text:via-helper
text:kept
text:tmp
text:tmp-str
attr:class=tmp-str
attr:data-x=data-x
EOT
KFNS=(k_literal k_concat k_macro k_stack k_pointer k_table k_mut k_mutate k_mut k_const_array k_ternary k_ternary_s k_helper k_cdstr k_twice)
sort "$out/kinds.slots" > "$out/kinds.slots.sorted"
for b in O0 Oz Oz-flto; do
  case "$b" in O0) o=(-O0);; Oz) o=(-Oz);; Oz-flto) o=(-Oz -flto);; esac
  n="$out/kinds_$b"
  if ! bw "$n.linked.wasm" "${o[@]}" "$here/kinds.c" "$here/stub.c"; then bad "kinds $b does not build:"; cat "$n.linked.wasm.log"; continue; fi
  "$CDLITS" --list "$n.linked.wasm" > "$n.list" 2> "$n.list.log" || { bad "kinds $b: --list failed:"; cat "$n.list.log"; continue; }
  cut -f2- "$n.list" | sort > "$n.slots"
  same "kinds $b: the slots are exactly the literals" "$out/kinds.slots.sorted" "$n.slots"
  if strip_to "$n.linked.wasm" "$n"; then
    nodelog "$n" "${KFNS[@]}" > "$n.node" 2> "$n.node.log" || cat "$n.node.log"
    same "kinds $b: the strings the page shows" "$out/kinds.expect" "$n.node"
  else bad "kinds $b: cdlits failed:"; cat "$n.log"; fi
done
same "three builds: the same --list output at -O0 and -Oz" "$out/kinds_O0.list" "$out/kinds_Oz.list"
same "three builds: the same --list output at -Oz and -Oz -flto" "$out/kinds_Oz.list" "$out/kinds_Oz-flto.list"
same "three builds: the same strings at -O0 and -Oz -flto" "$out/kinds_O0.node" "$out/kinds_Oz-flto.node"

# ---- the table text ----
n="$out/table"
cat > "$n.expect" <<'EOT'
text:1first-digit
text:same
text:same
text:same
text:</script><!-- ok
text:quote " back \u005c end
text:line\u000abreak and \u2028 sep \u2029 
text:
text:0 digit after the separator
text:tab\u0009\u0001 \u007f del
text:☃ snowman, 😀 and hé
text:tail\u005c
EOT
if bw "$n.linked.wasm" -Oz "$here/table.c" "$here/stub.c" && strip_to "$n.linked.wasm" "$n"; then
  nodelog "$n" k_table > "$n.node" 2> "$n.node.log" || cat "$n.node.log"
  same "table: every entry comes back byte for byte (three equal, </script>, quote, backslash, newline, U+2028, a digit after a separator)" "$n.expect" "$n.node"
  if [ "$(grep -c '' "$n.lits")" -eq 1 ] && ! grep -q '<' "$n.lits"; then ok "table: one line, no '<' in it"; else bad "table: the lits file is not one line without '<'"; fi
  if [ "$(grep -o 'same' "$n.lits" | wc -l | tr -d ' ')" -eq 3 ]; then ok "table: a literal at three sites is three entries"; else bad "table: 'same' is not written three times"; fi
else bad "table: build or cdlits failed:"; cat "$n.linked.wasm.log" "$n.log" 2> /dev/null; fi

# ---- the strip ----
n="$out/kinds_Oz"
MARKS=(hi-macro danger-s kept data-x)
GONE=(__start_cd_lits __stop_cd_lits __start_cd_chars __stop_cd_chars cd_lits_stamp)
has() { LC_ALL=C grep -c -F -- "$1" "$2" > /dev/null; }
bad_marks=""
for m in "${MARKS[@]}"; do has "$m" "$n.linked.wasm" || bad_marks="$bad_marks $m"; done
for e in "${GONE[@]}"; do has "$e" "$n.linked.wasm" || bad_marks="$bad_marks $e"; done
if [ -z "$bad_marks" ]; then ok "strip: the linked module holds the literals and the five exports (the control)"; else bad "strip: control: the linked module lacks:$bad_marks"; fi
strip_check() { # name module
  local name="$1" m="$2" left=""
  for x in "${MARKS[@]}" "${GONE[@]}"; do has "$x" "$m" && left="$left $x"; done
  if [ -z "$left" ]; then ok "strip: $name has no literal byte and none of the five exports"; else bad "strip: $name still holds:$left"; fi
  if command -v wasm-validate > /dev/null; then
    if wasm-validate --enable-bulk-memory "$m" > "$m.validate" 2>&1 || wasm-validate "$m" > "$m.validate" 2>&1; then ok "strip: $name validates"
    else bad "strip: $name does not validate:"; head -5 "$m.validate"; fi
  else echo "skip strip: wasm-validate is not installed"; fi
}
strip_check "the stripped module" "$n.wasm"
# A module another tool repacked: every data segment merged into one, so cd_chars is cut out of the middle.
if node "$here/stub.mjs" merge "$n.linked.wasm" "$out/merged.linked.wasm" && strip_to "$out/merged.linked.wasm" "$out/merged"; then
  strip_check "the merged module" "$out/merged.wasm"
  nodelog "$out/merged" "${KFNS[@]}" > "$out/merged.node" 2> "$out/merged.node.log" || cat "$out/merged.node.log"
  same "strip: the merged module still shows every string" "$out/kinds.expect" "$out/merged.node"
else bad "strip: the merged module: cdlits failed:"; cat "$out/merged.log" 2> /dev/null; fi

# ---- the build errors ----
e="$out/e"
cat > "$e.plus.c" <<'EOT'
#include "stub.h"
EX(k) void k(void) { cd_text("abc" + 1); }
EOT
cat > "$e.wide.c" <<'EOT'
#include "stub.h"
/* a slot by hand: the macro never classifies L"x" as a literal, but a tool must still refuse the spelling */
static const char c[] __attribute__((section("cd_chars"), used, aligned(1))) = "L\"x\"";
static const cd_lit_slot l __attribute__((section("cd_lits"), used)) = {c};
EX(k) void k(void) {}
EOT
cat > "$e.nul.c" <<'EOT'
#include "stub.h"
EX(k) void k(void) { cd_text("a\0b"); }
EOT
cat > "$e.utf8.c" <<'EOT'
#include "stub.h"
EX(k) void k(void) { cd_text("bad \xff byte"); }
EOT
cat > "$e.ok.c" <<'EOT'
#include "stub.h"
EX(k) void k(void) { cd_text("fine"); }
EOT
for c in plus wide nul utf8; do bw "$e.$c.wasm" -Oz -Wno-string-plus-int "$e.$c.c" "$here/stub.c" || { bad "error case $c does not build:"; cat "$e.$c.wasm.log"; }; done
rm -f "$e.plus.o.wasm" "$e.plus.o.lits"
expect_err '"abc" + 1' 'not a string literal: "abc" + 1, write the literal itself or pass cd_tmpz(...)' "$CDLITS" "$e.plus.wasm" "$e.plus.o.wasm" "$e.plus.o.lits"
expect_err 'L"x"' 'not a string literal: L"x"' "$CDLITS" "$e.wide.wasm" "$e.wide.o.wasm" "$e.wide.o.lits"
expect_err 'a NUL inside a literal' 'a NUL inside a literal' "$CDLITS" "$e.nul.wasm" "$e.nul.o.wasm" "$e.nul.o.lits"
expect_err 'bytes that are not UTF-8' 'not UTF-8' "$CDLITS" "$e.utf8.wasm" "$e.utf8.o.wasm" "$e.utf8.o.lits"
bw "$e.nostamp.wasm" -Oz -DNO_STAMP "$e.ok.c" "$here/stub.c" || { bad "error case nostamp does not build:"; cat "$e.nostamp.wasm.log"; }
expect_err 'a module without cd_lits_stamp' 'not a cdom module (link src/cdom.c with CDOM_LDFLAGS)' "$CDLITS" "$e.nostamp.wasm" "$e.nostamp.o.wasm" "$e.nostamp.o.lits"
bw "$e.again.wasm" -Oz -DSTAMP_INIT=5 "$e.ok.c" "$here/stub.c" || { bad "error case again does not build:"; cat "$e.again.wasm.log"; }
expect_err 'a stamp that is not its initial value' 'already processed' "$CDLITS" "$e.again.wasm" "$e.again.o.wasm" "$e.again.o.lits"
bw "$e.ok.wasm" -Oz "$e.ok.c" "$here/stub.c" && strip_to "$e.ok.wasm" "$e.once" || bad "error case once does not build"
expect_err 'a module processed twice' 'not a cdom module' "$CDLITS" "$e.once.wasm" "$e.twice.wasm" "$e.twice.lits"
expect_err 'no arguments' 'usage:' "$CDLITS"
# a failed run must leave no output behind
if [ ! -e "$e.plus.o.wasm" ] && [ ! -e "$e.plus.o.lits" ]; then ok "error: a failed run writes no output"; else bad "error: a failed run left its outputs"; fi

# ---- the compile errors: a non-string is not a string argument ----
ce() { # name body message: the file must not compile, and the compiler's message is the one expected
  local f="$out/ce_$1.c"
  printf '#include "stub.h"\nEX(k) void k(uint32_t some_id) { (void)some_id; %s }\n' "$2" > "$f"
  if "$CC" "${CDOM_CFLAGS[@]}" -I"$here" -c -o "$f.o" "$f" 2> "$f.log"; then bad "compile error: $1 compiles"
  elif grep -qF -- "$3" "$f.log"; then ok "compile error: $1 ($3)"
  else bad "compile error: $1 fails for another reason:"; head -5 "$f.log"; fi
}
printf '#include "stub.h"\nEX(k) void k(uint32_t some_id) { (void)some_id; cd_text("fine"); cd_str a = cd_s("x"), b = cd_s("y"); (void)(a.v != b.v); }\n' > "$out/ce_control.c"
if "$CC" "${CDOM_CFLAGS[@]}" -I"$here" -Wall -Wextra -Werror -c -o "$out/ce_control.o" "$out/ce_control.c" 2> "$out/ce_control.log"; then ok "compile errors: the control file compiles"
else bad "compile errors: the control file does not compile:"; cat "$out/ce_control.log"; fi
ce id 'cd_text(some_id);' 'incompatible integer to pointer conversion'
ce int 'cd_text(5);' 'incompatible integer to pointer conversion'
ce compare 'cd_str a = cd_s("x"), b = cd_s("y"); (void)(a != b);' 'invalid operands to binary expression'

echo "tests/lits: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
