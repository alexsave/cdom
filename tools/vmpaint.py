#!/usr/bin/env python3
"""vmpaint.py <variant> <mode> [--film dir]: the painted modern page (tools/gen_ns2_paint.py; docs/descent.md, "Painting a modern
page") in the XP guest's Netscape 2.02, through the guest agent and tools/vmfb.py (no typing).  Pages are tests/out/paint/<variant>/.
  load <page>   opens the page, waits until drawn, saves shots/ns2paint-<variant>-<page>.png and compares it (tools/paintcheck.py)
  links         opens still.html; moves the pointer onto each link box and prints when the status bar changed (shots of the status
                bar ns2paint-hover-<n>.png); compares the view after the hovers; taps the pill and prints the seconds to details.html's
                GET and to its end mark, the GIF requests details.html caused, and compares the details page; then taps its Back
                pill and prints the seconds back to still.html
  scroll        opens tall.html; compares the top, then taps the scrollbar's trough below the thumb (a page down) until the view
                stops changing, comparing each view (shots ns2paint-<variant>-tall-<n>.png)
Every browser window closes at the end.  The times are the Mac's wall seconds (the server's log and the screendumps)."""
import os, re, subprocess, sys, time
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, "/Users/alex/VMs/ie6-xp")
import vmfb, vm

SHOTS = vmfb.SHOTS
OUTD = os.path.join(HERE, "..", "tests", "out")
STATUS = (2, 434, 520, 448)
X0, Y0 = 12, 69


def check(shot, ref):
    r = subprocess.run([sys.executable, os.path.join(HERE, "paintcheck.py"), shot, ref], capture_output=True, text=True)
    return r.stdout.strip()


def grab(name):
    p = os.path.join(SHOTS, name + ".png")
    im = vmfb.screen(os.path.join(SHOTS, ".vp.ppm"))
    im.save(p)
    return p, im


def links_of(variant, page):
    out = []
    for l in open(os.path.join(OUTD, "paint", "src", page + ".links")):
        p = l.rstrip("\n").split(" ", 5)
        out.append((p[0], int(p[1]), int(p[2]), int(p[3]), int(p[4]), p[5]))
    return out


def wait_settled(off, page, limit=180):
    """Watches the log from byte off for page's GET and its end mark, and the screen until it is still for vmfb.STABLE s.
    Returns (t_get, t_end, last screen change, gif requests, the new offset)."""
    t0 = time.time()
    t_get = t_end = None
    gifs = 0
    prev, last = None, time.time()
    while time.time() - t0 < limit:
        time.sleep(0.4)
        im = vmfb.screen(os.path.join(SHOTS, ".vp.ppm"))
        now = time.time()
        b = im.crop((0, 0, 640, 430)).tobytes()
        if b != prev:
            prev, last = b, now
        lines, off = vmfb.log_lines_since(off)
        for l in lines:
            m = re.search(r'"GET (\S+)', l)
            if not m:
                continue
            path = m.group(1)
            if path.endswith("/" + page) and t_get is None:
                t_get = vmfb.log_time(l)
            elif t_get is not None and path.endswith("/end.gif"):
                t_end = vmfb.log_time(l)
            elif t_get is not None and re.search(r"/[0-9A-F]{2}\.gif$", path):
                gifs += 1
        if t_end is not None and now - last > vmfb.STABLE:
            break
    return t_get, t_end, last, gifs, off


