#!/bin/bash
# tools/cdpage standing alone (docs/page-tool-design.md, the test plan's tests/page rows): the pages it
# writes, composed here from their parts, and every page it refuses, with its message and no file left.
# Builds into tests/out/page; nothing is left in the tree.
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
top="$here/../.."
out="$here/../out/page"
CDPAGE="$top/tools/out/cdpage"
rm -rf "$out"
mkdir -p "$out" "$top/tools/out"
pass=0
fail=0
ok() { echo "ok   $*"; pass=$((pass + 1)); }
bad() { echo "FAIL $*"; fail=$((fail + 1)); }

# The tool: built as cdom.mk builds it (a temporary name, renamed).
if cc -std=c23 -O2 -Wall -Wextra -Werror -o "$CDPAGE.tmp$$" "$top/tools/cdpage.c" 2> "$out/cc.log"; then
  mv "$CDPAGE.tmp$$" "$CDPAGE"; ok "tools/cdpage.c builds with -Wall -Wextra -Werror"
else
  bad "tools/cdpage.c does not build:"; cat "$out/cc.log"; echo "tests/page: 0 passed, 1 failed"; exit 1
fi

# The parts: a shim, a table, and modules of each length modulo 3 (the base64 padding).
printf 'var cdom = "shim";\nfunction f() { return 1; }\n' > "$out/shim.js"
printf 'var cdomLits=[0,1,"\xe2\x98\x83","a\\x00b"];' > "$out/app.lits" # no final newline: the tool adds one
printf '\0asm\1\0\0\0' > "$out/m0.wasm"
printf '\0asm\1\0\0\0\7' > "$out/m1.wasm"
printf '\0asm\1\0\0\0\7\377' > "$out/m2.wasm"
pages="$top/tests/link/page"

# Edge pages beside tests/link/page's: CRLF lines, no final newline, a marker with tabs and CR, two script
# tags on a line (the last one is taken), an upper-case tag split from its marker, an empty URL before a
# real one, cdomBoot, the title twice on a line, a tag that never closes on its line.
e="$out/edge"
mkdir -p "$e"
printf '<!doctype html>\r\n<script>\r\n\t /*CDOM_SHIM*/ \t\r\ncdom.start(/*CDOM_WASM*/, "app");\r\n</script>' > "$e/crlf.html"
printf '<title>/*CDOM_TITLE*/ - /*CDOM_TITLE*/</title>\n<script></script><SCRIPT Type="module">\n/*CDOM_SHIM*/\ncdom.start(/*CDOM_WASM*/);\n</SCRIPT>\n' > "$e/tags.html"
printf '<script\n>\n<script data-a="1">\n/*CDOM_SHIM*/\ncdom.start("", 1);\ncdomBoot ( `boot.js`, 2);\n</script>\n' > "$e/boot.html"
printf '<script>\n/*CDOM_SHIM*/\nx = 1; cdom.start (/*CDOM_WASM*/, "app"); /*CDOM_WASM*/\n</script>\n' > "$e/two_line.html"
printf '<scripts>\n/*CDOM_SHIM*/\n' > "$e/notag.html"

# ---- the goldens: each page against its parts ----
# golden name page out flags...: the tool writes out from page, and it equals out.want
golden() {
  local name="$1" page="$2" o="$3"; shift 3
  if ! "$CDPAGE" --page "$page" --out "$o" --shim "$out/shim.js" --lits "$out/app.lits" "$@" 2> "$o.log"; then
    bad "$name: refused:"; cat "$o.log"; return
  fi
  if cmp -s "$o.want" "$o"; then ok "$name"; else bad "$name: the page is not its parts:"; diff "$o.want" "$o" | head -10; fi
}
table_shim() { cat "$out/app.lits"; echo; echo "</script>"; echo "$1"; cat "$out/shim.js"; }
b64() { base64 < "$1" | tr -d '\n\r'; }
t="$top/shim/page.html"
for m in m0 m1 m2; do
  o="$out/default_$m.html"
  { sed -n '1,5p' "$t"; echo '<title>a&lt;b &amp; c&gt;</title>'; sed -n '7,10p' "$t"; table_shim '<script>'
    echo "cdom.start(\"data:application/wasm;base64,$(b64 "$out/$m.wasm")\", \"app\");"; sed -n '13,$p' "$t"; } > "$o.want"
  golden "the default page, bundled ($(wc -c < "$out/$m.wasm" | tr -d ' ')-byte module), titled and escaped" "$t" "$o" --wasm "$out/$m.wasm" --title 'a<b & c>'
