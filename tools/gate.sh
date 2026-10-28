#!/bin/bash
# Every correctness gate for a candidate entry: the harness's keyed check, the smoke test,
# DOM identity against the frozen baseline entry, the keyed-diff fuzz (all seeds), the unit page
# (tests/unit.c) and the link-level tests (tests/link).
# Usage: tools/gate.sh <entry> [baseline=cdomb]   (PORT defaults to 8193)
set -u
top="$(cd "$(dirname "$0")/.." && pwd)"
e="$1"; base="${2:-cdomb}"; export PORT="${PORT:-8193}"
fail=0
step() { local n="$1"; shift; if "$@" > "$top/results/gate_${e}_$n.log" 2>&1; then echo "gate $n: ok"; else echo "gate $n: FAIL (results/gate_${e}_$n.log)"; fail=1; fi; }
keyed() { (cd "$top/jfb/webdriver-ts" && LANG=en_US.UTF-8 node dist/isKeyed.js --headless --chromeBinary "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" --framework "keyed/$e") > "$top/results/gate_keyed_raw.log" 2>&1
  grep "appear as" "$top/results/gate_keyed_raw.log"; grep -q "It'll appear as keyed" "$top/results/gate_keyed_raw.log"; }
step keyed keyed
step smoke node "$top/tools/smoke.mjs" "$e"
step dom node "$top/tools/domcheck.mjs" "$base" "$e"
step fuzz bash -c "nice -n 10 '$top/tests/build.sh' && node '$top/tools/fuzz.mjs'"
step unit node "$top/tools/unit.mjs"
step link "$top/tests/link/run.sh"
exit $fail
