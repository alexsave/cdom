#!/bin/bash
# A clang whose wasm-ld is older than LLVM 23 (tests/link/run.sh, the toolchain check): it says it is
# clang 18 with LLD 18, and any link of a source that names __rodata_start fails the way wasm-ld 18 to 22
# fails there; everything else goes to the real clang, REAL_CC. The probe comes in on stdin, an app's
# sources as files, so the stub reads both.
case "$*" in
  --version) echo "clang version 18.1.3"; exit 0 ;;
  *-print-prog-name=wasm-ld*) echo "$(dirname "$0")/old_wasm_ld.sh"; exit 0 ;;
esac
src="" stdin=0
for a in "$@"; do
  case "$a" in -) src="$src$(cat)"; stdin=1;; *.c) src="$src$(cat "$a")";; esac
done
case " $* " in *" -c "*) ;; *)
  if [[ "$src" == *__rodata_start* ]]; then
    echo "wasm-ld: error: undefined symbol: __rodata_start" >&2
    echo "clang: error: linker command failed with exit code 1 (use -v to see invocation)" >&2; exit 1
  fi ;;
esac
if [ $stdin = 1 ]; then printf '%s\n' "$src" | exec "$REAL_CC" "$@"; fi
exec "$REAL_CC" "$@"
