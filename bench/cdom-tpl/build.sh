#!/bin/bash
# Builds the templated rows app into jfb/frameworks/keyed/${NAME:-cdom-tpl}: cdtc compiles
# rows.cdt into out/rows.{h,c} (a build artifact, gitignored), then the wasm build is the
# one bench/build.sh uses for the hand-written app, with app.c + out/rows.c in place of its app.c.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
top="$here/../.."
CC="${WASM_CC:-/opt/homebrew/opt/llvm/bin/clang}"
OPT="${OPT:--O2}"
name="${NAME:-cdom-tpl}"
out="$top/jfb/frameworks/keyed/$name"
gen="$here/out"
mkdir -p "$gen" "$out"
nice -n 10 cc -O2 -Wall -Wextra -Werror -o "$gen/cdtc" "$top/tools/cdtc.c"
"$gen/cdtc" "$here/rows.cdt" -o "$gen/rows"
nice -n 10 "$CC" --target=wasm32 $OPT -nostdlib -mbulk-memory -Wall -Wextra -Werror \
  -Wl,--no-entry -Wl,--strip-all -I "$top/src" -I "$gen" -o "$out/app.wasm" \
  "$here/app.c" "$gen/rows.c" "$top/src/cdom.c" "$top/src/mini.c"
cp "$top/shim/cdom.js" "$out/cdom.js"
sed "s/cdom-\"keyed\"/$name-\"keyed\"/" "$top/bench/cdom-keyed/index.html" > "$out/index.html"
cat > "$out/package.json" <<JSON
{
  "name": "js-framework-benchmark-$name",
  "version": "0.1.0",
  "description": "cdom from a template: cdtc compiles rows.cdt to C that emits the op stream",
  "js-framework-benchmark": { "frameworkVersion": "0.1.0", "language": "C", "issues": [] },
  "scripts": { "build-prod": "../../../../bench/cdom-tpl/build.sh" }
}
JSON
printf '{ "name": "js-framework-benchmark-%s", "version": "0.1.0", "lockfileVersion": 3, "requires": true, "packages": {} }\n' "$name" > "$out/package-lock.json"
printf '%s %s: %s bytes, %s gz\n' "$name" "$OPT" "$(wc -c < "$out/app.wasm" | tr -d ' ')" "$(gzip -9c "$out/app.wasm" | wc -c | tr -d ' ')"
