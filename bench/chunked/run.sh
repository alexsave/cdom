#!/bin/bash
# The chunked-read prototype (bench/chunked/proto.c, docs/chunked-file-read-design.md): builds it natively with the
# image module's files as cdom.mk lists them, then checks every mode against today's whole-file decode and times them.
# Usage: bench/chunked/run.sh [reps=7]      inputs: bench/image/inputs (bench/image/inputs.py), plus p64.jpg if made.
# SAN=1 builds with AddressSanitizer and UndefinedBehaviorSanitizer at -O1 and only checks.
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"; top="$(cd "$here/../.." && pwd)"
. "$top/tools/cdomflags.sh"
var() { cdom_mk_var "$1"; }
read -r -a OWN <<< "$(var CDOM_IMAGE_OWN_SRC)"; read -r -a VENDOR <<< "$(var CDOM_IMAGE_VENDOR_SRC)"
read -r -a MINIZ <<< "$(var CDOM_IMAGE_MINIZ_SRC)"; read -r -a OWNF <<< "$(var CDOM_IMAGE_OWN_CFLAGS)"
read -r -a VENDORF <<< "$(var CDOM_IMAGE_VENDOR_CFLAGS)"; read -r -a MINIZF <<< "$(var CDOM_IMAGE_MINIZ_CFLAGS)"
o="$here/build${SAN:+-san}"; mkdir -p "$o"
if [ -n "${SAN:-}" ]; then H=(-std=c2x -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined)
else H=(-std=c2x -O2 -g); fi
H+=(-I"$top/src" -I"$top/tests/image")
cc() { clang "${H[@]}" "$@"; }
for f in "${OWN[@]}" "$top/src/cdom_image_sjlj.c" "$top/tests/image/host_alloc.c"; do cc "${OWNF[@]}" -c -o "$o/$(basename "$f" .c).o" "$f" || exit 1; done
cc "${OWNF[@]}" -Wall -Wextra -Werror -c -o "$o/proto.o" "$here/proto.c" || exit 1
for f in "${VENDOR[@]}"; do cc "${VENDORF[@]}" -c -o "$o/v_$(basename "$f" .c).o" "$f" || exit 1; done
for f in "${MINIZ[@]}"; do cc "${MINIZF[@]}" -c -o "$o/m_$(basename "$f" .c).o" "$f" || exit 1; done
cc -o "$o/proto" "$o"/*.o || exit 1
in="$top/bench/image/inputs"
files=(); for n in p12.jpg p12p.jpg p48.jpg p64.jpg p64p.jpg p12.png; do [ -f "$in/$n" ] && files+=("$in/$n"); done
"$o/proto" check "${files[@]}" || exit 1
# EDGE=dir: the inputs bench/chunked/edge.py made there
if [ -n "${EDGE:-}" ]; then QUIET=1 "$o/proto" check "$EDGE"/*.jpg "$EDGE"/*.png || exit 1; fi
# ORACLE=dir: also every input tests/image/oracle.py made there (its expected images are not used: whole is the reference)
if [ -n "${ORACLE:-}" ]; then QUIET=1 "$o/proto" check "$ORACLE"/inputs/*.jpg "$ORACLE"/inputs/*.png || exit 1; fi
[ -n "${SAN:-}" ] && exit 0
[ -n "${NOTIME:-}" ] && exit 0
for f in "${files[@]}"; do "$o/proto" time "$f" "${1:-7}"; done
