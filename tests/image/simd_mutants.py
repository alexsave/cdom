#!/usr/bin/env python3
"""The mutants of the wasm SIMD loops (tests/image/simd_mutants.sh): each plants one fault in a copy of src/ and names the
loop of tests/image/simd.mjs whose assertion must then fail.
Usage: python3 -I simd_mutants.py SRC_COPY NAME   applies mutant NAME to the copy (exactly one occurrence, or it stops)
       python3 -I simd_mutants.py --list          prints "name<TAB>loop<TAB>what" for each"""
import sys

J = 'jsimd/jsimd_wasm.c'
L = 'cdom_image_loops.c'
# (name, the loop that must fail, file, old, new, what)
M = [
  ('idct4-const', 'idct4', J, 'C1_847 = 15137', 'C1_847 = 15138', 'a rounding constant of the 4x4 IDCT is off by one'),
  ('idct4-pass1-round', 'idct4', J, 'V rnd = SPLAT(1 << 11), dc = SHL(z0, 2);', 'V rnd = SPLAT(1 << 10), dc = SHL(z0, 2);', 'the first pass of the 4x4 rounds with half the right constant'),
  ('idct4-lane', 'idct4', J, '0, 4, 8, 12, 1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15);\n  wasm_v128_store32_lane(output_buf[0]', '0, 4, 8, 12, 5, 1, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15);\n  wasm_v128_store32_lane(output_buf[0]', 'two output pixels of the 4x4 swapped'),
  ('idct4-dc-column-shortcut', 'idct4', J, '  w[0] = wasm_v128_bitselect(dc, SHR(ADD(s0, rnd), 12), acz);', '  w[0] = SHR(ADD(s0, rnd), 12);', 'the 4x4 skips the C\'s DC-only column shortcut for the first row (they differ when the C wraps)'),
  ('idct4-range-mask', 'idct4', J, 'wasm_v128_and(ADD(x, SPLAT(512)), SPLAT(1023))', 'wasm_v128_and(ADD(x, SPLAT(512)), SPLAT(2047))', 'the range limit does not wrap at 10 bits as the C\'s table does'),
  ('idct4-quant-low-half', 'idct4', J, 'static inline V deqB(const JCOEF *c, const int *q, int r) { return wasm_i32x4_mul(wasm_i32x4_load16x4(c + 8 * r + 4), wasm_v128_load(q + 8 * r + 4)); }', 'static inline V deqB(const JCOEF *c, const int *q, int r) { return wasm_i32x4_mul(wasm_i32x4_load16x4(c + 8 * r + 4), wasm_v128_load(q + 8 * r)); }', 'the right half of the block is dequantized with the left half\'s table entries'),
  ('idct2-const', 'idct2', J, 'C3_624 = 29692', 'C3_624 = 29693', 'a constant of the 2x2 IDCT is off by one'),
  ('idct2-lane', 'idct2', J, 'wasm_v128_store8_lane(output_buf[1] + output_col, b, 1);', 'wasm_v128_store8_lane(output_buf[1] + output_col, b, 3);', 'a pixel of the 2x2 goes to the wrong place'),
  ('idct2-sign', 'idct2', J, 'wasm_i32x4_make(1, 1, -1, -1)', 'wasm_i32x4_make(1, -1, -1, 1)', 'the 2x2\'s second pixel takes the wrong sign on a row'),
  ('ycc-const', 'ycc', J, 'MULC(crl, 91881)', 'MULC(crl, 93000)', 'the red constant is 1.4191 and not 1.4020 in the low half of a vector'),
  ('ycc-green-round', 'ycc', J, 'MULC(cbh, -22554), half)', 'MULC(cbh, -22554), SPLAT(30000))', 'the green channel of the high 4 lanes of 8 rounds with 30000, not 32768'),
  ('ycc-order', 'ycc random', J, 'wasm_v128_store(o + 16, wasm_i16x8_shuffle(rg0, ba0, 4, 12, 5, 13, 6, 14, 7, 15));\n      wasm_v128_store(o + 32, wasm_i16x8_shuffle(rg1', 'wasm_v128_store(o + 32, wasm_i16x8_shuffle(rg0, ba0, 4, 12, 5, 13, 6, 14, 7, 15));\n      wasm_v128_store(o + 16, wasm_i16x8_shuffle(rg1', 'pixels 4 to 7 and 8 to 11 of 16 swapped'),
  ('ycc-alpha', 'ycc random', J, 'wasm_i8x16_const_splat((int8_t)0xff);\n  while', 'wasm_i8x16_const_splat((int8_t)0xfe);\n  while', 'alpha is 254'),
  ('ycc-tail', 'ycc random', J, 'o[0] = clamp8(y + ((91881 * cr + 32768) >> 16));', 'o[0] = clamp8(y + ((91881 * cr + 30000) >> 16));', 'the pixels past the last 16 round the red channel with 30000'),
  ('ycc-read-past', 'ycc random', J, 'for (; i + 16 <= n; i += 16, o += 64) {', 'for (; i + 15 <= n; i += 16, o += 64) {', 'the loop reads and writes 16 pixels when only 15 are left (a trap at the top of memory)'),
  ('up-saturate', 'unfilter', L, 'wasm_v128_store(cur + i, wasm_i8x16_add(wasm_v128_load(cur + i), wasm_v128_load(prev + i)));', 'wasm_v128_store(cur + i, wasm_u8x16_add_sat(wasm_v128_load(cur + i), wasm_v128_load(prev + i)));', 'Up adds with saturation, not modulo 256'),
  ('sub4-carry', 'unfilter', L, 'carry = wasm_i8x16_shuffle(v, v, 12, 13, 14, 15, 12, 13, 14, 15, 12, 13, 14, 15, 12, 13, 14, 15);', 'carry = wasm_i8x16_shuffle(v, v, 8, 9, 10, 11, 8, 9, 10, 11, 8, 9, 10, 11, 8, 9, 10, 11);', 'Sub (4 bytes) carries the wrong pixel to the next 4'),
  ('sub3-store', 'unfilter', L, 'wasm_v128_store32_lane(cur + i + 8, v, 2);', 'wasm_v128_store32_lane(cur + i + 8, v, 3);', 'Sub (3 bytes) stores its last pixel\'s bytes from the wrong lane'),
  ('avg-rounds-up', 'unfilter', L, '  const V one = wasm_i8x16_const_splat(1);\n  V a = wasm_i8x16_const_splat(0);\n  uint32_t i = 0;\n  for (; i + 4 <= n; i += bpp) {\n    V b = ld_px(prev + i);', '  const V one = wasm_i8x16_const_splat(0);\n  V a = wasm_i8x16_const_splat(0);\n  uint32_t i = 0;\n  for (; i + 4 <= n; i += bpp) {\n    V b = ld_px(prev + i);', 'Avg rounds the half up (the floor is the filter)'),
  ('paeth-tie', 'unfilter', L, 'mb = wasm_i16x8_le(pb, pc);', 'mb = wasm_i16x8_lt(pb, pc);', 'Paeth takes the up-left byte on a tie of b and c (the order of the tie is the filter)'),
  ('paeth-upleft', 'unfilter', L, '    c = b;\n    a = wasm_u16x8_extend_low_u8x16(o);', '    a = wasm_u16x8_extend_low_u8x16(o);', 'Paeth never moves the up-left pixel along'),
  ('rgba-alpha', 'rgb to rgba', L, '0, 1, 2, 16, 3, 4, 5, 16, 6, 7, 8, 16, 9, 10, 11, 16));', '0, 1, 2, 16, 3, 4, 5, 16, 6, 7, 8, 16, 9, 10, 11, 0));', 'the last pixel of 4 gets the red byte of the first as its alpha'),
  ('rgba-read-past', 'rgb to rgba', L, 'for (; x + 6 <= w; x += 4)', 'for (; x + 5 <= w; x += 4)', 'the loop reads a byte past the row (a trap at the top of memory)'),
  ('across-alpha-leak', 'scaler across', L, 'wasm_i32x4_make(-1, -1, -1, 0)', 'wasm_i32x4_make(-1, -1, -1, -1)', 'the alpha of the pixel is added into the next output pixel\'s red sum'),
  ('across-weight', 'scaler across', L, 'wasm_i32x4_splat((int32_t)(uint32_t)(b - pos))', 'wasm_i32x4_splat((int32_t)(uint32_t)(b - pos) + 1)', 'every weight one too big'),
  ('down-halves', 'scaler down', L, 'wasm_u64x2_extmul_low_u32x4(v, w)));\n      wasm_v128_store(a + i + 2, wasm_i64x2_add(wasm_v128_load(a + i + 2), wasm_u64x2_extmul_high_u32x4(v, w)));', 'wasm_u64x2_extmul_high_u32x4(v, w)));\n      wasm_v128_store(a + i + 2, wasm_i64x2_add(wasm_v128_load(a + i + 2), wasm_u64x2_extmul_low_u32x4(v, w)));', 'the two halves of 4 sums take each other\'s products'),
  ('down-big-weight', 'scaler down', L, 'if (!(wgt >> 32)) {', 'if (1) {', 'a weight over 32 bits is cut to 32 bits'),
  ('down-read-past', 'scaler down', L, 'for (; i + 4 <= n; i += 4) {\n      V v = wasm_v128_load(h + i);', 'for (; i + 3 <= n; i += 4) {\n      V v = wasm_v128_load(h + i);', 'the loop reads and writes 4 sums when 3 are left (a trap at the top of memory)'),
]

def main():
  if sys.argv[1] == '--list':
    for n, loop, f, old, new, what in M: print(f'{n}\t{loop}\t{what}')
    return
  root, name = sys.argv[1], sys.argv[2]
  for n, loop, f, old, new, what in M:
    if n != name: continue
    p = f'{root}/{f}'
    s = open(p).read()
    if s.count(old) != 1: sys.exit(f'simd_mutants: {name}: {old!r} is in {f} {s.count(old)} times, not once')
    open(p, 'w').write(s.replace(old, new))
    return
  sys.exit(f'simd_mutants: no mutant {name}')

main()
