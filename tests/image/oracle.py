#!/usr/bin/env python3
"""The image module's test oracle (tests/image/run.sh): makes the inputs and the exact expected outputs.

Usage: python3 -I tests/image/oracle.py OUT_DIR

Writes OUT_DIR/inputs/*, OUT_DIR/expect/*.rgba and two lists that tests/image/test_image.c reads:
  cases.tsv   name, input, box width, box height, expected (a .rgba file, or STATUS:<name>), width, height, peak KB
  scaler.tsv  name, raw RGBA rows, source w h, scaled w h, orientation, expected .rgba, output w h

Every expected image is EXACT, in integers: the area average of the pixels the code under test is given, rounded to
nearest with halves up, (2 * sum + total) // (2 * total), with the orientation applied. The code under test must
match it to the byte (a difference of 0):
  - a PNG: its pixels, composited on white as the C code rounds it, ((c * a + 255 * (255 - a) + 127) // 255);
  - a JPEG: libjpeg-turbo's own decode at the scale the C code picks, by Homebrew's djpeg (the same release, 3.2.1,
    with SIMD off: JSIMD_FORCENONE=1, so it runs the C code cdom builds), -dct int, fancy upsampling.
Inputs no library writes are made here: our own PNG writer (every colour type, depth and filter, colour keys, a
palette shorter than its indexes, a broken checksum), cjpeg (4:4:0 and 4:1:1), jpegtran scan scripts (a baseline
JPEG with one scan per component; progressive files with too many scans), and segments spliced by hand (XMP before
EXIF). One random source: rng = random.Random(SEED); the big pictures use bench/image/inputs.py's scene and grain
with this same rng. Test tooling, never shipped.
"""
import os, random, struct, subprocess, sys, zlib
import numpy as np
from PIL import Image

SEED = 20261119
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "bench", "image"))
import inputs as bench   # the bench's scene and grain (bench/image/inputs.py), drawn from this file's rng

OUT = os.path.abspath(sys.argv[1])
IN, EX = os.path.join(OUT, "inputs"), os.path.join(OUT, "expect")
os.makedirs(IN, exist_ok=True)
os.makedirs(EX, exist_ok=True)
rng = random.Random(SEED)
DJPEG = os.environ.get("DJPEG", "/opt/homebrew/bin/djpeg")
CJPEG = os.environ.get("CJPEG", "/opt/homebrew/bin/cjpeg")
JPEGTRAN = os.environ.get("JPEGTRAN", "/opt/homebrew/bin/jpegtran")
f = lambda n: os.path.join(IN, n)

def noise(shape):
    """Bytes of the rng as an array (test inputs are small, so the draws are few)."""
    n = int(np.prod(shape))
    return np.frombuffer(rng.randbytes(n), np.uint8).reshape(shape)

# ---------- the exact area average, in integers (src/cdom_image.c, the scaler) ----------

