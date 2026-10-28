#!/bin/bash
# Flag arm: ignore eager compilation hints (Chrome 156 has no --always-turbofan)
ARM=maxlazy X="--max-lazy" exec "$(dirname "$0")/wrap.sh" "$@"