done
o="$out/default_url.html"
{ sed -n '1,10p' "$t"; table_shim '<script>'; printf '%s\n' 'cdom.start("/w/\"x\\y\x3c/script>\x0a\u2028.wasm", "app");'; sed -n '13,$p' "$t"; } > "$o.want"
golden "the default page, separate: the URL a JavaScript string with \\, \", <, a newline and U+2028 escaped, no title given" "$t" "$o" --wasm-url "$(printf '/w/"x\\y</script>\n\xe2\x80\xa8.wasm')"
o="$out/ok.html"
{ sed -n '1,3p' "$pages/ok.html"; table_shim '<script>'; sed -n '5,$p' "$pages/ok.html"; } > "$o.want"
golden "an app's own page that names its module, no mode" "$pages/ok.html" "$o"
o="$out/attrs.html"
{ sed -n '1,2p' "$pages/attrs.html"; table_shim '<script type="text/javascript" data-x="a b">'; sed -n '4,$p' "$pages/attrs.html"; } > "$o.want"
golden "the script tag written again with its attributes" "$pages/attrs.html" "$o" --wasm-url x.wasm
o="$out/crlf.html"
{ printf '<!doctype html>\r\n<script>\r\n'; table_shim '<script>'; printf 'cdom.start("data:application/wasm;base64,%s", "app");\r\n</script>\n' "$(b64 "$out/m1.wasm")"; } > "$o.want"
golden "CRLF lines kept, a marker line with tabs and a CR, a last line without a newline gains one" "$e/crlf.html" "$o" --wasm "$out/m1.wasm"
o="$out/tags.html"
{ echo '<title>T&amp; - T&amp;</title>'; echo '<script></script><SCRIPT Type="module">'; table_shim '<SCRIPT Type="module">'; echo 'cdom.start("u");'; echo '</SCRIPT>'; } > "$o.want"
golden "the last script tag of a line, in any case; every title on a line" "$e/tags.html" "$o" --wasm-url u --title 'T&'
o="$out/boot.html"
{ printf '<script\n>\n<script data-a="1">\n'; table_shim '<script data-a="1">'; sed -n '5,$p' "$e/boot.html"; } > "$o.want"
golden "a tag split across lines is not seen" "$e/boot.html" "$o"

# ---- the refusals: each with its message, and no file ----
# refused name page message flags...: the tool exits 1, says page: message, and writes nothing
refused() {
  local name="$1" page="$2" msg="$3"; shift 3
  local o="$out/refused_$name.html" rc=0
  "$CDPAGE" --page "$page" --out "$o" --shim "${SHIM:-$out/shim.js}" --lits "${LITS:-$out/app.lits}" "$@" > "$o.log" 2>&1 || rc=$?
  local left; left="$(find "$out" -name "refused_$name.html*" ! -name '*.log')"
  if [ "$rc" -ne 1 ]; then bad "refuses $name: exit $rc"; cat "$o.log"
  elif [ -n "$left" ]; then bad "refuses $name but left $left"
  elif ! grep -qxF -- "$msg" "$o.log"; then bad "refuses $name with another message (wanted: $msg):"; cat "$o.log"
  else ok "refuses $name: $msg"; fi
}
refused inline "$pages/inline.html" "$pages/inline.html:2: /*CDOM_SHIM*/ must be alone on its line"
refused none "$pages/none.html" "$pages/none.html: no /*CDOM_SHIM*/ line (a <script> whose line is only the marker, where the shim goes)" --wasm-url x
refused two "$pages/two.html" "$pages/two.html:6: a second /*CDOM_SHIM*/ (the page takes the shim once)"
refused two_wasm "$pages/two_wasm.html" "$pages/two_wasm.html:5: a second /*CDOM_WASM*/ (the page starts one module)" --wasm-url x
refused two_wasm_line "$e/two_line.html" "$e/two_line.html:3: a second /*CDOM_WASM*/ (the page starts one module)" --wasm-url x
refused noscript "$pages/noscript.html" "$pages/noscript.html:2: /*CDOM_SHIM*/ with no <script tag before it"
refused notag "$e/notag.html" "$e/notag.html:2: /*CDOM_SHIM*/ with no <script tag before it"
refused nomodule "$pages/marker.html" "$pages/marker.html:5: /*CDOM_WASM*/, and no module to put there"
refused loads "$pages/ok.html" "$pages/ok.html: this page loads mk_app.wasm; set APP_WASM_SEPARATE := 1, or start with the bundled module (the /*CDOM_WASM*/ marker)" --wasm "$out/m1.wasm"
refused loads_boot "$e/boot.html" "$e/boot.html: this page loads boot.js; set APP_WASM_SEPARATE := 1, or start with the bundled module (the /*CDOM_WASM*/ marker)" --wasm "$out/m1.wasm"
refused nostart "$pages/two.html" "$pages/two.html:6: a second /*CDOM_SHIM*/ (the page takes the shim once)" --wasm "$out/m1.wasm"
printf '<script>\n/*CDOM_SHIM*/\n</script>\n' > "$e/nostart.html"
refused nostart_end "$e/nostart.html" "$e/nostart.html: no /*CDOM_WASM*/ for the bundled module (cdom.start(/*CDOM_WASM*/, ...)); or set APP_WASM_SEPARATE := 1" --wasm "$out/m1.wasm"
LITS="$out/no.lits" refused nolits "$pages/ok.html" "$pages/ok.html: cannot read the string table $out/no.lits"
SHIM="$out/no.js" refused noshim "$pages/ok.html" "$pages/ok.html: cannot read the shim $out/no.js"
refused nowasm "$pages/marker.html" "$pages/marker.html: cannot read the module to bundle $out/no.wasm" --wasm "$out/no.wasm"
: > "$out/empty.wasm"
refused emptywasm "$pages/marker.html" "$pages/marker.html: cannot read the module to bundle $out/empty.wasm" --wasm "$out/empty.wasm"
refused nopage "$out/no.html" "$out/no.html: cannot read the page"
# the first fault in line order is the one named
printf '<script>\n/*CDOM_SHIM*/ x\n/*CDOM_SHIM*/\n' > "$e/first.html"
refused first "$e/first.html" "$e/first.html:2: /*CDOM_SHIM*/ must be alone on its line"
# the usage: a flag missing, unknown, given twice, or both module modes
usage() {
  local name="$1"; shift
  local rc=0
  "$CDPAGE" "$@" > "$out/usage.log" 2>&1 || rc=$?
  if [ "$rc" -eq 1 ] && grep -q '^usage: cdpage' "$out/usage.log"; then ok "usage: $name"; else bad "usage: $name (exit $rc):"; cat "$out/usage.log"; fi
}
usage "no --lits" --page "$pages/ok.html" --out "$out/u.html" --shim "$out/shim.js"
usage "an unknown flag" --page "$pages/ok.html" --out "$out/u.html" --shim "$out/shim.js" --lits "$out/app.lits" --bogus 1
usage "--page twice" --page "$pages/ok.html" --page "$pages/ok.html" --out "$out/u.html" --shim "$out/shim.js" --lits "$out/app.lits"
usage "--wasm and --wasm-url" --page "$pages/ok.html" --out "$out/u.html" --shim "$out/shim.js" --lits "$out/app.lits" --wasm "$out/m1.wasm" --wasm-url x
usage "a flag with no value" --page "$pages/ok.html" --out "$out/u.html" --shim "$out/shim.js" --lits

