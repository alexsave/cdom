#!/bin/bash
# Builds the cdom entry and its variants into jfb/frameworks/keyed/<name>.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
top="$here/.."
CC="${WASM_CC:-/opt/homebrew/opt/llvm/bin/clang}"
OPT="${OPT:--Oz}"
jfb="${JFB_KEYED:-$top/jfb/frameworks/keyed}" # JFB_KEYED builds the entries somewhere else
legacy="$top/tools/legacy/build.mjs"
[ -d "$top/tools/legacy/node_modules" ] || (cd "$top/tools/legacy" && npm ci --no-audit --no-fund)

build() { # name, version note, extra flags...
  local name="$1" note="$2"; shift 2
  local OPT="$OPT"; case "$name" in *-o2) OPT=-O2;; esac
  local out="$jfb/$name"
  mkdir -p "$out"
  "$CC" --target=wasm32 $OPT -nostdlib -mbulk-memory -Wall -Wextra -Werror \
    -Wl,--no-entry -Wl,--strip-all "$@" -o "$out/app.wasm" \
    "$here/cdom-keyed/src/app.c" "$top/src/cdom.c" "$top/src/mini.c"
  # The shim is inlined into the page: one request fewer, and it starts the wasm before the stylesheets.
  # It is minified first (npm install at the repo root provides terser); the source stays readable.
  rm -f "$out/cdom.js"
  "$top/node_modules/.bin/terser" "$top/shim/cdom.js" -c passes=2 -m -o "$out/cdom.min.tmp"
  sed -e "s/cdom-\"keyed\"/$name-\"keyed\"/" -e "/\/\*CDOM_SHIM\*\//{r $out/cdom.min.tmp" -e "d;}" "$here/cdom-keyed/index.html" > "$out/index.src.html"
  rm -f "$out/cdom.min.tmp"
  # The boot goes in a script of its own after the shim's; without WebAssembly it loads two of these instead:
  # the first pair with typed arrays, the second without (shim/boot.js).
  node "$legacy" page "$out/index.src.html" "$out/index.html"
  rm "$out/index.src.html"
  node "$legacy" shim "$out/cdom.legacy.js"
  node "$legacy" app "$out/app.wasm" "$out/app.wasm.js"
  node "$legacy" shim-ie9 "$out/cdom.legacy-ie9.js"
  node "$legacy" app-ie9 "$out/app.wasm" "$out/app.wasm.ie9.js"
  cat > "$out/package.json" <<JSON
{
  "name": "js-framework-benchmark-$name",
  "version": "0.1.0",
  "description": "cdom: C owns the tree and the diff; a fixed JS shim applies a batched op stream ($note)",
  "js-framework-benchmark": { "frameworkVersion": "0.1.0", "language": "C", "issues": [1139] },
  "scripts": { "build-prod": "../../../../bench/build.sh" }
}
JSON
  printf '{ "name": "js-framework-benchmark-%s", "version": "0.1.0", "lockfileVersion": 3, "requires": true, "packages": {} }\n' "$name" > "$out/package-lock.json"
  printf '%s %s: %s bytes, %s gz\n' "$name" "$OPT" "$(wc -c < "$out/app.wasm" | tr -d ' ')" "$(gzip -9c "$out/app.wasm" | wc -c | tr -d ' ')"
}

# NAME=x builds only the default entry, as jfb/frameworks/keyed/x (for side-by-side work).
if [ -n "${NAME:-}" ]; then build "$NAME" "interned strings, templates"; exit 0; fi
build cdom "interned strings, templates"
build cdom-pertext "per-op strings, templates" -DCDOM_INTERN_TEXT=0
build cdom-notpl "interned strings, no templates" -DCDOM_TEMPLATES=0
build cdom-o2 "interned strings, templates, -O2"
