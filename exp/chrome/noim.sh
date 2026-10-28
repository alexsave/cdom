#!/bin/bash
# Flag arm: no incremental marking (major GC marks atomically)
ARM=noim X="--no-incremental-marking" exec "$(dirname "$0")/wrap.sh" "$@"
