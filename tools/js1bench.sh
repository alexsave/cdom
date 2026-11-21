#!/bin/bash
# tools/js1bench.sh: builds tests/js1bench.c and its pages (tools/js1bench.mjs), tests/out/js1bencht.html, js1benchi.html
# and js1benchc.html, each through one Netscape code path, and js1benchif.html and js1benchcf.html, the interpreter's and
# the compiler's with Netscape 2's blockers worked around in the page (NS2FIX), from each branch's worktree:
#   JS1T (default ../cdom-js1t, branch js1-transform), JS1I (../cdom-js1i, js1-interp), JS1C (../cdom-js1c, js1-compile),
# each built first with its own tests/build.sh. The branches are not changed: their tools only read the .wasm here.
# Serve tests/out with /Users/alex/VMs/ie9-win7/serve.py and open js1bench<k>.html?<tag> in the XP VM.
# (No dash in the names: Netscape 2 cut "js1bench-i.html" typed on its command line at the "-i".)
set -euo pipefail
top="$(cd "$(dirname "$0")/.." && pwd)"
t="${JS1T:-$top/../cdom-js1t}" i="${JS1I:-$top/../cdom-js1i}" c="${JS1C:-$top/../cdom-js1c}"
if [ "$(uname -s)" = Darwin ]; then CC="${WASM_CC:-/opt/homebrew/opt/llvm/bin/clang}"; else CC="${WASM_CC:-clang}"; fi
. "$top/tools/cdomflags.sh"
out="$top/tests/out"
mkdir -p "$out"
"$CC" -O2 "${CDOM_CFLAGS[@]}" -Wall -Wextra -Werror "${CDOM_LDFLAGS[@]}" -o "$out/js1bench.wasm" "$top/tests/js1bench.c"
(cd "$t" && node tools/legacy/build.mjs app-js1 "$out/js1bench.wasm" "$out/js1bench.t.js1.js")
(cd "$i" && node tools/legacy/build.mjs app-js1 "$out/js1bench.wasm" "$out/js1bench.i.js1.js")
(cd "$c" && tools/js1.sh "$out/js1bench.wasm" "$out/js1bench.c.js1.js")
node "$top/tools/js1bench.mjs" t "$out/js1bencht.html" "$out/js1bench.t.js1.js"
node "$top/tools/js1bench.mjs" i "$out/js1benchi.html" "$i/tests/out/cdom.js1.js" "$out/js1bench.i.js1.js"
node "$top/tools/js1bench.mjs" c "$out/js1benchc.html" "$out/js1bench.c.js1.js"
NS2FIX=1 node "$top/tools/js1bench.mjs" i "$out/js1benchif.html" "$i/tests/out/cdom.js1.js" "$out/js1bench.i.js1.js"
NS2FIX=1 node "$top/tools/js1bench.mjs" c "$out/js1benchcf.html" "$out/js1bench.c.js1.js"
# The pages' own scripts through js1-compile's JavaScript 1.0 gate (the path files passed their own branch's gates).
(cd "$c" && node tools/js10.mjs check "$out/"js1bench?.html.driver.js "$out/"js1bench?f.html.driver.js)
