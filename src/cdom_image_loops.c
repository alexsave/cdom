/* cdom_image's hot loops, each as a scalar body and, with CDOM_IMAGE_SIMD=1, a wasm SIMD body (<wasm_simd128.h>):
 * the PNG row filters, the RGB to RGBA expansion, and the scaler's two row loops (cdom_image_int.h; the profile that
 * picked them, and the predictions, are docs/image-simd-hand.md). The module calls the name without a suffix; the
 * SIMD body is bit-exact with the scalar one on every input (tests/image/simd.c checks it, and that the tests
 * catch a wrong lane). The scalar bodies are the code the module has always had, moved here whole. */
#include <stddef.h>
#include <string.h>
#include "cdom_image_int.h"

/* ---- the PNG row filters ---- */

static int iabs(int v) { return v < 0 ? -v : v; }
static uint8_t paeth(int a, int b, int c) {
  int pa = iabs(b - c), pb = iabs(a - c), pc = iabs(a + b - 2 * c);
  return (uint8_t)(pa <= pb && pa <= pc ? a : pb <= pc ? b : c);
}

int cd_image_unfilter_scalar(uint32_t f, uint8_t *cur, const uint8_t *prev, uint32_t n, uint32_t bpp) {
  switch (f) {
    case 0: break;
    case 1: for (uint32_t i = bpp; i < n; i++) cur[i] = (uint8_t)(cur[i] + cur[i - bpp]); break;
    case 2: for (uint32_t i = 0; i < n; i++) cur[i] = (uint8_t)(cur[i] + prev[i]); break;
    case 3:
      for (uint32_t i = 0; i < bpp && i < n; i++) cur[i] = (uint8_t)(cur[i] + (prev[i] >> 1));
      for (uint32_t i = bpp; i < n; i++) cur[i] = (uint8_t)(cur[i] + ((cur[i - bpp] + prev[i]) >> 1));
      break;
    case 4:
      for (uint32_t i = 0; i < bpp && i < n; i++) cur[i] = (uint8_t)(cur[i] + prev[i]);
      for (uint32_t i = bpp; i < n; i++) cur[i] = (uint8_t)(cur[i] + paeth(cur[i - bpp], prev[i], prev[i - bpp]));
      break;
    default: return 0;
  }
  return 1;
}

/* The run of Paeth rows that starts at row (its filter byte first), at most most rows and 4: rows that are all Paeth, one
 * pitch apart. A row of another filter ends it (0 when the first is one). */
static uint32_t paeth_run(const uint8_t *row, uint32_t pitch, uint32_t most) {
  uint32_t k = 0;
  while (k < most && k < 4 && row[(size_t)k * pitch] == 4) k++;
  return k;
}

/* Paeth as a wavefront (docs/image-png-speed.md, "Paeth as a wavefront"): row j + 1 runs LAG pixels behind row j, so each
 * row's chain (its left pixel to the next) is in flight beside the others. Its b and c are row j's pixels, stored LAG and
 * LAG + 1 steps before they are read back: a whole step's latency covers the store and the load, and the critical chain
 * stays a's alone. */
#define LAG 2

/* The scalar wavefront over k rows (2 to 4): plain C, the compiler sees k independent chains. Whole pixels in the wave;
 * the bytes past them (n not a whole number of pixels) after it, row by row. */
