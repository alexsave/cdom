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
if cc -std=c2x -O2 -Wall -Wextra -Werror -o "$CDPAGE.tmp$$" "$top/tools/cdpage.c" 2> "$out/cc.log"; then
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
  { sed -n '1,5p' "$t"; echo '<title>a&lt;b &amp; c&gt;</title>'; sed -n '7p' "$t"; sed -n '9,13p' "$t"; table_shim '<script>'
    echo "cdom.start(\"data:application/wasm;base64,$(b64 "$out/$m.wasm")\", \"app\");"; sed -n '16,$p' "$t"; } > "$o.want"
  golden "the default page, bundled ($(wc -c < "$out/$m.wasm" | tr -d ' ')-byte module), titled and escaped" "$t" "$o" --wasm "$out/$m.wasm" --title 'a<b & c>'
done
o="$out/default_url.html"
{ sed -n '1,7p' "$t"; sed -n '9,13p' "$t"; table_shim '<script>'; printf '%s\n' 'cdom.start("/w/\"x\\y\x3c/script>\x0a\u2028.wasm", "app");'; sed -n '16,$p' "$t"; } > "$o.want"
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

# ---- the stylesheets: discovery, order, the list file, the escape, the refusals and the edges ----
# The tree is tests/link/apps/css/src (the fixture app's): seven stylesheets in subfolders, beside a dot-file,
# a dot-folder, notes.css.bak, UPPER.CSS and a link to the folder a. Each file's first line ends in a comment, /* its path */.
tree="$top/tests/link/apps/css/src"
cpage="$top/tests/link/apps/css/css.html"
order="10.css 9.css Z.css a.css a/b/deep.css a/x.css b/00.css "
# heads page: the /* path */ comments that end lines of the page, in order
heads() { grep -oE '/\* [^ ]+\.(css|CSS|bak) \*/$' "$1" | sed -E 's|^/\* (.*) \*/$|\1|' | tr '\n' ' '; }
css_page() { # out flags...: the fixture page with the tree's stylesheets, its module by URL
  local o="$1"; shift
  "$CDPAGE" --page "$cpage" --out "$o" --shim "$out/shim.js" --lits "$out/app.lits" --wasm-url x.wasm "$@" 2> "$o.log"
}
o="$out/css_order.html"
if ! css_page "$o" --css-dir "$tree"; then bad "discovery: refused the fixture tree:"; cat "$o.log"
elif [ "$(heads "$o")" != "$order" ]; then bad "discovery: the page holds [$(heads "$o")], not [$order]"
elif [ "$(grep -c -- '--last' "$o")" != 8 ] || grep -q -- '--hidden' "$o"; then bad "discovery: a stylesheet twice, or a skipped one in the page"
elif [ "$(grep -n 'b/00' "$o" | tail -1 | cut -d: -f1)" -ge "$(grep -n 'var cdomLits' "$o" | cut -d: -f1)" ]; then bad "discovery: a stylesheet after the table's script"
elif [ "$(sed -n '7p' "$o")" != '#probe { --first: "page"; --last: "page"; }' ] || [ "$(sed -n '8p' "$o")" != '.n-10 { order: 1; } /* 10.css */' ]; then bad "discovery: the page's own rules before the marker are not first"
else ok "discovery: the seven stylesheets in byte order of their paths ($order), each once; dot-names, .css.bak, .CSS and the linked folder skipped; all where the marker was, before the table"; fi
# The same tree, its files made in the reverse order (another directory order): the same page.
rev="$out/rev/src"; rm -rf "$out/rev"; mkdir -p "$rev"
for f in $(echo "$order" | tr ' ' '\n' | sed '/^$/d' | sort -r); do mkdir -p "$rev/$(dirname "$f")"; cp "$tree/$f" "$rev/$f"; done
mkdir -p "$rev/.dir"; cp "$tree/.dir/in.css" "$rev/.dir/"; cp "$tree/.hidden.css" "$tree/notes.css.bak" "$tree/UPPER.CSS" "$rev/"; ln -s a "$rev/linked"
if css_page "$out/css_rev.html" --css-dir "$rev" && cmp -s "$o" "$out/css_rev.html"; then ok "discovery: the tree made in the reverse order gives the same page"
else bad "discovery: the tree made in the reverse order gives another page"; diff "$o" "$out/css_rev.html" | head -6; fi
# A link to a file is read as that file.
ln -s ../9.css "$rev/b/link.css"
if css_page "$out/css_link.html" --css-dir "$rev" && [ "$(heads "$out/css_link.html")" = "${order}9.css " ]; then ok "discovery: a link to a file is read as that file, at its own path (b/link.css)"
else bad "discovery: a link to a file: [$(heads "$out/css_link.html")]"; fi
rm "$rev/b/link.css"

