#!/usr/bin/env python3
"""python3 tools/gen_ns2_gifs.py <page_data.c> <outdir>: the palette of demo/ns2render's page_data.c (rd_palette) as one-pixel
GIFs, <outdir>/xx.gif with xx the index in hex (the names render.c's ops and host.js's markup give), and end.gif, a pixel of
the paper that every draw requests last (its query holds the draw's times).  The GIF is tools/gen_probe_ns2_fb.py's."""
import os, re, struct, sys


def gif(rgb):
    return (b"GIF89a" + struct.pack("<HHBBB", 1, 1, 0x80, 0, 0) + bytes(rgb) + b"\0\0\0"
            + b"\x2c" + struct.pack("<HHHHB", 0, 0, 1, 1, 0) + b"\x02\x02\x44\x01\x00" + b"\x3b")


src, out = sys.argv[1], sys.argv[2]
pal = [int(v, 16) for v in re.search(r"rd_palette\[\d+\] = \{([^}]*)\}", open(src).read())[1].split(",")]
os.makedirs(out, exist_ok=True)
for i, p in enumerate(pal):
    open(os.path.join(out, "%02x.gif" % i), "wb").write(gif((p >> 16, p >> 8 & 255, p & 255)))
open(os.path.join(out, "end.gif"), "wb").write(gif((0xF6, 0xF7, 0xFB)))
print(out, len(pal), "GIFs and end.gif")
