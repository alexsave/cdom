#!/bin/bash
# The Mac's measurement of inflate-fast (docs/image-png-speed.md, "inflate-fast on the Mac"): main's scalar and SIMD
# pages (C-old, C-old-simd: built from a scratch worktree of the commit given), this branch's (C-js, C-simd) and the
# browser arms, 9 interleaved rounds at box 900 on the PNGs that differ in colour type and p12.jpg, in headless Chrome
# and then in Safari; results/<date>-<name>-{chrome,safari}.{log,tsv}.
# Usage: bench/image/inflate-mac.sh [old commit=origin/main] [name=inflate-mac]   (a name already used today is refused)
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"; cd "$here" || exit 1
top="$(cd "$here/../.." && pwd)"
day="$(date +%Y-%m-%d)"; name="${2:-inflate-mac}"
for br in chrome safari; do [ ! -e "results/$day-$name-$br.log" ] || { echo "results/$day-$name-$br.log exists: give another name" >&2; exit 1; }; done
make -s inputs > /dev/null && make -s page && make -s simd || exit 1
wt="$(mktemp -d)/cdom-old"; rev="${1:-origin/main}"
git -C "$top" worktree add --detach "$wt" "$rev" > /dev/null || exit 1
make -C "$wt/bench/image" SJLJ=js page > /dev/null && make -C "$wt/bench/image" simd > /dev/null || { git -C "$top" worktree remove --force "$wt"; exit 1; }
rm -rf build-old build-old-simd; cp -r "$wt/bench/image/build-js" build-old; cp -r "$wt/bench/image/build-simd" build-old-simd
git -C "$top" worktree remove --force "$wt"
echo "old: $(git -C "$top" rev-parse --short "$rev")"
for br in chrome safari; do
    { uptime; BROWSER=$br node run.mjs 9 900 C-old,C-old-simd,C-js,C-simd,B1,B2 p12.jpg,p12.png,p12a.png,p12g.png; uptime; } 2>&1 | tee "results/$day-$name-$br.log"
    raw="$(ls -t results/raw/*.tsv | head -1)"; cp "$raw" "results/$day-$name-$br.tsv"
done
