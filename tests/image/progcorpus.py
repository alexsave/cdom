#!/usr/bin/env python3
"""Progressive JPEGs for the progressive decoder's differential test (tests/image/prog.sh, docs/image-progressive-speed.md).

Usage: python3 -I tests/image/progcorpus.py OUT_DIR

Writes OUT_DIR/*.jpg and OUT_DIR/list.txt (one file name a line). The files: seeded pictures (gradients, edges and
noise) at odd and even sizes, in grey, 4:4:4, 4:2:0, 4:2:2, 4:4:0 and 4:1:1, through cjpeg with scan scripts of every
kind (spectral selection only, successive approximation, deep successive approximation, a scan per coefficient,
refinement bands that differ from the first bands, the standard script), with and without restart intervals and
optimized tables; jpegtran's progressive transcode of a baseline file; files of 500 and 501 scans (the cap, and one
over); a scan repeated (a bogus progression, which libjpeg takes with a warning); and cut and corrupted copies: the
file cut inside scans of each kind, and bytes of the entropy-coded data changed (bad Huffman codes, stray markers).
One random source: rng = random.Random(SEED). cjpeg and jpegtran from $CJPEG and $JPEGTRAN. Test tooling, never shipped.
"""
import os, random, subprocess, sys
import numpy as np

SEED = 20261120
OUT = os.path.abspath(sys.argv[1])
os.makedirs(OUT, exist_ok=True)
rng = random.Random(SEED)
CJPEG = os.environ.get("CJPEG", "/opt/homebrew/bin/cjpeg")
JPEGTRAN = os.environ.get("JPEGTRAN", "/opt/homebrew/bin/jpegtran")
names = []

