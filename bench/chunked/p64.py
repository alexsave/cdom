import os, random, sys
sys.path.insert(0, sys.argv[1])
import inputs as I
rng = random.Random(I.SEED); scene = I.make_scene(rng); tile = I.grain_tile(rng)
img = I.with_grain(I.render(scene, 9216, 6912), tile)
I.save_jpeg(img, os.path.join(sys.argv[2], "p64.jpg"), False)
I.save_jpeg(img, os.path.join(sys.argv[2], "p64p.jpg"), True)
