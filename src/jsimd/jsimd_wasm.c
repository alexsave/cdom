/* jsimd_wasm.c: libjpeg-turbo's SIMD dispatch (its "jsimd" interface, WITH_SIMD) answered with hand-written wasm SIMD,
 * <wasm_simd128.h> intrinsics only. Built when CDOM_IMAGE_SIMD=1 (cdom.mk), with libjpeg's own sources untouched: the
 * decoder's files include "../simd/jsimd.h" and "../simd/jsimddct.h" (src/jsimd/simd/, which cdom.mk puts on the
 * include path), ask jsimd_set_* whether a routine takes the case, and call the routine they name.
 *
 * Three routines, the loops the profile picked (docs/image-simd-hand.md): the reduced-size 4x4 and 2x2 IDCTs
 * (jidctred.c's; the decodes at 1/8 to 4/8 of the size) and the YCbCr to RGBA conversion (jdcolor.c's).
 *
 * Each one is bit-exact with libjpeg's C, for every input, not only for a valid file: the arithmetic is the C's own
 * 32-bit integer arithmetic (the same multiplies, adds and shifts, wrapping the same way), lane for lane, and the C's
 * shortcuts (a column whose AC terms are zero, a row whose AC terms are zero) are reproduced as lane selects, since
 * on a corrupt file's huge coefficients the shortcut and the general path differ in the C and the SIMD must too.
 * The range limit (the C's table lookup of the result masked to 10 bits) is the same function done in arithmetic. */
#include <stdint.h>
#include <wasm_simd128.h>
#define JPEG_INTERNALS
#include "jinclude.h"
#include "jpeglib.h"
#include "jdct.h"
#include "simd/jsimd.h"
#include "simd/jsimddct.h"

_Static_assert(sizeof(ISLOW_MULT_TYPE) == 4, "the IDCTs read the dequantization table as 32-bit ints (cdom.mk: -DMULTIPLIER=int)");
_Static_assert(sizeof(JCOEF) == 2, "the coefficients are 16 bits");
_Static_assert(DCTSIZE == 8 && BITS_IN_JSAMPLE == 8, "8x8 blocks of 8-bit samples");

typedef v128_t V;

#define SPLAT(n)    wasm_i32x4_const_splat(n)
#define ADD(a, b)   wasm_i32x4_add(a, b)
#define SUB(a, b)   wasm_i32x4_sub(a, b)
#define MULC(a, n)  wasm_i32x4_mul(a, SPLAT(n))
#define SHL(a, n)   wasm_i32x4_shl(a, n)
#define SHR(a, n)   wasm_i32x4_shr(a, n)       /* arithmetic, as the C's >> on a signed JLONG */

/* jidctred.c's constants (CONST_BITS 13, PASS1_BITS 2). */
enum { C0_211 = 1730, C0_509 = 4176, C0_601 = 4926, C0_720 = 5906, C0_765 = 6270, C0_850 = 6967, C0_899 = 7373,
       C1_061 = 8697, C1_272 = 10426, C1_451 = 11893, C1_847 = 15137, C2_172 = 17799, C2_562 = 20995, C3_624 = 29692 };

/* The C's range limit of an IDCT result x: table[x & 1023], the table of jdmaster.c's prepare_range_limit_table, which
 * for x mod 1024 is x + 128 below 128, 255 up to 512, 0 up to 896 and x - 896 above. As arithmetic: with x taken
 * to [-512, 511] (the mask), clamp(x + 128, 0, 255), i.e. ((x + 512) & 1023) - 384 clamped. Here the first two steps;
 * limit8 does the rest for 16 lanes at a time (two vectors of 4 per half). */
static inline V mask10(V x) { return wasm_v128_and(ADD(x, SPLAT(512)), SPLAT(1023)); }

/* Four vectors of masked results (lane k of vector j = output byte j of row/pixel k) to bytes 0 to 255: 16 bytes. */
static inline V limit8(V a, V b, V c, V d) {
  V lo = wasm_i16x8_sub(wasm_i16x8_narrow_i32x4(a, b), wasm_i16x8_const_splat(384));
  V hi = wasm_i16x8_sub(wasm_i16x8_narrow_i32x4(c, d), wasm_i16x8_const_splat(384));
  return wasm_u8x16_narrow_i16x8(lo, hi);
}

