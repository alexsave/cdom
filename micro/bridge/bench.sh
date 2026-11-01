#!/bin/bash
# The whole bridge measurement, one step per command (every step well under 15 minutes; headless
# Chrome only, one at a time, killed by its own PID inside run.mjs). `bench.sh plan` prints the
# steps in the order they are run; each round of a page workload is one command with every arm
# loaded once, fresh, in an order that rotates with the round (so no arm always goes first).
#   micro/bridge/bench.sh plan
#   micro/bridge/bench.sh build                       build.sh all
#   micro/bridge/bench.sh same                        every arm's DOM and reads equal to arm a's (exit 1 if not)
#   micro/bridge/bench.sh count                       crossings, strings and DOM hashes per flow, per arm
#   micro/bridge/bench.sh port <cpu> <round>          the port's flows, one load per arm
#   micro/bridge/bench.sh keyed <cpu> <round>         the js-framework-benchmark operations, one load per arm
#   micro/bridge/bench.sh micro <round>               ns per op per arm
#   micro/bridge/bench.sh fastapi <round>             the micro, arms a and f, with --js-flags=--wasm-fast-api
#   micro/bridge/bench.sh firefox <rounds>            the micro in headless Firefox
#   micro/bridge/bench.sh tables                      results/*.jsonl -> results/tables.md
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
PORT_ROUNDS_X1=7 PORT_ROUNDS_X4=5 KEYED_ROUNDS=7 MICRO_ROUNDS=5 FAST_ROUNDS=5
step="${1:-plan}"
case "$step" in
  plan)
    echo "$0 build"
    echo "$0 same"
    echo "$0 count"
    for r in $(seq 0 $((PORT_ROUNDS_X1 - 1))); do echo "$0 port 1 $r"; echo "$0 keyed 1 $r"; done
    for r in $(seq 0 $((PORT_ROUNDS_X4 - 1))); do echo "$0 port 4 $r"; done
    for r in $(seq 0 $((MICRO_ROUNDS - 1))); do echo "$0 micro $r"; done
    for r in $(seq 0 $((FAST_ROUNDS - 1))); do echo "$0 fastapi $r"; done
    echo "$0 firefox 3"
    echo "$0 tables"
    ;;
  build) "$here/build.sh" all ;;
  same) node "$here/run.mjs" same ;;
  count) node "$here/run.mjs" count ;;
  port) ROUND="$3" node "$here/run.mjs" port 1 "$2" ;;
  keyed) ROUND="$3" node "$here/run.mjs" keyed 1 "$2" ;;
  micro) ROUND="$2" node "$here/run.mjs" micro 1 1 ;;
  fastapi) ROUND="$2" TAG=fastapi CHROME_FLAGS="--js-flags=--wasm-fast-api" node "$here/run.mjs" micro 1 1 a,f ;;
  firefox) node "$here/ffmicro.mjs" "${2:-3}" ;;
  tables)
    out="$here/results/tables.md"
    : > "$out"
    for f in "$here"/results/{count_x1,port_x1,port_x4,keyed_x1,micro_x1,micro_x1_fastapi,micro_firefox}.jsonl; do
      [ -f "$f" ] || continue
      { echo; echo "### $(basename "$f" .jsonl)"; node "$here/summary.mjs" "$f" script total style layout paint; } >> "$out"
    done
    echo "wrote $out"
    ;;
  *) sed -n '2,16p' "$0"; exit 2 ;;
esac
