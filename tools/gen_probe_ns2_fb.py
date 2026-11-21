#!/usr/bin/env python3
"""Writes the probe pages of docs/descent.md "A framebuffer of 1x1 GIFs" into tests/out/fb/ (git ignores tests/out).
Run: python3 tools/gen_probe_ns2_fb.py [--stats] [substring ...]   (--stats prints the table of tags and bytes and writes nothing;
substrings: write only the fb-* pages whose name holds one of them, e.g. fb_photo_8_row; the 1 and 2 pixel pages are megabytes)

Files in tests/out/fb/ (no hyphen in a name: Netscape 2 reads '-r', '-i' in its command line as options, so such a URL fails to start):
  XX.gif                      the palette: 256 one-pixel GIFs (216 web-safe colours, then 40 greys), XX = the index in hex
  q1_<variant>.html           an 8x8 black and white checker of 16 pixel cells on a magenta page, in one layout variant
                              (the magenta that shows is a gap), and q1_one_<variant>.html with 1x1 cells
  fb_<pic>_<cell>_<mode>[_rle].html   a picture of 576 x 320 pixels (FB_W, FB_H) as (576/cell) x (320/cell) cells
        pic   photo (a dithered synthetic scene), art (the Game Boy Durak win screen, flat), check (a checkerboard),
              bars (vertical colour bars: the longest runs)
        mode  row (rows of IMG ended by BR, no white space) or tab (a TABLE, one cell per block or run)
        -rle  a horizontal run of one colour is one IMG of WIDTH = run * cell
  pal.html                    all 256 palette GIFs in a 16x16 table, each cell 8x8 (the first load of the palette)
  rw_*.html                   frames for the rewrite probes (Q4), see rewrite_pages()
Every picture page ends with the magenta 1x1 image end.gif (the server's log holds the time it was requested; a missing file would draw a broken-image icon).
The one random source is the seed below: the synthetic photo is the same on every run."""
import os, sys, struct
import numpy as np
from PIL import Image

SEED = 20261121
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "tests", "out", "fb")
ART = "/Users/alex/Dev/foolish-gameboy-art/gameboy/art/mockups/w_seoul.png"
W, H = int(os.environ.get("FB_W", 576)), int(os.environ.get("FB_H", 320))   # the picture area in pixels (the view of the guest's Netscape 2 is 640 x 380)
CELLS = (16, 8, 4, 2, 1)
BG = "#FF00FF"             # magenta: no palette colour is magenta, so any magenta inside a picture is a gap


def palette():
    p = []
    for r in range(6):
        for g in range(6):
            for b in range(6):
                p.append((r * 51, g * 51, b * 51))
    for k in range(40):
        v = 4 + 6 * k      # 4, 10, ... 238: never a multiple of 51
        p.append((v, v, v))
    return p


PAL = palette()


def gif(rgb):
    """A 1x1 GIF89a, 35 bytes: a 2 colour table, LZW minimum code size 2 and the codes clear, 0, end."""
    return (b"GIF89a" + struct.pack("<HHBBB", 1, 1, 0x80, 0, 0) + bytes(rgb) + b"\0\0\0"
            + b"\x2c" + struct.pack("<HHHHB", 0, 0, 1, 1, 0) + b"\x02\x02\x44\x01\x00" + b"\x3b")


def name(i):
    return "%02X.gif" % i


def pal_image():
    im = Image.new("P", (1, 1))
    flat = []
    for c in PAL:
        flat += list(c)
    flat += [0] * (768 - len(flat))
    im.putpalette(flat)
    return im


def to_grid(img, dither):
    """img (RGB, W/cell x H/cell) -> list of rows of palette indices."""
    q = img.convert("RGB").quantize(palette=pal_image(), dither=Image.FLOYDSTEINBERG if dither else Image.NONE)
    a = np.array(q)
    return a.tolist()


