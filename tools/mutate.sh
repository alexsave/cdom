#!/bin/bash
# Mutation check: apply one perl substitution to a file, run the test command,
# and require that it FAILS; the file is restored from a copy whatever happens.
# Usage: tools/mutate.sh <file> <perl s/// expression> <expected failure text> <command...>
# The expected text must appear in the command's output (the failing assertion).
set -uo pipefail
file="$1" expr="$2" want="$3"; shift 3
bak="$(mktemp)"; log="$(mktemp)"
cp -p "$file" "$bak"
trap 'cp -p "$bak" "$file"; rm -f "$bak" "$log"' EXIT
perl -0pi -e "$expr" "$file"
if cmp -s "$file" "$bak"; then echo "MUTANT DID NOT APPLY: $expr"; exit 2; fi
"$@" > "$log" 2>&1; code=$?
if [ $code -eq 0 ]; then echo "SURVIVED: $expr"; tail -5 "$log"; exit 1; fi
if ! grep -qF -- "$want" "$log"; then echo "FAILED FOR ANOTHER REASON: $expr (wanted '$want')"; tail -8 "$log"; exit 1; fi
echo "killed: $expr -> $(grep -F -- "$want" "$log" | head -1)"
