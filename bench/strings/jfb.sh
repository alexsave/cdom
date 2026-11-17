#!/bin/bash
# The whole-app runs: tools/pair.sh over the strings-bench entries (bench/strings/build.sh), interleaved,
# against the jfb server on 8093. Results: results/pairs/strings-<tag>/.
set -u
top="$(cd "$(dirname "$0")/../.." && pwd)"
export PORT=8093
TAG=strings-cpu FWS="sb-a sb-a2 sb-b sb-bx sb-at" "$top/tools/pair.sh" 01_ 02_ 03_ 04_ 05_ 06_ 07_ 08_ 09_
TAG=strings-notpl FWS="sb-a-n sb-a2-n sb-b-n" "$top/tools/pair.sh" 01_ 07_
TAG=strings-mem FWS="sb-a sb-a2 sb-b sb-bx" "$top/tools/pair.sh" 21_ 22_ 25_
