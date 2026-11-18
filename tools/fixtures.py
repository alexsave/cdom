#!/usr/bin/env python3
"""Writes the picked-file fixtures of tests/file.c (tools/file.mjs) into tests/fixtures, the same bytes on every
run: no randomness and no clock (PIL writes no time into a PNG or a JPEG). Run it again only to change them.
- land.png 300 x 200, port.png 200 x 300, small.png 50 x 30, odd.png 333 x 101: the fit's cases. Each is red on
  its left half and blue on its right, so a picture turned or mirrored shows.
- rot6.jpg: stored 60 x 40, red on the top half and blue on the bottom, with EXIF orientation 6 (turn 90 degrees
  clockwise to show): upright it is 40 x 60, blue on the left and red on the right, as a phone's portrait photo.
- notimage.png: text, named as an image.
- data.bin: 1,000 bytes, byte i = (i * 131 + 7) mod 256."""
import pathlib
from PIL import Image

out = pathlib.Path(__file__).resolve().parent.parent / 'tests' / 'fixtures'
out.mkdir(exist_ok=True)
RED, BLUE = (220, 30, 30), (30, 30, 220)

def halves(w, h, split_x):
  im = Image.new('RGB', (w, h), RED)
  if split_x: im.paste(BLUE, (w // 2, 0, w, h))
  else: im.paste(BLUE, (0, h // 2, w, h))
  return im

for name, w, h in [('land', 300, 200), ('port', 200, 300), ('small', 50, 30), ('odd', 333, 101)]:
  halves(w, h, True).save(out / f'{name}.png', optimize=True)
exif = Image.Exif()
exif[0x0112] = 6
halves(60, 40, False).save(out / 'rot6.jpg', quality=90, exif=exif.tobytes())
(out / 'notimage.png').write_bytes(b'this is text, not a picture\n')
(out / 'data.bin').write_bytes(bytes((i * 131 + 7) % 256 for i in range(1000)))
