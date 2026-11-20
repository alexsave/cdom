#!/bin/bash
# vmrun.sh <command line>: Start, Run in the XP guest (ie6-xp), types the line and presses Enter.
cd /Users/alex/VMs/ie6-xp || exit 1
python3 vm.py key meta_l-r; sleep 5
python3 vm.py keys "$1"; python3 vm.py key ret