static inline __attribute__((always_inline)) void paeth_px_s(uint8_t *cur, const uint8_t *up, uint32_t i, const uint32_t bpp) {
  for (uint32_t ch = 0; ch < bpp; ch++) cur[i + ch] = (uint8_t)(cur[i + ch] + paeth(cur[i + ch - bpp], up[i + ch], up[i + ch - bpp]));
}
static inline __attribute__((always_inline)) void wave_s(uint8_t *cur, size_t pitch, const uint8_t *prev, uint32_t n,
                                                         const uint32_t bpp, const uint32_t k) {
  uint8_t *o[4];
  const uint8_t *u[4];
  for (uint32_t j = 0; j < k; j++) { o[j] = cur + j * pitch; u[j] = j ? o[j - 1] : prev; }
  const uint32_t w = n / bpp, lo = (k - 1) * LAG + 1, t_end = w + (k - 1) * LAG;
  for (uint32_t t = 0; t < t_end; t++) {
    if (t >= lo && t < w) {                     /* every row inside, none at its first pixel */
      for (uint32_t j = 0; j < k; j++) paeth_px_s(o[j], u[j], (t - j * LAG) * bpp, bpp);
      continue;
    }
    for (uint32_t j = 0; j < k; j++) {
      if (t < j * LAG || t - j * LAG >= w) continue;
      uint32_t x = t - j * LAG;
      if (x) paeth_px_s(o[j], u[j], x * bpp, bpp);
      else for (uint32_t ch = 0; ch < bpp; ch++) o[j][ch] = (uint8_t)(o[j][ch] + u[j][ch]);
    }
  }
  for (uint32_t j = 0; j < k; j++)
    for (uint32_t i = w * bpp; i < n; i++)
      o[j][i] = (uint8_t)(o[j][i] + (i >= bpp ? paeth(o[j][i - bpp], u[j][i], u[j][i - bpp]) : u[j][i]));
}
static void paeth_rows_scalar(uint8_t *cur, size_t pitch, const uint8_t *prev, uint32_t n, uint32_t bpp, uint32_t k) {
  if (bpp == 3) { if (k == 4) wave_s(cur, pitch, prev, n, 3, 4); else if (k == 3) wave_s(cur, pitch, prev, n, 3, 3); else wave_s(cur, pitch, prev, n, 3, 2); }
  else { if (k == 4) wave_s(cur, pitch, prev, n, 4, 4); else if (k == 3) wave_s(cur, pitch, prev, n, 4, 3); else wave_s(cur, pitch, prev, n, 4, 2); }
}

#ifndef CDOM_PAETH_WAVE_SCALAR
#define CDOM_PAETH_WAVE_SCALAR 1
#endif
uint32_t cd_image_unfilter_run_scalar(uint8_t *row, uint32_t pitch, const uint8_t *prev, uint32_t n, uint32_t bpp, uint32_t avail) {
  uint32_t k = CDOM_PAETH_WAVE_SCALAR && (bpp == 3 || bpp == 4) ? paeth_run(row, pitch, avail) : 0;
  if (k >= 2) { paeth_rows_scalar(row + 1, pitch, prev, n, bpp, k); return k; }
  return avail && cd_image_unfilter_scalar(row[0], row + 1, prev, n, bpp) ? 1 : 0;
}

void cd_image_rgb_to_rgba_scalar(const uint8_t *r, uint8_t *out, uint32_t w) {
  for (uint32_t x = 0; x < w; x++, r += 3, out += 4) { out[0] = r[0]; out[1] = r[1]; out[2] = r[2]; out[3] = 255; }
}

/* ---- the scaler's row loops (cdom_image.c's cd_image_scaler_row) ---- */

/* The across loop for pixels of step bytes (4: RGBA, 3: RGB), inlined into each with the step a constant. */
static inline __attribute__((always_inline)) void across(uint32_t *h, const uint8_t *px, uint32_t sw, uint32_t dw,
                                                         uint32_t step) {
  memset(h, 0, (size_t)dw * 3 * sizeof(uint32_t));
  /* across: source pixel c covers [c*dw, (c+1)*dw) in units of 1/dw; output pixel ox ends at (ox+1)*sw */
  uint64_t pos = 0, hend = sw;
  uint32_t ox = 0;
  for (uint32_t c = 0; c < sw; c++, px += step) {
    uint64_t cend = (uint64_t)(c + 1) * dw;
    while (pos < cend) {
      uint64_t b = cend < hend ? cend : hend;
      uint32_t wgt = (uint32_t)(b - pos);
      uint32_t *q = h + (size_t)ox * 3;
      q[0] += wgt * px[0]; q[1] += wgt * px[1]; q[2] += wgt * px[2];
      pos = b;
      if (pos == hend) { ox++; hend += sw; }
    }
  }
}
void cd_image_scaler_across_scalar(uint32_t *h, const uint8_t *px, uint32_t sw, uint32_t dw) { across(h, px, sw, dw, 4); }
void cd_image_scaler_across_rgb(uint32_t *h, const uint8_t *px, uint32_t sw, uint32_t dw) { across(h, px, sw, dw, 3); }

void cd_image_scaler_down_scalar(uint64_t *a, const uint32_t *h, uint64_t wgt, uint32_t n) {
  for (uint32_t i = 0; i < n; i++) a[i] += wgt * h[i];
}

