#!/bin/bash
# The math bench on Alex's Mac (docs/math-design.md, "The Mac"), in the style of bench/image/prog-mac.sh: builds,
# then measures natively, in node, in headless Chrome and then Safari through the same page, 9 interleaved rounds
# each, and writes results/<date>-<name>-*, then the tables (run.sh does the work; this sets the Mac's browsers).
# Needs: brew install llvm lld binaryen brotli (and mpfr for the accuracy sweep, which runs only with ACCURACY=1:
# accuracy is the same on every machine but for glibc's, which the Mac does not have), node, Google Chrome, and
# Safari with "Allow Remote Automation" (Develop menu). CHROME names another Chrome binary.
# Usage: bench/math/mac.sh [name=mac]   (a name already used today is refused, so a run never overwrites a result)
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"; cd "$here" || exit 1
for t in wasm-opt brotli node; do command -v "$t" > /dev/null || { echo "mac.sh: no $t on the PATH (brew install binaryen brotli node)" >&2; exit 1; }; done
BROWSERS="chrome safari" ACCURACY="${ACCURACY:-0}" ./run.sh "${1:-mac}"
