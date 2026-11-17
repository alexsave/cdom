#!/bin/bash
# The macOS sample command on the Chrome renderer while jsread.html spins on one reader.
# Usage: bench/strings/sample.sh 'read=td&len=9'
set -u
here="$(cd "$(dirname "$0")" && pwd)"
out="$here/results/sample-$(echo "$1" | tr '&=' '__').txt"
node "$here/prof-spin.mjs" "$1" &
drv=$!
sleep 4
pid=$(ps -axo pid,pcpu,command | grep 'Renderer' | grep 'Google Chrome' | sort -k2 -nr | head -1 | awk '{print $1}')
/usr/bin/sample "$pid" 3 -file "$out" > /dev/null 2>&1
wait $drv
echo "$out"
