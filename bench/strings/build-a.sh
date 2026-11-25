#!/bin/bash
# The "before" entries: tutorial's own bench/build.sh (e35056c, /Users/alex/Dev/cdom) into a scratch
# directory, then copied into the harness as st-a and st-a2 (cdom) and st-a-n and st-a2-n (cdom-notpl).
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
before="${BEFORE:-/Users/alex/Dev/cdom}"
keyed="${KEYED:-/Users/alex/Dev/cdom/jfb/frameworks/keyed}"
scratch="$here/out/a-tutorial"
mkdir -p "$scratch"
(cd "$before" && JFB_KEYED="$scratch" bench/build.sh)
copy() { # from to
    local d="${keyed:?}/${2:?}"
    rm -rf "$d"
    cp -R "$scratch/$1" "$d"
    sed -i '' "s/js-framework-benchmark-$1\"/js-framework-benchmark-$2\"/" "$d/package.json" "$d/package-lock.json"
    sed -i '' "s/<title>$1-/<title>$2-/" "$d/index.html"
}
copy cdom st-a; copy cdom st-a2; copy cdom-notpl st-a-n; copy cdom-notpl st-a2-n
