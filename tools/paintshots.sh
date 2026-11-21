#!/bin/bash
# tools/paintshots.sh: the source pictures of docs/descent.md "Painting a modern page": each design page of design/ns2paint/ rendered
# by headless Chrome at its exact size, normally and with its text transparent, into tests/out/paint/src/ (git ignores tests/out).
# TALL_W is the tall page's width (the widest that stays exact beside Netscape 2's scrollbar, measured by the w<width> pages).
set -e
here=$(cd "$(dirname "$0")" && pwd); d="$here/../design/ns2paint"; o="$here/../tests/out/paint/src"
mkdir -p "$o"
tw=${TALL_W:-598}
for spec in "still 592 320" "details 592 320" "tall $tw 1570"; do
  set -- $spec
  node "$here/paintshot.mjs" "$d/$1.html" "$o/$1.png" "$2" "$3" "$o/$1.links"
  node "$here/paintshot.mjs" "$d/$1.html" "$o/$1_notext.png" "$2" "$3" "$o/$1.links" notext
done
