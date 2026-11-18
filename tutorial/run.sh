#!/bin/bash
# Builds every lesson (or the ones named) with its own Makefile, then runs the end-to-end test on them.
# Usage: tutorial/run.sh [lesson-dir ...]   SHOTS=dir also writes a screenshot of each lesson.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
if [ $# -eq 0 ]; then
  set -- "$here"/[0-9][0-9]-*/
fi
for d in "$@"; do
  make -s -C "$d"
done
node "$here/test.mjs" "$@"
