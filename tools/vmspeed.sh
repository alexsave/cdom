#!/bin/bash
# vmspeed.sh <browser exe line> <prefix> <wait s> <names...>: runs the speed pages probe-speed-<name>.html one after
# another in the guest, each with the tag <prefix><name>, waiting for the done mark; prints the wall time of each.
exe="$1"; prefix="$2"; wait="$3"; shift 3
for t in "$@"; do
    /Users/alex/Dev/cdom-probe/tools/vmrun.sh "$exe http://10.0.2.2:8102/probe-speed-$t.html?$prefix$t"
    /Users/alex/Dev/cdom-probe/tools/vmwait.sh /Users/alex/VMs/ie6-xp/serve-probe.log "$prefix$t" "$wait" | cut -c27-35 | paste -sd' ' -
done
