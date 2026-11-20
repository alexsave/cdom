#!/bin/bash
# Builds the cdom entry and its variants into jfb/frameworks/keyed/<name>.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
top="$here/.."
CC="${WASM_CC:-/opt/homebrew/opt/llvm/bin/clang}"
OPT="${OPT:--Oz}"
jfb="${JFB_KEYED:-$top/jfb/frameworks/keyed}" # JFB_KEYED builds the entries somewhere else
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
    -o "$out/app.wasm.linked" "$here/cdom-keyed/src/app.c" "$top/src/cdom.c" "$top/src/mini.c"
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
  # The boot goes in a script of its own after the shim's; without WebAssembly it loads two of these instead:
  # the first pair with typed arrays, the second without (shim/boot.js).
  node "$legacy" page "$out/index.src.html" "$out/index.html"
  rm "$out/index.src.html"
  node "$legacy" shim "$out/cdom.legacy.js"
  node "$legacy" app "$out/app.wasm" "$out/app.wasm.js"
  node "$legacy" shim-ie9 "$out/cdom.legacy-ie9.js"
  node "$legacy" shim-ie6 "$out/cdom.legacy-ie6.js"
  node "$legacy" app-ie9 "$out/app.wasm" "$out/app.wasm.ie9.js"
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

# NAME=x builds only the default entry, as jfb/frameworks/keyed/x (for side-by-side work); FLAGS adds
# compile flags to it (FLAGS=-DCDOM_BOUND=1 NAME=x builds cdom-bound's variant as x).
if [ -n "${NAME:-}" ]; then build "$NAME" "literal table, templates" ${FLAGS:-}; exit 0; fi
build cdom "literal table, templates"
build cdom-notpl "literal table, no templates" -DCDOM_TEMPLATES=0
build cdom-o2 "literal table, templates, -O2"
build cdom-bound "literal table, templates, the row class through cd_bound" -DCDOM_BOUND=1
