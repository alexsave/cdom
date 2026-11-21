#!/usr/bin/env python3
"""Writes demo/ns2render/page_data.c, the box description that demo/ns2render/render.c paints in Netscape 2.02 (docs/descent.md,
"Netscape 2 paints itself").  Offline, run once by hand; its output is committed, so a build needs neither Chrome nor the font.

Reads, for each page of design/ns2paint/ (still, details):
  tests/out/ns2render/<page>.tsv          the layout headless Chrome gave it (tools/ns2render_layout.mjs)
  tests/out/paint/src/<page>.png          the Chrome render, and <page>_notext.png with every glyph and icon transparent
                                          (tools/paintshots.sh): the palette's colours, and the icons' coverage
and the font Inter (INTER_TTF, the variable Inter 4 of github.com/rsms/inter, SIL Open Font License 1.1), rasterised once here
at each size and weight the pages use into glyphs of LEVELS grey levels, stored as runs per glyph row.

The boxes are a fixed vocabulary, not a CSS engine: a solid or translucent fill, a linear gradient (any angle, stops), corner
radii (top and bottom), a border as a ring, box-shadow as nested translucent rounded rectangles, the radial highlight as nested
translucent circles, text and icons as glyph items.  Usage: INTER_TTF=path python3 tools/gen_ns2_render.py [--levels N]
There is no randomness here."""
import math, os, re, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
TOP = os.path.join(HERE, "..")
LAY = os.path.join(TOP, "tests", "out", "ns2render")
SRC = os.path.join(TOP, "tests", "out", "paint", "src")
OUT = os.path.join(TOP, "demo", "ns2render", "page_data.c")
PAGES = ("still", "details")
W, H = 592, 320
PAPER = (0xF6, 0xF7, 0xFB)
ANCHORS = 64
LEVELS = int(sys.argv[sys.argv.index("--levels") + 1]) if "--levels" in sys.argv else 3
HOVER_PILL = (0xE0, 0xE7, 0xFF)   # the pill's hover fill (indigo 100), an anchor of the palette
SHADOW_LAYERS = 6
GRID = 8          # the nearest-colour grid's cells a channel (render.c's RD_GRID_SHIFT is 8 - log2 GRID)
RING_LAYERS = 8


def rgba(s):
    m = re.match(r"rgba?\(([\d.]+), ([\d.]+), ([\d.]+)(?:, ([\d.]+))?\)", s.strip())
    if not m:
        raise ValueError("colour " + s)
    return (int(m[1]), int(m[2]), int(m[3])), (float(m[4]) if m[4] is not None else 1.0)


def split_top(s):
    """s split at the commas outside parentheses."""
    out, d, cur = [], 0, ""
    for ch in s:
        if ch == "(":
            d += 1
        elif ch == ")":
            d -= 1
        if ch == "," and d == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    out.append(cur.strip())
    return out


class Doc:
    def __init__(self):
        self.paints = [(("s", PAPER), 0, PAPER, 0, 0, 0, [], 0, 0)]   # paint 0, the paper: composite 0, every row's start      # (kind, rgb, gx, gy, g0, stops)
        self.items = []       # dicts
        self.texts = []       # (rgb, alpha8, [(x, y, glyph)])
        self.links = []       # (x0, y0, x1, y1, target page, status)


def paint_solid(doc, rgb):
    key = ("s", rgb)
    for i, p in enumerate(doc.paints):
        if p[0] == key:
            return i
    doc.paints.append((key, 0, rgb, 0, 0, 0, [], 0, 0))
    return len(doc.paints) - 1