def scene(w, h):
    """A synthetic photo-like picture: sky gradient, sun, hills, noise.  Drawn at w x h so a finer grid has more detail."""
    rng = np.random.default_rng(SEED)
    y, x = np.mgrid[0:h, 0:w].astype(float)
    y /= h
    x /= w
    sky = np.stack([0.25 + 0.5 * y, 0.45 + 0.4 * y, 0.95 - 0.2 * y], -1)
    d = np.hypot((x - 0.72) * 1.6, y - 0.28)
    sky += np.clip(0.55 - d * 3, 0, 1)[..., None] * np.array([0.9, 0.7, 0.1])
    hill1 = 0.55 + 0.08 * np.sin(x * 9) + 0.05 * np.sin(x * 23 + 1)
    hill2 = 0.68 + 0.06 * np.sin(x * 6 + 2) + 0.04 * np.sin(x * 31)
    img = sky
    m1 = (y > hill1)[..., None]
    img = np.where(m1, np.stack([0.20 + 0.2 * (y - hill1), 0.35 + 0.3 * (y - hill1), 0.18 + 0 * y], -1), img)
    m2 = (y > hill2)[..., None]
    img = np.where(m2, np.stack([0.10 + 0 * y, 0.22 + 0.4 * (y - hill2), 0.10 + 0 * y], -1), img)
    img = img + rng.normal(0, 0.035, img.shape)
    return Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8), "RGB")


