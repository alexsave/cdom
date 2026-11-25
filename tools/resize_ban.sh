#!/bin/bash
# The browser resize ban (Alex's rule): cdom never asks the browser to scale an image. A resize is C's
# (src/cdom_image.c), the same pixels in every browser; the browser's scalers differ by browser and by version.
# So none of these appears in what cdom ships or builds with (shim/, src/, cdom.mk, Makefile, tools/):
#   createImageBitmap's resize options: resizeWidth, resizeHeight, resizeQuality
#   a canvas's imageSmoothingQuality
#   drawImage with a size (five arguments or nine: the forms that scale; three draw at the image's own size)
# bench/ and tests/ may use them (a benchmark's browser arm, a test's oracle), and are not read; nor is this file,
# nor tools/mutate.py, which holds each form to plant it (the gate's own mutation check).
# Fails naming each file and line. Run by tools/gate.sh; standalone: tools/resize_ban.sh [dir].
set -uo pipefail
top="${1:-$(cd "$(dirname "$0")/.." && pwd)}"
cd "$top" || exit 1
skip=" tools/resize_ban.sh tools/mutate.py "
files=()
while IFS= read -r -d '' f; do
    case "$skip" in *" $f "*) continue;; esac
    [ -f "$f" ] && files+=("$f")
done < <(git ls-files -z -- shim src tools cdom.mk Makefile)
if [ "${#files[@]}" -eq 0 ]; then echo "FAIL resize ban: no files to read under $top"; exit 1; fi
fail=0
# The option and property names, anywhere (a key, a member, a string).
if hits="$(grep -nE '\b(resizeWidth|resizeHeight|resizeQuality|imageSmoothingQuality)\b' "${files[@]}")"; then
    while IFS= read -r h; do echo "FAIL resize ban: $h"; done <<< "$hits"; fail=1
fi
# drawImage with more than three arguments: the commas at the call's own depth, across lines, counted by perl
# (strings and nested calls taken as they come: a comma inside one nested level of brackets is not counted).
if hits="$(perl -0777 -ne '
  while (/drawImage\s*\(/g) {
    my ($at, $depth, $commas, $i) = (pos, 1, 0, pos);
    while ($i < length && $depth) {
      my $c = substr($_, $i++, 1);
      $depth++ if $c =~ /[([{]/; $depth-- if $c =~ /[)\]}]/;
      $commas++ if $c eq "," && $depth == 1;
    }
    if ($commas >= 3) { my $line = 1 + (substr($_, 0, $at) =~ tr/\n//); print "$ARGV:$line: drawImage with ", $commas + 1, " arguments (a scaling form)\n"; }
  }' "${files[@]}")" && [ -n "$hits" ]; then
    while IFS= read -r h; do echo "FAIL resize ban: $h"; done <<< "$hits"; fail=1
fi
if [ $fail = 0 ]; then echo "ok   resize ban: no browser scaling (createImageBitmap's resize options, imageSmoothingQuality, drawImage with a size) in ${#files[@]} files of shim/, src/, cdom.mk, Makefile and tools/"; fi
exit $fail
