#!/bin/bash
# vmfbrun.sh <page> <wait s> <hang s> [expect]: one framebuffer probe (tools/vmfb.py), then the screen check (tools/fbcheck.py) of
# its shot against the picture the page was made from; both lines are appended to results/ns2fb_runs.txt and printed.
here=$(cd "$(dirname "$0")" && pwd)
page="$1"; wait="$2"; hang="$3"
out=$(python3 "$here/vmfb.py" "$page.html" --wait "$wait" --hang "$hang" --shot "ns2fb_$page" 2>&1 | tail -2)
chk=$(python3 "$here/fbcheck.py" "/Users/alex/VMs/ie6-xp/shots/ns2fb_$page.png" --expect "$page" 2>&1)
{ echo "== $page $(date +%H:%M:%S)"; echo "$out" | sed 's/ shot=.*//'; echo "$chk"; } | tee -a "$here/../results/ns2fb_runs.txt"
