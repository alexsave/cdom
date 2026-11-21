#!/usr/bin/env python3
"""The pixel test of demo/ns2render (docs/descent.md, "Netscape 2 paints itself"): the renderer's picture (the native build's
runs, rasterised: out/ns2render ppm) against the design's headless Chrome render (tests/out/paint/src, tools/paintshots.sh).
Text is drawn in our own font (Inter, at Chrome's character positions), so it cannot match Chrome's; the test compares the
picture without its text against Chrome's text-free render, and reports the text separately.
  non-text   every pixel: within TOL levels (the largest channel difference) of Chrome's pixel or of one of its 8 neighbours
             (Chrome anti-aliases every edge, the runs snap it to a pixel), for at least NONTEXT_MIN of the pixels
  corners    the same, over the r x r corner squares of every rounded box of the layout (radius 4 or more): at least CORNER_MIN
  overlap    the same, over the boxes that lie over others (the card over the hero's edge, the badge over the card, the pill
             over the hero): at least OVERLAP_MIN
  text       reported: the share of text pixels (where Chrome's render and its text-free render differ) within TOL
Exit 1 on a failure.  python3 tools/ns2render_check.py [--verbose]"""
import os, subprocess, sys
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
TOP = os.path.join(HERE, "..")
BIN = os.path.join(TOP, "demo", "ns2render", "out", "ns2render")
SRC = os.path.join(TOP, "tests", "out", "paint", "src")
LAY = os.path.join(TOP, "tests", "out", "ns2render")
OUT = os.path.join(TOP, "tests", "out", "ns2render")
TOL = 24                 # levels: the palette's worst error on the page (docs/descent.md, "Painting a modern page")
NONTEXT_MIN = 0.995
CORNER_MIN = 0.99
OVERLAP_MIN = 0.99
OVERLAP = ("div.card", "div.badge", "a.pill")


def ours(page, skip_text):
    p = os.path.join(OUT, f"{page}{'_notext' if skip_text else ''}.ppm")
    env = dict(os.environ, RD_SKIP_TEXT="1" if skip_text else "0")
    subprocess.run([BIN, "ppm", str(("still", "details").index(page)), "1", p], check=True, env=env)
    return np.array(Image.open(p).convert("RGB")).astype(int)


def near(a, ref):
    """Per pixel: the smallest, over ref's 3 x 3 neighbourhood, of the largest channel difference."""
    h, w, _ = ref.shape
    pr = np.pad(ref, ((1, 1), (1, 1), (0, 0)), mode="edge")
    best = np.full((h, w), 999)
    for dy in range(3):
        for dx in range(3):
            best = np.minimum(best, np.abs(a - pr[dy:dy + h, dx:dx + w]).max(-1))
    return best


def boxes(page):
    for l in open(os.path.join(LAY, page + ".tsv")):
        f = l.rstrip("\n").split("\t")
        if f[0] == "E":
            yield f[1], float(f[2]), float(f[3]), float(f[4]), float(f[5]), f[8]


def main():
    fails = 0
    for page in ("still", "details"):
        ref = np.array(Image.open(os.path.join(SRC, page + "_notext.png")).convert("RGB")).astype(int)
        full = np.array(Image.open(os.path.join(SRC, page + ".png")).convert("RGB")).astype(int)
        a = ours(page, True)
        d = near(a, ref)
        ok = d <= TOL
        textmask = (full != ref).any(-1)
        nt = ok[~textmask].mean()
        corner = np.zeros(ok.shape, bool)
        over = np.zeros(ok.shape, bool)
        for name, x, y, w, h, rad in boxes(page):
            r = min(w, h) / 2 if rad.endswith("%") or rad == "999px" else float(rad[:-2]) if rad.endswith("px") else 0
            r = int(min(r, w / 2, h / 2))
            x0, y0, x1, y1 = int(round(x)), int(round(y)), int(round(x + w)), int(round(y + h))
            if r >= 4:
                for cx, cy in ((x0, y0), (x1 - r, y0), (x0, y1 - r), (x1 - r, y1 - r)):
                    corner[max(cy, 0):max(cy + r, 0), max(cx, 0):max(cx + r, 0)] = True
            if name in OVERLAP:
                over[max(y0, 0):y1, max(x0, 0):x1] = True
        corner &= ~textmask
        over &= ~textmask
        cr = ok[corner].mean()
        ov = ok[over].mean()
        t = np.abs(ours(page, False) - full).max(-1)
        tx = (t[textmask] <= TOL).mean()
        exact = (np.abs(a - ref).max(-1) == 0)[~textmask].mean()
        res = [("non-text", nt, NONTEXT_MIN), ("corners", cr, CORNER_MIN), ("overlap", ov, OVERLAP_MIN)]
        for what, v, lo in res:
            good = v >= lo
            fails += not good
            print(f"{'ok  ' if good else 'FAIL'} {page} {what}: {100 * v:.2f}% of {int((~textmask).sum() if what == 'non-text' else (corner if what == 'corners' else over).sum())} pixels within {TOL} levels (needs {100 * lo:.1f}%)")
        print(f"     {page}: non-text pixels exactly Chrome's {100 * exact:.2f}%, worst {int(np.abs(a - ref).max(-1)[~textmask].max())} levels;"
              f" text pixels within {TOL} levels {100 * tx:.1f}% of {int(textmask.sum())}")
        Image.fromarray((np.clip(d, 0, 64) * 4).astype(np.uint8)).save(os.path.join(OUT, page + "_diff.png"))
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
