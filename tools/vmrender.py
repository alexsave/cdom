#!/usr/bin/env python3
"""vmrender.py [--wait 600] [--steps load,hover,unhover,click,back] [--tag t] [--query m1b32]: demo/ns2render (docs/descent.md, "Netscape 2 paints
itself") in the XP guest's Netscape 2.02, through the guest agent (tools/vmagent.py; no typing) and tools/vmfb.py's helpers.
Serves demo/ns2render/out on port 8110 (tools/vmserve.py), opens index.html, and for each step waits for the draw's end.gif
(its query: p page, s state, and the engine's milliseconds c compute, m markup, w write, t whole, f to the first batch written,
b bytes) and for the screen to stay still, then saves shots/ns2r-<tag>-<step>.png and compares the picture in it with the native
build's (out/ns2render ppm), pixel for pixel (XP's activation balloon masked):
  load      the frameset's GET to the still page drawn
  hover     the pointer onto the pill: the hover state drawn (and the status bar's text in the shot)
  unhover   the pointer onto Features: the normal state drawn again
  click     a click on Features: the details page drawn
  back      a click on the details page's Back pill: the still page drawn
Times are the Mac's wall seconds (the server's log and the screendumps), from the step's start; the guest's clock runs slow,
so its milliseconds are scaled by the factor the draw itself shows (wall from its first GIF request to end.gif, over t - f).
Every browser window closes at the end."""
import argparse, os, re, subprocess, sys, time
import numpy as np
from PIL import Image
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, "/Users/alex/VMs/ie6-xp")
import vmfb, vm

OUT = os.path.join(HERE, "..", "demo", "ns2render", "out")
SHOTS = vmfb.SHOTS
BALLOON = (238, 360)


def native(page, state):
    p = os.path.join(OUT, "ref_%d_%d.ppm" % (page, state))
    subprocess.run([os.path.join(OUT, "ns2render"), "ppm", str(page), str(state), p], check=True)
    return np.array(Image.open(p).convert("RGB")).astype(int)


def locate(s, ref):
    """The picture's top left on the screen: the offset (x 0 to 24, y 40 to 90) where the most pixels match."""
    best = None
    h, w, _ = ref.shape
    probe = ref[::7, ::7]
    for oy in range(40, 91):
        for ox in range(0, 25):
            c = s[oy:oy + h:7, ox:ox + w:7]
            if c.shape != probe.shape:
                continue
            n = (c == probe).all(-1).sum()
            if best is None or n > best[0]:
                best = (n, ox, oy)
    return best[1], best[2]


def compare(shot, page, state):
    s = np.array(Image.open(shot).convert("RGB")).astype(int)
    ref = native(page, state)
    ox, oy = locate(s, ref)
    h, w, _ = ref.shape
    hh, ww = min(h, s.shape[0] - oy), min(w, s.shape[1] - ox)
    keep = np.ones((hh, ww), bool)
    keep[max(BALLOON[1] - oy, 0):, max(BALLOON[0] - ox, 0):] = False
    bad = (s[oy:oy + hh, ox:ox + ww] != ref[:hh, :ww]).any(-1) & keep
    ys, xs = np.nonzero(bad)
    box = "" if not len(ys) else " in x %d-%d y %d-%d" % (xs.min(), xs.max(), ys.min(), ys.max())
    return "at %d,%d: %d of %d pixels compared differ%s (rows shown %d of %d)" % (ox, oy, int(bad.sum()), int(keep.sum()), box, hh, h)


def wait_draw(off, page, state, t0, limit):
    """Until end.gif?p<page>s<state> is requested and the screen has been still for vmfb.STABLE s. Returns a dict of times
    (Mac wall s from t0) and the end mark's numbers, and the new log offset."""
    r = dict(first_gif=None, last_gif=None, end=None, gifs=0, q=None, change=None)
    prev = None
    while time.time() - t0 < limit:
        time.sleep(0.4)
        im = vmfb.screen(os.path.join(SHOTS, ".vr.ppm"))
        now = time.time()
        if im is not None:
            b = im.crop((0, 0, 640, 450)).tobytes()
            if b != prev:
                prev, r["change"] = b, now - t0
        lines, off = vmfb.log_lines_since(off)
        for l in lines:
            m = re.search(r'"GET (\S+)', l)
            if not m:
                continue
            t = vmfb.log_time(l) - t0
            p = m.group(1)
            if re.search(r"/[0-9a-f]{2}\.gif$", p):
                r["gifs"] += 1
                if r["first_gif"] is None:
                    r["first_gif"] = t
                r["last_gif"] = t
            mm = re.search(r"/end\.gif\?p(\d+)s(\d+)c(\d+)m(\d+)w(\d+)t(\d+)f(-?\d+)b(\d+)", p)
            if mm and int(mm.group(1)) == page and int(mm.group(2)) == state:
                r["end"] = t
                r["q"] = dict(zip("pscmwtfb", map(int, mm.groups())))
        if r["end"] is not None and r["change"] is not None and now - t0 - r["change"] > vmfb.STABLE:
            break
    return r, off