def paint_linear(doc, ang, x, y, w, h, stops):
    """CSS linear-gradient(ang deg) over the box (x, y, w, h): t = 0 to 4096 along the gradient line, t = gx x + gy y + g0 in
    16.16 fixed point."""
    a = math.radians(ang)
    dx, dy = math.sin(a), -math.cos(a)
    ln = abs(w * dx) + abs(h * dy)
    cx, cy = x + w / 2, y + h / 2
    # t(x, y) = (((x + .5 - cx) dx + (y + .5 - cy) dy) / ln + .5) * 4096 at the pixel's centre, from the box's corner (ox, oy)
    ox, oy = int(math.floor(x)), int(math.floor(y))
    gx = dx / ln * 4096 * 65536
    gy = dy / ln * 4096 * 65536
    g0 = ((ox + 0.5 - cx) * dx + (oy + 0.5 - cy) * dy) / ln * 4096 * 65536 + 0.5 * 4096 * 65536
    doc.paints.append((("l", len(doc.paints)), 1, (0, 0, 0), int(round(gx)), int(round(gy)), int(round(g0)),
                       [(int(round(p * 4096)), c) for p, c in stops], ox, oy))
    return len(doc.paints) - 1


def parse_linear(s):
    body = s[s.index("(") + 1:s.rindex(")")]
    parts = split_top(body)
    ang = 180.0
    if parts[0].endswith("deg"):
        ang = float(parts[0][:-3])
        parts = parts[1:]
    stops = []
    for i, p in enumerate(parts):
        m = re.match(r"(rgba?\([^)]*\))\s*([\d.]+%)?", p)
        c, _ = rgba(m[1])
        pos = float(m[2][:-1]) / 100 if m[2] else i / (len(parts) - 1)
        stops.append((pos, c))
    return ang, stops


def radius(v, w, h):
    if v.endswith("%"):
        return min(w, h) * float(v[:-1]) / 100
    return min(float(v[:-2]), min(w, h) / 2)


