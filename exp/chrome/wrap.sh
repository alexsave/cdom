#!/bin/bash
# Shared by the flag-arm wrappers (proof of why direct calls win): execs Chrome with the arm's
# V8 flags merged into the harness's own --js-flags (the harness passes --js-flags=--expose-gc,
# and Chrome keeps only the last --js-flags switch, so a second switch would drop expose-gc).
# The caller sets X to the V8 flags. Every launch's final argument list is appended to
# results/chrome_flags.log so a run can show the flags it ran with.
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
args=(); seen=0
for a in "$@"; do
  case "$a" in --js-flags=*) a="$a $X"; seen=1;; esac
  args+=("$a")
done
[ $seen = 1 ] || args+=("--js-flags=$X")
printf '%s %s:' "$(date +%T)" "${ARM:-?}" >> "$here/../../results/chrome_flags.log"
printf ' %q' "${args[@]}" >> "$here/../../results/chrome_flags.log"; echo >> "$here/../../results/chrome_flags.log"
exec "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" "${args[@]}"
