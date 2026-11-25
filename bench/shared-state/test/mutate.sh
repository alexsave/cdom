#!/bin/bash
# Mutation check of test.mjs: each mutant is a copy of ../lib/cd_state.c with one line broken (the library
# itself is never edited), built into out/mut-<name>, and run; the mutant must make the named check FAIL.
# Usage: bench/shared-state/test/mutate.sh   (prints each mutant's FAIL lines and KILLED or SURVIVED)
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
lib="$here/../lib/cd_state.c"
status=0
# name | perl substitution on the library | the check number that must fail
mutants=(
  'queue-null|s/^\s*if \(w->slot\) queue\[w->slot - 1\] = nullptr;.*$//m|^FAIL (3|7)\.'
  'round-dedup|s/^\s*if \(v->round == round_no\) continue;\n//m|^FAIL 1\.'
  'no-cleanup|s/^\s*cd_on_free\(nullptr, region_gone, w\);.*$/  (void)region_gone;/m|^FAIL (2|6)\.'
  'read-always|s/if \(w->view == running\) return;/return;/|^FAIL 8\.'
)
for m in "${mutants[@]}"; do
    IFS='|' read -r name sub want <<< "$m"
    d="$here/out/mut-$name"; mkdir -p "$d"
    perl -0pe "$sub" "$lib" > "$d/cd_state.c"
    if cmp -s "$lib" "$d/cd_state.c"; then echo "MUTANT $name: the substitution changed nothing"; status=1; continue; fi
    LIB="$d/cd_state.c" OUT="$d" "$here/build.sh" || { echo "MUTANT $name: does not build"; status=1; continue; }
    res="$(node "$here/test.mjs" "out/mut-$name" 2>&1)"
    echo "== mutant $name"; diff <(cat "$lib") "$d/cd_state.c" | sed 's/^/   /'
    echo "$res" | grep -E '^FAIL' | sed 's/^/   /'
    if echo "$res" | grep -Eq "$want"; then echo "   KILLED (wanted $want)"; else echo "   SURVIVED (wanted $want)"; status=1; fi
done
exit $status
