#!/usr/bin/env python3
"""The image bench's inputs (docs/image-design.md, "The bench"): four synthetic photos, made from one seed.

Usage: python3 -I bench/image/inputs.py [OUT_DIR]      (default bench/image/inputs, ignored by git)

  p12.jpg   4032 x 3024, baseline, 4:2:0, quality 90   a phone photo
  p12p.jpg  the same pixels, progressive (optimized)   how editors and some browsers save
  p48.jpg   8064 x 6048, baseline, 4:2:0, quality 90   a 48 MP camera's full-size JPEG
  p12.png   4032 x 3024 RGB, compress level 6          a large PNG (a screenshot or an edited export)

Ported from raceclub's tools/resize_inputs.py (its seed and its scene), with one change for the rule on randomness:
every draw is in logical units or of a fixed count, so the number of draws does not depend on the size. The scene is
numbers in 0..1; the film grain is one GRAIN_TILE x GRAIN_TILE tile per channel, drawn once and repeated across the
image (raceclub drew width x height bytes per image). One random source: rng = random.Random(SEED), the one named
seed; fix it to another constant in one line for another set of inputs. Pillow; a bench tool, never shipped.
"""
import os, random, sys
from PIL import Image, ImageChops, ImageDraw, ImageFilter

SEED = 20261118
GRAIN = 5.0          # film grain amplitude in 8-bit levels (peak), as raceclub tuned it for phone-like JPEG sizes
GRAIN_BLUR = 0.6     # the grain is slightly soft, as film grain is
GRAIN_TILE = 1024    # the grain's tile, in pixels: a fixed number of draws at any image size

def make_scene(rng):
    """The scene as logical numbers: soft blobs (low frequencies), hard-edged blocks, and fine line patterns."""
    blobs = [dict(x=rng.random(), y=rng.random(), r=0.08 + 0.25 * rng.random(),
                  c=(rng.randrange(40, 255), rng.randrange(40, 255), rng.randrange(40, 255)))
             for _ in range(9)]
    blocks = [dict(x=rng.random() * 0.9, y=0.35 + rng.random() * 0.45, w=0.03 + 0.08 * rng.random(),
                   h=0.1 + 0.3 * rng.random(),
                   c=(rng.randrange(10, 90), rng.randrange(10, 90), rng.randrange(10, 90)))
              for _ in range(14)]
    hatches = [dict(x=rng.random() * 0.7, y=rng.random() * 0.7, w=0.2 + 0.1 * rng.random(), h=0.15,
                    pitch=rng.choice([3, 4, 5, 7]), diag=rng.random() < 0.5) for _ in range(5)]
    return dict(sky=(rng.randrange(60, 120), rng.randrange(110, 170), rng.randrange(190, 250)),
                horizon=(rng.randrange(230, 255), rng.randrange(150, 210), rng.randrange(90, 150)),
                ground=(rng.randrange(40, 80), rng.randrange(70, 110), rng.randrange(30, 60)),
                blobs=blobs, blocks=blocks, hatches=hatches)

def render(scene, w, h):
    """The scene at w x h, without grain: smooth gradients and soft shapes, then hard edges and fine lines."""
    sw, sh = max(2, w // 16), max(2, h // 16)   # the soft part is drawn small and scaled up (it is smooth)
    small = Image.new("RGB", (sw, sh))
    px = small.load()
    for y in range(sh):
        t = y / (sh - 1)
        for x in range(sw):
            if t < 0.6:
                u = t / 0.6
                c = [scene["sky"][i] * (1 - u) + scene["horizon"][i] * u for i in range(3)]
            else:
                u = (t - 0.6) / 0.4
                c = [scene["horizon"][i] * (1 - u) + scene["ground"][i] * u for i in range(3)]
            px[x, y] = tuple(int(v) for v in c)
    layer = Image.new("RGB", (sw, sh))
    mask = Image.new("L", (sw, sh), 0)
    ld, md = ImageDraw.Draw(layer), ImageDraw.Draw(mask)
    for b in scene["blobs"]:
        box = [(b["x"] - b["r"]) * sw, (b["y"] - b["r"]) * sh, (b["x"] + b["r"]) * sw, (b["y"] + b["r"]) * sh]
        ld.ellipse(box, fill=b["c"])
        md.ellipse(box, fill=150)
    mask = mask.filter(ImageFilter.GaussianBlur(max(1.0, sw / 60)))
    small = Image.composite(layer, small, mask)
    img = small.resize((w, h), Image.BICUBIC)
    d = ImageDraw.Draw(img)
    for k in scene["blocks"]:
        x0, y0 = k["x"] * w, k["y"] * h
        d.rectangle([x0, y0, x0 + k["w"] * w, y0 + k["h"] * h], fill=k["c"])
    for k in scene["hatches"]:
        x0, y0, x1, y1 = k["x"] * w, k["y"] * h, (k["x"] + k["w"]) * w, (k["y"] + k["h"]) * h
        d.rectangle([x0, y0, x1, y1], fill=(235, 235, 225))
        step = k["pitch"] * max(1, w // 4032)
        lw = max(1, w // 4032)
        if k["diag"]:
            for i in range(int((x1 - x0 + y1 - y0) / step)):
                s = i * step
                d.line([x0 + s, y0, x0, y0 + s], fill=(20, 20, 20), width=lw)
        else:
            xs = x0
            while xs < x1:
                d.line([xs, y0, xs, y1], fill=(20, 20, 20), width=lw)
                xs += step
    return img

def grain_tile(rng):
    """The grain: per channel, one tile of the one rng's bytes, softened, as offsets around 128."""
    bands = [Image.frombytes("L", (GRAIN_TILE, GRAIN_TILE), rng.randbytes(GRAIN_TILE * GRAIN_TILE))
             .filter(ImageFilter.GaussianBlur(GRAIN_BLUR)) for _ in range(3)]
    amp = GRAIN / 40.0   # uniform noise blurred has a deviation of about 25 levels; this maps it to about GRAIN
    return Image.merge("RGB", bands).point(lambda v: max(0, min(255, int(128 + (v - 128) * amp))))

def with_grain(img, tile):
    w, h = img.size
    g = Image.new("RGB", (w, h))
    for y in range(0, h, GRAIN_TILE):
        for x in range(0, w, GRAIN_TILE):
            g.paste(tile, (x, y))
    return ImageChops.add(img, g, 1.0, -128)

def save_jpeg(img, path, progressive):
    img.save(path, "JPEG", quality=90, subsampling=2, progressive=progressive, optimize=progressive)

def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "inputs")
    os.makedirs(out, exist_ok=True)
    rng = random.Random(SEED)
    scene = make_scene(rng)
    tile = grain_tile(rng)
    p12 = with_grain(render(scene, 4032, 3024), tile)
    save_jpeg(p12, os.path.join(out, "p12.jpg"), False)
    save_jpeg(p12, os.path.join(out, "p12p.jpg"), True)
    p12.save(os.path.join(out, "p12.png"), "PNG", compress_level=6)
    del p12
    save_jpeg(with_grain(render(scene, 8064, 6048), tile), os.path.join(out, "p48.jpg"), False)
    for n in ["p12.jpg", "p12p.jpg", "p48.jpg", "p12.png"]:
        im = Image.open(os.path.join(out, n))
        print(f"{n}\t{im.size[0]}x{im.size[1]}\t{os.path.getsize(os.path.join(out, n))}\tprogressive={im.info.get('progressive', 0)}")

if __name__ == "__main__":
    main()
