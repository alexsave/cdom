#!/bin/bash
# CDOM_RESUME (cdom.h): the hand-written and the templated rows apps built with resuming (the
# default) and without. The default exports the resume entry points; CDOM_RESUME=0 exports none
# of them, keeps every other export, and is smaller by the virtual render, the resolver and the
# matcher. Both natively compile too (a server may link either). Smoke and listfuzz on both builds
# are run by hand (PLATFORM.md); this pins what the flag removes. Usage: tests/resume_flag.sh
set -euo pipefail
top="$(cd "$(dirname "$0")/.." && pwd)"
out="$top/tests/out/resume_flag"; mkdir -p "$out"
CC="${WASM_CC:-/opt/homebrew/opt/llvm/bin/clang}"
F=(--target=wasm32 -Oz -nostdlib -mbulk-memory -Wall -Wextra -Werror -Wl,--no-entry -Wl,--strip-all)
nice -n 10 cc -O2 -Wall -Wextra -Werror -o "$out/cdtc" "$top/tools/cdtc.c"
"$out/cdtc" "$top/bench/cdom-tpl/rows.cdt" -o "$out/rows"
fails=0
ok() { if [ "$1" = 0 ]; then echo "ok   $2"; else echo "FAIL $2"; fails=$((fails+1)); fi; }
exports() { node -e 'console.log(WebAssembly.Module.exports(new WebAssembly.Module(require("fs").readFileSync(process.argv[1]))).map(e => e.name).sort().join(" "))' "$1"; }
for r in 1 0; do # 1: the default, no flag given (so the header's default is what is tested)
  D=(); [ $r = 0 ] && D=(-DCDOM_RESUME=0)
  set +e
  nice -n 10 "$CC" "${F[@]}" ${D[@]+"${D[@]}"} -o "$out/hand$r.wasm" "$top/bench/cdom-keyed/src/app.c" "$top/src/cdom.c" "$top/src/mini.c"
  ok $? "CDOM_RESUME=$r: the hand-written app builds for wasm"
  nice -n 10 "$CC" "${F[@]}" ${D[@]+"${D[@]}"} -I "$top/src" -I "$out" -o "$out/tpl$r.wasm" "$top/bench/cdom-tpl/app.c" "$out/rows.c" "$top/src/cdom.c" "$top/src/mini.c"
  ok $? "CDOM_RESUME=$r: the templated app builds for wasm"
  nice -n 10 cc -O2 -Wall -Wextra -Werror ${D[@]+"${D[@]}"} -c -o "$out/native$r.o" "$top/src/cdom.c"
  ok $? "CDOM_RESUME=$r: cdom.c builds natively"
  set -e
done
RES="cd_resume_stats cd_start_state cd_state_buf"
for a in hand tpl; do
  e1="$(exports "$out/${a}1.wasm")"; e0="$(exports "$out/${a}0.wasm")"
  has=0; for x in $RES; do case " $e1 " in *" $x "*) ;; *) has=1;; esac; done
  ok $has "$a: the default build exports $RES ($e1)"
  none=0; for x in $RES; do case " $e0 " in *" $x "*) none=1;; esac; done
  ok $none "$a: CDOM_RESUME=0 exports none of them ($e0)"
  rest1="$(echo $e1 | tr ' ' '\n' | grep -vxF -e cd_resume_stats -e cd_start_state -e cd_state_buf | tr '\n' ' ')"
  [ "$rest1" = "$(echo $e0 | tr ' ' '\n' | tr '\n' ' ')" ]; ok $? "$a: and keeps every other export ($rest1)"
  s1=$(wc -c < "$out/${a}1.wasm"); s0=$(wc -c < "$out/${a}0.wasm")
  [ $((s1 - s0)) -gt 5000 ]; ok $? "$a: CDOM_RESUME=0 is $((s1 - s0)) bytes smaller ($s1 -> $s0)"
done
echo "$([ $fails = 0 ] && echo 'resume_flag: all ok' || echo "resume_flag: $fails FAILED")"
exit $((fails != 0))
