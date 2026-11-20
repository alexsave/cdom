#!/bin/bash
# vmalerts.sh <count>: taps the OK of a Netscape 2 alert (299,170) <count> times, 1 s apart, to let a page that
# raises an error alert per failed block run to its end.
cd /Users/alex/VMs/ie6-xp || exit 1
for i in $(seq 1 "$1"); do python3 vm.py tap 299 170; sleep 1; done
