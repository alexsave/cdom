#!/bin/bash
# vmjs.sh <javascript expression> <shot name>: in the open Netscape 2 window, replaces the Location field with
# javascript:alert(<expression>) and loads it, then saves the screen (the alert shows the value) as shots/<name>.png.
cd /Users/alex/VMs/ie6-xp || exit 1
python3 vm.py tap 300 110; sleep 1; python3 vm.py key end shift-home; python3 vm.py key delete
python3 vm.py keys "javascript:alert($1)"; python3 vm.py key ret; sleep 8
python3 vm.py shot "shots/$2.png" >/dev/null
