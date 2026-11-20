#!/bin/bash
# The no-script fallback (docs/noscript-design.md): what a browser with no script shows, and that a browser with script
# never shows it nor fetches its images. Builds the app `make -C cdom new` writes into tests/out/noscript, as an app
# builds, with the fallback on (cdom.mk's default), and reads its page four ways: Chrome with script off and on
# (tests/noscript/chrome.mjs), lynx -dump, and the HTML 2.0 lens (tools/html2lens.py, and its Netscape 2 model).
# Then the GIFs (GIF87a, their sizes, 256 colours at most, the same bytes from the same inputs), the content stamp,
# an app's own page with and without the marker, and tools/cdpage's refusals: a script the HTML parser would read on
# past its </script> (checked against Chrome's own parser), a --> that would end the hiding comment, a module script.
# Prints "measure" lines for the design's predictions. Needs WASM_CC (cdom.mk), CHROME, Python 3 with Pillow, and lynx.
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
top="$(cd "$here/../.." && pwd)"
out="$top/tests/out/noscript"
CDPAGE="$top/tools/out/cdpage"
PY="${PYTHON:-python3}"
rm -rf "$out"
mkdir -p "$out" "$top/tools/out"
pass=0
fail=0
ok() { echo "ok   $*"; pass=$((pass + 1)); }
bad() { echo "FAIL $*"; fail=$((fail + 1)); }
finish() { echo "tests/noscript: $pass passed, $fail failed"; [ "$fail" = 0 ]; exit $?; }
# Portable to macOS's bash 3.2 and BSD tools: the clock, mtimes and in-place edits through Python.
now() { "$PY" -c 'import time; print(time.time())'; }
mtimes() { "$PY" -c 'import os, sys; print(" ".join(str(os.stat(f).st_mtime_ns) for f in sys.argv[1:]))' "$@"; }
subst() { "$PY" -c 'import sys; f, a, b = sys.argv[1:]; t = open(f).read(); assert a in t; open(f, "w").write(t.replace(a, b))' "$@"; }
since() { awk -v a="$1" -v b="$(now)" 'BEGIN { printf "%.2f", b - a }'; }
TEXT="Brother I'm sorry, the only way I could support your browser is if I had a server running just for you. And even if I did, anything you sent to that server would be decrypted. Regrettably I have chosen to drop support on this browser. Here's a snowman"
SHOT_ALT='[A screenshot of this page in a browser with JavaScript]'
SNOW_ALT='[A snowman]'

if cc -std=c2x -O2 -Wall -Wextra -Werror -o "$CDPAGE.tmp$$" "$top/tools/cdpage.c" 2> "$out/cc.log"; then mv "$CDPAGE.tmp$$" "$CDPAGE"
else bad "tools/cdpage.c does not build:"; cat "$out/cc.log"; finish; fi

