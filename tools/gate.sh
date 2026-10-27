#!/bin/bash
# Every correctness gate for a candidate entry: the harness's keyed check, the smoke test,
# DOM identity against the frozen baseline entry, and the keyed-diff fuzz (all seeds).
# Usage: tools/gate.sh <entry> [baseline=cdomb]   (PORT defaults to 8193)
set -u
top="$(cd "$(dirname "$0")/.." && pwd)"
e="$1"; base="${2:-cdomb}"; export PORT="${PORT:-8193}"
fail=0
step() { local n="$1"; shift; if "$@" > "$top/results/gate_$n.log" 2>&1; then echo "gate $n: ok"; else echo "gate $n: FAIL (results/gate_$n.log)"; fail=1; fi; }
keyed() { (cd "$top/jfb/webdriver-ts" && LANG=en_US.UTF-8 node dist/isKeyed.js --headless --chromeBinary "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" --framework "keyed/$e") > "$top/results/gate_keyed_raw.log" 2>&1
  grep "appear as" "$top/results/gate_keyed_raw.log"; grep -q "It'll appear as keyed" "$top/results/gate_keyed_raw.log"; }
step keyed keyed
step smoke node "$top/tools/smoke.mjs" "$e"
step dom node "$top/tools/domcheck.mjs" "$base" "$e"
step fuzz bash -c "nice -n 10 '$top/tests/build.sh' && node '$top/tools/fuzz.mjs'"
exit $fail
