#!/bin/bash
# vmfilm-uttt.sh <film dir> <out.mp4>: the Netscape 2 milestone take (docs/frames-design.md section 17): starts tools/vmfilm.py, opens
# demo/uttt's uttt.html?AJG7NYNQG4RFXXGVPGD7QR2ETSOYTWMCREAQ in Netscape 2.02 (tools/vmns2.sh: no typing, a hung start is
# retried), steps > four times, presses Play, answers Netscape 2's "Lengthy JavaScript" dialog, and films until the move counter has not changed for 40 s (the game's end) or 6
# minutes pass; builds the mp4 at the real pace.  Needs the guest agent and demo/uttt built.
dir="$1"; out="$2"
here=$(cd "$(dirname "$0")" && pwd)
rm -rf "$dir"; mkdir -p "$dir"
python3 "$here/vmfilm.py" capture "$dir" & cap=$!
sleep 2
VMDIR="$here/../demo/uttt/out" "$here/vmns2.sh" ns2 'uttt.html?AJG7NYNQG4RFXXGVPGD7QR2ETSOYTWMCREAQ' 150 film-open || { kill $cap; exit 1; }
sleep 3
python3 "$here/vmtime.py" 100 160 4
python3 /Users/alex/VMs/ie6-xp/vm.py tap 500 300; sleep 2
python3 /Users/alex/VMs/ie6-xp/vm.py tap 70 160
echo "play pressed at $(date +%s)"
python3 - <<'PY'
import subprocess, time
from PIL import Image, ImageChops
VM = "/Users/alex/VMs/ie6-xp/vm.py"; p = "/Users/alex/VMs/ie6-xp/shots/.film.png"
def region():
    subprocess.run(["python3", VM, "shot", p], capture_output=True)
    return Image.open(p).convert("RGB").crop((130, 150, 320, 172))
t0 = time.time(); prev = region(); last = time.time(); moves = 0; dialogs = 0
while time.time() - t0 < 360:
    time.sleep(1)
    cur = region()
    # Netscape 2's "Lengthy JavaScript still running. Continue?" (every dozen moves of Play): its blue title bar is at 300,178 where
    # the page has none; OK is tapped (docs/frames-design.md 17)
    px = Image.open(p).convert("RGB").getpixel((300, 178))
    if px[2] > 100 and px[0] < 80 and px[1] < 120:
        dialogs += 1; subprocess.run(["python3", VM, "tap", "278", "262"], capture_output=True); last = time.time(); continue
    if ImageChops.difference(cur, prev).getbbox(): moves += 1; last = time.time(); prev = cur
    elif time.time() - last > 40: break
print(f"{moves} counter changes in {time.time() - t0:.0f} s, {dialogs} Lengthy-JavaScript dialogs answered")
PY
sleep 3
kill $cap
python3 "$here/vmfilm.py" build "$dir" "$out"
