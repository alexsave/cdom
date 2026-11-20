#!/bin/bash
# The Mac's measurement of png-fast (docs/image-png-speed.md, "The Mac: Chrome and Safari"): the old C (main, built by
# old.sh), this branch's scalar and SIMD pages and the browser arms, 9 interleaved rounds at box 900, in headless Chrome
# and then in Safari; the logs and the per-run tables go to results/<date>-png-mac-{chrome,safari}.*.
# Usage: bench/image/png-mac.sh [old commit=origin/main]
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"; cd "$here" || exit 1
make -s inputs > /dev/null && make -s page && make -s simd && ./old.sh "${1:-origin/main}" || exit 1
day="$(date +%Y-%m-%d)"
for br in chrome safari; do
  { uptime; BROWSER=$br node run.mjs 9 900 C-old,C-js,C-simd,B1,B2; uptime; } 2>&1 | tee "results/$day-png-mac-$br.log"
  raw="$(ls -t results/raw/*.tsv | head -1)"; cp "$raw" "results/$day-png-mac-$br.tsv"
done
