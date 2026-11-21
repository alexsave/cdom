#!/usr/bin/env python3
"""vmfb.py <page> [--wait 120] [--shot name] [--keep] [--dir tests/out]: one framebuffer probe in Netscape 2.02 of the XP guest
(docs/descent.md "A framebuffer of 1x1 GIFs").  Through the guest agent (tools/vmagent.py; no typing): ends the browsers, restarts
tools/vmserve.py on <dir> (port 8110), opens http://10.0.2.2:8110/r<time>/fb/<page> in Netscape 2, then watches the server's log
and the screen (QEMU screendump, about 2 a second) until the page's end mark has been requested and the view has not changed for
STABLE seconds, or <wait> s have passed.  Prints, in the Mac's wall seconds from the page's GET: the end mark, the last image
request, the last change of the screen, the request counts (all, distinct GIFs); saves shots/<name>.png (the screen at the
end) and, unless --keep, ends the browser (every window and dialog closes).  A start that never asks for the page in 45 s is
retried once.  The guest's own clock runs about 3 times slow; none of these times use it."""
import argparse, os, re, subprocess, sys, time
sys.path.insert(0, "/Users/alex/VMs/ie6-xp")
from PIL import Image
import vm

HERE = os.path.dirname(os.path.abspath(__file__))
LOG = "/Users/alex/VMs/ie6-xp/serve-frames-real.log"
SHOTS = "/Users/alex/VMs/ie6-xp/shots"
STABLE = 8.0
NS2 = r"C:\NS202\Program\netscape.exe"


def agent(cmd, wait=30):
    return subprocess.run([sys.executable, os.path.join(HERE, "vmagent.py"), "send", cmd, "--wait", str(wait)],
                          capture_output=True, text=True).stdout.strip()


def kill_all():
    for p in ("netscape.exe", "iexplore.exe", "notepad.exe", "mshta.exe", "drwtsn32.exe", "dwwin.exe"):
        agent("kill " + p)


def log_time(line):
    m = re.search(r"\[(\d+)/(\w+)/(\d+) (\d+):(\d+):(\d+)\.(\d+)\]", line)
    if not m:
        return None
    t = time.mktime(time.strptime("%s/%s/%s %s:%s:%s" % m.groups()[:6], "%d/%b/%Y %H:%M:%S"))
    return t + int(m.group(7)) / 1000.0


def log_lines_since(off):
    """(complete lines after byte offset off, the new offset)."""
    with open(LOG, "rb") as f:
        f.seek(off)
        b = f.read()
    cut = b.rfind(b"\n") + 1
    return b[:cut].decode("latin-1").splitlines(), off + cut


def screen(path):
    vm.hmp("screendump " + path)
    for _ in range(40):
        try:
            im = Image.open(path)
            im.load()
            return im.convert("RGB")
        except Exception:
            time.sleep(0.05)
    return None


def run_once(page, d, wait, keep, shotname, hang):
    kill_all()
    time.sleep(3)
    subprocess.run(["pkill", "-f", "vmserve.py"])
    time.sleep(1)
    subprocess.Popen([sys.executable, os.path.join(HERE, "vmserve.py"), os.path.abspath(d), "8110"],
                     stdout=open(LOG, "a"), stderr=subprocess.STDOUT, start_new_session=True)
    time.sleep(1.5)
    off = os.path.getsize(LOG)
    rid = "r%d" % int(time.time())
    url = "http://10.0.2.2:8110/%s/fb/%s" % (rid, page)
    res = agent('run "%s" %s' % (NS2, url))
    t0 = time.time()
    tmp = os.path.join(SHOTS, ".fb.ppm")
    prev = None
    last_change = None
    t_get = t_end = t_lastimg = None
    n_all = 0
    gifs = {}
    hung = False
    done = False
    while time.time() - t0 < wait:
        time.sleep(0.4)
        im = screen(tmp)
        now = time.time()
        if im is not None:
            view = im.crop((0, 0, 640, 450)).tobytes()
            if view != prev:
                prev = view
                last_change = now
        lines, off = log_lines_since(off)
        for l in lines:
            if "GET /" not in l:
                continue
            t = log_time(l)
            m = re.search(r'"GET (\S+)', l)
            path = m.group(1)
            if "/fb/" + page in path and t_get is None:
                t_get = t
            if t_get is not None and "/fb/" in path:
                if re.search(r"/[0-9A-F]{2}\.gif$", path):
                    gifs[path] = gifs.get(path, 0) + 1
                    t_lastimg = t
                    n_all += 1
                elif path.endswith("/end.gif"):
                    t_end = t
        if t_get is None and now - t0 > 45:
            break
        # the known start hang: the page was served, the end mark has not come, and for `hang` seconds neither a request nor a change of the screen
        quiet = now - max(x for x in (t_get, t_lastimg, last_change, t0) if x is not None)
        if hang and t_get is not None and t_end is None and quiet > hang:
            hung = True
            break
        if t_end is not None and last_change is not None and now - last_change > STABLE:
            done = True
            break
    t_stop = time.time()
    shot = os.path.join(SHOTS, shotname + ".png")
    im = screen(tmp)
    if im is not None:
        im.save(shot)
    r = dict(url=url, got=t_get is not None and not hung, t_get=t_get, end=(t_end - t_get) if t_end and t_get else None,
             lastimg=(t_lastimg - t_get) if t_lastimg and t_get else None,
             change=(last_change - t_get) if last_change and t_get else None,
             gifreq=n_all, distinct=len(gifs), refetch=sum(1 for v in gifs.values() if v > 1), done=done, shot=shot,
             waited=t_stop - t0)
    if not keep:
        kill_all()
    return r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("page")
    ap.add_argument("--wait", type=float, default=120)
    ap.add_argument("--shot")
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--hang", type=float, default=30, help="seconds without a request or a screen change, after the page's GET and before the end mark, that count as a hung start (0: off)")
    ap.add_argument("--dir", default=os.path.join(HERE, "..", "tests", "out"))
    a = ap.parse_args()
    shot = a.shot or ("ns2fb-" + a.page.replace(".html", ""))
    for attempt in (1, 2):
        r = run_once(a.page, a.dir, a.wait, a.keep, shot, a.hang)
        if r["got"]:
            break
        print("attempt %d: the page was never requested or the start hung, retrying" % attempt)
    f = lambda v: "-" if v is None else "%.1f" % v
    print("%s: got=%s end=%ss lastimg=%ss screen-last-change=%ss gif-requests=%d distinct=%d refetched=%d settled=%s waited=%.0fs shot=%s"
          % (a.page, r["got"], f(r["end"]), f(r["lastimg"]), f(r["change"]), r["gifreq"], r["distinct"], r["refetch"], r["done"],
             r["waited"], r["shot"]))


if __name__ == "__main__":
    main()
