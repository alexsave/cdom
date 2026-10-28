#!/bin/bash
# Builds and runs tests/state.c natively: the hydrate entry's base64 decoding of the state blob.
# Usage: tests/state.sh
set -euo pipefail
top="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$top/tests/out"
nice -n 10 cc -O2 -Wall -Wextra -Werror -o "$top/tests/out/state" "$top/tests/state.c" "$top/src/cdom.c" "$top/src/cdom_html.c"
"$top/tests/out/state"
