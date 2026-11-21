#!/usr/bin/env python3
"""Paints the modern page of design/ns2paint/ into Netscape 2.02 as a framebuffer of 1x1 GIFs (docs/descent.md, "Painting a modern
page").  Reads the headless Chrome shots of tools/paintshots.sh (tests/out/paint/src/<page>.png, <page>_notext.png, <page>.links) and
writes, for each text treatment, tests/out/paint/<variant>/ (git ignores tests/out):
  XX.gif          the variant's palette: up to 256 one-pixel GIFs, XX = the index in hex
  end.gif         a pixel of the page's paper colour after the picture (the server's log holds the time it was requested)
  <page>.html     the picture as rows of IMG ended by BR, one IMG per horizontal run of a colour, and a run of identical rows one line
                  of HEIGHT = rows; the link boxes of the design (data-link) are A HREF=<target>.html round the runs inside them,
                  with onMouseOver setting the status bar to the box's data-status
  <page>.ref.png  the quantised picture the page draws, the oracle of tools/paintcheck.py
Variants (the text treatment; the rest of the picture is the same in all):
  aa       the text as Chrome anti-aliases it
  snap3    every text pixel snapped to 3 levels between its background and its ink (background, half, ink)
  bw       every text pixel snapped to 2 levels: text without anti-aliasing
The palette is the picture's own (palette_of): 256 colours over the variant's three pages together (they share the GIFs, so the
second page fetches few new ones), never dithered; "cube" in --stats is the fixed 216 web-safe colours and 40 greys of
tools/gen_probe_ns2_fb.py for comparison.  Usage: python3 tools/gen_ns2_paint.py [--stats] [--colours N] [--widths]
  --stats    prints the table of tags, links and bytes of every variant (and of the cube palette and of rows unmerged), writes nothing
  --colours  the palette's size (default 256)
  --widths   also writes tests/out/paint/w/w<width>.html: bars of 8 pixels, 800 tall, at widths 572 to 600, for the scroll-safe width (and w<width>.ref.png)
There is no randomness here: the colour cut and the mapping are deterministic."""
import os, struct, sys
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "tests", "out", "paint")
SRC = os.path.join(OUT, "src")
PAGES = ("still", "tall", "details")
PAPER = (0xF6, 0xF7, 0xFB)     # the design's --paper: the page's BGCOLOR and the margins round the picture
ANCHORS = 64   # the most frequent colours kept exact in every palette
VARIANTS = {"aa": 0, "snap3": 3, "bw": 2}   # levels of a text pixel's ink (0: untouched)


def gif(rgb):
    """A 1x1 GIF89a, 35 bytes (the same as tools/gen_probe_ns2_fb.py's)."""
    return (b"GIF89a" + struct.pack("<HHBBB", 1, 1, 0x80, 0, 0) + bytes(rgb) + b"\0\0\0"
            + b"\x2c" + struct.pack("<HHHHB", 0, 0, 1, 1, 0) + b"\x02\x02\x44\x01\x00" + b"\x3b")


def load(page):
    a = np.array(Image.open(os.path.join(SRC, page + ".png")).convert("RGB")).astype(np.int32)
    t = np.array(Image.open(os.path.join(SRC, page + "_notext.png")).convert("RGB")).astype(np.int32)
    links = []
    for l in open(os.path.join(SRC, page + ".links")):
        p = l.rstrip("\n").split(" ", 5)
        if len(p) >= 5:
            links.append((p[0], int(p[1]), int(p[2]), int(p[3]), int(p[4]), p[5] if len(p) > 5 else ""))
    return a, t, links


def snap(a, t, levels):
    """Every pixel where the normal render a differs from the text-free render t is text: its ink is the pixel of its 5x5
    neighbourhood that is farthest from its own background, its coverage the projection of a - t on ink - t, rounded to `levels`."""
    if not levels:
        return a
    h, w, _ = a.shape
    mask = (a != t).any(-1)
    d = np.where(mask, np.sqrt(((a - t) ** 2).sum(-1)), -1.0)
    pd = np.pad(d, 2, constant_values=-1.0)
    pa = np.pad(a, ((2, 2), (2, 2), (0, 0)))
    best, ink = np.full((h, w), -1.0), a.copy()
    for dy in range(5):
        for dx in range(5):
            dd = pd[dy:dy + h, dx:dx + w]
            better = dd > best
            best = np.where(better, dd, best)
            ink = np.where(better[..., None], pa[dy:dy + h, dx:dx + w], ink)
    v = (ink - t).astype(float)
    n2 = (v ** 2).sum(-1)
    al = np.where(n2 > 0, ((a - t) * v).sum(-1) / np.maximum(n2, 1), 0)
    al = np.clip(al, 0, 1)
    q = np.round(al * (levels - 1)) / (levels - 1)
    out = np.round(t + q[..., None] * v).astype(np.int32)
    return np.where(mask[..., None], out, a)