def weights(n_out, n_in):
    """W[i, j]: the overlap of output pixel i, [i * n_in, (i + 1) * n_in), with input pixel j, [j * n_out, (j + 1) * n_out)."""
    W = np.zeros((n_out, n_in), np.int64)
    for i in range(n_out):
        a, b = i * n_in, (i + 1) * n_in
        for j in range(a // n_out, min(n_in, -(-b // n_out))):
            W[i, j] = min(b, (j + 1) * n_out) - max(a, j * n_out)
    return W

def area_avg(a, dw, dh):
    """a: h x w x 3 uint8 -> dh x dw x 3 uint8, the exact area average rounded to nearest, halves up."""
    h, w = a.shape[:2]
    Wv, Wh = weights(dh, h), weights(dw, w)
    t = w * h
    out = np.empty((dh, dw, 3), np.uint8)
    for k in range(3):
        s = Wv @ a[:, :, k].astype(np.int64) @ Wh.T
        out[:, :, k] = np.minimum((2 * s + t) // (2 * t), 255)
    return out

def fit_box(w, h, bw, bh):
    """cdom_image.c's fit_box: the largest size inside bw x bh with w x h's aspect, never larger."""
    if w <= bw and h <= bh: return w, h
    if w * bh >= h * bw: return bw, max(1, (h * bw * 2 + w) // (w * 2))
    return max(1, (w * bh * 2 + h) // (h * 2)), bh

def target(w, h, o, bw, bh):
    """The scaled size as stored, for a box of the shown image."""
    return fit_box(w, h, bh, bw) if o >= 5 else fit_box(w, h, bw, bh)

def orient(a, o):
    return {1: lambda x: x, 2: lambda x: x[:, ::-1], 3: lambda x: x[::-1, ::-1], 4: lambda x: x[::-1],
            5: lambda x: x.transpose(1, 0, 2), 6: lambda x: x.transpose(1, 0, 2)[:, ::-1],
            7: lambda x: x.transpose(1, 0, 2)[::-1, ::-1], 8: lambda x: x.transpose(1, 0, 2)[::-1]}[o](a)

def over_white(rgb, a):
    c, a = rgb.astype(np.uint32), a.astype(np.uint32)[..., None]
    return ((c * a + 255 * (255 - a) + 127) // 255).astype(np.uint8)

cases, scalers = [], []

def save_expect(name, rgb):
    path = os.path.join(EX, name + ".rgba")
    np.concatenate([rgb, np.full(rgb.shape[:2] + (1,), 255, np.uint8)], axis=2).tofile(path)
    return path, rgb.shape[1], rgb.shape[0]

def case(name, path, bw, bh, rgb, maxkb=0):
    p, w, h = save_expect(name, np.ascontiguousarray(rgb))
    cases.append((name, path, bw, bh, p, w, h, maxkb))

def status_case(name, path, bw, bh, status):
    cases.append((name, path, bw, bh, "STATUS:" + status, 0, 0, 0))

# ---------- pictures ----------

def picture(w, h):
    """Smooth gradients, a hard-edged block, a stripe pattern and noise."""
    y, x = np.mgrid[0:h, 0:w].astype(np.float64)
    img = np.stack([127 + 120 * np.sin(x / w * 7 + y / h * 3), 127 + 120 * np.cos(y / h * 5 - x / w * 2),
                    127 + 120 * np.sin((x + y) / (w + h) * 11)], axis=2)
    img[h // 4: h // 2, w // 5: w // 2] = [250, 20, 20]
    img[h // 2: 3 * h // 4, w // 2: 4 * w // 5] = ((np.arange(w // 2, 4 * w // 5) % 3) * 120)[None, :, None]
    img += noise((h, w, 3)).astype(np.float64) / 32 - 4
    return np.clip(img, 0, 255).astype(np.uint8)

def big_picture(w, h):
    """The bench's scene and grain at w x h, from this file's rng."""
    return np.asarray(bench.with_grain(bench.render(bench.make_scene(rng), w, h), bench.grain_tile(rng)))

# ---------- PNG: our own writer, so filters, keys and broken streams are chosen ----------

def chunk(t, d):
    return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))

def paeth(a, b, c):
    a, b, c = a.astype(np.int32), b.astype(np.int32), c.astype(np.int32)
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    return np.where((pa <= pb) & (pa <= pc), a, np.where(pb <= pc, b, c))

def filtered(rows, bpp):
    """Row i gets filter i % 5, so every filter is used, the first row's included."""
    out = bytearray()
    prev = np.zeros_like(rows[0])
    for i, cur in enumerate(rows):
        fl = i % 5
        left = np.concatenate([np.zeros(bpp, np.uint8), cur[:-bpp]]) if len(cur) > bpp else np.zeros_like(cur)
        ul = np.concatenate([np.zeros(bpp, np.uint8), prev[:-bpp]]) if len(cur) > bpp else np.zeros_like(cur)
        if fl == 0: r = cur
        elif fl == 1: r = cur - left
        elif fl == 2: r = cur - prev
        elif fl == 3: r = cur - ((left.astype(np.int32) + prev) >> 1).astype(np.uint8)
        else: r = (cur.astype(np.int32) - paeth(left, prev, ul)).astype(np.uint8)
        out += bytes([fl]) + r.astype(np.uint8).tobytes()
        prev = cur
    return bytes(out)

def png_bytes(w, h, ctype, depth, rows, plte=None, trns=None, idat_split=None, zdata=None):
    bpp = max(1, {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype] * depth // 8)
    z = zdata if zdata is not None else zlib.compress(filtered(rows, bpp), 6)
    d = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, depth, ctype, 0, 0, 0))
    if plte is not None: d += chunk(b"PLTE", bytes(plte))
    if trns is not None: d += chunk(b"tRNS", bytes(trns))
    d += chunk(b"tEXt", b"Comment\0between the chunks")
    for i in range(0, len(z), idat_split or len(z) or 1): d += chunk(b"IDAT", z[i:i + (idat_split or len(z))])
    return d + chunk(b"IEND", b"")

def write_png(name, *a, **k):
    open(f(name), "wb").write(png_bytes(*a, **k))
    return f(name)

def pack_bits(vals, depth):
    h, w = vals.shape
    per = 8 // depth
    rows = []
    for r in vals:
        v = np.concatenate([r, np.zeros((-w) % per, np.uint8)]).reshape(-1, per).astype(np.uint32)
        b = np.zeros(v.shape[0], np.uint32)
        for k in range(per): b |= v[:, k] << (8 - depth - depth * k)
        rows.append(b.astype(np.uint8))
    return rows

def png_cases():
    W, H = 1203, 802            # odd sizes: no row or column ends where an output pixel does
    pic = picture(W, H)
    alpha = (np.arange(W)[None, :] * 255 // W).repeat(H, 0).astype(np.uint8)
    alpha[:60] = 0; alpha[-60:] = 255
    grey = pic[:, :, 1]
    def exp(rgb, bw=300, bh=300): return area_avg(rgb, *fit_box(W, H, bw, bh))
    case("png-rgb", write_png("rgb.png", W, H, 2, 8, [r.reshape(-1) for r in pic]), 300, 300, exp(pic))
    case("png-rgb-tall-box", f("rgb.png"), 500, 120, exp(pic, 500, 120))   # the height binds
    rgba = np.concatenate([pic, alpha[..., None]], axis=2)
    case("png-rgba", write_png("rgba.png", W, H, 6, 8, [r.reshape(-1) for r in rgba], idat_split=997), 300, 300,
         exp(over_white(pic, alpha)))
    case("png-grey", write_png("grey.png", W, H, 0, 8, [r for r in grey]), 300, 300, exp(np.stack([grey] * 3, 2)))
    ga = np.stack([grey, alpha], axis=2)
    case("png-grey-alpha", write_png("greya.png", W, H, 4, 8, [r.reshape(-1) for r in ga]), 300, 300,
         exp(over_white(np.stack([grey] * 3, 2), alpha)))
    for d in (1, 2, 4):
        v = (grey >> (8 - d)).astype(np.uint8)
        gv = (v.astype(np.uint32) * 255 // ((1 << d) - 1)).astype(np.uint8)
        case(f"png-grey{d}", write_png(f"grey{d}.png", W, H, 0, d, pack_bits(v, d)), 300, 300, exp(np.stack([gv] * 3, 2)))
    for d in (1, 2, 4, 8):
        n = 1 << d
        pal = noise((n, 3))
        tr = noise((n,)).copy(); tr[0] = 0; tr[-1] = 255
        idx = ((np.arange(W)[None, :] // 7 + np.arange(H)[:, None] // 5) % n).astype(np.uint8)
        rows = [r for r in idx] if d == 8 else pack_bits(idx, d)
        short = tr[: n - n // 4]                                   # a short tRNS: the rest opaque
        full = np.concatenate([short, np.full(n // 4, 255, np.uint8)])
        case(f"png-palette{d}", write_png(f"pal{d}.png", W, H, 3, d, rows, plte=pal.reshape(-1), trns=short), 300, 300,
             exp(over_white(pal[idx], full[idx])))
    # a palette of 3 entries, indexes 0 to 7: the indexes past PLTE are opaque black (as libpng's)
    pal3 = noise((3, 3))
    idx = ((np.arange(W)[None, :] // 9 + np.arange(H)[:, None] // 11) % 8).astype(np.uint8)
    full = np.concatenate([pal3, np.zeros((5, 3), np.uint8)])
    case("png-palette-index-past-plte", write_png("palpast.png", W, H, 3, 4, pack_bits(idx, 4), plte=pal3.reshape(-1)),
         300, 300, exp(full[idx]))
    # colour keys (tRNS for grey and RGB): a pixel of the key value is transparent, so white
    key = int(grey[H // 3, W // 3])
    g = np.where(grey == key, 255, grey).astype(np.uint8)
    case("png-grey-key", write_png("greykey.png", W, H, 0, 8, [r for r in grey], trns=struct.pack(">H", key)), 300, 300,
         exp(np.stack([g] * 3, 2)))
    v2 = (grey >> 6).astype(np.uint8)
    g2 = np.where(v2 == 2, 255, v2.astype(np.uint32) * 255 // 3).astype(np.uint8)
    case("png-grey2-key", write_png("grey2key.png", W, H, 0, 2, pack_bits(v2, 2), trns=struct.pack(">H", 2)), 300, 300,
         exp(np.stack([g2] * 3, 2)))
    q = (pic // 64 * 64).astype(np.uint8)                          # few colours, so the key covers many pixels
    k = q[H // 2, W // 3]
    hit = np.all(q == k, axis=2)
    case("png-rgb-key", write_png("rgbkey.png", W, H, 2, 8, [r.reshape(-1) for r in q],
                                  trns=struct.pack(">HHH", *[int(c) for c in k])), 300, 300,
         exp(np.where(hit[..., None], 255, q).astype(np.uint8)))
    case("png-rgb-key-over-255", write_png("rgbkey16.png", W, H, 2, 8, [r.reshape(-1) for r in q],
                                           trns=struct.pack(">HHH", 256 + int(k[0]), int(k[1]), int(k[2]))), 300, 300, exp(q))
    # the zlib stream's adler32: one wrong byte, and the checksum cut off
    z = bytearray(zlib.compress(filtered([r.reshape(-1) for r in pic], 3), 6))
    z[-1] ^= 0x5a
    status_case("png-adler-mismatch", write_png("adler.png", W, H, 2, 8, None, zdata=bytes(z)), 300, 300, "CORRUPT")
    status_case("png-adler-missing", write_png("noadler.png", W, H, 2, 8, None, zdata=bytes(z[:-4])), 300, 300, "CORRUPT")
    # a filter byte of 9 (none exists past 4), and a whole, well-formed stream of half the rows
    rows = [r.reshape(-1) for r in pic]
    raw = bytearray(filtered(rows, 3))
    raw[(W * 3 + 1) * 7] = 9
    status_case("png-filter9", write_png("filter9.png", W, H, 2, 8, None, zdata=zlib.compress(bytes(raw), 6)), 300, 300, "CORRUPT")
    status_case("png-short-rows", write_png("shortrows.png", W, H, 2, 8, None, zdata=zlib.compress(filtered(rows[:H // 2], 3), 6)),
                300, 300, "CORRUPT")
    # sizes: smaller than the box (never enlarged), thin, tall, one pixel, the box at its largest
    for (w, h, bw, bh) in [(200, 100, 300, 300), (700, 1, 256, 256), (1, 700, 256, 256), (1024, 768, 256, 256),
                           (1, 1, 256, 256), (301, 7, 100, 100), (640, 480, 8192, 8192), (640, 480, 1, 1)]:
        p = picture(max(w, 8), max(h, 8))[:h, :w]
        case(f"png-size{w}x{h}-box{bw}x{bh}", write_png(f"size{w}x{h}-box{bw}x{bh}.png", w, h, 2, 8, [r.reshape(-1) for r in p]),
             bw, bh, area_avg(p, *fit_box(w, h, bw, bh)))
    for (bw, bh) in [(0, 300), (300, 0), (8193, 300), (300, 8193)]:
        status_case(f"png-box{bw}x{bh}", f("rgb.png"), bw, bh, "BAD_BOX")

# ---------- JPEG ----------

def pick_num(w, h, dw, dh):
    for n in range(1, 8):
        if -(-w * n // 8) >= dw and -(-h * n // 8) >= dh: return n
    return 8

def djpeg(path, n):
    env = dict(os.environ, JSIMD_FORCENONE="1")
    raw = subprocess.run([DJPEG, "-scale", f"{n}/8", "-dct", "int", "-pnm", path], env=env, capture_output=True, check=True).stdout
    im = Image.open(__import__("io").BytesIO(raw))
    return np.asarray(im.convert("RGB"))

def exif_bytes(o):
    ex = Image.Exif(); ex[0x0112] = o
    return ex.tobytes()

def jpeg_case(name, path, bw, bh, o=1, maxkb=0):
    w, h = Image.open(path).size
    dw, dh = target(w, h, o, bw, bh)
    red = djpeg(path, pick_num(w, h, dw, dh))
    assert red.shape[1] >= dw and red.shape[0] >= dh, name
    case(name, path, bw, bh, orient(area_avg(red, dw, dh), o), maxkb)

def splice_after_soi(src, dst, segments):
    d = open(src, "rb").read()
    assert d[:2] == b"\xff\xd8"
    open(dst, "wb").write(d[:2] + b"".join(segments) + d[2:])
    return dst

def app(marker, payload):
    return bytes([0xff, marker]) + struct.pack(">H", len(payload) + 2) + payload

def scans(path):
    """The file split at each SOS: the header, then each scan (SOS and its data), then the EOI."""
    d = open(path, "rb").read()
    at = [i for i in range(len(d) - 1) if d[i] == 0xff and d[i + 1] == 0xda]
    return d[:at[0]], [d[a:b] for a, b in zip(at, at[1:] + [len(d) - 2])], d[-2:]

def jpeg_cases():
    W, H = 1203, 802
    base = Image.fromarray(picture(W, H))
    for sub, n in [(0, "444"), (1, "422"), (2, "420")]:
        base.save(f(f"s{n}.jpg"), "JPEG", quality=88, subsampling=sub)
        jpeg_case(f"jpeg-{n}", f(f"s{n}.jpg"), 300, 300)
    base.save(f("s.ppm"))
    for samp, n in [("1x2", "440"), ("4x1", "411")]:
        subprocess.run([CJPEG, "-sample", samp, "-quality", "88", "-outfile", f(f"s{n}.jpg"), f("s.ppm")], check=True)
        jpeg_case(f"jpeg-{n}", f(f"s{n}.jpg"), 300, 300)
    # a baseline JPEG with one scan per component (non-interleaved): libjpeg holds every coefficient for it
    open(f("one-per-comp.txt"), "w").write("0;\n1;\n2;\n")
    subprocess.run([JPEGTRAN, "-scans", f("one-per-comp.txt"), "-outfile", f("noninter.jpg"), f("s420.jpg")], check=True)
    assert open(f("noninter.jpg"), "rb").read().count(b"\xff\xc0") == 1
    jpeg_case("jpeg-non-interleaved", f("noninter.jpg"), 300, 300)
    base.convert("L").save(f("grey.jpg"), "JPEG", quality=88)
    jpeg_case("jpeg-grey", f("grey.jpg"), 300, 300)
    base.save(f("prog.jpg"), "JPEG", quality=85, progressive=True, subsampling=2)
    jpeg_case("jpeg-progressive", f("prog.jpg"), 300, 300)
    jpeg_case("jpeg-no-reduction", f("s444.jpg"), 1100, 1100)       # 1203 to 1100: scale 8/8
    jpeg_case("jpeg-3of8", f("s420.jpg"), 451, 451)                  # 1203 to 451: 3/8, a scale Pillow cannot make
    Image.fromarray(picture(400, 300)).save(f("small.jpg"), "JPEG", quality=90)
    jpeg_case("jpeg-smaller-than-box", f("small.jpg"), 900, 900)
    Image.fromarray(picture(900, 8)).save(f("thin.jpg"), "JPEG", quality=90)
    jpeg_case("jpeg-thin", f("thin.jpg"), 200, 200)
    for o in range(1, 9):
        base.save(f(f"exif{o}.jpg"), "JPEG", quality=88, subsampling=2, exif=exif_bytes(o))
        jpeg_case(f"jpeg-exif{o}", f(f"exif{o}.jpg"), 300, 200, o=o)   # a box that is not square: it is the shown image's
    # EXIF after an XMP APP1 (the reader passes over an APP1 that is not "Exif\0\0")
    xmp = app(0xe1, b"http://ns.adobe.com/xap/1.0/\0<x:xmpmeta xmlns:x='adobe:ns:meta/'/>")
    e6 = exif_bytes(6)
    exif6 = app(0xe1, e6 if e6.startswith(b"Exif\0\0") else b"Exif\0\0" + e6)
    jpeg_case("jpeg-exif-after-xmp", splice_after_soi(f("s420.jpg"), f("xmpexif.jpg"), [xmp, exif6]), 300, 300, o=6)
    # a fatal libjpeg error raised deep inside the decoder (a quantization table index of 15), baseline and progressive:
    # the error exit leaves through the guard (tools/image.mjs runs these many times over, in wasm, both ways)
    for src, name in [("s420.jpg", "dqt-bad.jpg"), ("prog.jpg", "dqt-bad-progressive.jpg")]:
        d = bytearray(open(f(src), "rb").read())
        i = d.index(b"\xff\xdb\x00\x43\x00")
        d[i + 4] = 0x0f
        open(f(name), "wb").write(d)
        status_case("jpeg-" + name[:-4], f(name), 300, 300, "CORRUPT")
    base.convert("CMYK").save(f("cmyk.jpg"), "JPEG", quality=88)
    status_case("jpeg-cmyk", f("cmyk.jpg"), 300, 300, "UNSUPPORTED")
    # scans: CD_IMAGE_SCANS_MAX (500) and one more. Extra copies of the last scan (a refinement, which libjpeg takes
    # with a warning) make a file of exactly that many scans; the many-scans file of the review's item 3 is the second.
    head, sc, tail = scans(f("prog.jpg"))
    for total, want in [(500, "OK"), (501, "TOO_BIG")]:
        d = head + b"".join(sc) + sc[-1] * (total - len(sc)) + tail
        open(f(f"scans{total}.jpg"), "wb").write(d)
        if want == "OK": cases.append((f"jpeg-scans{total}", f(f"scans{total}.jpg"), 300, 300, "STATUS-ONLY:OK", 0, 0, 0))
        else: status_case(f"jpeg-scans{total}", f(f"scans{total}.jpg"), 300, 300, want)

def big_cases():
    """The memory cases (each with its peak, the file counted: it stays in memory under the callback)."""
    p12 = big_picture(4032, 3024)
    im = Image.fromarray(p12)
    im.save(f("p12.jpg"), "JPEG", quality=90, subsampling=2)
    im.save(f("p12p.jpg"), "JPEG", quality=90, subsampling=2, progressive=True, optimize=True)
    write_png("p12.png", 4032, 3024, 2, 8, [r.reshape(-1) for r in p12])
    jpeg_case("jpeg-p12", f("p12.jpg"), 900, 900, maxkb=os.path.getsize(f("p12.jpg")) // 1024 + 6 * 1024)
    jpeg_case("jpeg-p12-progressive", f("p12p.jpg"), 900, 900, maxkb=os.path.getsize(f("p12p.jpg")) // 1024 + 44 * 1024)
    case("png-p12", f("p12.png"), 900, 900, area_avg(p12, *fit_box(4032, 3024, 900, 900)),
         maxkb=os.path.getsize(f("p12.png")) // 1024 + 6 * 1024)
    del p12, im
    # the memory budget (CD_IMAGE_MEM_MAX, 96 MiB). A progressive JPEG's store (src/cdom_image_jpegp.c) keeps, per
    # block, only the coefficients the IDCT reads: at 8/8 all 64, as libjpeg's 128 bytes, so 4:4:4 holds 6 bytes a pixel
    # and 4096 x 4096 (100,663,296, the budget itself) does not fit; at 2/8 (a 900 box) 25 and a nonzero mask, 58 bytes
    # a block, so the same file decodes in under half of it. A 4:2:0 non-interleaved baseline at 33 MP (3 bytes a pixel,
    # libjpeg's own store) is the same budget from item 1's side.
    for (w, h, sub, prog, name, box, want) in [(4000, 4000, 0, True, "jpeg-prog444-16mp", 900, "OK"),
                                               (4096, 4096, 0, True, "jpeg-prog444-over-budget", 4096, "TOO_BIG"),
                                               (7000, 5000, 2, False, "jpeg-noninter-35mp-over-budget", 900, "TOO_BIG")]:
        g = Image.fromarray(big_picture(w, h))
        path = f(name + ".jpg")
        if prog: g.save(path, "JPEG", quality=90, subsampling=sub, progressive=True)
        else:
            g.save(f("tmp.jpg"), "JPEG", quality=90, subsampling=sub)
            subprocess.run([JPEGTRAN, "-scans", f("one-per-comp.txt"), "-outfile", path, f("tmp.jpg")], check=True)
        del g
        if want == "OK": jpeg_case(name, path, box, box, maxkb=os.path.getsize(path) // 1024 + 100 * 1024)
        else: status_case(name, path, box, box, want)
    # the over-budget file at 2/8: 45.6 MB of coefficients (3 x 262,144 blocks x 58 bytes), and the scaler
    path = f("jpeg-prog444-over-budget.jpg")
    jpeg_case("jpeg-prog444-4096-at-2of8", path, 900, 900, maxkb=os.path.getsize(path) // 1024 + 48 * 1024)

def scaler_cases():
    for (sw, sh, dw, dh, o) in [(1000, 750, 900, 675, 1), (1203, 802, 300, 200, 1), (999, 1001, 7, 9, 1), (64, 64, 64, 64, 1),
                                (5, 5, 1, 1, 1), (400, 300, 400, 300, 6), (101, 53, 50, 26, 3), (1000, 750, 333, 250, 8),
                                (257, 129, 256, 128, 2), (100, 1, 10, 1, 4), (1, 100, 1, 10, 5), (640, 480, 321, 241, 7),
                                (2, 2, 1, 1, 1), (4, 2, 2, 1, 1)]:
        a = noise((sh, sw, 3)).copy()
        a[: max(1, sh // 3)] //= 4                       # a dark band, so a misplaced row shows
        if (sw, sh) in ((2, 2), (4, 2)):   # a checkerboard of 0 and 255 into 2 x 2 blocks: every sum is exactly half
            a[:] = (np.uint8(255) * (np.indices((sh, sw)).sum(0) % 2))[..., None]
        name = f"scaler-{sw}x{sh}-{dw}x{dh}-o{o}"
        src = f(name + ".src")
        np.concatenate([a, np.full((sh, sw, 1), 255, np.uint8)], axis=2).tofile(src)
        p, w, h = save_expect(name, orient(area_avg(a, dw, dh), o))
        scalers.append((name, src, sw, sh, dw, dh, o, p, w, h))

png_cases()
jpeg_cases()
scaler_cases()
if os.environ.get("IMAGE_BIG", "1") != "0": big_cases()
with open(os.path.join(OUT, "cases.tsv"), "w") as m:
    for c in cases: m.write("\t".join(str(x) for x in c) + "\n")
with open(os.path.join(OUT, "scaler.tsv"), "w") as m:
    for c in scalers: m.write("\t".join(str(x) for x in c) + "\n")
print(f"oracle: {len(cases)} file cases, {len(scalers)} scaler cases in {OUT}")
