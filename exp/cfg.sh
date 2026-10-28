#!/bin/bash
# Experiment: builds entry NAME with ops in MASK (bit = op code) as direct imports, then gates it.
# Usage: exp/cfg.sh NAME MASK   (MASK 0 = the op stream; MASK native = arm (b), natively bound DOM methods;
# MASK split = the op stream with apply() dispatching through a table of per-op functions, exp/gen_split.py, over
# the same generated shim as the other arms so the page differs only in apply;
# MASK dswitch:<mask> = the direct arm with every import routed into one big switch function, exp/gen_dswitch.py)
# SHIM_EDIT='sed -E expression' edits the generated shim (the proof's string-decoding arms); the build fails if it changes nothing.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"; top="$here/.."
name="$1"; mask="$2"
tmp="$top/results/exp_shim_$name.js"
if [ "$mask" = native ]; then cp "$here/cdom_native.js" "$tmp"; mask=0; EXTRA="-DCDOM_NATIVE=1 -mreference-types ${EXTRA:-}"
elif [ "${2#dswitch:}" != "$2" ]; then python3 "$here/gen_direct.py" "$top/shim/cdom.js" | python3 "$here/gen_dswitch.py" /dev/stdin > "$tmp"; mask="${2#dswitch:}"
elif [ "$mask" = split ]; then python3 "$here/gen_direct.py" "$top/shim/cdom.js" | python3 "$here/gen_split.py" /dev/stdin > "$tmp"; mask=0
else python3 "$here/gen_direct.py" "$top/shim/cdom.js" > "$tmp"; fi
if [ -n "${SHIM_EDIT:-}" ]; then cp "$tmp" "$tmp.orig"; sed -E -i '' "$SHIM_EDIT" "$tmp"; if cmp -s "$tmp" "$tmp.orig"; then echo "SHIM_EDIT changed nothing"; exit 1; fi; rm -f "$tmp.orig"; fi
# The page keeps the exports so a measurement can read cd_syncs; same for every arm.
SHIM="$tmp" NAME="$name" XFLAGS="-DCDOM_DIRECT=$mask ${EXTRA:-}" nice -n 10 "$top/bench/build.sh" >/dev/null
sed -i '' "s/cdom.start('app.wasm', 'main');/cdom.start('app.wasm', 'main').then(x => window.__x = x);/" "$top/jfb/frameworks/keyed/$name/index.html"
grep -q "window.__x" "$top/jfb/frameworks/keyed/$name/index.html"
# The fuzz runs against the same mask and shim.
if SHIM="$tmp" XFLAGS="-DCDOM_DIRECT=$mask ${EXTRA:-}" nice -n 10 "$top/tests/build.sh" && node "$top/tools/fuzz.mjs" > "$top/results/exp_fuzz_$name.log" 2>&1; then echo "$name fuzz: ok"; else echo "$name fuzz: FAIL"; exit 1; fi
"$top/tests/build.sh"
rm -f "$tmp"
echo "$name mask=$mask: $(wc -c < "$top/jfb/frameworks/keyed/$name/app.wasm" | tr -d ' ') bytes wasm, $(wc -c < "$top/jfb/frameworks/keyed/$name/index.html" | tr -d ' ') bytes html"