/* Dequantized rows of a block: row r, columns 0 to 3 (A) and 4 to 7 (B), as 32-bit lanes. */
static inline V deqA(const JCOEF *c, const int *q, int r) { return wasm_i32x4_mul(wasm_i32x4_load16x4(c + 8 * r), wasm_v128_load(q + 8 * r)); }
static inline V deqB(const JCOEF *c, const int *q, int r) { return wasm_i32x4_mul(wasm_i32x4_load16x4(c + 8 * r + 4), wasm_v128_load(q + 8 * r + 4)); }

/* 32-bit lanes per column (the 8 columns of coefficients) that have no AC term in the rows named: all lanes 1 bits when
 * the raw coefficients of those rows are all zero in that column (the C tests the coefficient, not the product). */
static inline V zero_mask_A(V acc) { return wasm_i32x4_extend_low_i16x8(wasm_i16x8_eq(acc, wasm_i16x8_const_splat(0))); }
static inline V zero_mask_B(V acc) { return wasm_i32x4_extend_high_i16x8(wasm_i16x8_eq(acc, wasm_i16x8_const_splat(0))); }
static inline V row16(const JCOEF *c, int r) { return wasm_v128_load(c + 8 * r); }

/* The transpose of a 4x4 block of 32-bit lanes: rows a to d in, columns out. */
static inline void transpose4(V a, V b, V c, V d, V *o0, V *o1, V *o2, V *o3) {
  V t0 = wasm_i32x4_shuffle(a, b, 0, 4, 1, 5), t1 = wasm_i32x4_shuffle(a, b, 2, 6, 3, 7);
  V t2 = wasm_i32x4_shuffle(c, d, 0, 4, 1, 5), t3 = wasm_i32x4_shuffle(c, d, 2, 6, 3, 7);
  *o0 = wasm_i64x2_shuffle(t0, t2, 0, 2); *o1 = wasm_i64x2_shuffle(t0, t2, 1, 3);
  *o2 = wasm_i64x2_shuffle(t1, t3, 0, 2); *o3 = wasm_i64x2_shuffle(t1, t3, 1, 3);
}

/* ---- the 4x4 IDCT (jpeg_idct_4x4) ---- */

/* The even and odd parts shared by both passes: inputs rows 0, 1, 2, 3, 5, 6, 7 (lanes are columns in the first pass,
 * rows in the second), outputs the four results before their descale: s0 = tmp10 + tmp2, s3 = tmp10 - tmp2,
 * s1 = tmp12 + tmp0, s2 = tmp12 - tmp0 of jidctred.c. */
static inline void parts4(V z0, V z1, V z2, V z3, V z5, V z6, V z7, V *s0, V *s1, V *s2, V *s3) {
  V t0 = SHL(z0, 14);                                   /* CONST_BITS + 1 */
  V t2 = ADD(MULC(z2, C1_847), MULC(z6, -C0_765));
  V t10 = ADD(t0, t2), t12 = SUB(t0, t2);
  V o0 = ADD(ADD(MULC(z7, -C0_211), MULC(z5, C1_451)), ADD(MULC(z3, -C2_172), MULC(z1, C1_061)));
  V o2 = ADD(ADD(MULC(z7, -C0_509), MULC(z5, -C0_601)), ADD(MULC(z3, C0_899), MULC(z1, C2_562)));
  *s0 = ADD(t10, o2); *s3 = SUB(t10, o2); *s1 = ADD(t12, o0); *s2 = SUB(t12, o0);
}

/* Pass 1 on four columns: the work-array rows 0 to 3 of those columns. */
static inline void col4(V z0, V z1, V z2, V z3, V z5, V z6, V z7, V acz, V w[4]) {
  V s0, s1, s2, s3;
  parts4(z0, z1, z2, z3, z5, z6, z7, &s0, &s1, &s2, &s3);
  V rnd = SPLAT(1 << 11), dc = SHL(z0, 2);              /* DESCALE by CONST_BITS - PASS1_BITS + 1 = 12; the dc shortcut */
  w[0] = wasm_v128_bitselect(dc, SHR(ADD(s0, rnd), 12), acz);
  w[1] = wasm_v128_bitselect(dc, SHR(ADD(s1, rnd), 12), acz);
  w[2] = wasm_v128_bitselect(dc, SHR(ADD(s2, rnd), 12), acz);
  w[3] = wasm_v128_bitselect(dc, SHR(ADD(s3, rnd), 12), acz);
}

