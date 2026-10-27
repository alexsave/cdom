#!/bin/bash
# Builds the cdom entry and its variants into jfb/frameworks/keyed/<name>.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
top="$here/.."
CC="${WASM_CC:-/opt/homebrew/opt/llvm/bin/clang}"
OPT="${OPT:--O2}"
jfb="$top/jfb/frameworks/keyed"

build() { # name, version note, extra flags...
  local name="$1" note="$2"; shift 2
  local OPT="$OPT"; case "$name" in *-oz) OPT=-Oz;; esac
  local out="$jfb/$name"
  mkdir -p "$out"
  "$CC" --target=wasm32 $OPT -nostdlib -mbulk-memory -Wall -Wextra -Werror \
    -Wl,--no-entry -Wl,--strip-all "$@" -o "$out/app.wasm" \
    "$here/cdom-keyed/src/app.c" "$top/src/cdom.c" "$top/src/mini.c"
  cp "$top/shim/cdom.js" "$out/cdom.js"
  sed "s/cdom-\"keyed\"/$name-\"keyed\"/" "$here/cdom-keyed/index.html" > "$out/index.html"
  cat > "$out/package.json" <<JSON
{
  "name": "js-framework-benchmark-$name",
  "version": "0.1.0",
  "description": "cdom: C owns the tree and the diff; a fixed JS shim applies a batched op stream ($note)",
  "js-framework-benchmark": { "frameworkVersion": "0.1.0", "language": "C", "issues": [] },
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
build cdom-oz "interned strings, templates, -Oz"
