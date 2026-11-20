#!/bin/bash
# A clang with the wasm-ld of LLVM $STUB_LLD (tests/link/run.sh, the toolchain check), over the real clang,
# REAL_CC. Below 23 it is what LLD 18 and LLD 22.1.8 do: a link whose code refers to __rodata_start fails
# with the symbol undefined (the real linker, asked to trace the symbol, says whether any input refers to
# it; a declaration nothing uses is no reference). From 23 every call goes to REAL_CC unchanged.
case "$*" in
  --version) echo "clang version $STUB_LLD"; exit 0 ;;
  *-print-prog-name=wasm-ld*) echo "$(dirname "$0")/stub_wasm_ld.sh"; exit 0 ;;
esac
[ "${STUB_LLD%%.*}" -ge 23 ] && exec "$REAL_CC" "$@"
case " $* " in *" -c "*) exec "$REAL_CC" "$@" ;; esac
tmp="$(mktemp -d)" || exit 1
trap 'rm -r "$tmp"' EXIT
# The same link into tmp, the probe on stdin kept in a file for the real one after it.
args=() trace=() next=""
for a in "$@"; do
  if [ "$a" = - ]; then cat > "$tmp/stdin.c"; a="$tmp/stdin.c"; fi
  args+=("$a")
  if [ "$next" = o ]; then trace+=("$tmp/out.wasm"); else trace+=("$a"); fi
  if [ "$a" = -o ]; then next=o; else next=""; fi
done
if "$REAL_CC" "${trace[@]}" -Wl,--trace-symbol=__rodata_start 2> /dev/null | grep -q "reference to __rodata_start"; then
  echo "wasm-ld: error: undefined symbol: __rodata_start" >&2
  echo "clang: error: linker command failed with exit code 1 (use -v to see invocation)" >&2; exit 1
fi
"$REAL_CC" "${args[@]}"
