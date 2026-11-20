#!/bin/bash
# The progressive decoder's differential test (docs/image-progressive-speed.md): tests/image/prog.c, natively, with
# AddressSanitizer and UndefinedBehaviorSanitizer, on the corpus tests/image/progcorpus.py makes (seeded), and on any
# more directories given (each with a list.txt). The binary holds two builds of the module: this tree's, as cdom.mk
# lists its files, and libjpeg-turbo's own progressive path (jdphuff.c and jdcoefct.c in place of
# src/cdom_image_jpegp.c and src/cdom_image_ljt_coef.c), linked into one object whose symbols are all renamed ref_*.
# With --fuzz SECONDS, it builds tests/image/fuzz_jpegp.c the same way (libFuzzer) and runs it for that long instead,
# seeded with the corpus's files under 100 KB; the corpus grows in tests/out/image/prog-test/fuzz.
# Usage: tests/image/prog.sh [--fuzz SECONDS] [DIR...]      HOST_CC: the compiler (default clang); LLVM_BIN: where llvm-nm
# and llvm-objcopy are (default: next to HOST_CC, else next to cdom.mk's WASM_CC, else the PATH)
set -uo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
top="$(cd "$here/../.." && pwd)"
out="$top/tests/out/image/prog-test"
. "$top/tools/cdomflags.sh"
var() { cdom_mk_var "$1"; }
read -r -a OWN <<< "$(var CDOM_IMAGE_OWN_SRC) $(var CDOM_IMAGE_INFLATE_SRC)"   # the inflate part is cdom's own, as run.sh builds it
read -r -a VENDOR <<< "$(var CDOM_IMAGE_VENDOR_SRC)"
read -r -a OWNF <<< "$(var CDOM_IMAGE_OWN_CFLAGS)"
read -r -a VENDORF <<< "$(var CDOM_IMAGE_VENDOR_CFLAGS)"
LJT="$top/third_party/libjpeg-turbo-3.2.1/src"
HCC="${HOST_CC:-clang}"
bin="${LLVM_BIN:-$(dirname "$(readlink -f "$(command -v "$HCC")")")}"
wbin="$(dirname "$(var WASM_CC)")"   # cdom.mk's wasm clang: Homebrew's LLVM on macOS, which is not on the PATH
tool() { local d; for d in "$bin" "$wbin"; do if [ -x "$d/$1" ]; then echo "$d/$1"; return; fi; done; command -v "$1"; }
NM="$(tool llvm-nm)"; OBJCOPY="$(tool llvm-objcopy)"
if [ -z "$NM" ] || [ -z "$OBJCOPY" ]; then echo "FAIL tests/image/prog.sh: no llvm-nm or llvm-objcopy (next to $HCC, next to $(var WASM_CC), or on the PATH; LLVM_BIN names them)"; exit 1; fi
fuzz=""
if [ "${1:-}" = --fuzz ]; then fuzz="$2"; shift 2; fi
SAN=(-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
[ -n "$fuzz" ] && SAN+=(-fsanitize=fuzzer-no-link)
H=(-std=c2x -O1 -g "${SAN[@]}" -I"$top/src" -I"$here")
o="$out/obj"
rm -rf "$o"; mkdir -p "$o/new" "$o/ref"
build() {
  local f d
  # this tree's module
  for f in "${OWN[@]}" "$top/src/cdom_image_sjlj.c"; do "$HCC" "${H[@]}" -Wall -Wextra -Werror -DCDOM_IMAGE_TESTING "${OWNF[@]}" -c -o "$o/new/$(basename "$f" .c).o" "$f" || return 1; done
  for f in "${VENDOR[@]}"; do "$HCC" "${H[@]}" "${VENDORF[@]}" -c -o "$o/new/v_$(basename "$f" .c).o" "$f" || return 1; done
  # libjpeg's own progressive path: the same files but cdom's two, and jdphuff.c and jdcoefct.c as upstream builds them
  for f in "${OWN[@]}" "$top/src/cdom_image_sjlj.c"; do
    [ "$(basename "$f")" = cdom_image_jpegp.c ] && continue
    "$HCC" "${H[@]}" "${OWNF[@]}" -c -o "$o/ref/$(basename "$f" .c).o" "$f" || return 1
  done
  for f in "${VENDOR[@]}" "$LJT/jdphuff.c" "$LJT/wrapper/jdcoefct-8.c"; do
    [ "$(basename "$f")" = cdom_image_ljt_coef.c ] && continue
    "$HCC" "${H[@]}" "${VENDORF[@]}" -c -o "$o/ref/v_$(basename "$f" .c).o" "$f" || return 1
  done
  # one relocatable object, through the compiler's own linker (ld.lld reads only ELF; macOS objects are Mach-O)
  "$HCC" -r -nostdlib -o "$o/ref.o" "$o"/ref/*.o || return 1
  # ref_ goes after Mach-O's leading underscore (C's ref_x is _ref_x there)
  local u=""; [ "$(uname)" = Darwin ] && u="_"
  "$NM" --defined-only --extern-only "$o/ref.o" | awk -v u="$u" 'NF == 3 { n = $3; if (u != "" && substr(n, 1, 1) == u) n = substr(n, 2); print $3 " " u "ref_" n }' > "$o/ref.syms"
  "$OBJCOPY" --redefine-syms="$o/ref.syms" "$o/ref.o" "$o/ref_renamed.o" || return 1
  "$HCC" "${H[@]}" -Wall -Wextra -Werror -c -o "$o/host_alloc.o" "$here/host_alloc.c" || return 1
  if [ -n "$fuzz" ]; then
    "$HCC" "${H[@]}" -Wall -Wextra -Werror -DCDOM_IMAGE_TESTING -I"$LJT" -c -o "$o/fuzz.o" "$here/fuzz_jpegp.c" || return 1
    "$HCC" "${H[@]}" -fsanitize=fuzzer -o "$out/fuzz_jpegp" "$o"/new/*.o "$o/ref_renamed.o" "$o/host_alloc.o" "$o/fuzz.o"
  else
    "$HCC" "${H[@]}" -Wall -Wextra -Werror -DCDOM_IMAGE_TESTING -I"$LJT" -c -o "$o/prog.o" "$here/prog.c" || return 1
    "$HCC" "${H[@]}" -o "$out/prog" "$o"/new/*.o "$o/ref_renamed.o" "$o/host_alloc.o" "$o/prog.o"
  fi
}
if ! build > "$out.build.log" 2>&1; then echo "FAIL the progressive differential test does not build:"; tail -20 "$out.build.log"; exit 1; fi
python3 -I "$here/progcorpus.py" "$out/corpus" || exit 1
# The two builds define the same globals under two names; ASan's one-definition check reads the names from before the
# renaming, so it is off for this binary (every other check is on).
export ASAN_OPTIONS="detect_odr_violation=0${ASAN_OPTIONS:+:$ASAN_OPTIONS}"
if [ -n "$fuzz" ]; then
  f="$out/fuzz"; mkdir -p "$f/corpus" "$f/seeds"
  find "$out/corpus" -name '*.jpg' -size -100k -exec cp {} "$f/seeds/" \;
  (cd "$f" && "$out/fuzz_jpegp" corpus seeds -max_total_time="$fuzz" -timeout=30 -rss_limit_mb=2048 -max_len=200000 \
     -artifact_prefix="$f/crash-" > "$f/fuzz.log" 2>&1)
  tail -n 3 "$f/fuzz.log"
  if ls "$f"/crash-* > /dev/null 2>&1; then echo "FAIL fuzz_jpegp found: $(ls "$f"/crash-*)"; exit 1; fi
  echo "ok   fuzz_jpegp: $fuzz s, no difference, crash or leak ($(ls "$f/corpus" | wc -l) inputs in the corpus)"
  exit 0
fi
fail=0
for d in "$out/corpus" "$@"; do
  if "$out/prog" "$d" > "$out/prog.log" 2>&1; then grep '^ok ' "$out/prog.log"; echo "ok   progressive differential: $(tail -1 "$out/prog.log")"
  else fail=1; echo "FAIL progressive differential ($d):"; grep -v '^prog:' "$out/prog.log" | head -20; tail -1 "$out/prog.log"; fi
done
exit $fail
