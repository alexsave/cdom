#!/bin/bash
# The math bench's sizes (docs/math-design.md, "Size"): each arm's size modules (make sizes: an empty module, one
# that calls sin, one that calls every function the arm has, each through wasm-opt -O3), as TSV: arm kind wasm_bytes
# brotli_bytes, brotli at quality 11 (the brotli command: apt install brotli, brew install brotli).
# Usage: bench/math/sizes.sh   (after make -C bench/math sizes)
set -euo pipefail
cd "$(dirname "$0")"
command -v brotli > /dev/null || { echo "sizes.sh: no brotli on the PATH" >&2; exit 1; }
printf 'arm\tkind\twasm_bytes\tbrotli_bytes\n'
for a in musl msun core ours js1; do
  for k in empty sin all; do
    f="build/size/$a-$k.wasm"
    [ -f "$f" ] || { echo "sizes.sh: no $f (make sizes)" >&2; exit 1; }
    printf '%s\t%s\t%s\t%s\n' "$a" "$k" "$(wc -c < "$f" | tr -d ' ')" "$(brotli -c -q 11 "$f" | wc -c | tr -d ' ')"
  done
done
