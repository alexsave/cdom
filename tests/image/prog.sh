#!/bin/bash
# The progressive decoder's differential test (docs/image-progressive-speed.md): tests/image/prog.c, natively, with
# AddressSanitizer and UndefinedBehaviorSanitizer, on the corpus tests/image/progcorpus.py makes (seeded), and on any
# more directories given (each with a list.txt). The binary holds two builds of the module: this tree's, as cdom.mk
# lists its files, and libjpeg-turbo's own progressive path (jdphuff.c and jdcoefct.c in place of
# src/cdom_image_jpegp.c and src/cdom_image_ljt_coef.c), linked into one object whose symbols are all renamed ref_*.
# Usage: tests/image/prog.sh [DIR...]      HOST_CC: the compiler (default clang); LLVM_BIN: where ld.lld, llvm-nm and
# llvm-objcopy are (default: next to HOST_CC, else the PATH)
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
top="$(cd "$here/../.." && pwd)"
out="$top/tests/out/image/prog-test"
. "$top/tools/cdomflags.sh"
var() { cdom_mk_var "$1"; }
read -r -a OWN <<< "$(var CDOM_IMAGE_OWN_SRC)"
read -r -a VENDOR <<< "$(var CDOM_IMAGE_VENDOR_SRC)"
read -r -a MINIZ <<< "$(var CDOM_IMAGE_MINIZ_SRC)"
read -r -a OWNF <<< "$(var CDOM_IMAGE_OWN_CFLAGS)"
read -r -a VENDORF <<< "$(var CDOM_IMAGE_VENDOR_CFLAGS)"
read -r -a MINIZF <<< "$(var CDOM_IMAGE_MINIZ_CFLAGS)"
LJT="$top/third_party/libjpeg-turbo-3.2.1/src"
HCC="${HOST_CC:-clang}"
bin="${LLVM_BIN:-$(dirname "$(readlink -f "$(command -v "$HCC")")")}"
tool() { if [ -x "$bin/$1" ]; then echo "$bin/$1"; else command -v "$1"; fi; }
LD="$(tool ld.lld)"; NM="$(tool llvm-nm)"; OBJCOPY="$(tool llvm-objcopy)"
SAN=(-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
H=(-std=c2x -O1 -g "${SAN[@]}" -I"$top/src" -I"$here")
o="$out/obj"
rm -rf "$o"; mkdir -p "$o/new" "$o/ref"
build() {
  local f d
  # this tree's module
  for f in "${OWN[@]}" "$top/src/cdom_image_sjlj.c"; do "$HCC" "${H[@]}" -Wall -Wextra -Werror -DCDOM_IMAGE_TESTING "${OWNF[@]}" -c -o "$o/new/$(basename "$f" .c).o" "$f" || return 1; done
  for f in "${VENDOR[@]}"; do "$HCC" "${H[@]}" "${VENDORF[@]}" -c -o "$o/new/v_$(basename "$f" .c).o" "$f" || return 1; done
  for f in "${MINIZ[@]}"; do "$HCC" "${H[@]}" "${MINIZF[@]}" -c -o "$o/new/m_$(basename "$f" .c).o" "$f" || return 1; done
  # libjpeg's own progressive path: the same files but cdom's two, and jdphuff.c and jdcoefct.c as upstream builds them
  for f in "${OWN[@]}" "$top/src/cdom_image_sjlj.c"; do
    [ "$(basename "$f")" = cdom_image_jpegp.c ] && continue
    "$HCC" "${H[@]}" "${OWNF[@]}" -c -o "$o/ref/$(basename "$f" .c).o" "$f" || return 1
  done
  for f in "${VENDOR[@]}" "$LJT/jdphuff.c" "$LJT/wrapper/jdcoefct-8.c"; do
    [ "$(basename "$f")" = cdom_image_ljt_coef.c ] && continue
    "$HCC" "${H[@]}" "${VENDORF[@]}" -c -o "$o/ref/v_$(basename "$f" .c).o" "$f" || return 1
  done
  for f in "${MINIZ[@]}"; do "$HCC" "${H[@]}" "${MINIZF[@]}" -c -o "$o/ref/m_$(basename "$f" .c).o" "$f" || return 1; done
  "$LD" -r -o "$o/ref.o" "$o"/ref/*.o || return 1
  "$NM" --defined-only --extern-only "$o/ref.o" | awk 'NF == 3 { print $3 " ref_" $3 }' > "$o/ref.syms"
  "$OBJCOPY" --redefine-syms="$o/ref.syms" "$o/ref.o" "$o/ref_renamed.o" || return 1
  "$HCC" "${H[@]}" -Wall -Wextra -Werror -c -o "$o/host_alloc.o" "$here/host_alloc.c" || return 1
  "$HCC" "${H[@]}" -Wall -Wextra -Werror -c -o "$o/prog.o" "$here/prog.c" || return 1
  "$HCC" "${H[@]}" -o "$out/prog" "$o"/new/*.o "$o/ref_renamed.o" "$o/host_alloc.o" "$o/prog.o"
}
if ! build > "$out.build.log" 2>&1; then echo "FAIL the progressive differential test does not build:"; tail -20 "$out.build.log"; exit 1; fi
python3 -I "$here/progcorpus.py" "$out/corpus" || exit 1
# The two builds define the same globals under two names; ASan's one-definition check reads the names from before the
# renaming, so it is off for this binary (every other check is on).
export ASAN_OPTIONS="detect_odr_violation=0${ASAN_OPTIONS:+:$ASAN_OPTIONS}"
fail=0
for d in "$out/corpus" "$@"; do
  if "$out/prog" "$d" > "$out/prog.log" 2>&1; then echo "ok   progressive differential: $(tail -1 "$out/prog.log")"
  else fail=1; echo "FAIL progressive differential ($d):"; grep -v '^prog:' "$out/prog.log" | head -20; tail -1 "$out/prog.log"; fi
done
exit $fail
