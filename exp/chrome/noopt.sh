#!/bin/bash
# Flag arm: no optimizing tiers: no TurboFan, no Maglev (code stays in Ignition/Sparkplug)
ARM=noopt X="--no-opt --no-maglev" exec "$(dirname "$0")/wrap.sh" "$@"
