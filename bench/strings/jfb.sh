#!/bin/bash
# strings step 10, the whole-app runs: tools/pair.sh over A, A2 (tutorial, bench/strings/build-a.sh) and
# the strings build (bench/strings/build-st.sh), against the jfb server on 8093. PAIR names a checkout
# whose jfb/ has the harness (webdriver-ts); results land in that checkout's results/pairs/<tag>.
set -u
pair="${PAIR:-/Users/alex/Dev/cdom-strings-bench/tools/pair.sh}"
export PORT=8093
TAG=step10-cpu FWS="st-a st-a2 st-s st-s-na" "$pair" 01_ 02_ 03_ 04_ 05_ 06_ 07_ 08_ 09_
TAG=step10-notpl FWS="st-a-n st-a2-n st-s-n" "$pair" 01_ 07_
TAG=step10-mem1 FWS="st-a st-a2 st-s" "$pair" 21_ 22_ 25_
TAG=step10-mem2 FWS="st-a st-a2 st-s" "$pair" 21_ 22_ 25_
