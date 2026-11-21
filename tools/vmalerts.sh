#!/bin/bash
# vmalerts.sh <max> [name]: reads the Netscape 2 alerts that are up, one at a time: while the screen shows an alert (the
# grey box at 12,100), saves its text as shots/<name>-<k>.png (a crop of the top 130 lines) and taps its OK (299,170),
# at most <max> times; never taps when there is no alert (a blind tap can open a desktop icon).  Prints how many it read.
cd /Users/alex/VMs/ie6-xp || exit 1
max="$1"; name="${2:-alert}"; k=0
while [ $k -lt "$max" ]; do
  python3 vm.py shot /Users/alex/VMs/ie6-xp/shots/.alert.png >/dev/null
  g=$(python3 -c "
from PIL import Image
p=Image.open('shots/.alert.png').convert('RGB').getpixel((300,110))
print(1 if abs(p[0]-187)<8 and abs(p[1]-187)<8 and abs(p[2]-187)<8 else 0)")
  [ "$g" = 1 ] || break
  k=$((k+1))
  python3 -c "
from PIL import Image
Image.open('shots/.alert.png').crop((0,0,640,130)).save('shots/$name-$k.png')"
  python3 vm.py tap 299 170; sleep 1
done
echo "alerts read: $k"
