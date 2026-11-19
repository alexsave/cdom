#!/bin/bash
# The Mac's measurement of the hand-written SIMD build (docs/image-simd-hand.md, "The Mac"): the scalar and SIMD pages
# and the browser arms, 9 interleaved rounds at box 900; the log and the per-run table go to results/<date>-simd-mac.*.
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"; cd "$here" || exit 1
make -s page && make -s simd || exit 1
day="$(date +%Y-%m-%d)"
{ uptime; node run.mjs 9 900 C-js,C-simd,B1,B2; uptime; } 2>&1 | tee "results/$day-simd-mac.log"
raw="$(ls -t results/raw/*.tsv | head -1)"; cp "$raw" "results/$day-simd-mac.tsv"
