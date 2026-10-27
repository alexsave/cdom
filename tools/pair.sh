#!/bin/bash
# Paired run: the listed frameworks run benchmark by benchmark, interleaved, each start
# gated on the 1-minute load being under 4 (bounded wait), results kept in results/pairs/$TAG.
# Usage: TAG=x FWS="vanillajs cdomb cdomp" [COUNT=15] [PORT=8193] tools/pair.sh 01_ 02_ ...
set -u
top="$(cd "$(dirname "$0")/.." && pwd)"
out="$top/results/pairs/${TAG:?TAG}"
mkdir -p "$out"
log="$out/loads.tsv"
fws=(${FWS:-vanillajs cdomp})
benches=("$@")
load1() { sysctl -n vm.loadavg | awk '{print $2}'; }
[ -f "$log" ] || printf 'benchmark\tframework\tload_before\tload_after\twaited_s\tgate\n' > "$log"
cd "$out"
for b in "${benches[@]}"; do
  for f in "${fws[@]}"; do
    w=0; gate=ok
    until awk -v l="$(load1)" 'BEGIN{exit !(l < 4)}'; do
      if [ $w -ge 1200 ]; then gate=TIMEOUT; break; fi
      sleep 10; w=$((w+10))
    done
    before=$(load1)
    PORT=${PORT:-8193} nice -n 0 node "$top/jfb/webdriver-ts/dist/benchmarkRunner.js" --headless \
      --chromeBinary "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" \
      --framework "keyed/$f" --benchmark "$b" ${COUNT:+--count $COUNT} > "$out/raw_${b}${f}.log" 2>&1
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$b" "$f" "$before" "$(load1)" "$w" "$gate" >> "$log"
  done
done
echo PAIR_DONE