void cd_image_scaler_div_scalar(uint8_t *out, const uint64_t *a, uint32_t n, uint64_t t) {
  const uint64_t t2 = t * 2;
  for (uint32_t i = 0; i < n; i++) {
    uint64_t v = (a[i] * 2 + t) / t2;
    out[i] = v > 255 ? 255 : (uint8_t)v;
  }
}

#if CDOM_IMAGE_SIMD
#include <wasm_simd128.h>
typedef v128_t V;

/* ---- the PNG row filters, SIMD ---- */

/* Up: 16 bytes at a time, any bytes a pixel. */
static void unfilter_up(uint8_t *cur, const uint8_t *prev, uint32_t n) {
  uint32_t i = 0;
  for (; i + 16 <= n; i += 16) wasm_v128_store(cur + i, wasm_i8x16_add(wasm_v128_load(cur + i), wasm_v128_load(prev + i)));
  for (; i < n; i++) cur[i] = (uint8_t)(cur[i] + prev[i]);
}

/* Sub, 3 and 4 bytes a pixel: a prefix sum over the pixels of 16 raw bytes (the bytes shifted by 1 pixel and 2 pixels, added), plus
 * the last pixel before them in every pixel. 4 bytes a pixel: 4 pixels a step; 3: 4 pixels (12 bytes, of the 16 loaded) a
 * step, stored as 8 and 4 bytes, so the 4 bytes past them (the next pixels' raw bytes) are never written. */
static inline V shift_up(V v, int bytes) {   /* the bytes moved toward higher lanes, zeros in */
  const V z = wasm_i8x16_const_splat(0);
  return bytes == 3 ? wasm_i8x16_shuffle(z, v, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28)
       : bytes == 4 ? wasm_i8x16_shuffle(z, v, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27)
       : bytes == 6 ? wasm_i8x16_shuffle(z, v, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25)
       :              wasm_i8x16_shuffle(z, v, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23);
}
static inline void unfilter_sub(uint8_t *cur, uint32_t n, const uint32_t bpp) {
  V carry = wasm_i8x16_const_splat(0);
  uint32_t i = 0;
  if (bpp == 4) {
    for (; i + 16 <= n; i += 16) {
      V v = wasm_v128_load(cur + i);
      v = wasm_i8x16_add(v, shift_up(v, 4));
      v = wasm_i8x16_add(v, shift_up(v, 8));
      v = wasm_i8x16_add(v, carry);
      wasm_v128_store(cur + i, v);
      carry = wasm_i8x16_shuffle(v, v, 12, 13, 14, 15, 12, 13, 14, 15, 12, 13, 14, 15, 12, 13, 14, 15);
    }
  } else {
    for (; i + 16 <= n; i += 12) {
      V v = wasm_v128_load(cur + i);
      v = wasm_i8x16_add(v, shift_up(v, 3));
      v = wasm_i8x16_add(v, shift_up(v, 6));
      v = wasm_i8x16_add(v, carry);
      wasm_v128_store64_lane(cur + i, v, 0);
      wasm_v128_store32_lane(cur + i + 8, v, 2);
      carry = wasm_i8x16_shuffle(v, v, 9, 10, 11, 9, 10, 11, 9, 10, 11, 9, 10, 11, 9, 10, 11, 9);
    }
  }
  /* the pixels left: from the last one done, as the scalar body */
  for (; i < n; i++) cur[i] = (uint8_t)(cur[i] + (i >= bpp ? cur[i - bpp] : 0));
}

/* One pixel of 3 or 4 bytes, in the low lanes of a vector: load and store. 3 bytes read 4 (the next pixel's first byte rides in
 * lane 3, which nothing uses), so the caller never starts a pixel closer than 4 bytes to the end. */
static inline V ld_px(const uint8_t *p) { return wasm_v128_load32_zero(p); }
static inline void st_px(uint8_t *p, V v, const uint32_t bpp) {
  if (bpp == 4) wasm_v128_store32_lane(p, v, 0);
  else { wasm_v128_store16_lane(p, v, 0); wasm_v128_store8_lane(p + 2, v, 2); }
}