def main():
    variant, mode = sys.argv[1], sys.argv[2]
    vmfb.SUB = "paint/" + variant
    d = os.path.join(OUTD, "paint", variant)
    ref = lambda p: os.path.join(d, p + ".ref.png")
    if mode == "load":
        page = sys.argv[3]
        r = vmfb.run_once(page + ".html", OUTD, 300, False, "ns2paint-%s-%s" % (variant, page), 60)
        print("%s %s: got=%s end=%s s lastimg=%s s screen-last-change=%s s gif-requests=%d distinct=%d settled=%s" % (
            variant, page, r["got"], *("-" if r[k] is None else "%.1f" % r[k] for k in ("end", "lastimg", "change")),
            r["gifreq"], r["distinct"], r["done"]))
        print("  " + check(r["shot"], ref(page)))
        return
    if mode == "links":
        r = vmfb.run_once("still.html", OUTD, 300, True, "ns2paint-%s-links-still" % variant, 60)
        print("still loaded: end=%.1f s" % r["end"])
        print("  before the hovers: " + check(r["shot"], ref("still")))
        for k, (tgt, x, y, w, h, text) in enumerate(links_of(variant, "still")):
            vm.tap(500, 441, 0)
            time.sleep(1.0)
            a = vmfb.screen(os.path.join(SHOTS, ".vp.ppm")).crop(STATUS).tobytes()
            vm.tap(X0 + x + w // 2, Y0 + y + h // 2, 0)
            t0 = time.time()
            ch = None
            while time.time() - t0 < 5:
                time.sleep(0.1)
                if vmfb.screen(os.path.join(SHOTS, ".vp.ppm")).crop(STATUS).tobytes() != a:
                    ch = time.time() - t0
                    break
            time.sleep(0.8)
            p, im = grab("ns2paint-hover-%d" % k)
            print("hover %d (%d,%d) '%s': status bar changed after %s s" % (k, X0 + x + w // 2, Y0 + y + h // 2, text,
                                                                         "%.1f" % ch if ch is not None else "never"))
        vm.tap(632, 441, 0)
        time.sleep(1)
        p, im = grab("ns2paint-%s-links-after-hover" % variant)
        print("  after the hovers: " + check(p, ref("still")))
        tgt, x, y, w, h, text = links_of(variant, "still")[-1]       # the pill
        off = os.path.getsize(vmfb.LOG)
        t_tap = time.time()
        vm.tap(X0 + x + w // 2, Y0 + y + h // 2)
        t_get, t_end, last, gifs, off = wait_settled(off, "details.html")
        vm.tap(632, 441, 0)
        time.sleep(0.8)
        p, im = grab("ns2paint-%s-details-after-click" % variant)
        f = lambda t: "-" if t is None else "%.1f" % (t - t_tap)
        print("click the pill (%d,%d): details.html GET after %s s, end mark after %s s, screen settled after %s s, %d GIF requests"
              % (X0 + x + w // 2, Y0 + y + h // 2, f(t_get), f(t_end), f(last), gifs))
        print("  details: " + check(p, ref("details")))
        tgt, x, y, w, h, text = links_of(variant, "details")[-1]     # Back home
        t_tap = time.time()
        vm.tap(X0 + x + w // 2, Y0 + y + h // 2)
        t_get, t_end, last, gifs, off = wait_settled(off, "still.html")
        vm.tap(632, 441, 0)
        time.sleep(0.8)
        p, im = grab("ns2paint-%s-still-after-back" % variant)
        print("click Back home: still.html GET after %s s, end mark after %s s, screen settled after %s s, %d GIF requests"
              % (f(t_get), f(t_end), f(last), gifs))
        print("  still again: " + check(p, ref("still")))
        vmfb.kill_all()
        return
    if mode == "scroll":
        r = vmfb.run_once("tall.html", OUTD, 400, True, "ns2paint-%s-tall-0" % variant, 60)
        print("tall loaded: got=%s end=%s s lastimg=%s s screen-last-change=%s s gif-requests=%d settled=%s" % (
            r["got"], *("-" if r[k] is None else "%.1f" % r[k] for k in ("end", "lastimg", "change")), r["gifreq"], r["done"]))
        print("  view 0: " + check(r["shot"], ref("tall")))
        prev = None
        for k in range(1, 12):
            vm.tap(629, 404)          # the trough above the down arrow: a page down while the thumb is above it
            time.sleep(2.5)
            vm.tap(632, 441, 0)
            time.sleep(0.5)
            p, im = grab("ns2paint-%s-tall-%d" % (variant, k))
            b = im.crop((2, 54, 638, 430)).tobytes()
            if b == prev:
                os.remove(p)
                vm.tap(629, 421)      # the down arrow, for the last rows under the thumb
                time.sleep(1.5)
                for _ in range(8):
                    vm.tap(629, 421)
                    time.sleep(0.4)
                vm.tap(632, 441, 0)
                time.sleep(1)
                p, im = grab("ns2paint-%s-tall-end" % variant)
                print("  view end: " + check(p, ref("tall")))
                break
            prev = b
            print("  view %d: " % k + check(p, ref("tall")))
        vmfb.kill_all()


main()
