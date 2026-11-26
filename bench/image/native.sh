#!/bin/bash
# The image bench's native profile (docs/image-simd-hand.md): builds the module's files as cdom.mk lists them for x86-64
# at -O2 -g, scalar, and runs perf record (cpu-clock) on each input, then prints the top functions by self time.
# Usage: bench/image/native.sh [reps=20] [inputs...]     PERF=path to perf (default: perf on the PATH)
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"; top="$(cd "$here/../.." && pwd)"
. "$top/tools/cdomflags.sh"
var() { cdom_mk_var "$1"; }
read -r -a OWN <<< "$(var CDOM_IMAGE_OWN_SRC) $(var CDOM_IMAGE_INFLATE_SRC)"; read -r -a VENDOR <<< "$(var CDOM_IMAGE_VENDOR_SRC)"
read -r -a OWNF <<< "$(var CDOM_IMAGE_OWN_CFLAGS)"
read -r -a VENDORF <<< "$(var CDOM_IMAGE_VENDOR_CFLAGS)"
o="$here/build-native"; mkdir -p "$o"
H=(-std=c2x -O2 -g -fno-omit-frame-pointer -I"$top/src" -I"$top/tests/image")
cc() { clang "${H[@]}" "$@"; }
for f in "${OWN[@]}" "$top/src/cdom_image_sjlj.c" "$top/tests/image/host_alloc.c" "$here/native.c"; do cc "${OWNF[@]}" -c -o "$o/$(basename "$f" .c).o" "$f" || exit 1; done
for f in "${VENDOR[@]}"; do cc "${VENDORF[@]}" -c -o "$o/v_$(basename "$f" .c).o" "$f" || exit 1; done
cc -o "$o/native" "$o"/*.o || exit 1
reps="${1:-20}"; shift || true
for i in "${@:-p12.jpg p12p.jpg p48.jpg p12.png}"; do for n in $i; do
    echo "== $n"; "${PERF:-perf}" record -q -F 5000 -e cpu-clock -o "$o/perf.data" "$o/native" "$here/inputs/$n" "$reps" 2>&1 | tail -1
    "${PERF:-perf}" report -i "$o/perf.data" --no-children --stdio 2>/dev/null | grep -v '^#' | grep -v '^$' | head -${TOP:-12}
done; done
