#!/bin/bash
# Bundled single-file pages (shim/page.html, the module as base64, as cdom.mk's APP_PAGE makes them) for
# tutorial/01-hello and the synthetic page (gen_synth.py), in each design:
#   a, a2 today; b the table baked into the page, cd_chars stripped; c and c1 decoded from memory at load.
# Output: bench/strings/out/pages/<app>-<design>/index.html
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"; top="$here/../.."
CC="${WASM_CC:-/opt/homebrew/opt/llvm/bin/clang}"
. "$top/tools/cdomflags.sh"
L=(-Wl,--export-if-defined=__start_cd_lits -Wl,--export-if-defined=__stop_cd_lits
  -Wl,--export-if-defined=__start_cd_chars -Wl,--export-if-defined=__stop_cd_chars)
o="$here/out/pages"; mkdir -p "$o"
python3 "$here/gen_synth.py" "${SITES:-2000}" "$here/apps/synth.c"
sed 's/cd_internz(FRUITS\[i\])/cd_tmpz(FRUITS[i])/' "$top/tutorial/01-hello/src/app.c" > "$here/out/hello_b.c"
page() { # app, design, source, shim, mode
  local d="$o/$1-$2"; mkdir -p "$d"
  local flags=(); [ "$5" != A ] && flags=(-DCDOM_STRB=1)
  "$CC" -Oz "${CDOM_CFLAGS[@]}" -Wall -Wextra -Werror "${CDOM_LDFLAGS[@]}" "${L[@]}" -Wl,--strip-all ${flags[@]+"${flags[@]}"} \
    -o "$d/linked.wasm" "$3" "$top/src/cdom.c" "$top/src/mini.c"
  local opts=""
  case "$5" in
    B) python3 "$here/lits.py" "$d/linked.wasm" "$d/lits.js" "$d/app.wasm" 2>/dev/null; opts=", undefined, {strMode:\"B\",lits:__cdomLits}";;
    A) cp "$d/linked.wasm" "$d/app.wasm";;
    *) cp "$d/linked.wasm" "$d/app.wasm"; opts=", undefined, {strMode:\"$5\"}";;
  esac
  python3 - "$top/shim/page.html" "$here/out/shim-$4.min.js" "$d/app.wasm" "$opts" "$1" > "$d/index.html" <<'PY'
import sys, base64
page, shim, wasm, opts, title = sys.argv[1:]
s = open(page).read()
s = s.replace('/*CDOM_SHIM*/', open(shim).read().rstrip('\n'))
s = s.replace('/*CDOM_TITLE*/', title)
import os
lits = os.path.join(os.path.dirname(wasm), 'lits.js')
if os.path.exists(lits):  # the table before the module: brotli codes it far worse after the base64 run
    s = s.replace('</head>', '<script>var __cdomLits=%s;</script>\n</head>' % open(lits).read())
s = s.replace('cdom.start(/*CDOM_WASM*/, "app")', 'cdom.start("data:application/wasm;base64,%s", "app"%s)' % (base64.b64encode(open(wasm, 'rb').read()).decode(), opts))
sys.stdout.write(s)
PY
  rm -f "$d/linked.wasm" "$d/lits.js"
  printf '%-14s wasm %6s B  page %7s B  brotli %6s B\n' "$1-$2" "$(wc -c < "$d/app.wasm" | tr -d ' ')" \
    "$(wc -c < "$d/index.html" | tr -d ' ')" "$(brotli -q 11 -c "$d/index.html" | wc -c | tr -d ' ')"
}
for app in hello synth; do
  src="$here/apps/synth.c"; srcb="$src"
  [ $app = hello ] && src="$top/tutorial/01-hello/src/app.c" && srcb="$here/out/hello_b.c"
  page $app a "$src" a A
  page $app a2 "$src" a A
  page $app b "$srcb" b B
  page $app c "$srcb" b C
  page $app c1 "$srcb" b C1
done