void jsimd_idct_4x4(j_decompress_ptr cinfo, jpeg_component_info *compptr, JCOEFPTR coef_block, JSAMPARRAY output_buf,
                    JDIMENSION output_col) {
  (void)cinfo;
  const int *q = (const int *)compptr->dct_table;
  const JCOEF *c = coef_block;
  /* AC all zero in a column: rows 1, 2, 3, 5, 6, 7 (the C does not look at row 4 for 4x4 output) */
  V acc = wasm_v128_or(wasm_v128_or(row16(c, 1), row16(c, 2)), wasm_v128_or(wasm_v128_or(row16(c, 3), row16(c, 5)), wasm_v128_or(row16(c, 6), row16(c, 7))));
  V wa[4], wb[4];
  V zA = zero_mask_A(acc), zB = zero_mask_B(acc);
  col4(deqA(c, q, 0), deqA(c, q, 1), deqA(c, q, 2), deqA(c, q, 3), deqA(c, q, 5), deqA(c, q, 6), deqA(c, q, 7), zA, wa);
  col4(deqB(c, q, 0), deqB(c, q, 1), deqB(c, q, 2), deqB(c, q, 3), deqB(c, q, 5), deqB(c, q, 6), deqB(c, q, 7), zB, wb);
  /* the work array's columns, each with a lane per row (column 4 is never read) */
  V T0, T1, T2, T3, T4, T5, T6, T7;
  transpose4(wa[0], wa[1], wa[2], wa[3], &T0, &T1, &T2, &T3);
  transpose4(wb[0], wb[1], wb[2], wb[3], &T4, &T5, &T6, &T7);
  (void)T4;
  V s0, s1, s2, s3;
  parts4(T0, T1, T2, T3, T5, T6, T7, &s0, &s1, &s2, &s3);
  V rnd = SPLAT(1 << 18);                               /* DESCALE by CONST_BITS + PASS1_BITS + 3 + 1 = 19 */
  V rowz = wasm_i32x4_eq(wasm_v128_or(wasm_v128_or(T1, T2), wasm_v128_or(wasm_v128_or(T3, T5), wasm_v128_or(T6, T7))), SPLAT(0));
  V dcv = SHR(ADD(T0, SPLAT(16)), 5);                   /* the zero-row shortcut: DESCALE(ws[0], PASS1_BITS + 3) */
  V x0 = mask10(wasm_v128_bitselect(dcv, SHR(ADD(s0, rnd), 19), rowz));
  V x1 = mask10(wasm_v128_bitselect(dcv, SHR(ADD(s1, rnd), 19), rowz));
  V x2 = mask10(wasm_v128_bitselect(dcv, SHR(ADD(s2, rnd), 19), rowz));
  V x3 = mask10(wasm_v128_bitselect(dcv, SHR(ADD(s3, rnd), 19), rowz));
  /* bytes (x0 x1 x2 x3) of lane r: row r's four pixels */
  V l = limit8(x0, x1, x2, x3);
  V b = wasm_i8x16_shuffle(l, l, 0, 4, 8, 12, 1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15);
  wasm_v128_store32_lane(output_buf[0] + output_col, b, 0);
  wasm_v128_store32_lane(output_buf[1] + output_col, b, 1);
  wasm_v128_store32_lane(output_buf[2] + output_col, b, 2);
  wasm_v128_store32_lane(output_buf[3] + output_col, b, 3);
}

/* ---- the 2x2 IDCT (jpeg_idct_2x2) ---- */

/* Pass 1 on four columns, rows 0, 1, 3, 5, 7 of the block (the others are not read): work-array rows 0 and 1. */
static inline void col2(V z0, V z1, V z3, V z5, V z7, V acz, V w[2]) {
  V t10 = SHL(z0, 15);                                  /* CONST_BITS + 2 */
  V o = ADD(ADD(MULC(z7, -C0_720), MULC(z5, C0_850)), ADD(MULC(z3, -C1_272), MULC(z1, C3_624)));
  V rnd = SPLAT(1 << 12), dc = SHL(z0, 2);              /* DESCALE by CONST_BITS - PASS1_BITS + 2 = 13 */
  w[0] = wasm_v128_bitselect(dc, SHR(ADD(ADD(t10, o), rnd), 13), acz);
  w[1] = wasm_v128_bitselect(dc, SHR(ADD(SUB(t10, o), rnd), 13), acz);
}

