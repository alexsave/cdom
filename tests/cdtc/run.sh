#!/bin/bash
# The template compiler's native tests (no browser):
#  1. goldens: every fixtures/*.cdt compiles to exactly fixtures/<name>.{h,c}.golden, and the
#     generated C compiles with -Wall -Wextra -Werror natively and for wasm32;
#  2. errors: every errors/*.cdt fails with exit 1 and exactly the line in errors/<name>.err;
#  3. round trip: feat.cdt linked with cdom.c and the C sink renders the HTML roundtrip.c expects;
#  5. nested lists: a click on every listener of fixtures/nested.cdt reaches the record it shows,
#     and the page shows every level in the state's order, after a reorder and a removal too;
#  4. op counts: the templated rows app makes the hand-written app's DOM calls for every action,
#     5 a row on create (two slot texts, clone, listen, close) and one for a label patch.
# Usage: tests/cdtc/run.sh        UPDATE=1 rewrites the goldens (read the diff before committing)
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
top="$(cd "$here/../.." && pwd)"
out="$top/tests/out/cdtc"
WCC="${WASM_CC:-/opt/homebrew/opt/llvm/bin/clang}"
CFLAGS=(-O2 -Wall -Wextra -Werror)
mkdir -p "$out"
fails=0
ok() { echo "ok   $*"; }
bad() { echo "FAIL $*"; fails=$((fails + 1)); }
nice -n 10 cc "${CFLAGS[@]}" -o "$out/cdtc" "$top/tools/cdtc.c" || { echo "FAIL cdtc does not build"; exit 1; }

# 1. goldens
for f in "$here"/fixtures/*.cdt; do
  b="$(basename "$f" .cdt)"
  if ! (cd "$here/fixtures" && "$out/cdtc" "$b.cdt" -o "$out/$b"); then bad "golden $b: cdtc failed"; continue; fi
  if [ -n "${UPDATE:-}" ]; then cp "$out/$b.h" "$here/fixtures/$b.h.golden"; cp "$out/$b.c" "$here/fixtures/$b.c.golden"; fi
  if cmp -s "$out/$b.h" "$here/fixtures/$b.h.golden" && cmp -s "$out/$b.c" "$here/fixtures/$b.c.golden"; then
    ok "golden $b: generated .h and .c equal the goldens byte for byte"
  else
    bad "golden $b: generated code differs from the golden"
    diff -u "$here/fixtures/$b.h.golden" "$out/$b.h" | head -20
    diff -u "$here/fixtures/$b.c.golden" "$out/$b.c" | head -40
  fi
  if nice -n 10 cc "${CFLAGS[@]}" -I "$top/src" -I "$out" -c -o "$out/$b.o" "$out/$b.c" &&
     nice -n 10 "$WCC" --target=wasm32 "${CFLAGS[@]}" -nostdlib -mbulk-memory -I "$top/src" -I "$out" -c -o "$out/$b.wasm.o" "$out/$b.c"; then
    ok "golden $b: compiles with -Wall -Wextra -Werror, native and wasm32"
  else
    bad "golden $b: generated C does not compile cleanly"
  fi
done

# 2. errors
for f in "$here"/errors/*.cdt; do
  b="$(basename "$f" .cdt)"
  (cd "$here/errors" && "$out/cdtc" "$b.cdt" -o "$out/err_$b" 2> "$out/$b.stderr" > /dev/null)
  code=$?
  if [ $code -eq 1 ] && cmp -s "$out/$b.stderr" "$here/errors/$b.err"; then
    ok "error $b: $(cat "$out/$b.stderr")"
  else
    bad "error $b: exit $code, want: $(cat "$here/errors/$b.err") got: $(cat "$out/$b.stderr")"
  fi
done

# 3. round trip
(cd "$here" && "$out/cdtc" feat.cdt -o "$out/feat") &&
  nice -n 10 cc "${CFLAGS[@]}" -I "$top/src" -I "$out" -o "$out/roundtrip" "$here/roundtrip.c" "$out/feat.c" \
    "$top/src/cdom.c" "$top/src/cdom_html.c" || { bad "round trip does not build"; }
if [ -x "$out/roundtrip" ]; then
  if "$out/roundtrip" > "$out/roundtrip.log" 2>&1; then ok "round trip: $(grep -c '^ok' "$out/roundtrip.log") checks"; else bad "round trip"; fi
  sed 's/^/     /' "$out/roundtrip.log"
fi

# 5. nested lists: every listener on every level finds its own record
"$out/cdtc" "$here/fixtures/nested.cdt" -o "$out/nested" &&
  nice -n 10 cc "${CFLAGS[@]}" -I "$top/src" -I "$out" -o "$out/nested_find" "$here/nested_find.c" "$out/nested.c" \
    "$top/src/cdom.c" "$top/src/cdom_html.c" || bad "nested find does not build"
if [ -x "$out/nested_find" ]; then
  if "$out/nested_find" > "$out/nested_find.log" 2>&1; then ok "nested lists: $(grep -c "^ok" "$out/nested_find.log") checks"; else bad "nested lists"; fi
  sed 's/^/     /' "$out/nested_find.log"
fi

# 4. op counts against the hand-written app
"$out/cdtc" "$top/bench/cdom-tpl/rows.cdt" -o "$out/rows" &&
  nice -n 10 cc "${CFLAGS[@]}" -DTPL -I "$top/src" -I "$out" -o "$out/opcount_tpl" "$here/opcount.c" \
    "$top/bench/cdom-tpl/app.c" "$out/rows.c" "$top/src/cdom.c" "$top/src/cdom_html.c" &&
  nice -n 10 cc "${CFLAGS[@]}" -I "$top/src" -o "$out/opcount_hand" "$here/opcount.c" \
    "$top/bench/cdom-keyed/src/app.c" "$top/src/cdom.c" "$top/src/cdom_html.c" || bad "op count drivers do not build"
"$out/opcount_hand" > "$out/ops_hand.txt"
"$out/opcount_tpl" > "$out/ops_tpl.txt"
paste "$out/ops_hand.txt" "$out/ops_tpl.txt" | awk '{printf "     %-16s hand %7s  template %7s\n", $1, $2, $4}'
if cmp -s "$out/ops_hand.txt" "$out/ops_tpl.txt"; then ok "op counts: the template app costs the hand-written ops for every action"; else bad "op counts differ from the hand-written app"; fi
run=$(awk '$1 == "run1000" {print $2}' "$out/ops_tpl.txt")
upd=$(awk '$1 == "update10th" {print $2}' "$out/ops_tpl.txt")
# 1000 creates + detach, one at and attach around the off-document fill; a slot whose value
# did not change since the last clone is not sent again (the seed gives one repeated label)
if [ "$run" -ge 5000 ] && [ "$run" -le 5003 ]; then ok "op counts: a templated row creates in 5 calls (run 1000 = $run)"; else bad "op counts: a templated row creates in 5 calls: run 1000 = $run, want 5000-5003 (5 a row + 3, less unchanged slots)"; fi
if [ "$upd" = 100 ]; then ok "op counts: a label patch is one call (update every 10th of 1000 = $upd)"; else bad "op counts: a label patch is one call: update every 10th of 1000 = $upd, want 100"; fi

echo
if [ $fails -eq 0 ]; then echo "cdtc tests: all ok"; else echo "cdtc tests: $fails FAILED"; fi
exit $((fails != 0))
