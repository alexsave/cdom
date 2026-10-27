#!/bin/bash
# The server render is deterministic and is the DOM the client builds:
#  1. `server --render n` twice gives identical bytes (n = 0, 1000, 10000);
#  2. GET /?n=1000 from two separate server starts gives those same bytes;
#  3. tests/ssr_match.mjs: the client-rendered page, seeded alike, serialises to them.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
top="$here/.."
out="$here/out/ssr"; mkdir -p "$out"
"$top/ssr/rows/build.sh"
srv="$top/ssr/rows/out/server"
for n in 0 1000 10000; do
  "$srv" --render $n > "$out/a$n.html"
  "$srv" --render $n > "$out/b$n.html"
  cmp "$out/a$n.html" "$out/b$n.html"
  echo "ok   --render $n twice: identical ($(wc -c < "$out/a$n.html" | tr -d ' ') bytes)"
done
pid=""
cleanup() { if [ -n "$pid" ]; then kill "$pid" 2>/dev/null || true; fi; }
trap cleanup EXIT
for start in 1 2; do
  "$srv" --port 8701 2>/dev/null & pid=$!
  n=0; until curl -sf -o /dev/null "http://127.0.0.1:8701/?n=0" || [ $n -gt 50 ]; do sleep 0.1; n=$((n+1)); done
  curl -sf "http://127.0.0.1:8701/?n=1000" > "$out/http$start.html"
  cmp "$out/http$start.html" "$out/a1000.html"
  echo "ok   server start $start: GET /?n=1000 equals --render 1000"
  if [ $start = 2 ]; then node "$here/ssr_match.mjs"; fi
  kill "$pid"; wait "$pid" 2>/dev/null || true; pid=""
done
echo "ssr_identical: all ok"
