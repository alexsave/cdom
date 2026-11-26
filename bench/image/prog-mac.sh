#!/bin/bash
# The Mac's measurement of the progressive decoder (docs/image-progressive-speed.md, "The Mac"): main's scalar page
# (old.sh), this branch's scalar and SIMD pages and the browser arms, 9 interleaved rounds at box 900 on the progressive
# inputs and three others, in headless Chrome and then Safari; results/<date>-prog-mac-{chrome,safari}.{log,tsv}.
# Usage: bench/image/prog-mac.sh [old commit=origin/main] [name=prog-mac]   (a name already used today is refused, so a
# second run never overwrites a committed result)
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"; cd "$here" || exit 1
day="$(date +%Y-%m-%d)"; name="${2:-prog-mac}"
for br in chrome safari; do [ ! -e "results/$day-$name-$br.log" ] || { echo "results/$day-$name-$br.log exists: give another name" >&2; exit 1; }; done
make -s inputs > /dev/null && make -s page && make -s simd && ./old.sh "${1:-origin/main}" || exit 1
in=p12p.jpg,p24p.jpg,p12p444.jpg,p12pmany.jpg,p48p.jpg,p12.jpg,p48.jpg,p12.png
for br in chrome safari; do
    { uptime; BROWSER=$br node run.mjs 9 900 C-old,C-js,C-simd,B1,B2 "$in"; uptime; } 2>&1 | tee "results/$day-$name-$br.log"
    raw="$(ls -t results/raw/*.tsv | head -1)"; cp "$raw" "results/$day-$name-$br.tsv"
done