/* Avg: floor((a + b) / 2) of the left and the up byte, as avgr_u (rounds up) less the bit that rounded. */
static inline void unfilter_avg(uint8_t *cur, const uint8_t *prev, uint32_t n, const uint32_t bpp) {
  const V one = wasm_i8x16_const_splat(1);
  V a = wasm_i8x16_const_splat(0);
  uint32_t i = 0;
  for (; i + 4 <= n; i += bpp) {
    V b = ld_px(prev + i);
    V avg = wasm_i8x16_sub(wasm_u8x16_avgr(a, b), wasm_v128_and(wasm_v128_xor(a, b), one));
    a = wasm_i8x16_add(ld_px(cur + i), avg);
    st_px(cur + i, a, bpp);
  }
  for (; i < n; i++) cur[i] = (uint8_t)(cur[i] + (((i >= bpp ? cur[i - bpp] : 0) + prev[i]) >> 1));
}

/* Paeth, a pixel at a time (each needs the one before it): the three channels in 16-bit lanes. The chain from one pixel to
 * the next is the left value through the three distances, a minimum, a compare, a select and an add: kept in 16-bit
 * lanes, with the bytes made only to store them. a is taken when pa <= pb and pa <= pc, i.e. pa <= min(pb, pc); else b
 * when pb <= pc; else c. */
static inline void unfilter_paeth(uint8_t *cur, const uint8_t *prev, uint32_t n, const uint32_t bpp) {
  /* The mask is a value clang cannot see (the empty asm), so the next left value stays a 16-bit add and an and. With the
   * constant, clang makes (pred + raw) & 0xff an 8-bit add between an i8x16.shuffle that narrows pred and an extend back,
   * which V8 lowers to five arm64 instructions in the chain: 10 deep in place of 8, and 0.80x of this loop's speed on an
   * Apple M-series core (docs/image-simd-hand.md, "Paeth on the Mac"). */
  int m = 0xff;
  __asm__("" : "+r"(m));
  const V lo8 = wasm_i16x8_splat((int16_t)m);
  V a = wasm_i8x16_const_splat(0);            /* the left pixel, 16-bit lanes: zero before the first pixel */
  V c = wasm_i8x16_const_splat(0);            /* the up-left pixel, likewise */
  uint32_t i = 0;
  for (; i + 4 <= n; i += bpp) {
    V b = wasm_u16x8_extend_low_u8x16(ld_px(prev + i));
    V raw = wasm_u16x8_extend_low_u8x16(ld_px(cur + i));
    V t = wasm_i16x8_sub(b, c), u = wasm_i16x8_sub(a, c);
    V pa = wasm_i16x8_abs(t), pb = wasm_i16x8_abs(u), pc = wasm_i16x8_abs(wasm_i16x8_add(t, u));
    V ma = wasm_i16x8_le(pa, wasm_i16x8_min(pb, pc)), mb = wasm_i16x8_le(pb, pc);
    V pred = wasm_v128_bitselect(a, wasm_v128_bitselect(b, c, mb), ma);
    a = wasm_v128_and(wasm_i16x8_add(pred, raw), lo8);
    st_px(cur + i, wasm_u8x16_narrow_i16x8(a, a), bpp);
    c = b;
  }
  for (; i < n; i++) {
    int l = i >= bpp ? cur[i - bpp] : 0, ul = i >= bpp ? prev[i - bpp] : 0;
    cur[i] = (uint8_t)(cur[i] + paeth(l, prev[i], ul));
  }
}

/* The SIMD wavefront: rows j and j + 1 in one vector of 16-bit lanes (row j in lanes 0 to 3, row j + 1 in 4 to 7), rows 2
 * and 3 in a second (K is 4; 2: the first only), row j + 1 LAG pixels behind row j. Step t makes pixel t - j * LAG of
 * row j. b is loaded from the row above (prev for row 0, the row before it for the others, which stored that pixel LAG
 * steps before), c is the step before's b, a the step before's result. A lane outside its row (before its first pixel,
 * past its last, or a row past k) reads zeros (z) and is not stored, so its a and c are zero when its row starts (Paeth of
 * a = c = 0 is b, the first pixel's rule). The pixels in the wave are those whose 4-byte load stays in the row (wv); the
 * bytes after them are made after the wave, row by row (each needs only its own row and the one above, both done). */
