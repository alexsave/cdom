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
    for f in ("src.pnm", "script.txt", "base.jpg"):
        os.remove(os.path.join(OUT, f))
    open(os.path.join(OUT, "list.txt"), "w").write("".join(n + "\n" for n in names))
    print(f"progcorpus: {len(names)} files in {OUT}")

if __name__ == "__main__":
    main()
