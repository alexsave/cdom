#!/bin/bash
# The whole chunked-read sweep, one command (docs/chunked-file-read-design.md, section 7; the Mac takes the numbers):
# the inputs made when missing (bench/image/inputs.py, bench/chunked/p64.py), then bench/chunked/run.mjs in Chrome and,
# on macOS, in Safari (Safari's Develop menu: Allow Remote Automation), each writing its tsv into bench/chunked/results/.
# Usage: bench/chunked/sweep.sh [rounds=15]      (SIZES, INPUTS, ARMS as run.mjs takes them)
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
top="$(cd "$here/../.." && pwd)"
rounds="${1:-15}"
in="$top/bench/image/inputs"
[ -f "$in/p12.png" ] || python3 -I "$top/bench/image/inputs.py" "$in"
[ -f "$in/p64.jpg" ] || python3 -I "$here/p64.py" "$top/bench/image" "$in"
BROWSER=chrome node "$here/run.mjs" "$rounds"
if [ "$(uname -s)" = Darwin ]; then BROWSER=safari node "$here/run.mjs" "$rounds"; fi
