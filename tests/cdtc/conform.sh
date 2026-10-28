#!/bin/bash
# Compiled templates through the shim-vs-sink harness (tests/conform.mjs): feat.cdt (every
# feature, 300 random steps with clicks) and the templated rows app (60 benchmark actions).
# Each compares Chrome's DOM with the C sink's HTML after every step, then hydrates the
# sink's first frame and replays. Usage: tests/cdtc/conform.sh [feat|rows]...
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
top="$(cd "$here/../.." && pwd)"
gen="$top/tests/out/cdtc/gen"
mkdir -p "$gen"
nice -n 10 cc -O2 -Wall -Wextra -Werror -o "$gen/cdtc" "$top/tools/cdtc.c" || exit 1
"$gen/cdtc" "$here/feat.cdt" -o "$gen/feat" || exit 1
"$gen/cdtc" "$top/bench/cdom-tpl/rows.cdt" -o "$gen/rows" || exit 1
bad=0
for what in ${@:-feat rows}; do
  echo "== $what"
  case "$what" in
    feat) CONFORM_NAME=cdtc_feat CONFORM_SRCS="$here/conform_feat.c $gen/feat.c" CONFORM_INC="$top/src $gen" \
            node "$top/tests/conform.mjs" || bad=1 ;;
    rows) CONFORM_NAME=cdtc_rows CONFORM_SRCS="$here/conform_rows.c $top/bench/cdom-tpl/app.c $gen/rows.c" CONFORM_INC="$top/src $gen" \
            node "$top/tests/conform.mjs" || bad=1 ;;
    *) echo "unknown: $what"; bad=1 ;;
  esac
done
exit $bad