# The list file: the seven paths in order with the FNV-1a 64 of their bytes; replaced only when its text
# changes (a rename gives a new inode), so a touch or a second run leaves it, and an edit, an added file,
# a removed one and a rename each replace it.
fnv() { python3 -c 'import sys
h = 0xcbf29ce484222325
for b in open(sys.argv[1], "rb").read(): h = ((h ^ b) * 0x100000001b3) & 0xffffffffffffffff
print("%016x" % h)' "$1"; }
L="$out/app.csslist"
want=""; for f in $order; do want+="$f	$(fnv "$tree/$f")"$'\n'; done
"$CDPAGE" --css-list "$tree" "$L"
if [ "$(cat "$L"; echo .)" = "$want." ]; then ok "the list file: the seven paths in byte order, a tab, the FNV-1a 64 of each file's bytes"
else bad "the list file is not the seven paths and their hashes:"; diff <(printf '%s' "$want") "$L"; fi
ino() { stat -f '%i' "$L"; }
listed() { # name, then whether the list must be replaced (1) or left (0), after the step before it
  local was="$1" name="$2" want="$3"
  "$CDPAGE" --css-list "$rev" "$L"
  local now; now="$(ino)"
  if [ "$want" = 1 ] && [ "$now" = "$was" ]; then bad "the list file: $name did not replace it"
  elif [ "$want" = 0 ] && [ "$now" != "$was" ]; then bad "the list file: $name replaced it"
  else ok "the list file: $name $([ "$want" = 1 ] && echo replaces it || echo leaves it as it is)"; fi
}
"$CDPAGE" --css-list "$rev" "$L"
i="$(ino)"; listed "$i" "a second run" 0
i="$(ino)"; touch "$rev/a.css"; listed "$i" "a touch" 0
i="$(ino)"; echo '.e { order: 2; }' >> "$rev/a.css"; listed "$i" "an edit" 1
i="$(ino)"; printf '.n { order: 3; }\n' > "$rev/a/new.css"; listed "$i" "an added file" 1
i="$(ino)"; rm "$rev/a/new.css"; listed "$i" "a removed file" 1
i="$(ino)"; mv "$rev/Z.css" "$rev/b/Z.css"; listed "$i" "a rename" 1
if grep -q '^b/Z.css	' "$L" && ! grep -q '^Z.css	' "$L"; then ok "the list file: a renamed file is listed at its new path"; else bad "the list file: the rename is not in it"; cat "$L"; fi
for d in "$out/no-such-dir" ""; do
  rm -f "$L"; "$CDPAGE" --css-list "$d" "$L"
  if [ -f "$L" ] && [ ! -s "$L" ]; then ok "the list file: $([ -n "$d" ] && echo 'a missing directory' || echo 'an empty DIR argument') gives an empty list"; else bad "the list file for [${d}] is not an empty file"; fi
done

# </style, in any case, cannot end the page's <style>: the page holds exactly one, its own.
if [ "$(grep -oi '</style' "$o" | wc -l | tr -d ' ')" = 1 ] && grep -qF '#probe::before { content: "<\/style><script>window.pwned=1</script>"; }' "$o" &&
   grep -qF 'The <\/STYLE> in this comment' "$o"; then ok "the escape: </style in a string and </STYLE in a comment are written <\\/, and the page has one </style, its own"
else bad "the escape: the page holds $(grep -oi '</style' "$o" | wc -l | tr -d ' ') </style"; fi

# Comments: one that fills its lines (spaces and tabs before it, spaces, tabs and a CR after it) is left
# out with its lines; one beside other text is kept, so no two tokens are joined (a, a comment, .b is a.b).
cm="$out/comments"; mkdir -p "$cm"
printf '/* gone */\n.a { order: 1; }\n  \t/* gone, over\n two lines */ \t\r\n.b { order: 2; } /* kept: after a rule */\n/* kept: before a rule */ .c { order: 3; }\n.d/**/.e { order: 4; }\n.f::before { content: "/* in a string */"; }\n/* the last line, gone */' > "$cm/c.css"
printf '.a { order: 1; }\n.b { order: 2; } /* kept: after a rule */\n/* kept: before a rule */ .c { order: 3; }\n.d/**/.e { order: 4; }\n.f::before { content: "/* in a string */"; }\n' > "$out/comments.want"
o="$out/css_comments.html"
if css_page "$o" --css-dir "$cm" && sed -n '8,/^#probe { --after/p' "$o" | sed '$d' | cmp -s - "$out/comments.want"; then
  ok "comments: one that fills its lines is left out with them; one beside a rule, and one in a string, are kept"
else bad "comments:"; sed -n '8,/^#probe { --after/p' "$o" | sed '$d' | diff "$out/comments.want" - | head -8; fi

# The refusals, each with its message and no file. A stylesheet's own faults: each file of tests/page/bad
# alone in a directory, and two made here (a NUL, bytes that are not UTF-8).
sheet_refused() { # name message: the directory $out/bad_name holds the one file
  local name="$1" msg="$2"
  local d="$out/bad_$name" o="$out/refused_css_$name.html" rc=0
  css_page "$o" --css-dir "$d" || rc=$?
  if [ "$rc" -ne 1 ]; then bad "refuses the stylesheet $name: exit $rc"
  elif compgen -G "$o" > /dev/null || compgen -G "$o.tmp*" > /dev/null; then bad "refuses the stylesheet $name but left a file"
  elif ! grep -qxF -- "$msg" "$o.log"; then bad "refuses the stylesheet $name with another message (wanted: $msg):"; cat "$o.log"
  else ok "refuses the stylesheet $name: ${msg#"$d/"}"; fi
}
for f in "$here"/bad/*.css; do
  n="$(basename "$f" .css)"; mkdir -p "$out/bad_$n"; cp "$f" "$out/bad_$n/"
done
b="$out/bad_"
sheet_refused comment "${b}comment/comment.css:2: a comment that never ends"
sheet_refused string "${b}string/string.css:2: a string that never ends (a newline before its closing quote)"
sheet_refused string_end "${b}string_end/string_end.css:3: a string that never ends"
sheet_refused url "${b}url/url.css:2: a url( that never ends"
sheet_refused open "${b}open/open.css:2: a { that is never closed"
sheet_refused close "${b}close/close.css:3: a } with no { before it"
sheet_refused import "${b}import/import.css:2: @import; put the file in the same folder instead (every .css file there is in the page, in byte order of its path)"
mkdir -p "${b}nul" "${b}utf8"
printf '.a { order: 1; }\n.b\0 { order: 2; }\n' > "${b}nul/nul.css"
printf '.a { order: 1; }\n\n.b { content: "\xc3\x28"; }\n' > "${b}utf8/utf8.css"
sheet_refused nul "${b}nul/nul.css:2: a NUL byte"
sheet_refused utf8 "${b}utf8/utf8.css:3: bytes that are not UTF-8 (a page is UTF-8)"
mkdir -p "${b}unreadable"; printf '.a { order: 1; }\n' > "${b}unreadable/u.css"; chmod 000 "${b}unreadable/u.css"
sheet_refused unreadable "cdpage: cannot read the stylesheet ${b}unreadable/u.css"
chmod 644 "${b}unreadable/u.css"
# What looks like a fault and is not: @import in a comment and in a string, a } in a string, /* in an
# unquoted url(), an escaped quote, a string continued past a newline by a backslash, braces in a comment.
good="$out/good"; mkdir -p "$good"; cp "$here/good.css" "$good/"
if css_page "$out/css_good.html" --css-dir "$good" && grep -qF "$(tail -1 "$here/good.css")" "$out/css_good.html"; then ok "a stylesheet with every corner the scanner must pass (good.css) is taken"
else bad "good.css is refused:"; cat "$out/css_good.html.log"; fi
# The page's faults with the marker: no marker and files found; the marker outside a <style>, after the
# shim, twice, beside other text.
mk_css_page() { # name, then the page's lines
  local n="$1"; shift; printf '%s\n' "$@" > "$e/css_$n.html"; }
mk_css_page outside '<style></style>' '/*CDOM_CSS*/' '<script>' '/*CDOM_SHIM*/' '</script>'
mk_css_page after '<script>' '/*CDOM_SHIM*/' '</script>' '<style>' '/*CDOM_CSS*/' '</style>'
mk_css_page twice '<style>' '/*CDOM_CSS*/' '/*CDOM_CSS*/' '</style>' '<script>' '/*CDOM_SHIM*/' '</script>'
mk_css_page beside '<style>' '/*CDOM_CSS*/ p {}' '</style>' '<script>' '/*CDOM_SHIM*/' '</script>'
mk_css_page none '<style>' '</style>' '<script>' '/*CDOM_SHIM*/' '</script>'
mk_css_page attrs '<STYLE media="all">' '/*CDOM_CSS*/' '</STYLE>' '<script>' '/*CDOM_SHIM*/' '</script>'
refused css_outside "$e/css_outside.html" "$e/css_outside.html:2: /*CDOM_CSS*/ outside a <style> element" --css-dir "$tree"
refused css_after "$e/css_after.html" "$e/css_after.html:5: /*CDOM_CSS*/ after /*CDOM_SHIM*/ (the styles come before the shim's script)" --css-dir "$tree"
refused css_twice "$e/css_twice.html" "$e/css_twice.html:3: a second /*CDOM_CSS*/ (the page takes the stylesheets once)" --css-dir "$tree"
refused css_beside "$e/css_beside.html" "$e/css_beside.html:2: /*CDOM_CSS*/ must be alone on its line" --css-dir "$tree"
refused css_none "$e/css_none.html" "$e/css_none.html: $tree/ holds 10.css (and 6 more), and this page has no /*CDOM_CSS*/ line (a <style> whose line is only the marker, before the shim's script); add it, or move the files out of $tree/" --css-dir "$tree/"
refused css_none_one "$e/css_none.html" "$e/css_none.html: $good/ holds good.css, and this page has no /*CDOM_CSS*/ line (a <style> whose line is only the marker, before the shim's script); add it, or move the files out of $good/" --css-dir "$good"
refused css_baddir "$e/css_none.html" "cdpage: cannot read the directory $cpage" --css-dir "$cpage"
o="$out/css_attrs.html"
if "$CDPAGE" --page "$e/css_attrs.html" --out "$o" --shim "$out/shim.js" --lits "$out/app.lits" --css-dir "$tree" 2> "$o.log" && [ "$(heads "$o")" = "$order" ]; then
  ok "the marker in a <STYLE> with attributes (any letter case) takes the stylesheets"
else bad "the marker in a <STYLE> with attributes:"; cat "$o.log"; fi

# The edges: a byte order mark dropped; a newline after a file that has none; an empty file adds nothing;
# no --css-dir, a missing directory, or one with no stylesheet: the marker line removed and nothing else.
ed="$out/edges"; mkdir -p "$ed/s"
printf '\xef\xbb\xbf.bom { order: 1; }\n' > "$ed/s/1-bom.css"
printf '.nonl { order: 2; }' > "$ed/s/2-nonl.css"
: > "$ed/s/3-empty.css"
printf '.last { order: 4; }\n' > "$ed/s/4-last.css"
o="$out/css_edges.html"
{ sed -n '1,6p' "$cpage"; sed -n '7p' "$cpage"; printf '.bom { order: 1; }\n.nonl { order: 2; }\n.last { order: 4; }\n'; sed -n '9,15p' "$cpage"; table_shim '<script>'
  echo 'cdom.start("x.wasm", "app");'; sed -n '18,$p' "$cpage"; } > "$o.want"
if css_page "$o" --css-dir "$ed/s" && cmp -s "$o.want" "$o"; then ok "the edges: the byte order mark dropped, a newline after a file with none, an empty file adds nothing"
else bad "the edges:"; diff "$o.want" "$o" | head -8; fi
mkdir -p "$ed/none"; printf 'not a stylesheet\n' > "$ed/none/notes.txt"
{ sed -n '1,7p' "$cpage"; sed -n '9,15p' "$cpage"; table_shim '<script>'; echo 'cdom.start("x.wasm", "app");'; sed -n '18,$p' "$cpage"; } > "$out/css_empty.want"
for c in "no --css-dir:" "a missing directory:--css-dir $ed/missing" "a directory with no stylesheet:--css-dir $ed/none" "an empty --css-dir:--css-dir "; do
  name="${c%%:*}"; flags="${c#*:}"; o="$out/css_empty.html"
  if [ -z "$flags" ]; then css_page "$o"; elif [ "$flags" = "--css-dir " ]; then css_page "$o" --css-dir ""; else css_page "$o" --css-dir "${flags#--css-dir }"; fi
  if cmp -s "$out/css_empty.want" "$o"; then ok "the edges: $name, the marker line removed and nothing else changed"
  else bad "the edges: $name"; cat "$o.log"; diff "$out/css_empty.want" "$o" | head -4; fi
done

echo "tests/page: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
