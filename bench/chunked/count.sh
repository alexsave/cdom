#!/bin/bash
# Instructions per decode (valgrind's cachegrind, no cache simulation): a count the VM's timing noise does not move.
# Usage: bench/chunked/count.sh FILE...      (after bench/chunked/run.sh built bench/chunked/build/proto)
here="$(cd "$(dirname "$0")" && pwd)"
for f in "$@"; do for arm in "whole 0" "pull 65536" "push 65536" "pull 262144" "push 262144" "push 1048576"; do
  set -- $arm
  n=$(valgrind --tool=cachegrind --cache-sim=no --cachegrind-out-file=/dev/null "$here/build/proto" once "$f" $1 $2 2>&1 | sed -n 's/.*I *refs: *//p' | tr -d ,)
  printf '%s\t%s\t%s\t%s\n' "$(basename "$f")" "$1" "$2" "$n"
done; done
