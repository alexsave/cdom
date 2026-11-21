#!/usr/bin/env python3
"""vmclick.py <lk page> click|hover [--wait 120]: the clickable-cell probes (tools/gen_probe_ns2_fb.py lk_*, docs/descent.md).
click: for each line of tests/out/fb/lk_expect.txt for that page ("page x y id"): opens the page in Netscape 2 (tools/vmfb.py), taps
x,y of the guest, and reads the server's log for the /mark/c<id> the handler navigates to; prints the click point, the expected id
and the id the log holds (with the time from the tap).  hover (lk_shape_mo): opens the page, moves the pointer over each point and
prints the seconds until the status bar (2,434 to 520,448) changes, and saves shots/ns2fb_lk_hover_<n>.png with the status text."""
import os, re, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, "/Users/alex/VMs/ie6-xp")
import vmfb, vm
from PIL import Image

def points(page, limit):
    pts = []
    for l in open(os.path.join(HERE, "..", "tests", "out", "fb", "lk_expect.txt")):
        p, x, y, i = l.split()
        if p == page:
            pts.append((int(x), int(y), int(i)))
    return pts[:limit] if limit else pts

def status_bytes():
    vm.hmp("screendump /Users/alex/VMs/ie6-xp/shots/.st.ppm")
    for _ in range(40):
        try:
            im = Image.open("/Users/alex/VMs/ie6-xp/shots/.st.ppm"); im.load(); return im.convert("RGB")
        except Exception:
            time.sleep(0.05)

def main():
    page, mode = sys.argv[1], sys.argv[2]
    d = os.path.join(HERE, "..", "tests", "out")
    for k, (x, y, want) in enumerate(points(page, int(sys.argv[3]) if len(sys.argv) > 3 else 0)):
        for attempt in (1, 2, 3):
            r = vmfb.run_once(page + ".html", d, 120, True, "ns2fb_%s_%d" % (page, k), 30)
            if r["got"]:
                break
        if mode == "click":
            off = os.path.getsize(vmfb.LOG)
            vm.tap(500, 440, 0); time.sleep(1)
            t0 = time.time()
            vm.tap(x, y)
            got = None
            while time.time() - t0 < 30 and got is None:
                time.sleep(0.5)
                lines, off2 = vmfb.log_lines_since(off)
                for l in lines:
                    m = re.search(r"/mark/c(\d+)", l)
                    if m:
                        got = int(m.group(1))
            print("%s click (%d,%d): expected id %d, handler reported %s after %.1f s" % (page, x, y, want, got, time.time() - t0), flush=True)
        else:
            vm.tap(500, 440, 0); time.sleep(1)
            a = status_bytes().crop((2, 434, 520, 448)).tobytes()
            vm.tap(x, y, 0)
            t0 = time.time(); ch = None
            while time.time() - t0 < 10:
                time.sleep(0.2)
                im = status_bytes()
                if im.crop((2, 434, 520, 448)).tobytes() != a:
                    ch = time.time() - t0; break
            time.sleep(1)
            status_bytes().save("/Users/alex/VMs/ie6-xp/shots/ns2fb_lk_hover_%d.png" % k)
            print("%s hover (%d,%d): status bar changed after %s s (expected text: id %d)" % (page, x, y, "%.1f" % ch if ch else "never", want), flush=True)
        vmfb.kill_all()

main()