static inline V paeth_v(V a, V b, V c, V raw, V lo8) {
  V t = wasm_i16x8_sub(b, c), u = wasm_i16x8_sub(a, c);
  V pa = wasm_i16x8_abs(t), pb = wasm_i16x8_abs(u), pc = wasm_i16x8_abs(wasm_i16x8_add(t, u));
  V ma = wasm_i16x8_le(pa, wasm_i16x8_min(pb, pc)), mb = wasm_i16x8_le(pb, pc);
  return wasm_v128_and(wasm_i16x8_add(wasm_v128_bitselect(a, wasm_v128_bitselect(b, c, mb), ma), raw), lo8);
}
static inline V ld2(const uint8_t *p, const uint8_t *q) {   /* 4 bytes of each, in 16-bit lanes 0 to 3 and 4 to 7 */
  return wasm_u16x8_extend_low_u8x16(wasm_v128_load32_lane(q, wasm_v128_load32_zero(p), 1));
}
static inline void st_hi(uint8_t *p, V v, const uint32_t bpp) {   /* bytes 4 to 7 (or 6) of v */
  if (bpp == 4) wasm_v128_store32_lane(p, v, 1);
  else { wasm_v128_store16_lane(p, v, 2); wasm_v128_store8_lane(p + 2, v, 6); }
}
static inline __attribute__((always_inline)) void wave_v(uint8_t *cur, size_t pitch, const uint8_t *prev, uint32_t n,
                                                         const uint32_t bpp, const uint32_t K, uint32_t k) {
  int m = 0xff;                                 /* the mask clang cannot see, as unfilter_paeth's */
  __asm__("" : "+r"(m));
  const V lo8 = wasm_i16x8_splat((int16_t)m);
  uint8_t z[8] = {0};
  uint8_t *o[4];
  const uint8_t *u[4];
  for (uint32_t j = 0; j < K; j++) {
    o[j] = j < k ? cur + j * pitch : z;
    u[j] = j < k ? (j ? o[j - 1] : prev) : z;
  }
  const uint32_t wv = n >= 4 ? (n - 4) / bpp + 1 : 0, lo = (k - 1) * LAG, t_end = wv + (k - 1) * LAG;
  V aA = wasm_i8x16_const_splat(0), cA = aA, aB = aA, cB = aA;
  uint32_t t = 0;
  for (int part = 0; part < 3; part++) {
    /* part 0: the steps before every row is inside (t below lo); 1: every row inside (lo to wv); 2: the steps after */
    uint32_t stop = part == 0 ? (lo < t_end ? lo : t_end) : part == 1 ? wv : t_end;
    if (part == 1) {
      if (t >= stop) continue;
      uint8_t *o0 = o[0] + (t - 0 * LAG) * bpp, *o1 = o[1] + (t - 1 * LAG) * bpp;
      const uint8_t *u0 = u[0] + (t - 0 * LAG) * bpp, *u1 = u[1] + (t - 1 * LAG) * bpp;
      uint8_t *o2 = 0, *o3 = 0;
      const uint8_t *u2 = 0, *u3 = 0;
      size_t s2 = 0, s3 = 0;
      if (K == 4) {
        s2 = k > 2 ? bpp : 0; s3 = k > 3 ? bpp : 0;
        o2 = o[2] + (k > 2 ? (t - 2 * LAG) * bpp : 0); o3 = o[3] + (k > 3 ? (t - 3 * LAG) * bpp : 0);
        u2 = u[2] + (k > 2 ? (t - 2 * LAG) * bpp : 0); u3 = u[3] + (k > 3 ? (t - 3 * LAG) * bpp : 0);
      }
      for (; t < stop; t++) {
        V bA = ld2(u0, u1), rA = ld2(o0, o1);
        aA = paeth_v(aA, bA, cA, rA, lo8);
        cA = bA;
        V nA = wasm_u8x16_narrow_i16x8(aA, aA);
        st_px(o0, nA, bpp); st_hi(o1, nA, bpp);
        o0 += bpp; o1 += bpp; u0 += bpp; u1 += bpp;
        if (K == 4) {
          V bB = ld2(u2, u3), rB = ld2(o2, o3);
          aB = paeth_v(aB, bB, cB, rB, lo8);
          cB = bB;
          V nB = wasm_u8x16_narrow_i16x8(aB, aB);
          st_px(o2, nB, bpp); st_hi(o3, nB, bpp);
          o2 += s2; o3 += s3; u2 += s2; u3 += s3;
        }
      }
      continue;
    }
    for (; t < stop; t++) {
      const uint8_t *pu[4], *po[4];
      int in[4];
      for (uint32_t j = 0; j < K; j++) {
        in[j] = j < k && t >= j * LAG && t - j * LAG < wv;
        size_t i = in[j] ? (size_t)(t - j * LAG) * bpp : 0;
        pu[j] = in[j] ? u[j] + i : z; po[j] = in[j] ? o[j] + i : z;
      }
      V bA = ld2(pu[0], pu[1]), rA = ld2(po[0], po[1]);
      aA = paeth_v(aA, bA, cA, rA, lo8);
      cA = bA;
      V nA = wasm_u8x16_narrow_i16x8(aA, aA);
      if (in[0]) st_px(o[0] + (size_t)t * bpp, nA, bpp);
      if (in[1]) st_hi(o[1] + (size_t)(t - LAG) * bpp, nA, bpp);
      if (K == 4) {
        V bB = ld2(pu[2], pu[3]), rB = ld2(po[2], po[3]);
        aB = paeth_v(aB, bB, cB, rB, lo8);
        cB = bB;
        V nB = wasm_u8x16_narrow_i16x8(aB, aB);
        if (in[2]) st_px(o[2] + (size_t)(t - 2 * LAG) * bpp, nB, bpp);
        if (in[3]) st_hi(o[3] + (size_t)(t - 3 * LAG) * bpp, nB, bpp);
      }
    }
  }
  for (uint32_t j = 0; j < k; j++)
    for (uint32_t i = wv * bpp; i < n; i++)
      o[j][i] = (uint8_t)(o[j][i] + (i >= bpp ? paeth(o[j][i - bpp], u[j][i], u[j][i - bpp]) : u[j][i]));
}
static void paeth_rows_simd(uint8_t *cur, size_t pitch, const uint8_t *prev, uint32_t n, uint32_t bpp, uint32_t k) {
  if (bpp == 3) { if (k == 2) wave_v(cur, pitch, prev, n, 3, 2, 2); else wave_v(cur, pitch, prev, n, 3, 4, k); }
  else { if (k == 2) wave_v(cur, pitch, prev, n, 4, 2, 2); else wave_v(cur, pitch, prev, n, 4, 4, k); }
}

