#!/usr/bin/env python3
"""fbcheck.py <shot.png> [--expect <page>] [--cell N]: reads a screendump of a framebuffer probe (tools/gen_probe_ns2_fb.py) on the
magenta page.  Prints the picture's bounding box (every pixel of the view that is not magenta, the XP activation balloon at the
bottom right masked), the magenta pixels inside it (gaps), the run lengths of colours down the column 2 pixels inside the left edge
and along the row 2 pixels inside the top edge (a stray strip under each row shows there), and the number of distinct colours.
With --expect pal (the palette page) it lists the cells of the 16x16 table whose colour is not the GIF's.
With --expect <fb page> it also compares the box with the picture the page was made from (cell size from the page name) pixel by
pixel and prints the mismatches (a dithering display shows there)."""
import argparse, os, sys
from PIL import Image

MAG = (255, 0, 255)
VIEW = (2, 54, 638, 430)      # the view of the guest's maximised Netscape 2 without toolbar (measured on q1-view)


def runs(seq):
    out, i = [], 0
    while i < len(seq):
        j = i
        while j < len(seq) and seq[j] == seq[i]:
            j += 1
        out.append((seq[i], j - i))
        i = j
    return out


def name_of(c):
    return {(0, 0, 0): "K", (255, 255, 255): "W", MAG: "M"}.get(c, "#%02X%02X%02X" % c)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("shot")
    ap.add_argument("--expect")
    ap.add_argument("--cell", type=int)
    a = ap.parse_args()
    im = Image.open(a.shot).convert("RGB")
    px = im.load()
    x0, y0, x1, y1 = VIEW
    xs, ys = [], []
    for y in range(y0, y1):
        for x in range(x0, x1):
            if x >= 238 and y >= 360:
                continue
            if px[x, y] != MAG:
                xs.append(x)
                ys.append(y)
    if not xs:
        print("no picture: the view is all magenta")
        return
    bx0, bx1, by0, by1 = min(xs), max(xs), min(ys), max(ys)
    w, h = bx1 - bx0 + 1, by1 - by0 + 1
    gap = sum(1 for y in range(by0, by1 + 1) for x in range(bx0, bx1 + 1) if px[x, y] == MAG and not (x >= 238 and y >= 360))
    cols = set(px[x, y] for y in range(by0, by1 + 1) for x in range(bx0, bx1 + 1))
    print("box x %d..%d y %d..%d = %d x %d, magenta inside %d, distinct colours %d" % (bx0, bx1, by0, by1, w, h, gap, len(cols)))
    col = [px[bx0 + 2, y] for y in range(by0, min(by1 + 1, 368))]
    print("down x=%d: %s" % (bx0 + 2, " ".join("%s%d" % (name_of(c), n) for c, n in runs(col))[:600]))
    row = [px[x, by0 + 2] for x in range(bx0, bx1 + 1)]
    print("along y=%d: %s" % (by0 + 2, " ".join("%s%d" % (name_of(c), n) for c, n in runs(row))[:600]))
    if a.expect == "pal":
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import gen_probe_ns2_fb as g
        bad = []
        for i, want in enumerate(g.PAL):
            got = px[bx0 + (i % 16) * 8 + 4, by0 + (i // 16) * 8 + 4]
            if got != want:
                bad.append((i, want, got))
        print("palette cells different from the GIF's colour: %d of 256" % len(bad))
        for i, want, got in bad[:12]:
            print("  %02X want %s got %s" % (i, want, got))
        return
    if a.expect:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import gen_probe_ns2_fb as g
        parts = a.expect.replace(".html", "").split("_")
        pic, cell = parts[1], int(parts[2])
        grid = g.picture(pic, cell)
        bad = tot = 0
        for gy, r in enumerate(grid):
            for gx, c in enumerate(r):
                want = g.PAL[c]
                # the centre of the cell, away from any gap or edge effect
                x = bx0 + gx * cell + cell // 2
                y = by0 + gy * cell + cell // 2
                if y >= 360 and x >= 238:
                    continue
                if y > y1 or x > x1:
                    continue
                tot += 1
                if px[x, y] != want:
                    bad += 1
        print("expected %dx%d cells: centres compared %d, different %d" % (len(grid[0]), len(grid), tot, bad))


if __name__ == "__main__":
    main()
