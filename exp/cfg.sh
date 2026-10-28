#!/bin/bash
# Experiment: builds entry NAME with ops in MASK (bit = op code) as direct imports, then gates it.
# Usage: exp/cfg.sh NAME MASK   (MASK 0 = the op stream; MASK native = arm (b), natively bound DOM methods)
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"; top="$here/.."
name="$1"; mask="$2"
tmp="$top/results/exp_shim_$name.js"
if [ "$mask" = native ]; then cp "$here/cdom_native.js" "$tmp"; mask=0; EXTRA="-DCDOM_NATIVE=1 -mreference-types ${EXTRA:-}"
else python3 "$here/gen_direct.py" "$top/shim/cdom.js" > "$tmp"; fi
# The page keeps the exports so a measurement can read cd_syncs; same for every arm.
SHIM="$tmp" NAME="$name" XFLAGS="-DCDOM_DIRECT=$mask ${EXTRA:-}" nice -n 10 "$top/bench/build.sh" >/dev/null
sed -i '' "s/cdom.start('app.wasm', 'main');/cdom.start('app.wasm', 'main').then(x => window.__x = x);/" "$top/jfb/frameworks/keyed/$name/index.html"
grep -q "window.__x" "$top/jfb/frameworks/keyed/$name/index.html"
# The fuzz runs against the same mask and shim.
if SHIM="$tmp" XFLAGS="-DCDOM_DIRECT=$mask ${EXTRA:-}" nice -n 10 "$top/tests/build.sh" && node "$top/tools/fuzz.mjs" > "$top/results/exp_fuzz_$name.log" 2>&1; then echo "$name fuzz: ok"; else echo "$name fuzz: FAIL"; exit 1; fi
"$top/tests/build.sh"
rm -f "$tmp"
echo "$name mask=$mask: $(wc -c < "$top/jfb/frameworks/keyed/$name/app.wasm" | tr -d ' ') bytes wasm, $(wc -c < "$top/jfb/frameworks/keyed/$name/index.html" | tr -d ' ') bytes html"
