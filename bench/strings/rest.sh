#!/bin/bash
# Step 10's remaining runs, serially: a second pass of swap (P5 asks for two), the micro, startup and memory.
set -u
here="$(cd "$(dirname "$0")" && pwd)"; top="$here/../.."
r="$here/results"; mkdir -p "$r"
PORT=8093 TAG=step10-swap2 FWS="st-a st-a2 st-s" "${PAIR:-/Users/alex/Dev/cdom-strings-bench/tools/pair.sh}" 05_
cd "$top"
bench/strings/micro-s.sh
node bench/strings/micro-s.mjs > "$r/micro-s-1.tsv" 2>&1
node bench/strings/micro-s.mjs > "$r/micro-s-2.tsv" 2>&1
P=bench/strings/out/pages
R=15 node bench/strings/startup.mjs ha=$P/hello-a/index.html ha2=$P/hello-a2/index.html hs=$P/hello-s/index.html \
  sa=$P/synth-a/index.html sa2=$P/synth-a2/index.html ss=$P/synth-s/index.html > "$r/startup-pages.tsv" 2>&1
R=15 CYCLES=5 node bench/strings/startup.mjs ka=keyed/st-a/index.html ka2=keyed/st-a2/index.html ks=keyed/st-s/index.html > "$r/startup-keyed.tsv" 2>&1
echo REST_DONE
