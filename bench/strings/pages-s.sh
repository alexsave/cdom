#!/bin/bash
# Startup pages for step 10, in out/pages/<page>/index.html:
#   hello-a, hello-a2, synth-a, synth-a2  before (from strings-bench's bench/strings/pages.sh: tutorial's cdom)
#   hello-s   tutorial/01-hello as this branch builds it (tutorial/run.sh, cdom.mk's APP_PAGE)
#   synth-s   gen_synth.py's 2,000-site page in plain C, linked through cdlits, bundled by tools/cdpage
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"; top="$here/../.."
bench="${BENCH:-/Users/alex/Dev/cdom-strings-bench/bench/strings/out/pages}"
CC="${WASM_CC:-/opt/homebrew/opt/llvm/bin/clang}"
. "$top/tools/cdomflags.sh"
o="$here/out/pages"; mkdir -p "$o"
for p in hello-a hello-a2 synth-a synth-a2; do mkdir -p "$o/$p"; cp "$bench/$p/index.html" "$o/$p/"; done
make -s -C "$top/tutorial/01-hello"
mkdir -p "$o/hello-s"; cp "$top/tutorial/01-hello/build/index.html" "$o/hello-s/"
python3 "$here/gen_synth.py" 2000 "$here/out/synth-lit.c"
sed -E 's/CD_LIT\(("([^"\\]|\\.)*")\)/\1/g' "$here/out/synth-lit.c" > "$here/out/synth.c"
if grep -q CD_LIT "$here/out/synth.c"; then echo "synth.c still has CD_LIT" >&2; exit 1; fi
d="$o/synth-s"; mkdir -p "$d"
"$CC" -Oz "${CDOM_CFLAGS[@]}" -Wall -Wextra -Werror "${CDOM_LDFLAGS[@]}" -Wl,--strip-all -o "$d/linked.wasm" \
  "$here/out/synth.c" "$top/src/cdom.c" "$top/src/mini.c"
"$CDOM_LITS" "$d/linked.wasm" "$d/app.wasm" "$d/app.wasm.lits"
"$CDOM_PAGE_TOOL" --page "$top/shim/page.html" --out "$d/index.html" --shim "$top/shim/cdom.min.js" --lits "$d/app.wasm.lits" \
  --wasm "$d/app.wasm" --title synth
rm "$d/linked.wasm"
for p in hello-a hello-s synth-a synth-s; do
    printf '%-9s page %7s B  brotli %6s B\n' "$p" "$(wc -c < "$o/$p/index.html" | tr -d ' ')" "$(brotli -q 11 -c "$o/$p/index.html" | wc -c | tr -d ' ')"
done
printf 'synth-s wasm %s B\n' "$(wc -c < "$d/app.wasm" | tr -d ' ')"
