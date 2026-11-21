#!/bin/bash
# vmprobe.sh <ns2|ns3|ns4|ie4> <mark tag> <page and query> <wait s> [shot name]: ends every browser in the guest, opens
# http://10.0.2.2:8110/<page and query> in the browser, waits <wait> s, then prints the marks of <mark tag> in the server's log
# (/Users/alex/VMs/ie6-xp/serve-frames-real.log), then prints the marks (unescaped) and, with a shot name, saves the
# screen as shots/<name>.png.  It does not close the browser: the caller looks, then runs the next probe (which ends it).
# VMDIR=<dir> serves that directory instead of tests/out (the limits probes: VMDIR=tests).
b="$1"; tag="$2"; page="$3"; wait="$4"; shot="$5"
log=/Users/alex/VMs/ie6-xp/serve-frames-real.log
here=$(cd "$(dirname "$0")" && pwd)
"$here/vmopen.sh" kill
# A fresh server for each run: Netscape 3.04 (killed mid-load by the run before) leaves the guest's connections in a state where
# the next run's requests are not answered ("Waiting for reply" for ever), and a new server clears it.
pkill -f vmserve.py; sleep 1
(cd /Users/alex/VMs/ie6-xp && nohup python3 "$here/vmserve.py" "${VMDIR:-$here/../tests/out}" 8110 >> "$log" 2>&1 &)
sleep 1
before=$(grep -c "/mark/$tag" "$log")
"$here/vmopen.sh" "$b" "http://10.0.2.2:8110/r$(date +%s)/$page"
n=0
until [ $n -ge $(( wait / 2 )) ]; do sleep 2; n=$((n+1)); done
sleep 4
[ -n "$shot" ] && (cd /Users/alex/VMs/ie6-xp && python3 vm.py shot "shots/$shot.png" >/dev/null)
grep "/mark/$tag" "$log" | sed -n "$((before+1)),\$p" | sed 's/.*GET \/mark\///; s/ HTTP.*//' | python3 -c "
import sys, urllib.parse
for l in sys.stdin: print(urllib.parse.unquote(l.rstrip()))"