void jsimd_idct_2x2(j_decompress_ptr cinfo, jpeg_component_info *compptr, JCOEFPTR coef_block, JSAMPARRAY output_buf,
                    JDIMENSION output_col) {
  (void)cinfo;
  const int *q = (const int *)compptr->dct_table;
  const JCOEF *c = coef_block;
  V acc = wasm_v128_or(wasm_v128_or(row16(c, 1), row16(c, 3)), wasm_v128_or(row16(c, 5), row16(c, 7)));
  V wa[2], wb[2];
  col2(deqA(c, q, 0), deqA(c, q, 1), deqA(c, q, 3), deqA(c, q, 5), deqA(c, q, 7), zero_mask_A(acc), wa);
  col2(deqB(c, q, 0), deqB(c, q, 1), deqB(c, q, 3), deqB(c, q, 5), deqB(c, q, 7), zero_mask_B(acc), wb);
  /* the work array's columns 0, 1, 3, 5, 7 with the lanes (row 0, row 1, row 0, row 1): the second pair makes the
   * second output pixel, the first pair the first */
  V X0 = wasm_i32x4_shuffle(wa[0], wa[1], 0, 4, 0, 4), X1 = wasm_i32x4_shuffle(wa[0], wa[1], 1, 5, 1, 5);
  V X3 = wasm_i32x4_shuffle(wa[0], wa[1], 3, 7, 3, 7);
  V X5 = wasm_i32x4_shuffle(wb[0], wb[1], 1, 5, 1, 5), X7 = wasm_i32x4_shuffle(wb[0], wb[1], 3, 7, 3, 7);
  V t10 = SHL(X0, 15);
  V o = ADD(ADD(MULC(X7, -C0_720), MULC(X5, C0_850)), ADD(MULC(X3, -C1_272), MULC(X1, C3_624)));
  V sg = wasm_i32x4_make(1, 1, -1, -1);
  V x = SHR(ADD(ADD(t10, wasm_i32x4_mul(o, sg)), SPLAT(1 << 19)), 20);   /* CONST_BITS + PASS1_BITS + 3 + 2 */
  V rowz = wasm_i32x4_eq(wasm_v128_or(wasm_v128_or(X1, X3), wasm_v128_or(X5, X7)), SPLAT(0));
  x = mask10(wasm_v128_bitselect(SHR(ADD(X0, SPLAT(16)), 5), x, rowz));
  V b = limit8(x, x, x, x);
  wasm_v128_store8_lane(output_buf[0] + output_col, b, 0);
  wasm_v128_store8_lane(output_buf[0] + output_col + 1, b, 2);
  wasm_v128_store8_lane(output_buf[1] + output_col, b, 1);
  wasm_v128_store8_lane(output_buf[1] + output_col + 1, b, 3);
}

int jsimd_set_idct_2x2(j_decompress_ptr cinfo) { (void)cinfo; return 1; }
int jsimd_set_idct_4x4(j_decompress_ptr cinfo) { (void)cinfo; return 1; }

/* ---- YCbCr to RGBA and RGBX (jdcolor.c's ycc_extrgbx_convert) ---- */

/* jdcolor.c's build_ycc_rgb_table, in arithmetic: with cb' = cb - 128 and cr' = cr - 128,
 *   r = y + ((91881 * cr' + 32768) >> 16)      b = y + ((116130 * cb' + 32768) >> 16)
 *   g = y + ((-22554 * cb' + 32768 - 46802 * cr') >> 16)
 * (FIX(1.40200), FIX(1.77200), FIX(0.34414) and FIX(0.71414) at 16 bits), each limited to 0 to 255. */
static inline uint8_t clamp8(int v) { return v < 0 ? 0 : v > 255 ? 255 : (uint8_t)v; }

/* 8 pixels' r, g and b offsets (16-bit lanes) from their cb' and cr' (16-bit lanes, signed). */
static inline void offs8(V cb, V cr, V *r, V *g, V *b) {
  V cbl = wasm_i32x4_extend_low_i16x8(cb), cbh = wasm_i32x4_extend_high_i16x8(cb);
  V crl = wasm_i32x4_extend_low_i16x8(cr), crh = wasm_i32x4_extend_high_i16x8(cr);
  V half = SPLAT(32768);
  *r = wasm_i16x8_narrow_i32x4(SHR(ADD(MULC(crl, 91881), half), 16), SHR(ADD(MULC(crh, 91881), half), 16));
  *b = wasm_i16x8_narrow_i32x4(SHR(ADD(MULC(cbl, 116130), half), 16), SHR(ADD(MULC(cbh, 116130), half), 16));
  *g = wasm_i16x8_narrow_i32x4(SHR(ADD(ADD(MULC(cbl, -22554), half), MULC(crl, -46802)), 16),
                               SHR(ADD(ADD(MULC(cbh, -22554), half), MULC(crh, -46802)), 16));
}