def palette_of(pics, n):
    """The variant's palette, [(r, g, b)] of at most n: the ANCHORS most frequent colours of the pictures together, exact (the
    flat colours of the UI: paper, card white, ink; a cut over everything moved the paper to a mint tint), then a max-coverage cut
    of every other pixel to the rest of the budget (a median cut left errors up to 78 levels on the chart's bars)."""
    allpx = np.concatenate([p.reshape(-1, 3) for p in pics])
    u, c = np.unique(allpx, axis=0, return_counts=True)
    order = np.lexsort((u[:, 2], u[:, 1], u[:, 0], -c))     # by count, then by colour: the same order on every run
    anchors = [tuple(int(v) for v in u[i]) for i in order[:ANCHORS]]
    keep = np.ones(len(u), bool)
    keep[order[:ANCHORS]] = False
    rest = np.repeat(u[keep], c[keep], axis=0)
    q = Image.fromarray(rest.reshape(1, -1, 3).astype(np.uint8), "RGB").quantize(n - len(anchors), method=Image.Quantize.MAXCOVERAGE,
                                                                               dither=Image.Dither.NONE)
    flat = q.getpalette()
    used = sorted(set(np.array(q).ravel().tolist()))
    return anchors + [tuple(flat[3 * i:3 * i + 3]) for i in used]


def cube():
    sys.path.insert(0, HERE)
    import gen_probe_ns2_fb as g
    return list(g.PAL)


def mapped(pic, pal):
    """pic (h, w, 3) -> indices into pal, the nearest colour, never dithered."""
    p = Image.new("P", (1, 1))
    flat = [c for rgb in pal for c in rgb]
    p.putpalette(flat + [0] * (768 - len(flat)))
    q = Image.fromarray(pic.astype(np.uint8), "RGB").quantize(palette=p, dither=Image.Dither.NONE)
    return np.array(q)


def runs(row):
    """[(colour, x, n)] of a row of indices."""
    cut = np.flatnonzero(np.diff(row)) + 1
    st = np.concatenate([[0], cut])
    en = np.concatenate([cut, [len(row)]])
    return [(int(row[s]), int(s), int(e - s)) for s, e in zip(st, en)]


def lines_of(idx, links, merge):
    """[(y, k, pieces)]: a line of k identical rows at y; pieces are (colour, n, link id or -1), the runs split at link edges."""
    h, w = idx.shape
    out = []
    y = 0
    while y < h:
        segs = [(i, x, x + lw) for i, (_, x, ly, lw, lh, _) in enumerate(links) if ly <= y < ly + lh]
        k = 1
        if merge:
            while y + k < h and np.array_equal(idx[y + k], idx[y]) and \
                    [(i, x, x + lw) for i, (_, x, ly, lw, lh, _) in enumerate(links) if ly <= y + k < ly + lh] == segs:
                k += 1
        pieces = []
        for c, x, n in runs(idx[y]):
            cuts = sorted(set([x, x + n] + [b for _, a0, a1 in segs for b in (a0, a1) if x < b < x + n]))
            for a0, a1 in zip(cuts, cuts[1:]):
                lid = next((i for i, s0, s1 in segs if s0 <= a0 and a1 <= s1), -1)
                pieces.append((c, a1 - a0, lid))
        out.append((y, k, pieces))
        y += k
    return out


def html(page, idx, links, merge):
    statuses = []
    for l in links:
        if l[5] not in statuses:
            statuses.append(l[5])
    js = "".join(' if (i == %d) { window.status = "%s"; }\n' % (k, s.replace('"', "'")) for k, s in enumerate(statuses))
    head = ('<HTML><HEAD><TITLE>Quillmere</TITLE>\n<SCRIPT LANGUAGE="JavaScript">\nfunction s(i) {\n%s}\n</SCRIPT></HEAD>\n'
            '<BODY BGCOLOR="#%02X%02X%02X">\n' % ((js,) + PAPER))
    body, tags, anchors = [], 0, 0
    for y, k, pieces in lines_of(idx, links, merge):
        line, cur = [], -1
        for c, n, lid in pieces:
            if lid != cur:
                if cur >= 0:
                    line.append("</A>")
                if lid >= 0:
                    t, st = links[lid][0], statuses.index(links[lid][5])
                    line.append('<A HREF="%s.html" onMouseOver="s(%d); return true">' % (t, st))
                    anchors += 1
                cur = lid
            line.append("<IMG SRC=%02X.gif WIDTH=%d HEIGHT=%d BORDER=0>" % (c, n, k) if lid >= 0 else "<IMG SRC=%02X.gif WIDTH=%d HEIGHT=%d>" % (c, n, k))
            tags += 1
        if cur >= 0:
            line.append("</A>")
        body.append("".join(line) + "<BR>\n")
    s = head + "".join(body) + "<IMG SRC=end.gif WIDTH=1 HEIGHT=1>\n</BODY></HTML>\n"
    return s, tags, anchors


