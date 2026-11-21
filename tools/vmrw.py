#!/usr/bin/env python3
"""vmrw.py <x> <y> [--n 1] [--stable 6] [--limit 300] [--shot name]: taps the guest at x,y n times (a link of a rw_* or rwt_* probe's
control frame, tools/gen_probe_ns2_fb.py) and for each tap prints, in wall seconds from the tap, when the view (below the control
frame) first changed, when it last changed (the draw is done after that: it stays the same for --stable seconds), and the number of
different screens seen.  The pointer goes elsewhere before each tap: Netscape 2 needs it to arrive at the link.  --shot saves
the screen after the last tap as shots/<name>.png."""
import argparse, os, subprocess, sys, time
sys.path.insert(0, "/Users/alex/VMs/ie6-xp")
from PIL import Image
import vm

SHOTS = "/Users/alex/VMs/ie6-xp/shots"


def view_bytes(tmp):
    vm.hmp("screendump " + tmp)
    for _ in range(40):
        try:
            im = Image.open(tmp)
            im.load()
            return im.convert("RGB").crop((2, 104, 638, 430)).tobytes(), im
        except Exception:
            time.sleep(0.05)
    return None, None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("x", type=int)
    ap.add_argument("y", type=int)
    ap.add_argument("--n", type=int, default=1)
    ap.add_argument("--stable", type=float, default=6)
    ap.add_argument("--limit", type=float, default=300)
    ap.add_argument("--shot")
    a = ap.parse_args()
    tmp = os.path.join(SHOTS, ".rw.ppm")
    prev, _ = view_bytes(tmp)
    for i in range(a.n):
        vm.tap(500, 300, 0)
        time.sleep(1)
        prev, _ = view_bytes(tmp)
        t0 = time.time()
        vm.tap(a.x, a.y)
        first = last = None
        screens = 0
        while time.time() - t0 < a.limit:
            time.sleep(0.3)
            cur, im = view_bytes(tmp)
            now = time.time()
            if cur is not None and cur != prev:
                prev = cur
                screens += 1
                last = now - t0
                if first is None:
                    first = last
            if last is not None and now - t0 - last > a.stable:
                break
        f = lambda v: "-" if v is None else "%.1f" % v
        print("tap %d: first change %s s, last change %s s, %d screens" % (i + 1, f(first), f(last), screens), flush=True)
    if a.shot:
        vm.hmp("screendump " + os.path.join(SHOTS, ".rwend.ppm"))
        time.sleep(0.3)
        Image.open(os.path.join(SHOTS, ".rwend.ppm")).convert("RGB").save(os.path.join(SHOTS, a.shot + ".png"))


if __name__ == "__main__":
    main()
