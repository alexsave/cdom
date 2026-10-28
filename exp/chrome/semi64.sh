#!/bin/bash
# Flag arm: young generation fixed at 64 MB semi-spaces
ARM=semi64 X="--min-semi-space-size=64 --max-semi-space-size=64" exec "$(dirname "$0")/wrap.sh" "$@"