# ---- the default demo, built as an app builds it ----
app="$out/app"
sh "$top/tools/new.sh" "$app" > /dev/null || { bad "tools/new.sh did not write the app"; finish; }
mk() { make -s -C "$app" CDOM="$top" "$@"; }
t=$(now)
if ! mk CDOM_NOSCRIPT=0 > "$out/make0.log" 2>&1; then bad "the default demo does not build with CDOM_NOSCRIPT=0:"; cat "$out/make0.log"; finish; fi
t_off=$(since "$t")
cp "$app/build/index.html" "$out/off.html"
if ls "$app/build/"*.gif > /dev/null 2>&1 || [ -e "$app/build/index.html.noscript" ]; then bad "CDOM_NOSCRIPT=0 made a GIF or a stamp"
elif grep -qF -e '<noscript' -e 'CDOM_NOSCRIPT' -e '<!--' -e '<wbr' "$out/off.html"; then bad "CDOM_NOSCRIPT=0: the page has the fallback, its marker or a comment wrapper"
else ok "CDOM_NOSCRIPT=0: no fallback, no marker line, no wrapper, no GIF"; fi
rm -rf "$app/build"
t=$(now)
if ! mk > "$out/make1.log" 2>&1; then bad "the default demo does not build with the fallback on (cdom.mk's default):"; cat "$out/make1.log"; finish; fi
t_on=$(since "$t")
page="$app/build/index.html"
shot="$app/build/index.noscript.gif"
snow="$app/build/cdom-snowman.gif"
[ -f "$shot" ] && [ -f "$snow" ] && ok "the default page's build wrote index.noscript.gif and cdom-snowman.gif beside it" || { bad "no GIFs beside the page"; finish; }
echo "measure P1: the screenshot GIF is $(wc -c < "$shot" | tr -d ' ') bytes"
echo "measure P2: the snowman GIF is $(wc -c < "$snow" | tr -d ' ') bytes"
echo "measure P3: the fallback adds $(( $(wc -c < "$page") - $(wc -c < "$out/off.html") )) bytes to the page ($(wc -c < "$out/off.html" | tr -d ' ') to $(wc -c < "$page" | tr -d ' '))"
echo "measure P4: a clean build takes ${t_on} s with the fallback, ${t_off} s without (indicative)"
# The page with the fallback is the page without it, but for the block at the marker and the comment wrappers.
if ! diff <(awk '/^<script><!--$/ { held = $0; next } held && /^document\.write\("<!-"\+"-"\);$/ { skip = 1 } held && !skip { print held }
    { held = "" } !skip { print } /<\/noscript><wbr -->$/ { skip = 0 }' "$page" \
    | sed -e 's|^<style><!--$|<style>|' -e 's|^<script><!--$|<script>|' -e 's|^//--></script>$|</script>|' -e 's|^--></style>$|</style>|') "$out/off.html" > "$out/diff.log"; then
  bad "the page with the fallback is not the page without it plus the block and the wrappers:"; head "$out/diff.log"
else ok "the page with the fallback is the page without it, plus the block at the marker and <!-- --> around its style and scripts"; fi

# ---- the GIFs ----
"$PY" - "$shot" 640 480 "$snow" 96 96 > "$out/gif.log" 2>&1 <<'EOF'
import sys
from PIL import Image
a = sys.argv[1:]
for f, w, h in zip(a[0::3], a[1::3], a[2::3]):
    b = open(f, 'rb').read()
    im = Image.open(f)
    flags = b[10]
    print(f, b[:6], int.from_bytes(b[6:8], 'little'), int.from_bytes(b[8:10], 'little'), 2 << (flags & 7) if flags & 0x80 else 0, im.mode)
    assert b[:6] == b'GIF87a', 'not GIF87a'
    assert (int.from_bytes(b[6:8], 'little'), int.from_bytes(b[8:10], 'little')) == (int(w), int(h)), 'size'
    assert flags & 0x80 and (2 << (flags & 7)) <= 256, 'no global colour table of 256 at most'
    assert b'\x21' not in b[13 + 3 * (2 << (flags & 7)):][:1], 'an extension block first'
    assert im.mode == 'P' and len(im.getcolors(256) or []) > 1, 'one colour, or more than 256'
EOF
if [ $? = 0 ]; then ok "both GIFs are GIF87a with a global colour table of 256 at most, at their sizes (640x480, 96x96), more than one colour"
else bad "the GIFs:"; cat "$out/gif.log"; fi
cp "$shot" "$out/shot1.gif"; cp "$snow" "$out/snow1.gif"
rm -f "$page.noscript"
mk -B > "$out/make2.log" 2>&1
if cmp -s "$shot" "$out/shot1.gif" && cmp -s "$snow" "$out/snow1.gif"; then ok "determinism: shot again (the stamp removed, make -B), both GIFs are the same bytes"
else bad "determinism: a second shot of the same page wrote other bytes"; cat "$out/make2.log"; fi
"$PY" - "$out/grad.png" "$out/few.png" <<'EOF'
import sys
from PIL import Image
g = Image.new('RGB', (64, 64))
g.putdata([(x * 4, y * 4, (x * y) % 256) for y in range(64) for x in range(64)])
g.save(sys.argv[1])
f = Image.new('RGB', (64, 64), (255, 255, 255))
f.putdata([((x // 8) * 30, (y // 8) * 30, 99) for y in range(64) for x in range(64)])
f.save(sys.argv[2])
EOF
same=1
for p in grad few; do
  "$PY" "$top/tools/noscript_gif.py" "$out/$p.png" "$out/$p.1.gif" && "$PY" "$top/tools/noscript_gif.py" "$out/$p.png" "$out/$p.2.gif" && cmp -s "$out/$p.1.gif" "$out/$p.2.gif" || same=0
done
exact=$("$PY" -c 'import sys
from PIL import Image
a, b = Image.open(sys.argv[1]).convert("RGB"), Image.open(sys.argv[2]).convert("RGB")
print(int(a.tobytes() == b.tobytes()))' "$out/few.png" "$out/few.1.gif")
if [ "$same" = 1 ] && [ "$exact" = 1 ]; then ok "tools/noscript_gif.py: the same PNG twice is the same GIF twice, by median cut (4096 colours) and exactly (64 colours, every pixel kept)"
else bad "tools/noscript_gif.py: not the same bytes twice ($same), or a picture of 64 colours not kept exactly ($exact)"; fi

# ---- the content stamp ----
m1=$(mtimes "$shot" "$snow" "$page.noscript")
sleep 1
touch "$app/build/app.wasm"
t=$(now)
mk > "$out/make3.log" 2>&1
t_hit=$(since "$t")
if [ "$m1" = "$(mtimes "$shot" "$snow" "$page.noscript")" ]; then ok "the content stamp: a page rewritten with the same bytes makes neither GIF again"
else bad "the content stamp: a page of the same bytes made the GIFs again"; fi
echo "measure P5: that make (the page rewritten, the stamp matching) took ${t_hit} s"
subst "$app/src/app.c" 'cd_text("cdom")' 'cd_text("cdom!")'
mk > "$out/make4.log" 2>&1
if cmp -s "$shot" "$out/shot1.gif"; then bad "the content stamp: the app drew another word, and the screenshot is the old one"
else ok "the content stamp: the app draws another word, and the screenshot is made again"; fi
subst "$app/src/app.c" 'cd_text("cdom!")' 'cd_text("cdom")'
mk > "$out/make5.log" 2>&1
if cmp -s "$shot" "$out/shot1.gif"; then ok "determinism: the app put back, the screenshot is the first one's bytes again"
else bad "determinism: the app put back, the screenshot differs from the first"; fi

# ---- Chrome, script off and on ----
node "$here/chrome.mjs" off "$page" index.noscript.gif=640x480 cdom-snowman.gif=96x96 > "$out/chrome_off.log" 2>&1
grep -E '^(ok|FAIL)' "$out/chrome_off.log" | while IFS= read -r l; do echo "${l:0:220}"; done
pass=$((pass + $(grep -c '^ok' "$out/chrome_off.log"))); fail=$((fail + $(grep -c '^FAIL' "$out/chrome_off.log")))
grep -qE '^(ok|FAIL)' "$out/chrome_off.log" || { bad "chrome.mjs off did not run:"; cat "$out/chrome_off.log"; }
node "$here/chrome.mjs" on "$page" index.noscript.gif=640x480 cdom-snowman.gif=96x96 > "$out/chrome_on.log" 2>&1
grep -E '^(ok|FAIL)' "$out/chrome_on.log" | while IFS= read -r l; do echo "${l:0:220}"; done
pass=$((pass + $(grep -c '^ok' "$out/chrome_on.log"))); fail=$((fail + $(grep -c '^FAIL' "$out/chrome_on.log")))
grep -qE '^(ok|FAIL)' "$out/chrome_on.log" || { bad "chrome.mjs on did not run:"; cat "$out/chrome_on.log"; }
# P8: the same page without its <noscript> element, the script-written comment alone hiding the block.
sed 's|</\{0,1\}noscript>||g' "$page" > "$app/build/nonoscript.html"
echo "measure P8: without <noscript>, Chrome with script requests: $(node "$here/chrome.mjs" gifs "$app/build/nonoscript.html" 2>&1 | tail -1)"
rm -f "$app/build/nonoscript.html"

# ---- lynx ----
if ! command -v lynx > /dev/null; then bad "lynx is not installed (apt-get install lynx)"
else
  lynx -dump -nolist -force_html -assume_charset=iso-8859-1 "$page" > "$out/lynx.txt" 2>&1
  got=$(tr -s ' \n\t' '   ' < "$out/lynx.txt" | sed 's/^ //; s/ $//')
  if [ "$got" = "$SHOT_ALT $TEXT $SNOW_ALT" ]; then ok "lynx -dump: the screenshot's ALT text, the paragraph and the snowman's ALT text, and nothing else"
  else bad "lynx -dump shows other text:"; head -c 600 "$out/lynx.txt"; echo; fi
  if grep -qF -e cdomLits -e document.write -e cdom.start -e '{' "$out/lynx.txt"; then bad "lynx -dump shows script or CSS source"
  else ok "lynx -dump: no script source (cdomLits, document.write, cdom.start) and no CSS ({)"; fi
fi

# ---- the HTML 2.0 lens ----
printf 'IMG index.noscript.gif 640x480 %s\nP\nTEXT %s\nIMG cdom-snowman.gif 96x96 %s\n' "$SHOT_ALT" "$TEXT" "$SNOW_ALT" > "$out/lens.want"
"$PY" "$top/tools/html2lens.py" "$page" > "$out/lens.txt" 2>&1
if cmp -s "$out/lens.want" "$out/lens.txt"; then ok "the HTML 2.0 lens (no script): the screenshot, a paragraph of exactly the text, the snowman, and nothing else"
else bad "the HTML 2.0 lens shows other than the fallback:"; diff "$out/lens.want" "$out/lens.txt" | cut -c1-200 | head; fi
"$PY" "$top/tools/html2lens.py" --script "$page" > "$out/lens_nn2.txt" 2>&1
if [ ! -s "$out/lens_nn2.txt" ]; then ok "the lens as Netscape 2 (script, no <noscript>): the script's comment hides the whole block, and no text shows"
else bad "the lens as Netscape 2 shows:"; head -c 400 "$out/lens_nn2.txt"; echo; fi
"$PY" "$top/tools/html2lens.py" "$out/off.html" > "$out/lens_off.txt" 2>&1
if grep -q '^TEXT .*cdomLits' "$out/lens_off.txt"; then ok "the lens sees a leak: the page built without the fallback shows its string table's source to a browser with no script"
else bad "the lens does not see the script source in a page with no wrappers"; fi

# ---- an app's own page (APP_PAGE_SRC) ----
printf '<!doctype html>\n<title>own</title>\n<style>\n/*CDOM_CSS*/\n</style>\n<body>\n  /*CDOM_NOSCRIPT*/\n<main id="app"></main>\n<script>\n/*CDOM_SHIM*/\ncdom.start(/*CDOM_WASM*/, "app");\n</script>\n' > "$app/own.html"
grep -v CDOM_NOSCRIPT "$app/own.html" > "$app/plain.html"
if ! mk APP_PAGE_SRC=own.html APP_PAGE=build/own.html > "$out/own.log" 2>&1; then bad "an own page with /*CDOM_NOSCRIPT*/ does not build:"; cat "$out/own.log"
elif [ ! -f "$app/build/own.noscript.gif" ] || ! "$PY" "$top/tools/html2lens.py" "$app/build/own.html" | grep -qxF "TEXT $TEXT"; then bad "an own page with the marker: no own.noscript.gif, or the lens does not show the paragraph"
else ok "an own page with /*CDOM_NOSCRIPT*/ (APP_PAGE_SRC) gets the fallback and its own screenshot, own.noscript.gif"; fi
if ! mk APP_PAGE_SRC=plain.html APP_PAGE=build/plain.html > "$out/plain.log" 2>&1; then bad "an own page without the marker does not build:"; cat "$out/plain.log"
elif [ -e "$app/build/plain.noscript.gif" ] || [ -e "$app/build/plain.html.noscript" ] || grep -qF -e '<!--' -e '<noscript' "$app/build/plain.html"; then bad "an own page without the marker got a fallback, a wrapper or a GIF"
else ok "an own page without /*CDOM_NOSCRIPT*/ gets no fallback, no wrapper and no GIF, and no Chrome runs"; fi

# ---- tools/cdpage's refusals ----
r="$out/refuse"
mkdir -p "$r"
printf 'var cdomLits=[0];' > "$r/ok.lits"
printf 'var cdom = 1;\n' > "$r/ok.js"
printf '<!doctype html>\n<body>\n/*CDOM_NOSCRIPT*/\n<div id="app"></div>\n<script>\n/*CDOM_SHIM*/\n</script>\n' > "$r/page.html"
NS=(--noscript-shot a.gif=640x480 --noscript-snowman s.gif=96x96)
# refuse name page shim lits want [flags...]: cdpage refuses, says want, and leaves no file
refuse() {
  local name="$1" pg="$2" shim="$3" lits="$4" want="$5"; shift 5
  rm -f "$r/out.html"
  if "$CDPAGE" --page "$pg" --out "$r/out.html" --shim "$shim" --lits "$lits" "$@" 2> "$r/$name.log"; then bad "$name: taken"
  elif [ -e "$r/out.html" ] || compgen -G "$r/out.html.tmp*" > /dev/null; then bad "$name: refused but left a file"
  elif ! grep -qF -- "$want" "$r/$name.log"; then bad "$name: refused with another message:"; cat "$r/$name.log"
  else ok "$name: $(head -c 200 "$r/$name.log")"; fi
}
take() {
  local name="$1" pg="$2" shim="$3" lits="$4"; shift 4
  if "$CDPAGE" --page "$pg" --out "$r/out.html" --shim "$shim" --lits "$lits" "$@" 2> "$r/$name.log"; then ok "$name"; else bad "$name: refused:"; cat "$r/$name.log"; fi
}
printf 'var a = 1; // <!-- then <script> in a comment\n' > "$r/planted.js"
printf 'var cdomLits=[0,"<!--","<script>"];' > "$r/planted.lits"
printf 'var s = "<script>";\n' > "$r/tag.js"
# Without the fallback's wrappers, the planted sequence leaves the parser double-escaped at the </script>: refused.
refuse "a shim with <!-- and then <script (the planted sequence), no fallback" "$r/page.html" "$r/planted.js" "$r/ok.lits" 'double-escaped'
refuse "a string table with <!-- and then <script, no fallback" "$r/page.html" "$r/ok.js" "$r/planted.lits" 'double-escaped'
take "a shim with <script and no <!--, no fallback, is taken (the parser is never escaped)" "$r/page.html" "$r/tag.js" "$r/ok.lits"
# With them, the wrapper's closing --> takes the parser from double-escaped back to plain script data, before the
# </script>: the same texts are safe, taken, and Chrome reads every script of the page to its first </script>.
scripts_of() { "$PY" -c 'import sys
t = open(sys.argv[1], encoding="latin-1").read(); low = t.lower(); out = []; i = 0
while True:
    i = low.find("<script", i)
    if i < 0: break
    b = t.index(">", i) + 1; e = low.index("</script", b); out.append(t[b:e]); i = e
open(sys.argv[2], "w", encoding="latin-1").write(chr(0).join(out))' "$1" "$1.content"; }
for w in planted.js:ok.lits ok.js:planted.lits tag.js:ok.lits; do
  rm -f "$r/out.html"
  if "$CDPAGE" --page "$r/page.html" --out "$r/out.html" --shim "$r/${w%%:*}" --lits "$r/${w##*:}" "${NS[@]}" 2> "$r/w.log"; then
    cp "$r/out.html" "$r/wrapped_${w%%.*}_${w##*:}.html"; scripts_of "$r/wrapped_${w%%.*}_${w##*:}.html"
  else bad "with the fallback, the shim ${w%%:*} and table ${w##*:} are refused:"; cat "$r/w.log"; fi
done
node "$here/chrome.mjs" ends "$r"/wrapped_*.html > "$r/wrapped.log" 2>&1
if [ "$(grep -c ' ended$' "$r/wrapped.log")" = 3 ]; then ok "with the fallback, the planted shim, the planted table and a <script in a string are taken: their wrapper's --> ends the escape, and Chrome reads every script to its first </script>"
else bad "with the fallback, a planted page runs on in Chrome:"; cat "$r/wrapped.log"; fi
printf 'var a = 2; while (a-->0) {}\n' > "$r/arrow.js"
refuse "a shim with --> under the fallback (it would end the hiding comment)" "$r/page.html" "$r/arrow.js" "$r/ok.lits" 'holds "-->"' "${NS[@]}"
take "the same shim without the fallback is taken" "$r/page.html" "$r/arrow.js" "$r/ok.lits"
printf '<!doctype html>\n<body>\n/*CDOM_NOSCRIPT*/\n<script type="module">\n/*CDOM_SHIM*/\n</script>\n' > "$r/module.html"
refuse "a module script under the fallback (<!-- is a syntax error in a module)" "$r/module.html" "$r/ok.js" "$r/ok.lits" 'module script' "${NS[@]}"
printf '<!doctype html>\n<body>\n<script>\n/*CDOM_NOSCRIPT*/\n/*CDOM_SHIM*/\n</script>\n' > "$r/inscript.html"
refuse "the marker inside a <script>" "$r/inscript.html" "$r/ok.js" "$r/ok.lits" 'inside a <script> element' "${NS[@]}"
printf '<!doctype html>\n<body>\n/*CDOM_NOSCRIPT*/\n/*CDOM_NOSCRIPT*/\n<script>\n/*CDOM_SHIM*/\n</script>\n' > "$r/twice.html"
refuse "the marker twice" "$r/twice.html" "$r/ok.js" "$r/ok.lits" 'a second /*CDOM_NOSCRIPT*/' "${NS[@]}"
refuse "an image url with --" "$r/page.html" "$r/ok.js" "$r/ok.lits" 'no --' --noscript-shot a--b.gif=640x480 --noscript-snowman s.gif=96x96
take "a plain shim with the fallback is taken" "$r/page.html" "$r/ok.js" "$r/ok.lits" "${NS[@]}"
# The check against Chrome's own parser: for each planted script text, cdpage refuses exactly when Chrome's first
# script runs on past the first </script>.
e="$out/ends"
mkdir -p "$e"
cases=('<!--<script>' '<!-- <SCRIPT x>' '<!--<script/' '<!--<scripts>' '<!-- --> <script>' '<!--<script></script>' '<!--<script>-->'
  '<!-->a<script>' '<!--->a<script>' "$(printf '<!--<script\t>')" "$(printf '<!--<script\n')" '<!-- <script> --> x' 'x<script>' '<!--<script>--!>'
  '<!--<script>- ->' '<!--<script></scripty>' "$(printf '<!--\nvar a; // <!-- <script>\n//-->')" "$(printf '<!--\n<script>-->x<!--<script>\n//-->')")
i=0
pages=()
for c in "${cases[@]}"; do
  i=$((i + 1))
  { printf '<!doctype html>\n<script>%s</script>\n<p id="after">x</p>\n<script>\n/*CDOM_SHIM*/\n</script>\n' "$c"; } > "$e/c$i.html"
  scripts_of "$e/c$i.html"
  if "$CDPAGE" --page "$e/c$i.html" --out "$e/c$i.out.html" --shim "$r/ok.js" --lits "$r/ok.lits" 2> "$e/c$i.log"; then echo ended > "$e/c$i.cd"
  elif grep -qF 'double-escaped' "$e/c$i.log"; then echo ran-on > "$e/c$i.cd"
  else echo "error" > "$e/c$i.cd"; fi
  pages+=("$e/c$i.html")
done
node "$here/chrome.mjs" ends "${pages[@]}" > "$e/chrome.log" 2>&1
agree=0
i=0
for c in "${cases[@]}"; do
  i=$((i + 1))
  ch=$(grep "^c$i.html " "$e/chrome.log" | cut -d' ' -f2)
  if [ -n "$ch" ] && [ "$ch" = "$(cat "$e/c$i.cd")" ]; then agree=$((agree + 1)); else bad "the parse check and Chrome disagree on $(printf '%q' "$c"): cdpage $(cat "$e/c$i.cd"), Chrome ${ch:-nothing}"; fi
done
[ "$agree" = "${#cases[@]}" ] && ok "the parse check agrees with Chrome's parser on all ${#cases[@]} planted script texts ($(grep -c ran-on "$e/chrome.log") run on past </script>, $(grep -c ' ended' "$e/chrome.log") end there)"
finish
