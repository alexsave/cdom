#!/bin/bash
# The image module's fuzz targets (tests/image/fuzz_jpeg.c, fuzz_png.c, fuzz_inflate.c), native, with libFuzzer, AddressSanitizer and
# UndefinedBehaviorSanitizer (Homebrew's clang: Apple's has no libFuzzer). Seeds: the oracle's small inputs
# (tests/image/run.sh makes them in tests/out/image/inputs) and each cut at a third and two thirds; the corpus grows in
# tests/out/image/fuzz. The limits catch the review's item 1: no single allocation over 100 MB (the module's budget is
# 96 MiB, so its largest block is under it) and the process under 320 MB.
# Usage: tests/image/fuzz.sh [seconds per target = 600]     FUZZ_TARGETS: the targets to run (default "jpeg png inflate")
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
top="$(cd "$here/../.." && pwd)"
out="$top/tests/out/image/fuzz"
mkdir -p "$out"
targets="${FUZZ_TARGETS:-jpeg png inflate}"
. "$top/tools/cdomflags.sh"
CC="${FUZZ_CC:-/opt/homebrew/opt/llvm/bin/clang}"
read -r -a OWN <<< "$(cdom_mk_var CDOM_IMAGE_OWN_SRC) $(cdom_mk_var CDOM_IMAGE_INFLATE_SRC)"
read -r -a VENDOR <<< "$(cdom_mk_var CDOM_IMAGE_VENDOR_SRC)"
read -r -a OWNF <<< "$(cdom_mk_var CDOM_IMAGE_OWN_CFLAGS)"
read -r -a VENDORF <<< "$(cdom_mk_var CDOM_IMAGE_VENDOR_CFLAGS)"
F=(-std=c2x -O1 -g -fsanitize=fuzzer-no-link,address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -I"$top/src" -I"$here")
o="$out/obj"; rm -rf "$o"; mkdir -p "$o"
for f in "${OWN[@]}" "$top/src/cdom_image_sjlj.c" "$here/host_alloc.c"; do "$CC" "${F[@]}" -Wall -Wextra -Werror "${OWNF[@]}" -c -o "$o/$(basename "$f" .c).o" "$f"; done
for f in "${VENDOR[@]}"; do "$CC" "${F[@]}" "${VENDORF[@]}" -c -o "$o/v_$(basename "$f" .c).o" "$f"; done
for t in jpeg png; do
  case " $targets " in *" $t "*) ;; *) continue;; esac
  "$CC" -O1 -g -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined -I"$top/src" -I"$here" -o "$out/fuzz_$t" "$here/fuzz_$t.c" "$o"/*.o
  mkdir -p "$out/corpus_$t" "$out/seeds_$t"
  ext=$([ $t = jpeg ] && echo jpg || echo png)
  for s in "$top/tests/out/image/inputs"/*."$ext"; do
    [ "$(wc -c < "$s")" -le 200000 ] || continue
    b="$(basename "$s")"; n="$(wc -c < "$s")"
    cp "$s" "$out/seeds_$t/$b"
    head -c $((n / 3)) "$s" > "$out/seeds_$t/$b.cut1"; head -c $((2 * n / 3)) "$s" > "$out/seeds_$t/$b.cut2"
  done
done
# the inflate against zlib (fuzz_inflate.c, which links the system's zlib); its seeds: a byte that picks the pieces,
# then zlib streams of a few kinds (one-off, from Python's zlib, nothing random)
"$CC" -O1 -g -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined -I"$top/src" -I"$here" -o "$out/fuzz_inflate" \
  "$here/fuzz_inflate.c" "$top/src/cdom_image_inflate.c" -lz
mkdir -p "$out/corpus_inflate" "$out/seeds_inflate"
python3 -I - "$out/seeds_inflate" <<'PY'
import sys, zlib
d = sys.argv[1]
data = [bytes(range(256)) * 40, b"abcabcabd" * 900, bytes(5000), bytes((i * 7 + (i >> 5)) & 255 for i in range(70000))]
k = 0
for x in data:
    for level, strat in [(0, 0), (1, 0), (6, 0), (9, 0), (6, zlib.Z_HUFFMAN_ONLY), (6, zlib.Z_RLE), (6, zlib.Z_FIXED)]:
        c = zlib.compressobj(level, zlib.DEFLATED, 15, 8, strat)
        open(f"{d}/s{k}", "wb").write(bytes([k % 7]) + c.compress(x) + c.flush()); k += 1
PY
secs="${1:-600}"
cd "$out"
export ASAN_OPTIONS="quarantine_size_mb=16"   # leaks: the targets check the counted heap
for t in $targets; do
  ./fuzz_$t "corpus_$t" "seeds_$t" -max_total_time="$secs" -timeout=20 -rss_limit_mb=320 -malloc_limit_mb=100 -max_len=200000 \
    -artifact_prefix="$out/crash-$t-" > "$t.log" 2>&1 &
done
wait || true
fail=0
for t in $targets; do
  tail -n 2 "$t.log"
  if ls "$out"/crash-$t-* > /dev/null 2>&1; then echo "FAIL fuzz_$t found: $(ls "$out"/crash-$t-*)"; fail=1; else echo "ok   fuzz_$t: $secs s, no crash, leak or limit"; fi
done
exit $fail
