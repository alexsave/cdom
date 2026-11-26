#!/bin/bash
# strings step 10: bench/build.sh's build() for the "after" entries, with the app source a parameter:
#   st-s (templates), st-s-n (no templates), st-s-na (templates, the app's cd_tmp_ahead calls removed, P5a).
# The copy of the app without cd_tmp_ahead is made here (bench/strings/out), never in the tree.
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
top="$here/.."
CC="${WASM_CC:-/opt/homebrew/opt/llvm/bin/clang}"
OPT="${OPT:--Oz}"
jfb="${JFB_KEYED:-/Users/alex/Dev/cdom/jfb/frameworks/keyed}" # JFB_KEYED builds the entries somewhere else
legacy="$top/tools/legacy/build.mjs"
. "$top/tools/cdomflags.sh" # CDOM_CFLAGS and CDOM_LDFLAGS, cdom.mk's set for apps
[ -d "$top/tools/legacy/node_modules" ] || (cd "$top/tools/legacy" && npm ci --no-audit --no-fund)

build() { # name, version note, extra flags...
    local name="$1" note="$2"; shift 2
    local OPT="$OPT"; case "$name" in *-o2) OPT=-O2;; esac
    local out="$jfb/$name"
    mkdir -p "$out"
    "$CC" $OPT "${CDOM_CFLAGS[@]}" -Wall -Wextra -Werror \
      "${CDOM_LDFLAGS[@]}" -Wl,--strip-all "$@" \
      -o "$out/app.wasm.linked" "${APP_SRC:-$here/cdom-keyed/src/app.c}" "$top/src/cdom.c" "$top/src/mini.c"
    "$CDOM_LITS" "$out/app.wasm.linked" "$out/app.wasm" "$out/app.wasm.lits"
    rm -f "$out/app.wasm.linked"
    # The shim is inlined into the page: one request fewer, and it starts the wasm before the stylesheets.
    # It is the committed shim/cdom.min.js, as an app inlines it (tools/minshim.mjs makes it from cdom.js).
    rm -f "$out/cdom.js"
    node "$top/tools/minshim.mjs" check
    # Inlined as cdom.mk's cdom_page does it (tools/cdpage), after the entry's name goes in.
    sed -e "s/cdom-\"keyed\"/$name-\"keyed\"/" "$here/cdom-keyed/index.html" > "$out/index.named.html"
    "$CDOM_PAGE_TOOL" --page "$out/index.named.html" --out "$out/index.src.html" --shim "$top/shim/cdom.min.js" --lits "$out/app.wasm.lits"
    rm -f "$out/app.wasm.lits" # inlined in the page now
    rm "$out/index.named.html"
    # The boot goes in a script of its own after the shim's; without WebAssembly it loads these two instead.
    node "$legacy" page "$out/index.src.html" "$out/index.html"
    rm "$out/index.src.html"
    node "$legacy" shim "$out/cdom.legacy.js"
    node "$legacy" app "$out/app.wasm" "$out/app.wasm.js"
    cat > "$out/package.json" <<JSON
{
  "name": "js-framework-benchmark-$name",
  "version": "0.1.0",
  "description": "cdom: C owns the tree and the diff; a fixed JS shim makes one DOM call per operation ($note)",
  "js-framework-benchmark": { "frameworkVersion": "0.1.0", "language": "C", "issues": [1139] },
  "scripts": { "build-prod": "../../../../bench/build.sh" }
}
JSON
    printf '{ "name": "js-framework-benchmark-%s", "version": "0.1.0", "lockfileVersion": 3, "requires": true, "packages": {} }\n' "$name" > "$out/package-lock.json"
    printf '%s %s: %s bytes, %s gz\n' "$name" "$OPT" "$(wc -c < "$out/app.wasm" | tr -d ' ')" "$(gzip -9c "$out/app.wasm" | wc -c | tr -d ' ')"
}

out="$here/strings/out"; mkdir -p "$out"
sed 's/^\( *\)\(else \)\{0,1\}cd_tmp_ahead(arena + first, arena_len - first);/\1\2(void)first;/' "$here/cdom-keyed/src/app.c" > "$out/app-noahead.c"
grep -q cd_tmp_ahead "$out/app-noahead.c" && { echo "app-noahead.c still calls cd_tmp_ahead" >&2; exit 1; }
build st-s "literal table, templates"
build st-s-n "literal table, no templates" -DCDOM_TEMPLATES=0
APP_SRC="$out/app-noahead.c" build st-s-na "literal table, templates, no cd_tmp_ahead"
