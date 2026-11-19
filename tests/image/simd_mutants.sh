#!/bin/bash
# Mutation check of tests/image/simd.c (docs/image-simd-hand.md): each mutant of simd_mutants.py is built into a copy of the
# sources and run; the run must fail, on the line of the loop the mutant names ("FAIL <loop>"). Prints one line per
# mutant. Usage: tests/image/simd_mutants.sh [names...]
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
out="$here/../out/image-simd-mutant"
miss=0
list="$(python3 -I "$here/simd_mutants.py" --list)"
while IFS=$'\t' read -r name loop what; do
  if [ $# -gt 0 ] && [[ " $* " != *" $name "* ]]; then continue; fi
  log="$(SIMD_MUTANT="$name" "$here/simd.sh" "$out" 2>&1)"; rc=$?
  if [ $rc -ne 0 ] && echo "$log" | grep -q "^FAIL $loop"; then
    echo "caught  $name  ($loop): $(echo "$log" | grep "^FAIL $loop" | head -1 | sed 's/^FAIL [^:]*: //')"
  else
    echo "MISSED  $name  ($loop; rc $rc): $(echo "$log" | head -3 | tr '\n' ' ')"; miss=$((miss + 1))
  fi
done <<< "$list"
exit $((miss ? 1 : 0))
