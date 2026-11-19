#!/bin/bash
# The native arms interleaved: ROUNDS rounds, each running every arm's process once for RUNS runs, the arm order
# rotated each round; prints every run (arm, round, run, ms) and then each arm's median, min and max.
# Usage: native_round.sh <png> <native binary> [arms...]   (ROUNDS=7 RUNS=3; the arms: native.c's)
set -euo pipefail
png="$1"; nat="$2"; shift 2
arms=("$@"); R="${ROUNDS:-7}"; N="${RUNS:-3}"
tmp="$(mktemp)"
for ((r = 0; r < R; r++)); do
  for ((k = 0; k < ${#arms[@]}; k++)); do
    a="${arms[$(((k + r) % ${#arms[@]}))]}"
    "$nat" "$png" "$a" "$N" | grep -v '^#' | awk -v r=$r '{print $1"\t"r"\t"$2"\t"$3}' >> "$tmp"
  done
done
cat "$tmp"
echo "arm	median ms	min	max	runs"
for a in "${arms[@]}"; do
  awk -v a="$a" '$1==a{print $4}' "$tmp" | sort -n | awk -v a="$a" '{v[NR]=$1} END{m=(NR%2)?v[(NR+1)/2]:(v[NR/2]+v[NR/2+1])/2; printf "%s\t%.1f\t%.1f\t%.1f\t%d\n", a, m, v[1], v[NR], NR}'
done
rm -f "$tmp"