void jsimd_color_deconvert(j_decompress_ptr cinfo, JSAMPIMAGE input_buf, JDIMENSION input_row, JSAMPARRAY output_buf,
                           int num_rows) {
  const JDIMENSION n = cinfo->output_width;
  const V c128 = wasm_i16x8_const_splat(128), opaque = wasm_i8x16_const_splat((int8_t)0xff);
  while (--num_rows >= 0) {
    const uint8_t *py = input_buf[0][input_row], *pb = input_buf[1][input_row], *pr = input_buf[2][input_row];
    input_row++;
    uint8_t *o = *output_buf++;
    JDIMENSION i = 0;
    for (; i + 16 <= n; i += 16, o += 64) {
      V y = wasm_v128_load(py + i), cb = wasm_v128_load(pb + i), cr = wasm_v128_load(pr + i);
      V rl, gl, bl, rh, gh, bh;
      offs8(wasm_i16x8_sub(wasm_u16x8_extend_low_u8x16(cb), c128), wasm_i16x8_sub(wasm_u16x8_extend_low_u8x16(cr), c128), &rl, &gl, &bl);
      offs8(wasm_i16x8_sub(wasm_u16x8_extend_high_u8x16(cb), c128), wasm_i16x8_sub(wasm_u16x8_extend_high_u8x16(cr), c128), &rh, &gh, &bh);
      V yl = wasm_u16x8_extend_low_u8x16(y), yh = wasm_u16x8_extend_high_u8x16(y);
      V r = wasm_u8x16_narrow_i16x8(wasm_i16x8_add(yl, rl), wasm_i16x8_add(yh, rh));
      V g = wasm_u8x16_narrow_i16x8(wasm_i16x8_add(yl, gl), wasm_i16x8_add(yh, gh));
      V b = wasm_u8x16_narrow_i16x8(wasm_i16x8_add(yl, bl), wasm_i16x8_add(yh, bh));
      V rg0 = wasm_i8x16_shuffle(r, g, 0, 16, 1, 17, 2, 18, 3, 19, 4, 20, 5, 21, 6, 22, 7, 23);
      V rg1 = wasm_i8x16_shuffle(r, g, 8, 24, 9, 25, 10, 26, 11, 27, 12, 28, 13, 29, 14, 30, 15, 31);
      V ba0 = wasm_i8x16_shuffle(b, opaque, 0, 16, 1, 17, 2, 18, 3, 19, 4, 20, 5, 21, 6, 22, 7, 23);
      V ba1 = wasm_i8x16_shuffle(b, opaque, 8, 24, 9, 25, 10, 26, 11, 27, 12, 28, 13, 29, 14, 30, 15, 31);
      wasm_v128_store(o, wasm_i16x8_shuffle(rg0, ba0, 0, 8, 1, 9, 2, 10, 3, 11));
      wasm_v128_store(o + 16, wasm_i16x8_shuffle(rg0, ba0, 4, 12, 5, 13, 6, 14, 7, 15));
      wasm_v128_store(o + 32, wasm_i16x8_shuffle(rg1, ba1, 0, 8, 1, 9, 2, 10, 3, 11));
      wasm_v128_store(o + 48, wasm_i16x8_shuffle(rg1, ba1, 4, 12, 5, 13, 6, 14, 7, 15));
    }
    for (; i < n; i++, o += 4) {                        /* fewer than 16 left: the same arithmetic, a pixel at a time */
      int y = py[i], cb = pb[i] - 128, cr = pr[i] - 128;
      o[0] = clamp8(y + ((91881 * cr + 32768) >> 16));
      o[1] = clamp8(y + ((-22554 * cb + 32768 - 46802 * cr) >> 16));
      o[2] = clamp8(y + ((116130 * cb + 32768) >> 16));
      o[3] = 255;
    }
  }
}

/* RGBA and RGBX only; the other orders (BGR, ARGB, ...) and the 565 conversion stay libjpeg's C. */
int jsimd_set_ycc_rgb(j_decompress_ptr cinfo) {
  return cinfo->out_color_space == JCS_EXT_RGBA || cinfo->out_color_space == JCS_EXT_RGBX;
}
