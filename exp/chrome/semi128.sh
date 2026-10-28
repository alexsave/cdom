#!/bin/bash
# Flag arm: young generation fixed at 128 MB semi-spaces
ARM=semi128 X="--min-semi-space-size=128 --max-semi-space-size=128" exec "$(dirname "$0")/wrap.sh" "$@"
