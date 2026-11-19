#!/bin/bash
# The bench's "old C" arm: the C arm's page (js) built from another commit (default origin/main, main's current tip:
# git fetch origin main first), in a scratch worktree, copied to bench/image/build-old/ (run.mjs's arm C-old). PR #56's
# numbers (docs/image-png-speed.md, step 5) were taken with 2d20ccd, main before png-fast and image-simd: pass it to
# build that one again.
# Usage: bench/image/old.sh [commit=origin/main] [worktree dir=a new temporary one]
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
top="$(cd "$here/../.." && pwd)"
rev="${1:-origin/main}"; wt="${2:-$(mktemp -d)/cdom-old}"
git -C "$top" worktree add --detach "$wt" "$rev" > /dev/null
make -C "$wt/bench/image" SJLJ=js page > /dev/null
rm -rf "$here/build-old"; cp -r "$wt/bench/image/build-js" "$here/build-old"
git -C "$top" worktree remove --force "$wt"
echo "build-old: $(git -C "$top" rev-parse --short "$rev"), $(wc -c < "$here/build-old/app.wasm") bytes of wasm"
