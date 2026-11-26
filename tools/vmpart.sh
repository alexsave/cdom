#!/bin/bash
# vmpart.sh <browser exe line> <tag> <page> <max s>: opens http://10.0.2.2:8102/<page>?<tag> from Run, brings its window
# forward from the taskbar, then taps the "Yes" of IE 4's script error dialog (289,344) every 2 s until the page's done
# mark reaches the server log.  (IE 4's dialog does not take the Enter key reliably, and opens behind its window.)
exe="$1"; tag="$2"; page="$3"; max=$(( $4 / 2 ))
/Users/alex/Dev/cdom-probe/tools/vmrun.sh "$exe http://10.0.2.2:8102/$page?$tag"
cd /Users/alex/VMs/ie6-xp || exit 1
sleep 10; python3 vm.py tap 340 465
n=0
until grep -q "/mark/$tag/p-[A-Za-z0-9]*-done-end" serve-probe.log || [ $n -ge $max ]; do
    sleep 2; python3 vm.py tap 289 344; n=$((n+1))
done
sleep 3
grep -c "/mark/$tag/" serve-probe.log
