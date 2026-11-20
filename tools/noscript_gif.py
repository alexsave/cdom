#!/usr/bin/env python3
"""The no-script fallback's GIFs (docs/noscript-design.md): a PNG from headless Chrome made a GIF that NCSA Mosaic 1 can
show inline. GIF87a (no extension block, no transparency: the 1987 format, which every GIF reader reads), 256 colours at
most, no interlace. The palette is fixed by the pixels alone: a picture of 256 colours or fewer keeps them all,
exactly, in the order getcolors gives sorted by count and then by value; one of more is cut to 256 by median cut with
no dithering. Neither uses a random seed, a clock or the file's name, so the same PNG makes the same bytes.

  python3 tools/noscript_gif.py in.png out.gif
  python3 tools/noscript_gif.py --version      what the bytes depend on beside the PNG: Python's and Pillow's versions
"""
import sys

from PIL import Image


def gif(src, dst):
    im = Image.open(src)
    im.load()
    if im.mode in ('RGBA', 'LA', 'P'):
        im = im.convert('RGBA')
        flat = Image.new('RGB', im.size, (255, 255, 255))
        flat.paste(im, mask=im.getchannel('A'))
        im = flat
    else:
        im = im.convert('RGB')
    colours = im.getcolors(256)
    if colours is not None:
        order = [c for _, c in sorted(colours, key=lambda nc: (-nc[0], nc[1]))]
        pal = Image.new('P', (1, 1))
        flat = [v for c in order for v in c]
        pal.putpalette(flat + flat[:3] * (256 - len(order)))
        out = im.quantize(palette=pal, dither=Image.Dither.NONE)
    else:
        out = im.quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    out.info = {}
    out.save(dst, 'GIF', optimize=False, interlace=False)
    with open(dst, 'rb') as f:
        head = f.read(6)
    if head != b'GIF87a':
        sys.exit(f'noscript_gif.py: {dst} starts {head!r}, not GIF87a')


if __name__ == '__main__':
    if sys.argv[1:] == ['--version']:
        import PIL
        print(f'python {sys.version.split()[0]} pillow {PIL.__version__}')
    elif len(sys.argv) == 3:
        gif(sys.argv[1], sys.argv[2])
    else:
        sys.exit(__doc__)