def picture(w, h, grey):
    """Gradients, a few hard-edged boxes and noise of a random strength: every band of the DCT has something in it."""
    y, x = np.mgrid[0:h, 0:w].astype(np.float64)
    c = 3 if not grey else 1
    img = np.zeros((h, w, c))
    for ch in range(c):
        a, b, f = rng.random(), rng.random(), 0.02 + 0.2 * rng.random()
        img[:, :, ch] = 128 + 90 * np.sin(x * f * a + y * f * b + ch)
    for _ in range(4):
        x0, y0 = rng.randrange(w), rng.randrange(h)
        img[y0:y0 + 1 + rng.randrange(max(1, h // 3)), x0:x0 + 1 + rng.randrange(max(1, w // 3)), :] = rng.randrange(256)
    amp = rng.choice([0, 3, 12, 40])
    noise = np.frombuffer(rng.randbytes(w * h * c), np.uint8).reshape(h, w, c).astype(np.float64)
    img += (noise - 127.5) * amp / 64
    return np.clip(img, 0, 255).astype(np.uint8)

def write_pnm(path, a):
    h, w, c = a.shape
    with open(path, "wb") as f:
        f.write(b"P6" if c == 3 else b"P5")
        f.write(f"\n{w} {h}\n255\n".encode())
        f.write(a.tobytes())

# Scan scripts in cjpeg's -scans syntax, for three components (Y, Cb, Cr) and for one (grey).
def script_spectral(n):
    if n == 1: return "0: 0-0, 0, 0; 0: 1-2, 0, 0; 0: 3-9, 0, 0; 0: 10-63, 0, 0;"
    return "0,1,2: 0-0, 0, 0; 0: 1-5, 0, 0; 2: 1-63, 0, 0; 1: 1-63, 0, 0; 0: 6-20, 0, 0; 0: 21-63, 0, 0;"

def script_deep(n):
    comps = range(n)
    dc = ",".join(str(c) for c in comps)
    s = [f"{dc}: 0-0, 0, 3;"] + [f"{c}: 1-63, 0, 4;" for c in comps]
    for al in (2, 1, 0): s.append(f"{dc}: 0-0, {al + 1}, {al};")
    for c in comps:
        for al in (3, 2, 1, 0): s.append(f"{c}: 1-63, {al + 1}, {al};")
    return " ".join(s)

def script_split(n):
    """Refinement bands other than the first bands, and the bands in an order other than the natural one."""
    s = [",".join(str(c) for c in range(n)) + ": 0-0, 0, 1;"]
    for c in range(n):
        s += [f"{c}: 10-63, 0, 2;", f"{c}: 1-9, 0, 2;", f"{c}: 1-30, 2, 1;", f"{c}: 31-63, 2, 1;"]
    s.append(",".join(str(c) for c in range(n)) + ": 0-0, 1, 0;")
    for c in range(n): s += [f"{c}: 40-63, 1, 0;", f"{c}: 1-39, 1, 0;"]
    return " ".join(s)

def script_each(n):
    """A scan for every AC coefficient of the first component, with one bit held back, then one refinement per band."""
    s = [",".join(str(c) for c in range(n)) + ": 0-0, 0, 0;"]
    s += [f"0: {k}-{k}, 0, 1;" for k in range(1, 64)]
    s += [f"{c}: 1-63, 0, 0;" for c in range(1, n)]
    s += ["0: 1-31, 1, 0;", "0: 32-63, 1, 0;"]
    return " ".join(s)

def script_high(n):
    """Bands that a small IDCT never reads (zigzag 13 and up is outside 3x3, 21 and up outside 1x1's DC only)."""
    s = [",".join(str(c) for c in range(n)) + ": 0-0, 0, 0;"]
    for c in range(n): s += [f"{c}: 1-2, 0, 1;", f"{c}: 3-12, 0, 1;", f"{c}: 13-63, 0, 1;"]
    for c in range(n): s += [f"{c}: 13-63, 1, 0;", f"{c}: 1-12, 1, 0;"]
    return " ".join(s)

SCRIPTS = {"spectral": script_spectral, "deep": script_deep, "split": script_split, "each": script_each, "high": script_high}

def cjpeg(src, dst, args):
    subprocess.run([CJPEG] + args + ["-outfile", dst, src], check=True)

def add(name):
    names.append(name)
    return os.path.join(OUT, name)

def scans_of(d):
    """[(start, end)] of each scan, from its SOS marker to the marker after its data."""
    out, i = [], 2
    while i + 4 <= len(d):
        if d[i] != 0xff: i += 1; continue
        m = d[i + 1]
        if m == 0xd9: break
        if m in (0x00, 0xff) or 0xd0 <= m <= 0xd7: i += 1; continue
        n = d[i + 2] << 8 | d[i + 3]
        if m == 0xda:
            j = i + 2 + n
            while j + 1 < len(d) and not (d[j] == 0xff and d[j + 1] not in (0, 0xff) and not 0xd0 <= d[j + 1] <= 0xd7): j += 1
            out.append((i, j)); i = j; continue
        i += 2 + n
    return out

def variants(path, cuts, corrupts):
    """Cut and corrupted copies of the file at path."""
    d = open(path, "rb").read()
    sc = scans_of(d)
    base = os.path.basename(path)[:-4]
    for k in range(cuts):
        s, e = sc[rng.randrange(len(sc))]
        at = rng.randrange(s + 1, e) if e > s + 1 else e
        open(add(f"{base}-cut{k}.jpg"), "wb").write(d[:at])
    for k in range(corrupts):
        b = bytearray(d)
        for _ in range(1 + rng.randrange(3)):
            s, e = sc[rng.randrange(len(sc))]
            hdr = 2 + (b[s + 2] << 8 | b[s + 3])
            if e - s - hdr < 2: continue
            at = s + hdr + rng.randrange(e - s - hdr)
            b[at] = rng.choice([rng.randrange(256), 0xff, b[at] ^ (1 << rng.randrange(8))])
        open(add(f"{base}-bad{k}.jpg"), "wb").write(bytes(b))

SIZES = [(203, 137), (64, 48), (17, 9), (1, 1), (8, 8), (333, 517), (161, 23)]
SAMPLES = [("grey", None), ("444", "1x1"), ("420", "2x2"), ("422", "2x1"), ("440", "1x2"), ("411", "4x1")]

def seg(m, data=b"", length=None):
    """A marker segment: 0xFF, the marker, the length field (len(data) + 2 unless given), the data."""
    n = len(data) + 2 if length is None else length
    return bytes([0xff, m, n >> 8, n & 0xff]) + data

# What goes between two scans, as libjpeg's read_markers takes it (jdmarker.c): segments it skips by their length
# (APPn and COM, skip_variable and get_interesting_appn, and DNL and DAC, which this build skips too), the zero and
# one lengths it skips nothing for, tables it reads (DHT, DQT, DRI: valid ones, and short ones it refuses), the
# parameterless markers (RSTn, TEM), fill bytes, stray bytes, and markers it refuses.
BETWEEN = {
    "com0": seg(0xfe, length=0), "com1": seg(0xfe, length=1), "com2": seg(0xfe), "com-text": seg(0xfe, b"cdom"),
    "com-ff": seg(0xfe, b"\xff\xda\x00\x08\x01\x01\x00\x00\x3f\x00\xff\xd9\xff"),   # marker-shaped bytes inside
    "app0-0": seg(0xe0, length=0), "app0-1": seg(0xe0, length=1), "app1-0": seg(0xe1, length=0),
    "app1-1": seg(0xe1, length=1), "app14-0": seg(0xee, length=0), "app15-1": seg(0xef, length=1),
    "app0-jfif": seg(0xe0, b"JFIF\x00\x01\x02\x00\x00\x01\x00\x01\x00\x00"), "app14-short": seg(0xee, b"Adob"),
    "app14-adobe": seg(0xee, b"Adobe\x00\x64\x00\x00\x00\x00\x01"), "app2-long": seg(0xe2, bytes(range(256)) * 3),
    "dnl": seg(0xdc, b"\x00\x10"), "dnl0": seg(0xdc, length=0), "dac0": seg(0xcc, length=0), "dac": seg(0xcc, b"\x01\x10"),
    "dri0": seg(0xdd, b"\x00\x00"), "dri5": seg(0xdd, b"\x00\x05"), "dri-len5": seg(0xdd, b"\x00\x05\x00"),
    "dri-len0": seg(0xdd, length=0), "dht2": seg(0xc4), "dht0": seg(0xc4, length=0), "dht1": seg(0xc4, length=1),
    "dht-short": seg(0xc4, b"\x10" + bytes(15)), "dht-index": seg(0xc4, b"\x05" + b"\x01" + bytes(15) + b"\x00"),
    "dqt2": seg(0xdb), "dqt0": seg(0xdb, length=0), "dqt-index": seg(0xdb, b"\x04" + bytes(range(1, 65))),
    "dqt-short": seg(0xdb, b"\x01" + bytes(range(1, 64))),
    "rst3": b"\xff\xd3", "tem": b"\xff\x01", "fill": b"\xff\xff\xff\xff", "stray": b"\x12\x34\xff\x00\x56",
    # right after a scan's data, bytes are taken as more of the data (the walk's scan_end, libjpeg's entropy decoder);
    # after a segment, they are next_marker's to skip: stray bytes, 0xFF 0x00, fill bytes, RSTn and TEM there
    "com-stray": seg(0xfe, b"x") + b"\x12\x34", "com-stuffed": seg(0xfe, b"x") + b"\x12\xff\x00\x34\xff\xff\x00",
    "com-fill": seg(0xfe, b"x") + b"\xff\xff\xff", "com-rst": seg(0xfe, b"x") + b"\xff\xd5\x77", "com-tem": seg(0xfe, b"x") + b"\xff\x01",
    "sof": seg(0xc0, b"\x08\x00\x10\x00\x10\x01\x01\x11\x00"), "soi": b"\xff\xd8", "jpg0": seg(0xf0, b"\x00"),
    "res": seg(0x02, b"\x00"),
}

REFUSED = {"dri-len5", "dri-len0", "dht0", "dht1", "dht-short", "dht-index", "dqt0", "dqt-index", "dqt-short", "sof", "soi",
           "jpg0", "res"}

def segments():
    """Progressive files with what BETWEEN holds put after a scan's data, one kind a file (after the first scan, as
    the walk's first step, and after a middle one), all the ones libjpeg takes after every scan, and segments that end
    exactly at the end of the file or run past it."""
    bases = []
    for (w, h, samp, extra) in [(80, 56, "2x2", []), (40, 24, None, []), (48, 32, "1x1", ["-restart", "1"])]:
        src = os.path.join(OUT, "src.pnm")
        write_pnm(src, picture(w, h, samp is None))
        p = os.path.join(OUT, "base.jpg")
        cjpeg(src, p, ["-quality", "85", "-progressive"] + (["-grayscale"] if samp is None else ["-sample", samp]) + extra)
        bases.append((f"{'grey' if samp is None else samp.replace('x', '')}{'-rst' if extra else ''}", open(p, "rb").read()))
    for (bname, d) in bases:
        sc = scans_of(d)
        ends = [e for (s, e) in sc]
        # "-follow" in a name: libjpeg reads every marker of the file without an error, so the walk must not give up
        # on it (tests/image/prog.c)
        for name, b in BETWEEN.items():
            follow = "" if name in REFUSED else "-follow"
            for k in (0, len(sc) // 2):
                open(add(f"seg-{bname}-{name}-after{k}{follow}.jpg"), "wb").write(d[:ends[k]] + b + d[ends[k]:])
        # every kind libjpeg takes without an error, after every scan
        ok = [b for name, b in BETWEEN.items() if name not in REFUSED]
        # a later scan libjpeg stops at: a refused band (Ah 3, Al 1), which the walk models, and Huffman tables never
        # defined (3 and 3), which it does not (libjpeg's list is then a prefix of the walk's)
        s1 = sc[len(sc) // 2][0]
        nc = d[s1 + 4]
        for name, at, v in [("badal", s1 + 5 + 2 * nc + 2, 0x31), ("notable", s1 + 6, 0x33)]:
            open(add(f"seg-{bname}-{name}.jpg"), "wb").write(d[:at] + bytes([v]) + d[at + 1:])
        out, at = bytearray(), 0
        for k, e in enumerate(ends[:-1]):
            out += d[at:e] + ok[k % len(ok)] + ok[(k + 7) % len(ok)]
            at = e
        out += d[at:]
        open(add(f"seg-{bname}-many-follow.jpg"), "wb").write(bytes(out))
        # the file ending at a segment's end, inside one, and with a zero length as its last bytes
        cut = d[:ends[0]]
        for name, tail in [("end-com", seg(0xfe, b"xy")), ("end-com0", seg(0xfe, length=0)), ("end-app1", seg(0xe1, length=1)),
                           ("end-dht2", seg(0xc4)), ("end-dri", seg(0xdd, b"\x00\x00")), ("end-dri-cut", seg(0xdd, b"\x00\x00")[:5]),
                           ("end-dqt-cut", seg(0xdb, b"\x00" + bytes(64))[:30]), ("end-dqt-len", seg(0xdb, b"\x00" + bytes(64))[:4]), ("end-dht-cut", seg(0xc4, b"\x10" + b"\x01" + bytes(15) + b"\x00")[:9]),
                           ("end-com-past", seg(0xfe, b"abc", length=40)), ("end-len-cut", b"\xff\xfe\x00"), ("end-marker", b"\xff\xfe"),
                           ("end-sos-cut", d[sc[1][0]:sc[1][0] + 5]), ("end-fill", b"\xff\xff")]:
            open(add(f"seg-{bname}-{name}.jpg"), "wb").write(cut + tail)

def shapes():
    """Wide images (over 4000 pixels, and one of 65,500 a row: 8,188 blocks), 3x1 and 1x3 sampling, grey at odd sizes."""
    for (w, h, samp, q) in [(4100, 24, "2x2", 75), (4001, 9, "1x1", 90), (65500, 8, None, 75), (9000, 17, "2x1", 60),
                            (203, 137, "3x1", 85), (137, 203, "1x3", 85), (61, 7, "3x1", 50), (7, 61, "1x3", 98),
                            (1, 7, None, 90), (7, 1, None, 90), (13, 29, None, 50), (255, 3, None, 98), (9, 9, None, 75), (127, 129, None, 85)]:
        src = os.path.join(OUT, "src.pnm")
        write_pnm(src, picture(w, h, samp is None))
        sname = "grey" if samp is None else samp.replace("x", "")
        base = ["-quality", str(q)] + (["-grayscale"] if samp is None else ["-sample", f"{samp},1x1,1x1"])
        for extra, tag in [([], "std"), (["-restart", "1"], "rst1")]:
            cjpeg(src, add(f"shape-{sname}-{w}x{h}-{tag}.jpg"), base + ["-progressive"] + extra)
        sp = os.path.join(OUT, "script.txt")
        open(sp, "w").write(script_split(1 if samp is None else 3))
        cjpeg(src, add(f"shape-{sname}-{w}x{h}-split.jpg"), base + ["-scans", sp])

def main():
    k = 0
    for (w, h) in SIZES:
        for (sname, samp) in SAMPLES:
            grey = samp is None
            src = os.path.join(OUT, "src.pnm")
            write_pnm(src, picture(w, h, grey))
            ncomp = 1 if grey else 3
            base = ["-quality", str(rng.choice([50, 75, 90, 98]))] + (["-grayscale"] if grey else ["-sample", samp])
            # the standard script, optimized or not, with and without restarts
            for extra, tag in [([], "std"), (["-optimize"], "opt"), (["-restart", "1"], "rst1"), (["-restart", "3B"], "rst3b")]:
                p = add(f"{sname}-{w}x{h}-{tag}.jpg")
                cjpeg(src, p, base + ["-progressive"] + extra)
                if tag == "std" and w * h >= 64: variants(p, 2, 2)
                if tag == "rst3b" and w * h >= 64: variants(p, 1, 2)
            for scname, fn in SCRIPTS.items():
                sp = os.path.join(OUT, "script.txt")
                open(sp, "w").write(fn(ncomp))
                extra = rng.choice([[], ["-optimize"], ["-restart", "2B"]])
                p = add(f"{sname}-{w}x{h}-{scname}.jpg")
                cjpeg(src, p, base + ["-scans", sp] + extra)
                if w * h >= 1000 and k % 3 == 0: variants(p, 1, 1)
                k += 1
    # jpegtran: a baseline file made progressive (the coefficients unchanged), and the scan cap
    src = os.path.join(OUT, "src.pnm")
    write_pnm(src, picture(400, 300, False))
    cjpeg(src, os.path.join(OUT, "base.jpg"), ["-quality", "85"])
    p = add("jpegtran-prog.jpg")
    subprocess.run([JPEGTRAN, "-progressive", "-outfile", p, os.path.join(OUT, "base.jpg")], check=True)
    d = open(p, "rb").read()
    sc = scans_of(d)
    # each scan with the tables written before it (jpegtran writes a DHT before every scan)
    head, tail = d[:sc[0][0]], d[sc[-1][1]:]
    body = [d[(sc[k - 1][1] if k else sc[0][0]):e] for k, (s, e) in enumerate(sc)]
    for total in (500, 501):
        open(add(f"scans{total}.jpg"), "wb").write(head + b"".join(body) + body[-1] * (total - len(body)) + tail)
    open(add("repeated-scan.jpg"), "wb").write(head + b"".join(body[:5]) + body[3] + b"".join(body[5:]) + tail)
    open(add("no-eoi.jpg"), "wb").write(head + b"".join(body))
    open(add("dc-only.jpg"), "wb").write(head + body[0] + tail)
    segments()
    shapes()
    for f in ("src.pnm", "script.txt", "base.jpg"):
        os.remove(os.path.join(OUT, f))
    open(os.path.join(OUT, "list.txt"), "w").write("".join(n + "\n" for n in names))
    print(f"progcorpus: {len(names)} files in {OUT}")

if __name__ == "__main__":
    main()