# ---- the differential: cdpage against tools/page.awk, which it replaces ----
# Every page, each mode, with and without a title: the same bytes, the same exit status, the same message.
# Removed with page.awk, when the goldens above are the whole story.
if [ -f "$top/tools/page.awk" ]; then
  awk_page() { # page out mode title
    local mode="$3" title="$4" in=()
    case "$mode" in bundle) in=(-v wasm=bundle);; url) in=(-v wasm=url);; *) in=(-v wasm=);; esac
    [ -n "$title" ] && in+=(-v title=1)
    if [ "$mode" = bundle ]; then base64 < "$out/m2.wasm" | tr -d '\n\r'; else :; fi |
      CDOM_WASM_URL='/w/"x"\y.wasm' CDOM_TITLE="$title" awk -v shim="$out/shim.js" -v lits="$out/app.lits" ${in[@]+"${in[@]}"} \
        -f "$top/tools/page.awk" "$1" > "$2" 2> "$2.err"
  }
  c_page() {
    local mode="$3" title="$4" in=()
    case "$mode" in bundle) in=(--wasm "$out/m2.wasm");; url) in=(--wasm-url '/w/"x"\y.wasm');; esac
    [ -n "$title" ] && in+=(--title "$title")
    "$CDPAGE" --page "$1" --out "$2" --shim "$out/shim.js" --lits "$out/app.lits" ${in[@]+"${in[@]}"} 2> "$2.err"
  }
  d="$out/diff"; mkdir -p "$d"
  n=0 same=0
  for p in "$pages"/*.html "$top/shim/page.html" "$e"/*.html; do
    for mode in bundle url none; do
      for title in '' 'a<b & "c">'; do
        # the edge pages with a title twice on a line differ by design (cdpage replaces every one)
        [ -n "$title" ] && [ "$p" = "$e/tags.html" ] && continue
        n=$((n + 1)); k="$d/$n"
        awk_page "$p" "$k.awk" "$mode" "$title"; ra=$?
        c_page "$p" "$k.c" "$mode" "$title"; rc=$?
        [ "$ra" -ne 0 ] && rm -f "$k.awk" && : > "$k.awk"
        [ -e "$k.c" ] || : > "$k.c"
        if [ "$ra" -eq "$rc" ] && cmp -s "$k.awk" "$k.c" && cmp -s "$k.awk.err" "$k.c.err"; then same=$((same + 1))
        else bad "differential: $(basename "$p") $mode title=${title:+yes}: page.awk exit $ra, cdpage exit $rc"; diff "$k.awk.err" "$k.c.err" | head -4; cmp "$k.awk" "$k.c" | head -2; fi
      done
    done
  done
  [ "$same" -eq "$n" ] && ok "differential: $n pages and modes, cdpage writes what page.awk writes, byte for byte, the same exit status and message"
fi

echo "tests/page: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