def picture(pic, cell):
    gw, gh = W // cell, H // cell
    if pic == "photo":
        return to_grid(scene(gw, gh), True)
    if pic == "art":
        src = Image.open(ART).convert("RGB").crop((0, 0, 640, 640 * H // W))
        return to_grid(src.resize((gw, gh), Image.BOX), False)
    if pic == "check":
        return [[0 if (x + y) & 1 == 0 else 215 for x in range(gw)] for y in range(gh)]   # 0 = black, 215 = white (index of FFFFFF)
    if pic == "bars":
        return [[(x * 256 // gw) % 256 for x in range(gw)] for y in range(gh)]
    raise SystemExit("no such picture " + pic)


def runs(row):
    out, i = [], 0
    while i < len(row):
        j = i
        while j < len(row) and row[j] == row[i]:
            j += 1
        out.append((row[i], j - i))
        i = j
    return out


def img(i, w, h, extra=""):
    return "<IMG SRC=%s WIDTH=%d HEIGHT=%d%s>" % (name(i), w, h, extra)


def body(grid, cell, mode, rle, extra=""):
    out = []
    if mode == "tab":
        out.append("<TABLE BORDER=0 CELLPADDING=0 CELLSPACING=0>\n")
        for row in grid:
            cs = runs(row) if rle else [(c, 1) for c in row]
            out.append("<TR>" + "".join("<TD>" + img(c, n * cell, cell, extra) + "</TD>" for c, n in cs) + "</TR>\n")
        out.append("</TABLE>\n")
    else:
        for row in grid:
            cs = runs(row) if rle else [(c, 1) for c in row]
            out.append("".join(img(c, n * cell, cell, extra) for c, n in cs) + "<BR>\n")
    return "".join(out)


def count_tags(grid, rle):
    return sum(len(runs(r)) if rle else len(r) for r in grid)


def page(title, content, tag=None):
    return ("<HTML><HEAD><TITLE>%s</TITLE></HEAD>\n<BODY BGCOLOR=\"%s\">\n%s"
            "<IMG SRC=end.gif WIDTH=1 HEIGHT=1>\n</BODY></HTML>\n" % (title, BG, content))


def write(fn, s):
    with open(os.path.join(OUT, fn), "w", newline="") as f:
        f.write(s)


def q1_pages():
    chk = [[0 if (x + y) & 1 == 0 else 215 for x in range(8)] for y in range(8)]
    variants = {
        "row": ("row", ""), "row_top": ("row", " ALIGN=TOP"), "row_absmid": ("row", " ALIGN=ABSMIDDLE"),
        "row_absbot": ("row", " ALIGN=ABSBOTTOM"), "row_bottom": ("row", " ALIGN=BOTTOM"),
        "tab": ("tab", ""), "tab_top": ("tab", " ALIGN=TOP"), "tab_absmid": ("tab", " ALIGN=ABSMIDDLE"),
        "tab_absbot": ("tab", " ALIGN=ABSBOTTOM"),
    }
    for v, (mode, ex) in variants.items():
        write("q1_%s.html" % v, page("q1 " + v, body(chk, 16, mode, False, ex), "q1_" + v))
        write("q1_one_%s.html" % v, page("q1 one " + v, body(chk, 1, mode, False, ex), "q1_one_" + v))
    # with a tiny font: the strut of a line is the font's, so FONT SIZE=1 around a row may shrink the strip
    s = "<FONT SIZE=1>" + body(chk, 16, "row", False) + "</FONT>"
    write("q1_row_font1.html", page("q1 row font1", s, "q1_row_font1"))
    s = "<FONT SIZE=1>" + body(chk, 16, "tab", False) + "</FONT>"
    write("q1_tab_font1.html", page("q1 tab font1", s, "q1_tab_font1"))
    # the view's size: a magenta page with nothing in it
    write("q1_view.html", page("q1 view", "", "q1_view"))


def pal_page():
    rows = []
    for y in range(16):
        rows.append("<TR>" + "".join("<TD>" + img(y * 16 + x, 8, 8) + "</TD>" for x in range(16)) + "</TR>\n")
    write("pal.html", page("palette", "<TABLE BORDER=0 CELLPADDING=0 CELLSPACING=0>\n" + "".join(rows) + "</TABLE>\n", "pal"))


def pic_pages(stats):
    table = []
    for pic in ("photo", "art", "check", "bars"):
        for cell in CELLS:
            grid = picture(pic, cell)
            for mode in ("row", "tab"):
                for rle in (False, True):
                    n = "fb_%s_%d_%s%s" % (pic, cell, mode, "_rle" if rle else "")
                    b = body(grid, cell, mode, rle)
                    s = page(n, b, n)
                    table.append((n, len(grid[0]), len(grid), count_tags(grid, rle), len(s.encode())))
                    if not stats and (not FILTER or any(f in n for f in FILTER)):
                        write(n + ".html", s)
    return table


def js_string_var(var, text, piece=20000):
    """script blocks that build the string var from literals of at most `piece` bytes (a long literal lexes slowly in Netscape 2)."""
    out, first = [], True
    for k in range(0, len(text), piece):
        out.append("<SCRIPT LANGUAGE=\"JavaScript\">\n%s%s = \"%s\";\n</SCRIPT>\n" % ("var " if first else "", var, text[k:k + piece]) if first
                   else "<SCRIPT LANGUAGE=\"JavaScript\">\n%s = %s + \"%s\";\n</SCRIPT>\n" % (var, var, text[k:k + piece]))
        first = False
    return "".join(out) or "<SCRIPT LANGUAGE=\"JavaScript\">\nvar %s = \"\";\n</SCRIPT>\n" % var


def rewrite_pages(pic, cell, rle, tiles):
    """Q4.  rw_<pic>_<cell>[_rle].html: a frameset, a 26 pixel control frame (ctl) over the view; the control frame's script holds
    the picture's HTML in string variables P1 (written by go(1)) and writes the small page B (go(0)) over it.
    rwt_<pic>_<cell>_<n>.html: the view is n x n frames t0.., the picture is cut into the n x n parts, tile(j) rewrites part j only,
    and go(1) writes every part (the whole picture through n x n frames)."""
    grid = picture(pic, cell)
    gh, gw = len(grid), len(grid[0])
    blank = '<BODY BGCOLOR="%s"><P>blank' % BG
    scripts = ""
    if tiles == 1:
        scripts += js_string_var("P1", body(grid, cell, "row", rle).replace("\n", ""))
        links = '<A HREF="javascript:go(1)">picture</A> <A HREF="javascript:go(0)">blank</A>'
        fn = ("function go(k) {\n var d = parent.view.document;\n d.open(\"text/html\");\n"
              " if (k == 1) { d.write(\"<BODY BGCOLOR=\" + BGS + \">\" + P1); } else { d.write(BLANK); }\n d.close();\n}\n")
        frames = '<FRAME SRC="rwb.html" NAME=view>'
        name_ = "rw_%s_%d%s" % (pic, cell, "_rle" if rle else "")
    else:
        n = tiles
        tw, th = gw // n, gh // n
        parts = []
        for ty in range(n):
            for tx in range(n):
                sub = [r[tx * tw:(tx + 1) * tw] for r in grid[ty * th:(ty + 1) * th]]
                parts.append(body(sub, cell, "row", rle).replace("\n", ""))
        for j, t in enumerate(parts):
            scripts += js_string_var("P%d" % j, t)
        links = ('<A HREF="javascript:tile(0)">tile 0</A> <A HREF="javascript:tile(%d)">tile %d</A> <A HREF="javascript:go(1)">all</A> '
                 '<A HREF="javascript:go(0)">blank</A>' % (n * n // 2 + n // 2, n * n // 2 + n // 2))
        pn = ",".join("P%d" % j for j in range(n * n))
        fn = ("var NT = %d;\nfunction put(j, s) {\n var d = 0;\n if (j == 0) { d = parent.view.t0.document; }\n" % (n * n))
        for j in range(1, n * n):
            fn += " if (j == %d) { d = parent.view.t%d.document; }\n" % (j, j)
        fn += " d.open(\"text/html\");\n d.write(s);\n d.close();\n}\n"
        fn += "function tile(j) {\n"
        for j in range(n * n):
            fn += " if (j == %d) { put(%d, \"<BODY BGCOLOR=\" + BGS + \">\" + P%d); }\n" % (j, j, j)
        fn += "}\nfunction go(k) {\n var j = 0;\n while (j < NT) {\n  if (k == 1) { tile(j); } else { put(j, BLANK); }\n  j = j + 1;\n }\n}\n"
        rows = ",".join(["*"] * n)
        inner = "".join('<FRAME SRC="rwb.html" NAME=t%d>' % j for j in range(n * n))
        frames = "<FRAMESET ROWS=\"%s\" COLS=\"%s\">%s</FRAMESET>" % (rows, rows, inner)
        name_ = "rwt_%s_%d_%d%s" % (pic, cell, n, "_rle" if rle else "")
    # the view is its own frame set: view.t<j> is a frame of the nested set when tiles > 1
    ctl = ('<HTML><HEAD><TITLE>ctl</TITLE>\n<SCRIPT LANGUAGE="JavaScript">\nvar BGS = "#FF00FF";\nvar BLANK = "<BODY BGCOLOR=#FF00FF><P>blank";\n%s</SCRIPT>\n'
           '</HEAD><BODY>\n%s%s\n</BODY></HTML>\n') % (fn, scripts, links)
    # scripts of the picture are in the BODY of the control page: the functions are in the head and called later by a click
    write(name_ + "_ctl.html", ctl)
    write("rwb.html", '<HTML><HEAD><TITLE>b</TITLE></HEAD><BODY BGCOLOR="%s"><P>blank</BODY></HTML>\n' % BG)
    view = frames if tiles == 1 else '<FRAME SRC="%s_view.html" NAME=view>' % name_
    top = ('<HTML><HEAD><TITLE>%s</TITLE></HEAD>\n<FRAMESET ROWS="26,*">\n<FRAME SRC="%s_ctl.html" NAME=ctl>\n%s\n</FRAMESET></HTML>\n'
           % (name_, name_, view))
    write(name_ + ".html", top)
    if tiles > 1:
        write(name_ + "_view.html", "<HTML><HEAD><TITLE>v</TITLE></HEAD>\n%s\n</HTML>\n" % frames)
    return name_


FILTER = [a for a in sys.argv[1:] if not a.startswith("--")]   # substrings of the picture pages to write (default: all)


def main():
    stats = "--stats" in sys.argv
    os.makedirs(OUT, exist_ok=True)
    if not stats:
        with open(os.path.join(OUT, "end.gif"), "wb") as f:
            f.write(gif((255, 0, 255)))   # the end mark: a magenta pixel on the magenta page, so it shows nothing and the server's log has its time
        for i, c in enumerate(PAL):
            with open(os.path.join(OUT, name(i)), "wb") as f:
                f.write(gif(c))
        q1_pages()
        pal_page()
    if not stats:
        for spec in ("photo 8 0 1", "photo 4 0 1", "photo 16 0 1", "art 8 0 1", "art 8 1 1", "photo 8 0 2", "photo 8 0 4"):
            pic, cell, rle, tiles = spec.split()
            if not FILTER or any(f in "rw_" + spec.replace(" ", "_") for f in FILTER):
                rewrite_pages(pic, int(cell), rle == "1", int(tiles))
    t = pic_pages(stats)
    print("%-28s %5s %5s %8s %10s" % ("page", "cols", "rows", "tags", "bytes"))
    for r in t:
        print("%-28s %5d %5d %8d %10d" % r)


if __name__ == "__main__":
    main()
