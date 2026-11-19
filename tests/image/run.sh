#!/bin/bash
# The image module's tests (docs/image-design.md, "Tests"):
#   1. third_party/ is the upstream archives with the documented edits (tools/third_party.py check);
#   2. the oracle (tests/image/oracle.py) makes the inputs and the exact expected images into tests/out/image;
#   3. the native build (the module's files as cdom.mk lists them, with AddressSanitizer and UndefinedBehaviorSanitizer,
#      on tests/image/host_alloc.c's counted heap) runs tests/image/test_image.c: every image equal to the oracle's to
#      the byte, the refusals, a heap that runs out at every allocation, the peaks;
#   4. the wasm build, both ways out of a libjpeg error (CDOM_IMAGE_SJLJ js and eh), runs every case in Chrome
#      (tools/image.mjs, through tools/cdp.mjs): the same statuses and the same bytes as the native build, and a
#      capped heap answering NO_ROOM.
# IMAGE_BIG=0 skips the oracle's big inputs (the 12 MP, 16 MP and 35 MP files) for a quick run.
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
top="$(cd "$here/../.." && pwd)"
out="$top/tests/out/image"
mkdir -p "$out"
. "$top/tools/cdomflags.sh"
fail=0
python3 -I "$top/tools/third_party.py" check || fail=1
var() { cdom_mk_var "$1"; }
read -r -a OWN <<< "$(var CDOM_IMAGE_OWN_SRC)"
read -r -a VENDOR <<< "$(var CDOM_IMAGE_VENDOR_SRC)"
read -r -a MINIZ <<< "$(var CDOM_IMAGE_MINIZ_SRC)"
read -r -a OWNF <<< "$(var CDOM_IMAGE_OWN_CFLAGS)"
read -r -a VENDORF <<< "$(var CDOM_IMAGE_VENDOR_CFLAGS)"
read -r -a MINIZF <<< "$(var CDOM_IMAGE_MINIZ_CFLAGS)"
HCC="${HOST_CC:-clang}"
SAN=(-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
H=(-std=c2x -O1 -g "${SAN[@]}" -I"$top/src" -I"$here")
o="$out/native"
rm -rf "$o"; mkdir -p "$o"
build() { # the native test binary, each file as cdom.mk compiles it (its part's flags), cdom's own with -Wall -Werror
  local f
  for f in "${OWN[@]}" "$top/src/cdom_image_sjlj.c" "$here/host_alloc.c" "$here/test_image.c"; do
    "$HCC" "${H[@]}" -Wall -Wextra -Werror "${OWNF[@]}" -c -o "$o/$(basename "$f" .c).o" "$f" || return 1
  done
  for f in "${VENDOR[@]}"; do "$HCC" "${H[@]}" "${VENDORF[@]}" -c -o "$o/v_$(basename "$f" .c).o" "$f" || return 1; done
  for f in "${MINIZ[@]}"; do "$HCC" "${H[@]}" "${MINIZF[@]}" -c -o "$o/m_$(basename "$f" .c).o" "$f" || return 1; done
  "$HCC" "${H[@]}" -o "$o/test_image" "$o"/*.o
}
if ! build > "$o/build.log" 2>&1; then echo "FAIL the native image tests do not build:"; cat "$o/build.log"; exit 1; fi
if ! python3 -I "$here/oracle.py" "$out" > "$out/oracle.log" 2>&1; then echo "FAIL the oracle:"; cat "$out/oracle.log"; exit 1; fi
tail -1 "$out/oracle.log"
(cd "$out" && ASAN_OPTIONS=detect_leaks=1 "$o/test_image") || fail=1
if [ "${IMAGE_WASM:-1}" != 0 ]; then node "$top/tools/image.mjs" "$out" || fail=1; fi
exit $fail