int cd_image_unfilter_simd(uint32_t f, uint8_t *cur, const uint8_t *prev, uint32_t n, uint32_t bpp) {
  if (f == 0) return 1;
  if (f == 2) { unfilter_up(cur, prev, n); return 1; }
  if (f > 4) return 0;
  if (bpp == 3) {
    if (f == 1) unfilter_sub(cur, n, 3); else if (f == 3) unfilter_avg(cur, prev, n, 3); else unfilter_paeth(cur, prev, n, 3);
  } else if (bpp == 4) {
    if (f == 1) unfilter_sub(cur, n, 4); else if (f == 3) unfilter_avg(cur, prev, n, 4); else unfilter_paeth(cur, prev, n, 4);
  } else return cd_image_unfilter_scalar(f, cur, prev, n, bpp);   /* 1 and 2 bytes a pixel (grey, palette, grey+alpha) */
  return 1;
}

uint32_t cd_image_unfilter_run_simd(uint8_t *row, uint32_t pitch, const uint8_t *prev, uint32_t n, uint32_t bpp, uint32_t avail) {
  uint32_t k = bpp == 3 || bpp == 4 ? paeth_run(row, pitch, avail) : 0;
  if (k >= 2) { paeth_rows_simd(row + 1, pitch, prev, n, bpp, k); return k; }
  return avail && cd_image_unfilter_simd(row[0], row + 1, prev, n, bpp) ? 1 : 0;
}

/* ---- RGB to RGBA ---- */

void cd_image_rgb_to_rgba_simd(const uint8_t *r, uint8_t *out, uint32_t w) {
  const V ff = wasm_i8x16_const_splat((int8_t)0xff);
  uint32_t x = 0;
  for (; x + 6 <= w; x += 4)    /* 16 bytes read, 12 of them pixels: 4 pixels, and the read stays inside the row */
    wasm_v128_store(out + 4 * x, wasm_i8x16_shuffle(wasm_v128_load(r + 3 * x), ff, 0, 1, 2, 16, 3, 4, 5, 16, 6, 7, 8, 16, 9, 10, 11, 16));
  cd_image_rgb_to_rgba_scalar(r + 3 * x, out + 4 * x, w - x);
}

