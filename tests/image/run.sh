#!/bin/bash
# The image module's tests (docs/image-design.md, "Tests"):
#   1. third_party/ is the upstream archives with the documented edits (tools/third_party.py check);
#   2. the oracle (tests/image/oracle.py) makes the inputs and the exact expected images into tests/out/image;
#   3. the native build (the module's files as cdom.mk lists them, with AddressSanitizer and UndefinedBehaviorSanitizer,
#      on tests/image/host_alloc.c's counted heap) runs tests/image/test_image.c: every image equal to the oracle's to
#      the byte, the refusals, a heap that runs out at every allocation, the peaks;
#   4. cdom's inflate against zlib (inflate_diff.c), pushed whole and in pieces;
#   4b. the fed decoder (tests/image/test_chunked.c: cd_image_begin .. cd_image_end and cd_image_file_cb, the chunked-read
#      design's tests 1 to 9) on the oracle's inputs and bench/chunked/edge.py's, natively under the same sanitizers;
#      CHUNKED_FULL=1 every piece size on every input, CHUNKED_BENCH=dir bench/image/inputs.py's files too;
#   5. the wasm build, both ways out of a libjpeg error (CDOM_IMAGE_SJLJ js and eh), each scalar and with the hand-written
#      SIMD loops (CDOM_IMAGE_SIMD=0 and 1), runs every case in Chrome (tools/image.mjs, through tools/cdp.mjs): the same
#      statuses and the same bytes as the native build, and a capped heap answering NO_ROOM;
#   6. the SIMD loops against their scalar counterparts, on random and edge inputs (tests/image/simd.sh, in node);
#      IMAGE_SIMD=0 skips it;
#   7. the progressive decoder against libjpeg-turbo's own, byte for byte at every scale, on a seeded corpus of
#      progressive files (tests/image/prog.sh, natively, with the sanitizers); IMAGE_PROG=0 skips it.
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
read -r -a OWN <<< "$(var CDOM_IMAGE_OWN_SRC) $(var CDOM_IMAGE_INFLATE_SRC)"
read -r -a VENDOR <<< "$(var CDOM_IMAGE_VENDOR_SRC)"
read -r -a OWNF <<< "$(var CDOM_IMAGE_OWN_CFLAGS)"
read -r -a VENDORF <<< "$(var CDOM_IMAGE_VENDOR_CFLAGS)"
HCC="${HOST_CC:-clang}"
SAN=(-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
H=(-std=c2x -O1 -g "${SAN[@]}" -I"$top/src" -I"$here")
o="$out/native"
rm -rf "$o"; mkdir -p "$o"
build() { # the native test binaries, each file as cdom.mk compiles it (its part's flags), cdom's own with -Wall -Werror
    local f
    for f in "${OWN[@]}" "$top/src/cdom_image_sjlj.c" "$here/host_alloc.c" "$here/test_image.c" "$top/src/cdom_image_file.c" "$here/test_chunked.c"; do
        "$HCC" "${H[@]}" -Wall -Wextra -Werror "${OWNF[@]}" -c -o "$o/$(basename "$f" .c).o" "$f" || return 1
    done
    for f in "${VENDOR[@]}"; do "$HCC" "${H[@]}" "${VENDORF[@]}" -c -o "$o/v_$(basename "$f" .c).o" "$f" || return 1; done
    local lib=() g
    for g in "$o"/*.o; do case "$(basename "$g")" in test_image.o|test_chunked.o|cdom_image_file.o) ;; *) lib+=("$g");; esac; done
    "$HCC" "${H[@]}" -o "$o/test_image" "${lib[@]}" "$o/test_image.o"
    "$HCC" "${H[@]}" -o "$o/test_chunked" "${lib[@]}" "$o/cdom_image_file.o" "$o/test_chunked.o"
}
if ! build > "$o/build.log" 2>&1; then echo "FAIL the native image tests do not build:"; cat "$o/build.log"; exit 1; fi
if ! python3 -I "$here/oracle.py" "$out" > "$out/oracle.log" 2>&1; then echo "FAIL the oracle:"; cat "$out/oracle.log"; exit 1; fi
tail -1 "$out/oracle.log"
(cd "$out" && "$o/test_image") || fail=1
if ! python3 -I "$top/bench/chunked/edge.py" "$out/edge" > "$out/edge.log" 2>&1; then echo "FAIL bench/chunked/edge.py:"; cat "$out/edge.log"; fail=1; fi
(cd "$out" && "$o/test_chunked") || fail=1
# cdom's inflate against zlib (tests/image/inflate_diff.c): the seeded corpus, cut and damaged streams, under the
# same sanitizers; it links the system's zlib (zlib.h and -lz) as the reference
if "$HCC" "${H[@]}" -Wall -Wextra -Werror -o "$o/inflate_diff" "$here/inflate_diff.c" "$top/src/cdom_image_inflate.c" -lz > "$o/inflate_diff.log" 2>&1; then
    "$o/inflate_diff" || fail=1
else echo "FAIL inflate_diff does not build:"; cat "$o/inflate_diff.log"; fail=1; fi
if [ "${IMAGE_WASM:-1}" != 0 ]; then node "$top/tools/image.mjs" "$out" || fail=1; fi
if [ "${IMAGE_SIMD:-1}" != 0 ]; then "$here/simd.sh" "$out/simd" || fail=1; fi
if [ "${IMAGE_PROG:-1}" != 0 ]; then "$here/prog.sh" || fail=1; fi
exit $fail
