#!/usr/bin/env python3
"""Edge inputs for the fed decoder (tests/image/test_chunked.c, the test plan's item 2 and 3): files whose structure meets piece edges.

Usage: python3 -I bench/chunked/edge.py OUT_DIR       (Pillow; cjpeg from libjpeg-turbo, $CJPEG or on the PATH, for the restart one)

  e-zero.png      zero-length private chunks before, between and after the IDATs, and an empty IDAT
  e-idat3.png     the image data split into IDATs of 1 to 7 bytes
  e-text.png      a 200,000-byte tEXt before the IDAT (skipped across many pieces)
  e-pal.png       a palette image with tRNS, its IDATs of 5 bytes
  e-com.jpg       a 65,533-byte COM segment (the largest) ahead of the frame
  e-fill.jpg      5,000 fill bytes (FF) before every marker after SOI
  e-icc-exif.jpg  an APP2 of 65,533 bytes, then the EXIF (orientation 6): the frame lies past 64 KiB
  e-restart.jpg   restart markers every MCU row (cjpeg -restart 1)
"""
import io, os, random, struct, subprocess, sys, zlib
from PIL import Image

SEED = 20261119

def chunk(t, d):
    return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)

def png(w, h, ct, raw_rows, idat_sizes, extra_before=b"", extra_mid=b"", extra_after=b"", plte=b"", trns=b""):
    depth = 8
    z = zlib.compress(b"".join(b"\0" + r for r in raw_rows), 6)
    out = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, depth, ct, 0, 0, 0))
    out += extra_before
    if plte: out += chunk(b"PLTE", plte)
    if trns: out += chunk(b"tRNS", trns)
    at, i = 0, 0
    while at < len(z):
        n = idat_sizes[i % len(idat_sizes)]; i += 1
        out += chunk(b"IDAT", z[at:at + n]); at += n
        if i == 2: out += extra_mid
    return out + extra_after + chunk(b"IEND", b"")

def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    rng = random.Random(SEED)
    w, h = 61, 47
    rgb = [bytes(rng.randrange(256) if rng.random() < 0.3 else (x * 4 + y) & 255 for x in range(w * 3)) for y in range(h)]
    z0 = chunk(b"zzZz", b"")
    open(os.path.join(out, "e-zero.png"), "wb").write(png(w, h, 2, rgb, [4000, 0, 4000], z0, z0 + chunk(b"IDAT", b""), z0))
    open(os.path.join(out, "e-idat3.png"), "wb").write(png(w, h, 2, rgb, [1, 2, 3, 4, 5, 6, 7]))
    text = b"Comment\0" + bytes(rng.randrange(32, 127) for _ in range(200000 - 8))
    open(os.path.join(out, "e-text.png"), "wb").write(png(w, h, 2, rgb, [100000], chunk(b"tEXt", text)))
    pal = bytes(rng.randrange(256) for _ in range(3 * 16))
    trns = bytes(rng.randrange(256) for _ in range(16))
    idx = [bytes(rng.randrange(16) for _ in range(w)) for _ in range(h)]
    open(os.path.join(out, "e-pal.png"), "wb").write(png(w, h, 3, idx, [5], plte=pal, trns=trns))

    img = Image.frombytes("RGB", (w, h), b"".join(rgb)).resize((640, 480))
    b = io.BytesIO(); img.save(b, "JPEG", quality=85); j = b.getvalue()
    com = b"\xff\xfe" + struct.pack(">H", 65535) + bytes(rng.randrange(256) & 0x7f for _ in range(65533))
    open(os.path.join(out, "e-com.jpg"), "wb").write(j[:2] + com + j[2:])
    # fill bytes before every marker after SOI, up to the scan (entropy data cannot hold them)
    sos = j.index(b"\xff\xda")
    head, rest, at, parts = j[2:sos], j[sos:], 0, [j[:2]]
    while at < len(head):
        n = struct.unpack(">H", head[at + 2:at + 4])[0]
        parts.append(b"\xff" * 5000 + head[at:at + 2 + n]); at += 2 + n
    parts.append(b"\xff" * 5000 + rest)
    open(os.path.join(out, "e-fill.jpg"), "wb").write(b"".join(parts))
    tiff = b"MM\0*" + struct.pack(">I", 8) + struct.pack(">H", 1) + struct.pack(">HHII", 0x0112, 3, 1, 6 << 16) + b"\0\0\0\0"
    exif = b"Exif\0\0" + tiff
    app1 = b"\xff\xe1" + struct.pack(">H", len(exif) + 2) + exif
    app2 = b"\xff\xe2" + struct.pack(">H", 65535) + b"ICC_PROFILE\0" + bytes(rng.randrange(256) & 0x7f for _ in range(65533 - 12))
    open(os.path.join(out, "e-icc-exif.jpg"), "wb").write(j[:2] + app2 + app1 + j[2:])
    ppm = os.path.join(out, "e.ppm"); img.save(ppm)
    subprocess.run([os.environ.get("CJPEG", "cjpeg"), "-restart", "1", "-quality", "85", "-outfile", os.path.join(out, "e-restart.jpg"), ppm], check=True)
    os.remove(ppm)

if __name__ == "__main__":
    main()
