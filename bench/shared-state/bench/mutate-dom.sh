#!/bin/bash
# Mutation check of check.mjs's DOM check: each mutant breaks one entry's source in place (a copy is kept
# and put back, whatever happens), rebuilds that entry, and the DOM check must FAIL; then the entry is
# rebuilt from the restored source. Usage: bench/shared-state/bench/mutate-dom.sh
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"; top="$here/../../.."
status=0
# file | entry | perl substitution
mutants=(
  'entry_l.c|L|s/  if \(\(rev \^ it->data\) & 1\) cd_set_class\(r->tr, sel_cls\[rev & 1\]\);\n//'
  'entry_c.c|C|s/uint32_t sum = currency_ver \+ m->rev, sel/uint32_t sum = m->rev, sel/'
  'entry_e.c|E|s/  cd_watch_at\(&r->w\[1\], &selected_state, row_view, r, &r->w\[0\]\);\n//'
)
for m in "${mutants[@]}"; do
    IFS='|' read -r file entry sub <<< "$m"
    f="$here/src/$file"
    cp "$f" "$f.orig"
    perl -0pi -e "$sub" "$f"
    if cmp -s "$f" "$f.orig"; then echo "MUTANT $file: changed nothing"; status=1; mv "$f.orig" "$f"; continue; fi
    echo "== mutant $file ($entry)"; diff "$f.orig" "$f" | sed 's/^/   /'
    if ENTRIES="$entry" "$here/build.sh" > /dev/null; then
        res="$(node "$here/check.mjs" dom 2>&1)"
        echo "$res" | grep '^FAIL' | sed 's/^/   /'
        if echo "$res" | grep -q '^FAIL'; then echo "   KILLED"; else echo "   SURVIVED"; status=1; fi
    else echo "   does not build"; status=1; fi
    mv "$f.orig" "$f"
    ENTRIES="$entry" "$here/build.sh" > /dev/null
done
node "$here/check.mjs" dom | tail -1
exit $status