/* ---- the scaler ---- */

/* The sum of the output pixel being made is a vector of 32-bit lanes (r, g, b, and a whose sum nothing uses) in a register;
 * a source pixel adds its pieces to it, and a finished one is stored (12 of its 16 bytes) and the next starts from zero. No
 * sum is loaded, so nothing waits on a store before it: every output pixel is written once, and h is not read. */
void cd_image_scaler_across_simd(uint32_t *h, const uint8_t *px, uint32_t sw, uint32_t dw) {
  V sum = wasm_i32x4_const_splat(0);
  uint64_t pos = 0, hend = sw;
  uint32_t ox = 0;
  for (uint32_t c = 0; c < sw; c++, px += 4) {
    V p = wasm_u32x4_extend_low_u16x8(wasm_u16x8_extend_low_u8x16(wasm_v128_load32_zero(px)));
    uint64_t cend = (uint64_t)(c + 1) * dw;
    while (pos < cend) {
      uint64_t b = cend < hend ? cend : hend;
      sum = wasm_i32x4_add(sum, wasm_i32x4_mul(p, wasm_i32x4_splat((int32_t)(uint32_t)(b - pos))));
      pos = b;
      if (pos == hend) {
        uint32_t *q = h + (size_t)ox * 3;
        wasm_v128_store64_lane(q, sum, 0);
        wasm_v128_store32_lane(q + 2, sum, 2);
        sum = wasm_i32x4_const_splat(0);
        ox++; hend += sw;
      }
    }
  }
}

void cd_image_scaler_down_simd(uint64_t *a, const uint32_t *h, uint64_t wgt, uint32_t n) {
  uint32_t i = 0;
  if (!(wgt >> 32)) {            /* the weight is at most the target height, under 65536: always */
    const V w = wasm_i32x4_splat((int32_t)(uint32_t)wgt);
    for (; i + 4 <= n; i += 4) {
      V v = wasm_v128_load(h + i);
      wasm_v128_store(a + i, wasm_i64x2_add(wasm_v128_load(a + i), wasm_u64x2_extmul_low_u32x4(v, w)));
      wasm_v128_store(a + i + 2, wasm_i64x2_add(wasm_v128_load(a + i + 2), wasm_u64x2_extmul_high_u32x4(v, w)));
    }
  }
  for (; i < n; i++) a[i] += wgt * h[i];
}
/* The divide in doubles, two sums at a time. x = 2a + t is at most 511 * t, under 2^41, and d = 2t under 2^33: both are
 * exact doubles, and so is hi * 2^33 + 2 * lo, the sum of x made from the 32-bit halves of a (it is under 2^53). The quotient
 * q = x / d is under 256, and a double division is correctly rounded, so it is q to within 2^-53 of its size, under
 * 2^-45; when x / d is not a whole number it is at least 1 / d, over 2^-33, from every whole number, so the double never
 * crosses one: its integer part is the integer quotient. (When x / d is whole the division is exact.) */
void cd_image_scaler_div_simd(uint8_t *out, const uint64_t *a, uint32_t n, uint64_t t) {
  const V tt = wasm_f64x2_splat((double)t), d = wasm_f64x2_splat((double)(t * 2)), k33 = wasm_f64x2_splat(8589934592.0);
  const V cap = wasm_i32x4_const_splat(255);
  uint32_t i = 0;
  for (; i + 2 <= n; i += 2) {
    V v = wasm_v128_load(a + i);
    V lo = wasm_f64x2_convert_low_u32x4(wasm_i32x4_shuffle(v, v, 0, 2, 0, 2));
    V hi = wasm_f64x2_convert_low_u32x4(wasm_i32x4_shuffle(v, v, 1, 3, 1, 3));
    V x = wasm_f64x2_add(wasm_f64x2_add(wasm_f64x2_mul(hi, k33), wasm_f64x2_add(lo, lo)), tt);
    V q = wasm_u32x4_min(wasm_u32x4_trunc_sat_f64x2_zero(wasm_f64x2_div(x, d)), cap);
    wasm_v128_store16_lane(out + i, wasm_i8x16_shuffle(q, q, 0, 4, 0, 4, 0, 4, 0, 4, 0, 4, 0, 4, 0, 4, 0, 4), 0);
  }
  cd_image_scaler_div_scalar(out + i, a + i, n - i, t);
}
#endif
