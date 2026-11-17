#!/bin/bash
# strings-bench: builds the keyed entries for each string design into jfb/frameworks/keyed/sb-*.
#   sb-a, sb-a2   today's cdom and today's app (bench/cdom-keyed): the baseline, twice, for the noise band
#   sb-at         today's cdom, the app with labels in its own arena and cd_tmp at each use
#   sb-b          the prototype (-DCDOM_STRB=1): table baked into the page, cd_chars stripped
#   sb-c          the same module unstripped; the shim decodes the slots from memory at load
#   sb-c1         as sb-c, one TextDecoder call over cd_chars split at its NULs
#   sb-d          literal addresses as Map keys (-DCDOM_LITD=1)
#   *-n           the same with -DCDOM_TEMPLATES=0
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
top="$here/../.."
CC="${WASM_CC:-/opt/homebrew/opt/llvm/bin/clang}"
jfb="$top/jfb/frameworks/keyed"
. "$top/tools/cdomflags.sh"
out="$here/out"; mkdir -p "$out"
LITFLAGS=(-Wl,--export-if-defined=__start_cd_lits -Wl,--export-if-defined=__stop_cd_lits
  -Wl,--export-if-defined=__start_cd_chars -Wl,--export-if-defined=__stop_cd_chars)
node "$here/minify.mjs" "$top/shim/cdom.js" "$out/shim-a.min.js"
node "$here/minify.mjs" "$here/proto/cdom-b.js" "$out/shim-b.min.js"

# name, app source, shim (a|b), start options or "", flags...
entry() {
  local name="$1" src="$2" shim="$3" mode="$4"; shift 4
  local d="$jfb/$name"; mkdir -p "$d"
  "$CC" -Oz "${CDOM_CFLAGS[@]}" -Wall -Wextra -Werror "${CDOM_LDFLAGS[@]}" "${LITFLAGS[@]}" -Wl,--strip-all "$@" \
    -o "$d/app.linked.wasm" "$src" "$top/src/cdom.c" "$top/src/mini.c"
  local opts="undefined"
  case "$mode" in
    B) python3 "$here/lits.py" "$d/app.linked.wasm" "$d/lits.js" "$d/app.wasm"; opts="{strMode:\"B\",lits:$(cat "$d/lits.js")}";;
    C|C1|D) cp "$d/app.linked.wasm" "$d/app.wasm"; opts="{strMode:\"$mode\"}";;
    *) cp "$d/app.linked.wasm" "$d/app.wasm";;
  esac
  rm -f "$d/app.linked.wasm" "$d/lits.js"
  {
    printf '<!DOCTYPE html>\n<html lang="en">\n<head>\n  <meta charset="utf-8"/>\n  <title>%s-"keyed"</title>\n  <script>\n' "$name"
    cat "$out/shim-$shim.min.js"
    printf '  </script>\n  <script>\ncdom.start("app.wasm", "main", undefined, %s);\n  </script>\n' "$opts"
    printf '  <link href="/css/currentStyle.css" rel="stylesheet"/>\n</head>\n<body>\n<div id='"'"'main'"'"'></div>\n</body>\n</html>\n'
  } > "$d/index.html"
  cat > "$d/package.json" <<JSON
{ "name": "js-framework-benchmark-$name", "version": "0.1.0", "description": "cdom strings-bench $name",
  "js-framework-benchmark": { "frameworkVersion": "0.1.0", "language": "C", "issues": [1139] } }
JSON
  printf '%-8s wasm %6s B, page brotli %6s B\n' "$name" "$(wc -c < "$d/app.wasm" | tr -d ' ')" \
    "$(cat "$d/index.html" "$d/app.wasm" | brotli -q 11 -c | wc -c | tr -d ' ')"
}
A="$top/bench/cdom-keyed/src/app.c"; B="$here/apps/keyed_b.c"
for t in "" -n; do
  tf=(); [ -n "$t" ] && tf=(-DCDOM_TEMPLATES=0)
  entry "sb-a$t"  "$A" a "" ${tf[@]+"${tf[@]}"}
  entry "sb-a2$t" "$A" a "" ${tf[@]+"${tf[@]}"}
  entry "sb-b$t"  "$B" b B -DCDOM_STRB=1 ${tf[@]+"${tf[@]}"}
done
entry sb-at "$B" a ""
entry sb-c  "$B" b C  -DCDOM_STRB=1
entry sb-c1 "$B" b C1 -DCDOM_STRB=1
entry sb-d  "$B" b D  -DCDOM_STRB=1 -DCDOM_LITD=1
