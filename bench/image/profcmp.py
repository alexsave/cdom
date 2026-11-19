#!/usr/bin/env python3
"""A one-off tool: the per-loop table of docs/image-simd-hand.md from bench/image/profile.mjs outputs.
Usage: python3 -I bench/image/profcmp.py DIR TAG   (DIR holds <rep>-<input>-<build-js|build-simd>.txt, as the doc's loop writes them)
Prints, per input and loop, the scalar and the SIMD self time per run (ms, mean over the reps) and their ratio."""
import glob, os, re, sys
LOOPS = {   # loop: (scalar functions, SIMD functions); the SIMD helpers are counted with their callers (V8 inlines some, others not)
  'idct4': (['jpeg_idct_4x4'], ['jsimd_idct_4x4', 'parts4', 'col4']),
  'idct2': (['jpeg_idct_2x2'], ['jsimd_idct_2x2', 'col2']),
  'ycc': (['ycc_extrgbx_convert'], ['jsimd_color_deconvert']),
  'scaler (row, across, down)': (['cd_image_scaler_row', 'cd_image_scaler_across_scalar', 'cd_image_scaler_down_scalar'], ['cd_image_scaler_row', 'cd_image_scaler_across_simd', 'cd_image_scaler_down_simd']),
  'unfilter': (['cd_image_unfilter_scalar'], ['cd_image_unfilter_simd', 'unfilter_paeth', 'unfilter_sub', 'unfilter_avg', 'unfilter_up']),
  'rgb to rgba': (['cd_image_rgb_to_rgba_scalar'], ['cd_image_rgb_to_rgba_simd']),
  'png row loop (png_resize)': (['cd_image_png_resize'], ['cd_image_png_resize']),
  'decode_mcu*': (['decode_mcu', 'decode_mcu_AC_refine', 'decode_mcu_AC_first'], ['decode_mcu', 'decode_mcu_AC_refine', 'decode_mcu_AC_first']),
  'tinfl_decompress': (['tinfl_decompress'], ['tinfl_decompress']),
}
d = sys.argv[1]
def read(path):
  t = {}
  for l in open(path):
    m = re.match(r'\s*[\d.]+%\s+([\d.]+) ms\s+(\S+)', l)
    if m: t[m.group(2)] = float(m.group(1))
    m = re.match(r'# \S+ \S+ \S+ \d+ runs, resize ([\d.]+) ms mean', l)
    if m: t['_resize'] = float(m.group(1))
  return t
for inp in ['p12.jpg', 'p12p.jpg', 'p48.jpg', 'p12.png']:
  runs = {b: [read(p) for p in sorted(glob.glob(f'{d}/*-{inp}-{b}.txt'))] for b in ['build-js', 'build-simd']}
  print(f'== {inp} ({len(runs["build-js"])} profiles each)')
  if not runs['build-js'] or not runs['build-simd']: continue   # an input not profiled in this set
  for loop, (sc, si) in LOOPS.items():
    a = sum(sum(r.get(f, 0) for f in dict.fromkeys(sc)) for r in runs['build-js']) / len(runs['build-js'])
    b = sum(sum(r.get(f, 0) for f in dict.fromkeys(si)) for r in runs['build-simd']) / len(runs['build-simd'])
    if a + b < 0.5: continue
    print(f'  {loop:30s} scalar {a:7.1f} ms   simd {b:7.1f} ms   {a / b if b else float("inf"):5.2f}x')
