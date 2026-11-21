#!/usr/bin/env python3
"""paintcheck.py <shot.png> <ref.png> [--diff out.png]: compares a screendump of a painted page in the XP guest's Netscape 2.02
(tools/gen_ns2_paint.py; docs/descent.md, "Painting a modern page") with the picture the page was made from, pixel by pixel.
The picture's left edge is at x 12 of the screen and its top at y 69 when the page is not scrolled; a scrolled page is found by
matching screen rows with the picture's rows.  Every pixel of the view (x 2 to 637, y 54 to 429, less a vertical scrollbar's 16
columns when the picture is taller than the view) that shows the picture is compared.  Prints the scroll offset, the rows of the
picture shown, the pixels compared and different (with the box round the different ones); exit 1 if any differ."""
import argparse, sys
import numpy as np
from PIL import Image

VIEW = (2, 54, 638, 430)
X0, Y0 = 12, 69


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("shot")
    ap.add_argument("ref")
    ap.add_argument("--diff")
    a = ap.parse_args()
    s = np.array(Image.open(a.shot).convert("RGB")).astype(int)
    r = np.array(Image.open(a.ref).convert("RGB")).astype(int)
    h, w, _ = r.shape
    vx0, vy0, vx1, vy1 = VIEW
    if h > vy1 - Y0:
        vx1 -= 16                       # the vertical scrollbar
    xr = min(w, vx1 - X0)
    # the scroll: the picture rows a screen row shows (the middle of the view, then every 40th row, until one matches exactly);
    # a plain row (the paper) matches many picture rows, so every candidate is compared whole and the closest is taken
    def diff_at(scroll):
        top, bot = max(vy0, Y0 - scroll), min(vy1, Y0 - scroll + h)
        return int((s[top:bot, X0:X0 + xr] != r[top - Y0 + scroll:bot - Y0 + scroll, :xr]).any(-1).sum())
    best = None
    for y in list(range((vy0 + vy1) // 2, vy1, 40)) + list(range(vy0, (vy0 + vy1) // 2, 40)):
        strip = s[y, X0:X0 + xr]
        cands = [Y0 + int(row) - y for row in np.flatnonzero((r[:, :xr] == strip).all(-1).all(-1))]
        cands = [c for c in cands if c >= 0]
        if cands:
            best = min(cands, key=diff_at)
            break
    if best is None:
        print("no row of the picture is on the screen exactly; compared as not scrolled")
        best = 0
    top = max(vy0, Y0 - best)
    bot = min(vy1, Y0 - best + h)
    sv = s[top:bot, X0:X0 + xr]
    rv = r[top - Y0 + best:bot - Y0 + best, :xr]
    bad = (sv != rv).any(-1)
    n = int(bad.sum())
    print("scroll %d: picture rows %d..%d at screen y %d..%d, %d x %d pixels compared, %d different"
          % (best, top - Y0 + best, bot - Y0 + best - 1, top, bot - 1, xr, bot - top, n), end="")
    if n:
        ys, xs = np.nonzero(bad)
        print(", in x %d..%d y %d..%d of the screen" % (X0 + xs.min(), X0 + xs.max(), top + ys.min(), top + ys.max()))
    else:
        print()
    if xr < w:
        print("the picture is %d wide, %d columns of it are under the scrollbar or past the view" % (w, w - xr))
    if a.diff:
        d = s.copy().astype(np.uint8)
        d[top:bot, X0:X0 + xr][bad] = (255, 0, 255)
        Image.fromarray(d).save(a.diff)
    sys.exit(1 if n else 0)


if __name__ == "__main__":
    main()
