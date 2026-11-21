#!/bin/bash
# vmns2.sh <browser ns2|ns3|ns4|ie4> <page and query> <wait s> <shot name>: through the guest agent (tools/vmagent.py, running in the
# guest as C:\a.exe), ends the browsers, opens http://10.0.2.2:8110/<page> in the browser (tests/out or $VMDIR served by
# tools/vmserve.py, which this restarts each run), waits, saves shots/<name>.png.  No typing into the guest.
b="$1"; page="$2"; wait="$3"; shot="$4"
here=$(cd "$(dirname "$0")" && pwd)
case "$b" in
  ns2) exe='C:\NS202\Program\netscape.exe' ;; ns3) exe='C:\NS304\Program\netscape.exe' ;; ns4) exe='C:\NS408\Program\netscape.exe' ;;
  ie4) exe='C:\Program Files\Utilu IE Collection\IE401\iexplore.exe' ;;
esac
for p in netscape.exe iexplore.exe notepad.exe mshta.exe; do python3 "$here/vmagent.py" send "kill $p" >/dev/null; done
pkill -f vmserve.py; sleep 1
# a fresh server for each run (see vmprobe.sh)
(cd /Users/alex/VMs/ie6-xp && nohup python3 "$here/vmserve.py" "${VMDIR:-$here/../tests/out}" 8110 >> serve-frames-real.log 2>&1 &)
sleep 1
python3 "$here/vmagent.py" send "run \"$exe\" http://10.0.2.2:8110/r$(date +%s)/$page" >/dev/null
sleep "$wait"
python3 /Users/alex/VMs/ie6-xp/vm.py shot "/Users/alex/VMs/ie6-xp/shots/$shot.png" >/dev/null