def item(doc, x0, y0, x1, y1, rt, rb, paint, alpha, state=3, kind=0, text=0, inner=None):
    x0, y0, x1, y1 = int(round(x0)), int(round(y0)), int(round(x1)), int(round(y1))
    if x1 <= x0 or y1 <= y0 or alpha <= 0:
        return
    rt = min(int(round(rt)), (x1 - x0) // 2, (y1 - y0) // 2)
    rb = min(int(round(rb)), (x1 - x0) // 2, (y1 - y0) // 2)
    doc.items.append(dict(x0=x0, y0=y0, x1=x1, y1=y1, rt=rt, rb=rb, paint=paint, alpha=max(1, min(255, int(round(alpha * 255)))),
                          state=state, kind=kind, text=text, bw=inner or 0))


def phi(z):
    return 0.5 * (1 + math.erf(z / math.sqrt(2)))


def shadow(doc, x, y, w, h, r, sh, state):
    """box-shadow (ox, oy, blur, spread, colour, alpha) as SHADOW_LAYERS nested rounded rectangles: the shadow's alpha at a
    distance d outside the shape is a * Phi(-d / sigma), sigma = blur / 2; each layer's own alpha makes the composite of the
    layers over a point at the middle of its band equal that."""
    ox, oy, blur, spread, c, a = sh
    sig = max(blur / 2, 0.5)
    sx, sy, sw, shh, sr = x + ox - spread, y + oy - spread, w + 2 * spread, h + 2 * spread, r + spread
    n = SHADOW_LAYERS if blur > 4 else 2
    edges = [2 * sig - 4 * sig * k / n for k in range(n + 1)]   # from 2 sigma outside to 2 sigma inside
    have = 0.0
    for k in range(n):
        d_out = edges[k]
        mid = (edges[k] + edges[k + 1]) / 2
        want = a * phi(-mid / sig)
        if want <= have:
            continue
        la = 1 - (1 - want) / (1 - have)
        have = want
        g = d_out
        item(doc, sx - g, sy - g, sx + sw + g, sy + shh + g, max(sr + g, 0), max(sr + g, 0), paint_solid(doc, c), la, state)


def rings(doc, x, y, w, h, s):
    """radial-gradient(circle, c a0 0%, c 0 p%) of the box: RING_LAYERS nested circles of the farthest corner's radius times p."""
    body = s[s.index("(") + 1:s.rindex(")")]
    parts = split_top(body)[1:]
    c, a0 = rgba(re.match(r"(rgba?\([^)]*\))", parts[0])[1])
    p = float(re.search(r"([\d.]+)%", parts[1])[1]) / 100
    R = math.hypot(w / 2, h / 2) * p
    cx, cy = x + w / 2, y + h / 2
    have = 0.0
    for k in range(RING_LAYERS):
        rr = R * (RING_LAYERS - k) / RING_LAYERS
        mid = R * (RING_LAYERS - k - 0.5) / RING_LAYERS
        want = a0 * (1 - mid / R)
        la = 1 - (1 - want) / (1 - have)
        have = want
        item(doc, cx - rr, max(cy - rr, 0), cx + rr, cy + rr, rr, rr, paint_solid(doc, c), la)
        if cy - rr < 0:   # the top cut by the page: the circle's top rows are above row 0, so the box keeps its shape
            doc.items[-1]["y0"] = int(round(cy - rr))


# --- glyphs -------------------------------------------------------------------------------------------------------------
class Glyphs:
    def __init__(self, ttf):
        self.ttf = ttf
        self.fonts = {}
        self.list = []     # (w, h, rows: [[(x, n, level)]])
        self.index = {}

    def font(self, size, weight):
        k = (size, weight)
        if k not in self.fonts:
            f = ImageFont.truetype(self.ttf, size)
            f.set_variation_by_axes([min(max(size, 14), 32), weight])
            self.fonts[k] = f
        return self.fonts[k]

    def add(self, cov):
        """cov (h, w) in 0..1 -> a glyph id; LEVELS levels, level 0 transparent."""
        q = np.round(cov * (LEVELS - 1)).astype(int)
        ys = np.flatnonzero(q.any(1))
        xs = np.flatnonzero(q.any(0))
        if not len(ys):
            return None, 0, 0
        q = q[ys[0]:ys[-1] + 1, xs[0]:xs[-1] + 1]
        rows = []
        for r in q:
            runs, x = [], 0
            while x < len(r):
                e = x
                while e < len(r) and r[e] == r[x]:
                    e += 1
                if r[x]:
                    runs.append((x, e - x, int(r[x])))
                x = e
            rows.append(runs)
        key = (q.shape, tuple(tuple(r) for r in rows))
        if key not in self.index:
            self.index[key] = len(self.list)
            self.list.append((q.shape[1], q.shape[0], rows))
        return self.index[key], int(xs[0]), int(ys[0])

    def char(self, ch, size, weight):
        f = self.font(size, weight)
        asc, desc = f.getmetrics()
        im = Image.new("L", (int(size * 2) + 8, asc + desc + 8), 0)
        ImageDraw.Draw(im).text((4, 4), ch, font=f, fill=255)
        g, ox, oy = self.add(np.array(im) / 255.0)
        return g, ox - 4, oy - 4 - asc     # offsets from the pen position on the baseline


def build(page, glyphs, pal_anchor):
    doc = Doc()
    lay = [l.rstrip("\n").split("\t") for l in open(os.path.join(LAY, page + ".tsv"))]
    a = np.array(Image.open(os.path.join(SRC, page + ".png")).convert("RGB")).astype(float)
    t = np.array(Image.open(os.path.join(SRC, page + "_notext.png")).convert("RGB")).astype(float)
    base = float(next(l[1] for l in lay if l[0] == "B"))
    targets = {"details": 1, "still": 0}
    els = [l for l in lay if l[0] == "E"]
    chars = [l for l in lay if l[0] == "C"]
    zs = sorted(set(int(l[13]) for l in els) | set(int(l[9]) for l in chars))
    for z in zs:
        for l in (l for l in els if int(l[13]) == z):
            _, name, x, y, w, h, bg, img, rtl, rbl, shs, bw, bc, _, link, bbw, bbc = l
            x, y, w, h = float(x), float(y), float(w), float(h)
            states = [3]
            if name == "a.pill" and page == "still":
                states = [1, 2]
            if name == "svg":
                # an icon: its coverage from the two Chrome renders, ink the svg's colour, as one glyph
                ink, _ = rgba(bg)
                X0, Y0, X1, Y1 = int(x), int(y), int(math.ceil(x + w)), int(math.ceil(y + h))
                d = a[Y0:Y1, X0:X1] - t[Y0:Y1, X0:X1]
                v = np.array(ink, float)[None, None, :] - t[Y0:Y1, X0:X1]
                n2 = (v ** 2).sum(-1)
                cov = np.clip(np.where(n2 > 0, (d * v).sum(-1) / np.maximum(n2, 1), 0), 0, 1)
                g, ox, oy = glyphs.add(cov)
                doc.texts.append((ink, 255, [(X0 + ox, Y0 + oy, g)]))
                item(doc, X0 + ox, Y0 + oy, X0 + ox + glyphs.list[g][0], Y0 + oy + glyphs.list[g][1], 0, 0, 0, 1, 3, 1, len(doc.texts) - 1)
                continue
            rt, rb = radius(rtl, w, h), radius(rbl, w, h)
            for st in states:
                if shs != "none":
                    for s in split_top(shs):
                        m = re.match(r"(rgba?\([^)]*\)) (-?[\d.]+)px (-?[\d.]+)px ([\d.]+)px ([\d.]+)px", s)
                        c, al = rgba(m[1])
                        shadow(doc, x, y, w, h, rt, (float(m[2]), float(m[3]), float(m[4]), float(m[5]), c, al), st)
                bbv = float(bbw[:-2]) if bbw.endswith("px") else 0
                if bbv > 0 and not (float(bw[:-2]) if bw.endswith("px") else 0):
                    c, al = rgba(bbc)   # a bottom border alone: a line under the box (the details page's spec rows)
                    item(doc, x, y + h - bbv, x + w, y + h, 0, 0, paint_solid(doc, c), al, st)
                bwv = float(bw[:-2]) if bw.endswith("px") else 0
                if bwv > 0:
                    c, al = rgba(bc)
                    item(doc, x, y, x + w, y + h, rt, rb, paint_solid(doc, c), al, st, 2, 0, int(round(bwv)))
                    x2, y2, w2, h2 = x + bwv, y + bwv, w - 2 * bwv, h - 2 * bwv
                    rt2, rb2 = max(rt - bwv, 0), max(rb - bwv, 0)
                else:
                    x2, y2, w2, h2, rt2, rb2 = x, y, w, h, rt, rb
                if img.startswith("linear-gradient"):
                    ang, stops = parse_linear(img)
                    item(doc, x2, y2, x2 + w2, y2 + h2, rt2, rb2, paint_linear(doc, ang, x2, y2, w2, h2, stops), 1, st)
                elif img.startswith("radial-gradient"):
                    rings(doc, x, y, w, h, img)
                else:
                    c, al = rgba(bg)
                    if st == 2:
                        c = HOVER_PILL
                    item(doc, x2, y2, x2 + w2, y2 + h2, rt2, rb2, paint_solid(doc, c), al, st)
        # the text of this z: one text item per (colour, line, size, weight)
        groups = {}
        for l in (l for l in chars if int(l[9]) == z):
            _, ch, x, top, w, h, fs, fw, col, _, link = l
            key = (col, round(float(top) + float(fs[:-2]) * base), fs, fw)
            groups.setdefault(key, []).append((ch, float(x)))
        for (col, bl, fs, fw), cs in groups.items():
            c, al = rgba(col)
            size, weight = float(fs[:-2]), int(fw)
            places = []
            for ch, x in cs:
                g, ox, oy = glyphs.char(ch, size, weight)
                if g is not None:
                    places.append((int(round(x)) + ox, bl + oy, g))
            if not places:
                continue
            doc.texts.append((c, int(round(al * 255)), places))
            x0 = min(p[0] for p in places)
            y0 = min(p[1] for p in places)
            x1 = max(p[0] + glyphs.list[p[2]][0] for p in places)
            y1 = max(p[1] + glyphs.list[p[2]][1] for p in places)
            item(doc, x0, y0, x1, y1, 0, 0, 0, 1, 3, 1, len(doc.texts) - 1)
    for l in lay:
        if l[0] == "L":
            x, y, w, h = int(l[2]), int(l[3]), int(l[4]), int(l[5])
            doc.links.append((x, y, min(x + w, W), min(y + h, H), targets[l[1]], l[6]))
    return doc


def palette(extra):
    pics = [np.array(Image.open(os.path.join(SRC, p + ".png")).convert("RGB")) for p in PAGES]
    allpx = np.concatenate([p.reshape(-1, 3) for p in pics])
    u, c = np.unique(allpx, axis=0, return_counts=True)
    order = np.lexsort((u[:, 2], u[:, 1], u[:, 0], -c))
    anchors = [tuple(int(v) for v in u[i]) for i in order[:ANCHORS]]
    for e in extra:
        if e not in anchors:
            anchors.append(e)
    keep = np.ones(len(u), bool)
    keep[order[:ANCHORS]] = False
    rest = np.repeat(u[keep], c[keep], axis=0)
    q = Image.fromarray(rest.reshape(1, -1, 3).astype(np.uint8), "RGB").quantize(256 - len(anchors), method=Image.Quantize.MAXCOVERAGE,
                                                                               dither=Image.Dither.NONE)
    flat = q.getpalette()
    used = sorted(set(np.array(q).ravel().tolist()))
    pal = anchors + [tuple(flat[3 * i:3 * i + 3]) for i in used]
    out = []
    for p in pal:
        if p not in out:
            out.append(p)
    return out[:256]


def grid(pal):
    """For each of the GRID x GRID x GRID cells of RGB, the palette entries that can be nearest to some colour in the cell: every
    entry whose nearest point of the cell is no farther than the best entry's farthest corner."""
    pa = np.array(pal, float)
    start, cand = [], []
    side = 256 // GRID
    for r in range(GRID):
        for g in range(GRID):
            for b in range(GRID):
                lo = np.array([r, g, b]) * side
                hi = lo + side - 1
                near = np.clip(pa, lo, hi)
                dmin = ((pa - near) ** 2).sum(1)
                far = np.where(np.abs(pa - lo) > np.abs(pa - hi), lo, hi)
                dmax = ((pa - far) ** 2).sum(1)
                ok = np.flatnonzero(dmin <= dmax.min())
                start.append(len(cand))
                cand.extend(int(i) for i in ok)
    start.append(len(cand))
    return start, cand


def cstr(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main():
    ttf = os.environ.get("INTER_TTF")
    if not ttf:
        sys.exit("INTER_TTF=<Inter variable .ttf> (github.com/rsms/inter, OFL 1.1)")
    glyphs = Glyphs(ttf)
    docs = [build(p, glyphs, None) for p in PAGES]
    pal = palette([PAPER, HOVER_PILL])
    start, cand = grid(pal)
    o = []
    w = o.append
    w("/* GENERATED by tools/gen_ns2_render.py from design/ns2paint/ as headless Chrome lays it out; do not edit.")
    w("   Glyphs: Inter 4.001 (Copyright 2016 The Inter Project Authors, https://github.com/rsms/inter), SIL Open Font License 1.1,")
    w(f"   rasterised at the pages' sizes and weights into {LEVELS} levels. */")
    w('#include "render.h"')
    w(f"const uint32_t rd_palette[{len(pal)}] = {{" + ",".join("0x%02x%02x%02x" % p for p in pal) + "};")
    w(f"const int rd_npalette = {len(pal)};")
    w(f"const uint16_t rd_grid_start[{len(start)}] = {{" + ",".join(map(str, start)) + "};")
    w(f"const uint8_t rd_grid_cand[{len(cand)}] = {{" + ",".join(map(str, cand)) + "};")
    runs, rowstart, gl = [], [], []
    for gw, gh, rows in glyphs.list:
        gl.append((gw, gh, len(rowstart)))
        for r in rows:
            rowstart.append(len(runs))
            runs.extend(r)
    rowstart.append(len(runs))
    assert all(gw < 256 and gh < 256 and r0 < 65536 for gw, gh, r0 in gl) and all(x < 256 and n < 256 for x, n, _ in runs)
    w(f"const uint32_t rd_glyphs[{len(gl)}] = {{" + ",".join(str(gw | gh << 8 | r0 << 16) for gw, gh, r0 in gl) + "};")
    w(f"const uint16_t rd_glyph_rows[{len(rowstart)}] = {{" + ",".join(map(str, rowstart)) + "};")
    w(f"const uint32_t rd_glyph_runs[{len(runs)}] = {{" + ",".join(str(x | n << 8 | lv << 16) for x, n, lv in runs) + "};")
    w(f"const int rd_levels = {LEVELS};")
    for name, d in zip(PAGES, docs):
        stops = []
        ps = []
        for key, kind, rgb, gx, gy, g0, st, ox, oy in d.paints:
            ps.append("{%d,%d,%d,0x%02x%02x%02x,%d,%d,%d,%d,%d}" % (kind, len(stops), len(st), *rgb, gx, gy, g0, ox, oy))
            stops.extend(st)
        w(f"static const RdStop {name}_stops[{max(len(stops), 1)}] = {{" + ",".join("{%d,0x%02x%02x%02x}" % (p, *c) for p, c in stops) + "};")
        w(f"static const RdPaint {name}_paints[{len(ps)}] = {{" + ",".join(ps) + "};")
        places = []
        tx = []
        for rgb, al, pl in d.texts:
            tx.append("{%d,%d,0x%02x%02x%02x,%d}" % (len(places), len(pl), *rgb, al))
            places.extend(pl)
        assert all(0 <= x < 1024 and 0 <= y < 1024 and gi < 4096 for x, y, gi in places)
        w(f"static const uint32_t {name}_places[{len(places)}] = {{" + ",".join(str(x | y << 10 | gi << 20) for x, y, gi in places) + "};")
        w(f"static const RdText {name}_texts[{len(tx)}] = {{" + ",".join(tx) + "};")
        w(f"static const RdItem {name}_items[{len(d.items)}] = {{")
        for it in d.items:
            w("  {%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d}," % (it["x0"], it["y0"], it["x1"], it["y1"], it["rt"], it["rb"], it["paint"],
                                                         it["alpha"], it["state"], it["kind"], it["bw"], it["text"]))
        w("};")
        w(f"static const RdLink {name}_links[{len(d.links)}] = {{" + ",".join("{%d,%d,%d,%d,%d}" % l[:5] for l in d.links) + "};")
    w(f"const RdPage rd_pages[{len(PAGES)}] = {{")
    for name, d in zip(PAGES, docs):
        w(f"  {{{name}_items,{len(d.items)},{name}_paints,{len(d.paints)},{name}_stops,{name}_texts,{name}_places,{name}_links,{len(d.links)}}},")
    w("};")
    w(f"const int rd_npages = {len(PAGES)};")
    open(OUT, "w").write("\n".join(o) + "\n")
    # the links' status texts, for the host (JavaScript 1.0 statements, page * 16 + link)
    js = ["// GENERATED by tools/gen_ns2_render.py; do not edit. For link n = page * 16 + index: rsT[\"k\" + n] its status text,",
          "// rsG[\"k\" + n] the page it leads to, rsH[\"k\" + n] 1 for the pill, whose hover the page draws (string keys: Netscape 2",
          "// reads o[n], n a number, as the nth property)."]
    js.append("var rsT=new Object(),rsG=new Object(),rsH=new Object();")
    for p, d in enumerate(docs):
        for i, l in enumerate(d.links):
            k = f'"k{p * 16 + i}"'
            js.append(f"rsT[{k}]={cstr(l[5])};rsG[{k}]={l[4]};rsH[{k}]={1 if p == 0 and l[5].startswith('Get started') else 0};")
    open(os.path.join(TOP, "demo", "ns2render", "status.js"), "w").write("\n".join(js) + "\n")
    print(OUT, "palette", len(pal), "grid candidates", len(cand), "glyphs", len(gl), "glyph runs", len(runs),
          " ".join(f"{n}: items {len(d.items)} paints {len(d.paints)} texts {len(d.texts)} links {len(d.links)}" for n, d in zip(PAGES, docs)))


if __name__ == "__main__":
    main()