def variant_pics(levels):
    return {p: (snap(*load(p)[:2], levels), load(p)[2]) for p in PAGES}


def write_widths():
    d = os.path.join(OUT, "w")
    os.makedirs(d, exist_ok=True)
    cols = [(0x4F, 0x46, 0xE5), (0xF4, 0x72, 0xB6), (0x14, 0xB8, 0xA6), (0x14, 0x16, 0x32)]
    for i, c in enumerate(cols):
        with open(os.path.join(d, "%02X.gif" % i), "wb") as f:
            f.write(gif(c))
    with open(os.path.join(d, "end.gif"), "wb") as f:
        f.write(gif(PAPER))
    for w in range(572, 601, 2):
        rows = []
        for r in range(100):            # 100 rows of 8 pixels, the bars shifted by a colour each row: a wrapped row shows anywhere
            row = "".join("<IMG SRC=%02X.gif WIDTH=%d HEIGHT=8>" % ((x // 8 + r) % 4, min(8, w - x)) for x in range(0, w, 8))
            rows.append(row + "<BR>\n")
        ref = np.array([[cols[(x // 8 + y // 8) % 4] for x in range(w)] for y in range(800)], dtype=np.uint8)
        Image.fromarray(ref, "RGB").save(os.path.join(d, "w%d.ref.png" % w))
        with open(os.path.join(d, "w%d.html" % w), "w") as f:
            f.write('<HTML><HEAD><TITLE>w%d</TITLE></HEAD>\n<BODY BGCOLOR="#%02X%02X%02X">\n%s<IMG SRC=end.gif WIDTH=1 HEIGHT=1>\n</BODY></HTML>\n'
                    % ((w,) + PAPER + ("".join(rows),)))


def main():
    stats = "--stats" in sys.argv
    n = int(sys.argv[sys.argv.index("--colours") + 1]) if "--colours" in sys.argv else 256
    if "--widths" in sys.argv:
        write_widths()
    print("%-14s %-8s %7s %7s %6s %9s %8s" % ("variant", "page", "colours", "tags", "links", "bytes", "lines"))
    for v, levels in VARIANTS.items():
        pics = variant_pics(levels)
        pal = palette_of([pics[p][0] for p in PAGES], n)
        d = os.path.join(OUT, v if n == 256 else "%s_%d" % (v, n))
        if not stats:
            os.makedirs(d, exist_ok=True)
            for i, c in enumerate(pal):
                with open(os.path.join(d, "%02X.gif" % i), "wb") as f:
                    f.write(gif(c))
            with open(os.path.join(d, "end.gif"), "wb") as f:
                f.write(gif(PAPER))
        rows_cube = []
        for p in PAGES:
            pic, links = pics[p]
            idx = mapped(pic, pal)
            s, tags, anchors = html(p, idx, links, True)
            nl = len(lines_of(idx, links, True))
            print("%-14s %-8s %7d %7d %6d %9d %8d" % (v, p, len(set(idx.ravel().tolist())), tags, anchors, len(s.encode()), nl))
            if stats:
                s1, tags1, _ = html(p, idx, links, False)
                print("%-14s %-8s %7s %7d %6s %9d %8d   (rows not merged)" % (v, p, "", tags1, "", len(s1.encode()), idx.shape[0]))
                if v == "aa":
                    t = load(p)[1]
                    tidx = mapped(t, pal)
                    s3, tags3, _ = html(p, tidx, links, True)
                    print("%-14s %-8s %7d %7d %6s %9d %8s   (no text at all: the text-free render)" % (v, p, len(set(tidx.ravel().tolist())), tags3, "", len(s3.encode()), ""))
                    cidx = mapped(pic, cube())
                    s2, tags2, _ = html(p, cidx, links, True)
                    print("%-14s %-8s %7d %7d %6s %9d %8s   (the fixed cube palette)" % (v, p, len(set(cidx.ravel().tolist())), tags2, "", len(s2.encode()), ""))
            else:
                with open(os.path.join(d, p + ".html"), "w", newline="") as f:
                    f.write(s)
                rgb = np.array(pal, dtype=np.uint8)[idx]
                Image.fromarray(rgb, "RGB").save(os.path.join(d, p + ".ref.png"))


if __name__ == "__main__":
    main()
