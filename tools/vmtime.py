#!/usr/bin/env python3
"""vmtime.py <x> <y> <n>: taps the guest at x,y n times; after each tap polls the screen (QEMU screendump) until the region
around the move counter (130,150 to 320,172) changes, and prints the wall seconds from the tap to the change (the tap's own
~0.1 s included) and the total.  A Netscape 2 per-move time for docs/frames-design.md 17."""
import os, subprocess, sys, time
from PIL import Image, ImageChops
VM = "/Users/alex/VMs/ie6-xp/vm.py"
def shot(p):
    subprocess.run(["python3", VM, "shot", p], capture_output=True)
    return Image.open(p).convert("RGB").crop((130, 150, 320, 172))
x, y, n = int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3])
p = "/Users/alex/VMs/ie6-xp/shots/.vmtime.png"
prev = shot(p)
tot = 0.0
for i in range(n):
    t0 = time.time()
    # the page under the pointer was just replaced: Netscape 2 needs the pointer to arrive at the button, so it goes elsewhere first
    subprocess.run(["python3", VM, "tap", "500", "300"], capture_output=True)
    t0 = time.time()
    subprocess.run(["python3", VM, "tap", str(x), str(y)], capture_output=True)
    while True:
        cur = shot(p)
        if ImageChops.difference(cur, prev).getbbox(): break
        if time.time() - t0 > 60: print("no change in 60 s"); sys.exit(1)
    dt = time.time() - t0
    time.sleep(1.5)  # let the draw finish (the counter text is drawn with the rest of the document)
    prev = shot(p)
    print(f"tap {i + 1}: {dt:.1f} s")
    tot += dt
print(f"mean {tot / n:.1f} s")
