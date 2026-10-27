#!/bin/bash
# Final runs: each (benchmark, framework) waits for the 1-minute load to drop under 4
# (bounded at 20 minutes, flagged if it never does) and logs the load before and after.
set -u
top="$(cd "$(dirname "$0")/.." && pwd)"
log="$top/results/loads.tsv"
fws=(${FWS:-vanillajs react-hooks cdom cdom-pertext cdom-notpl wasm-bindgen dioxus})
benches=("${@:-01_ 02_ 03_ 04_ 05_ 06_ 07_ 08_ 09_ 21_ 22_ 25_ 40_}")
load1() { sysctl -n vm.loadavg | awk '{print $2}'; }
[ -f "$log" ] || printf 'benchmark\tframework\tload_before\tload_after\twaited_s\tgate\n' > "$log"
cd "$top/jfb/webdriver-ts"
for b in ${benches[@]}; do
  for f in "${fws[@]}"; do
    w=0; gate=ok
    until awk -v l="$(load1)" 'BEGIN{exit !(l < 4)}'; do
      if [ $w -ge 1200 ]; then gate=TIMEOUT; break; fi
      sleep 10; w=$((w+10))
    done
    before=$(load1)
    PORT=8093 node dist/benchmarkRunner.js --headless --chromeBinary "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" \
      --framework "keyed/$f" --benchmark "$b" ${EXTRA:-} > "$top/results/raw_${b}${f}${TAG:-}.log" 2>&1
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$b" "$f" "$before" "$(load1)" "$w" "$gate" "${TAG:-}" >> "$log"
    if [ -n "${KEEP:-}" ]; then mkdir -p "$top/results/$KEEP"; cp results/*.json "$top/results/$KEEP/" 2>/dev/null; fi
  done
done
echo FINAL_DONE
