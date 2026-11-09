#!/bin/bash
# Builds both arms of the SVG micro and runs them.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
CC="${WASM_CC:-/opt/homebrew/opt/llvm/bin/clang}"
. "$here/../../tools/cdomflags.sh" # CDOM_CFLAGS and CDOM_LDFLAGS, cdom.mk's set for apps
# The js arm's own copy of cdom.h comes first (-I before CDOM_CFLAGS's -I of src).
F=(-Oz "${CDOM_LDFLAGS[@]}")
for arm in c js; do mkdir -p "$here/out/$arm"; done
"$CC" "${F[@]}" "${CDOM_CFLAGS[@]}" -o "$here/out/c/open.wasm" "$here/open.c" "$here/../../src/cdom.c" "$here/../../src/mini.c"
# js arm: C never says SVG; the shim's open checks the tag and the parent's namespace, as the port's did.
sed 's/if (svg_depth || tag == svg_tag) {/if (0) {/' "$here/../../src/cdom.c" > "$here/out/js/cdom.c"
cp "$here/../../src/cdom.h" "$here/out/js/"
"$CC" "${F[@]}" -I"$here/out/js" "${CDOM_CFLAGS[@]}" -o "$here/out/js/open.wasm" "$here/open.c" "$here/out/js/cdom.c" "$here/../../src/mini.c"
cp "$here/../../shim/cdom.js" "$here/out/c/cdom.js"
sed 's|  function open(t) { push(doc.createElement(S(t))); }|  function open(t) { const tag = S(t), up = sp ? stack[sp - 1] : null; push(tag === "svg" \|\| (up \&\& up.namespaceURI === SVG) ? doc.createElementNS(SVG, tag) : doc.createElement(tag)); }|' "$here/../../shim/cdom.js" > "$here/out/js/cdom.js"
grep -q 'up.namespaceURI === SVG' "$here/out/js/cdom.js"
for arm in c js; do sed "s/SHIM/cdom.js/" "$here/page.html" > "$here/out/$arm/page.html"; done
node "$here/run.mjs" "${1:-15}"
