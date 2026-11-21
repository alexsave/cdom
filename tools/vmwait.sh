#!/bin/bash
# vmwait.sh <log> <tag> <seconds>: waits (a capped loop, 2 s steps) until the server log has the "done" mark of <tag>,
# then prints the log's time of the page GET and of the done mark, for the wall time of a page.
log="$1"; tag="$2"; max=$(( $3 / 2 )); n=0
until grep -q "/mark/$tag/p-[A-Za-z0-9]*-done-end" "$log" || [ $n -ge $max ]; do sleep 2; n=$((n+1)); done
grep "?$tag HTTP" "$log" | tail -1 | cut -c1-70
grep "/mark/$tag/p-[A-Za-z0-9]*-done-end" "$log" | tail -1 | cut -c1-70
