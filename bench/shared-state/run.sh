#!/bin/bash
# The shared-state study (docs/shared-state-design.md): every run, one CPU job at a time, results under
# bench/shared-state/results/. How to run:
#   bench/shared-state/run.sh                 everything: the cd_state prototype's checks and their mutants,
#                                             the four badges, the benchmark's DOM check (and its mutants) and
#                                             crossings, 5 timed passes with the memory runs, the summary, S14
#   bench/shared-state/run.sh bench           only the benchmark: build, DOM check, crossings, 5 passes, summary
#   bench/shared-state/run.sh one SC ENTRY    one scenario for one entry, one pass, into results/one/ (the
#                                             committed TSVs are not touched); SC is a key of bench/page.html's
#                                             SC (S1, S3_100k, S9a1, ...) or S7mem / S10mem for the memory runs
#   bench/shared-state/run.sh caret           S14 only, printed and written to results/caret.txt
#   bench/shared-state/run.sh checks          everything untimed: the prototype's checks and mutants, the
#                                             badges, the DOM check and crossings with its mutants, S14
# The timed runs append to results/times.tsv and results/memory.tsv: delete them first for a fresh run.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
what="${1:-all}"
build_bench() { "$here/bench/build.sh" > /dev/null; }
case "$what" in
  one)
    build_bench
    rm -rf "$here/results/one"
    RES="$here/results/one" ONLY="$2" ENTRIES="$3" PASSES=1 node "$here/bench/run.mjs"
    [ -f "$here/results/one/times.tsv" ] && RES="$here/results/one" node "$here/bench/summarize.mjs" > /dev/null
    exit 0;;
  caret)
    "$here/caret/build.sh"
    node "$here/caret/caret.mjs" | tee "$here/results/caret.txt"
    exit 0;;
esac
if [ "$what" = checks ]; then
  "$here/test/build.sh"
  node "$here/test/test.mjs"
  "$here/test/mutate.sh"
  for d in a-rerun b-module c-version e-state; do make -s -C "$here/badge/$d" > /dev/null; done
  node "$here/badge/drive.mjs" "$here/badge"
  build_bench
  node "$here/bench/check.mjs"
  "$here/bench/mutate-dom.sh"
  "$here/caret/build.sh"
  node "$here/caret/caret.mjs" | tee "$here/results/caret.txt"
  exit 0
fi
if [ "$what" = all ]; then
  "$here/test/build.sh"
  node "$here/test/test.mjs"
  "$here/test/mutate.sh"
  for d in a-rerun b-module c-version e-state; do make -s -C "$here/badge/$d" > /dev/null; done
  node "$here/badge/drive.mjs" "$here/badge"
fi
build_bench
node "$here/bench/check.mjs"
[ "$what" = all ] && "$here/bench/mutate-dom.sh"
PASSES=5 node "$here/bench/run.mjs"
node "$here/bench/summarize.mjs"
if [ "$what" = all ]; then
  "$here/caret/build.sh"
  node "$here/caret/caret.mjs" | tee "$here/results/caret.txt"
fi
