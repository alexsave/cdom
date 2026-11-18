#!/bin/bash
# cdom builds on Linux out of the box: in an Ubuntu 24.04 container (Docker) with only the distribution's
# make, gcc, clang and lld, and cdom.mk's defaults (HOST_CC cc = gcc 13, WASM_CC clang = clang 18),
# tutorial lesson 1 builds its page, and both host tools also build with gcc and clang under Alpine 3.20
# (gcc 13, clang 17), the oldest compilers that take -std=c2x and refuse -std=c23.
# Usage: tools/linux.sh   (needs docker; reads the tree, writes nothing in it)
set -u
top="$(cd "$(dirname "$0")/.." && pwd)"
bad=0
ubuntu='apt-get update -qq >/dev/null && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq make gcc clang lld >/dev/null 2>&1
cp -r /src /w && rm -rf /w/tools/out /w/tutorial/01-hello/build
make -s -C /w/tutorial/01-hello > /tmp/log 2>&1 && [ -s /w/tutorial/01-hello/build/index.html ] || { cat /tmp/log; exit 1; }'
if docker run --rm -v "$top:/src:ro" ubuntu:24.04 bash -c "$ubuntu" > /tmp/cdom-linux.$$ 2>&1; then
  echo "ok   ubuntu 24.04: lesson 1 builds with cdom.mk's defaults"
else
  echo "FAIL ubuntu 24.04: lesson 1 does not build:"; head -5 /tmp/cdom-linux.$$; bad=1
fi
rm -f /tmp/cdom-linux.$$
alpine='apk add -q gcc clang musl-dev >/dev/null 2>&1; r=0
for cc in gcc clang; do for t in cdlits cdpage; do
  $cc -std=c2x -O2 -Wall -Wextra -Werror -o /tmp/$t /src/tools/$t.c 2>/tmp/err || { echo "$cc $t: $(head -1 /tmp/err)"; r=1; }
done; done; exit $r'
if out=$(docker run --rm -v "$top:/src:ro" alpine:3.20 sh -c "$alpine" 2>&1); then
  echo "ok   alpine 3.20: cdlits and cdpage build with gcc 13 and clang 17"
else
  echo "FAIL alpine 3.20: $out"; bad=1
fi
exit $bad
