#!/bin/bash
# Builds and runs tests/cdv.c natively (the cdv value format through the C builders and the
# C sink's decoder, traps, flush safety, PROP markup), with interned and with per-op strings.
# Usage: tests/cdv.sh
set -euo pipefail
top="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$top/tests/out"
nice -n 10 cc -O2 -Wall -Wextra -Werror -o "$top/tests/out/cdv" "$top/tests/cdv.c" "$top/src/cdom.c" "$top/src/cdom_html.c"
nice -n 10 cc -O2 -Wall -Wextra -Werror -DCDOM_INTERN_TEXT=0 -o "$top/tests/out/cdv_pertext" "$top/tests/cdv.c" "$top/src/cdom.c" "$top/src/cdom_html.c"
"$top/tests/out/cdv"
echo "-- per-op strings (CDOM_INTERN_TEXT=0)"
"$top/tests/out/cdv_pertext"