def report(step, r, extra=""):
    q = r["q"]
    if not q:
        print("%s: no end mark (first gif %s, gifs %d, last screen change %s)" % (step, r["first_gif"], r["gifs"], r["change"]))
        return
    span = q["t"] - max(q["f"], 0)
    k = (r["end"] - r["first_gif"]) / (span / 1000.0) if r["first_gif"] is not None and span > 0 else float("nan")
    sc = lambda v: v / 1000.0 * k
    print("%s: end.gif %.1f s, last screen change %.1f s, first gif %.1f s, gif requests %d; engine ms c %d m %d w %d t %d f %d, "
          "bytes %d; clock factor %.2f, so compute %.1f s, markup %.1f s, write %.1f s, whole draw %.1f s%s"
          % (step, r["end"], r["change"], r["first_gif"] or -1, r["gifs"], q["c"], q["m"], q["w"], q["t"], q["f"], q["b"], k,
             sc(q["c"]), sc(q["m"]), sc(q["w"]), sc(q["t"]), extra))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--wait", type=float, default=600)
    ap.add_argument("--steps", default="load,hover,unhover,click,back")
    ap.add_argument("--tag", default="a")
    ap.add_argument("--query", default="", help="index.html's query: host.js's options, m<mode> b<rows a batch>")
    a = ap.parse_args()
    steps = a.steps.split(",")
    vmfb.kill_all()
    time.sleep(3)
    subprocess.run(["pkill", "-f", "vmserve.py"])
    time.sleep(1)
    subprocess.Popen([sys.executable, os.path.join(HERE, "vmserve.py"), os.path.abspath(OUT), "8110"],
                     stdout=open(vmfb.LOG, "a"), stderr=subprocess.STDOUT, start_new_session=True)
    time.sleep(1.5)
    vm.tap(632, 441, 0)
    off = os.path.getsize(vmfb.LOG)
    t0 = time.time()
    vmfb.agent('run "%s" http://10.0.2.2:8110/r%d/index.html%s' % (vmfb.NS2, int(t0), "?" + a.query if a.query else ""))
    r, off = wait_draw(off, 0, 1, t0, a.wait)
    shot = os.path.join(SHOTS, "ns2r-%s-load.png" % a.tag)
    vm.tap(632, 441, 0)
    time.sleep(0.5)
    vmfb.screen(os.path.join(SHOTS, ".vr.ppm")).save(shot)
    report("load", r, "; " + compare(shot, 0, 1) + "; " + shot)
    ox, oy = locate(np.array(Image.open(shot).convert("RGB")).astype(int), native(0, 1))
    # the pill and Features, from page_data's link boxes (status.js order): still's links 3 and 0
    pill = (ox + 18 + 58, oy + 192 + 17)
    feat = (ox + 142 + 26, oy + 10 + 12)
    back = (ox + 18 + 58, oy + 252 + 17)
    plan = {"hover": (pill, 0, 0, 2), "unhover": (feat, 0, 0, 1), "click": (feat, 1, 1, 1), "back": (back, 1, 0, 1)}
    for step in steps[1:]:
        (x, y), click, page, state = plan[step]
        off = os.path.getsize(vmfb.LOG)
        t0 = time.time()
        vm.tap(x, y, 1 if click else 0)
        if step == "hover":
            time.sleep(1.0)
            vmfb.screen(os.path.join(SHOTS, ".vr.ppm")).save(os.path.join(SHOTS, "ns2r-%s-hover-status.png" % a.tag))
        r, off = wait_draw(off, page, state, t0, a.wait)
        vm.tap(632, 441, 0)   # the pointer off the picture (no mouseout in Netscape 2: the hover state stays)
        time.sleep(0.5)
        shot = os.path.join(SHOTS, "ns2r-%s-%s.png" % (a.tag, step))
        vmfb.screen(os.path.join(SHOTS, ".vr.ppm")).save(shot)
        report(step, r, "; " + compare(shot, page, state) + "; " + shot)
    vmfb.kill_all()
    time.sleep(2)
    vmfb.screen(os.path.join(SHOTS, ".vr.ppm")).save(os.path.join(SHOTS, "ns2r-%s-final.png" % a.tag))


if __name__ == "__main__":
    main()
