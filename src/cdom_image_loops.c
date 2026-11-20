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

/* Paeth as a wavefront (docs/image-png-speed.md, "Paeth as a wavefront"): row j + 1 runs LAG pixels behind row j, so each
 * row's chain (its left pixel to the next) is in flight beside the others. A lag of 2 keeps the path from row j's pixel to
 * row j + 1's b a whole step longer than the chain, so it is never the critical one; with 1 it would be (it adds the
 * move into the lower row's lanes to the chain). The SIMD build's (below); the scalar build has none: four rows
 * interleaved in plain C measured 1.11 times the time of a row at a time (the scalar Paeth is bound by its operations,
 * not by a chain: byte i needs byte i - bpp only, so bpp chains are in flight already), so a run there is a row. */
#ifndef CDOM_PAETH_LAG
#define CDOM_PAETH_LAG 2
#endif
#define LAG CDOM_PAETH_LAG

uint32_t cd_image_unfilter_run_scalar(uint8_t *row, uint32_t pitch, const uint8_t *prev, uint32_t n, uint32_t bpp, uint32_t avail) {
  (void)pitch;
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
/* across on sw grey pixels, one sum an output pixel (docs/image-png-speed.md, "Grey PNG"): output pixel ox covers sw units
 * from source pixel c, r units into it (a source pixel is dw units, and dw <= sw): the rest of pixel c, the whole pixels
 * after it (summed, then weighted by dw once), and left units of the pixel where it ends. The same pieces as across's,
 * so the same integer sums; the last output pixel ends at the row's end with left 0, so nothing past it is read. */
void cd_image_scaler_across_grey(uint32_t *h, const uint8_t *px, uint32_t sw, uint32_t dw) {
  uint32_t c = 0, r = 0;
  for (uint32_t ox = 0; ox < dw; ox++) {
    uint32_t sum = (dw - r) * px[c++], left = sw - (dw - r), m = 0;
    for (; left >= dw; left -= dw) m += px[c++];
    sum += m * dw;
    if (left) sum += left * px[c];
    r = left;
    h[ox] = sum;
  }
}

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

/* The run of Paeth rows that starts at row (its filter byte first), at most most rows and cap: rows that are all Paeth, one
 * pitch apart. A row of another filter ends it (0 when the first is one). */
static uint32_t paeth_run(const uint8_t *row, uint32_t pitch, uint32_t most, uint32_t cap) {
  uint32_t k = 0;
  while (k < most && k < cap && row[(size_t)k * pitch] == 4) k++;
  return k;
}

/* The SIMD wavefront: four rows in one vector of bytes, row j in bytes 4j to 4j + 3 (its pixel's 3 or 4 bytes; for 3, the
 * fourth is the next pixel's first raw byte, which rides along and is stored back as it was), row j + 1 LAG pixels behind
 * row j. Step t makes pixel t - j * LAG of row j. Row 0's b is loaded from prev; a lagging row's b is the pixel its upper
 * row made LAG steps before, kept in a register (the results of the last LAG steps) and moved up 4 bytes. It is never
 * stored and loaded back: a 4-byte load of 3-byte pixels spans the stores before it, which no store-to-load forward
 * serves. c is the step before's b, a the step before's result.
 * Paeth in bytes: with p = b - c and q = a - c, pa = |p|, pb = |q| and pc = |p + q|. Each difference is made as its two
 * saturated halves (one of them zero), so pc is the distance between the sum of the positive halves and the sum of the
 * negative ones. When p and q have one sign that sum may pass 255 and saturate: pc is then pa + pb, at least pa and pb,
 * and a saturated pc is too, so both compares (pa <= pc, pb <= pc) answer as the exact one would. When they differ, pc is
 * the distance of two single halves, exact. The sum with the raw byte wraps, as the filter's arithmetic does.
 * A row's lanes before its first pixel hold zeros (its raw bytes read from z, its b the upper row's lanes from before that
 * one started), so its a and c are zero when it starts (Paeth of a = c = 0 is b, the first pixel's rule); lanes past a
 * row's last pixel are not stored, nor are rows past k (in the main loop they share the last row's bytes and are stored
 * before it). The pixels in the wave are those whose 4-byte load stays in the row (wv); the bytes after them are made
 * after the wave, row by row (each needs only its own row and the one above, both done by then). */
static inline __attribute__((always_inline)) V paeth_v(V a, V b, V c, V raw) {
  V pp = wasm_u8x16_sub_sat(b, c), pm = wasm_u8x16_sub_sat(c, b), qp = wasm_u8x16_sub_sat(a, c), qm = wasm_u8x16_sub_sat(c, a);
  V pa = wasm_v128_or(pp, pm), pb = wasm_v128_or(qp, qm);
  V sp = wasm_u8x16_add_sat(pp, qp), sm = wasm_u8x16_add_sat(pm, qm);
  V pc = wasm_v128_or(wasm_u8x16_sub_sat(sp, sm), wasm_u8x16_sub_sat(sm, sp));
  V ma = wasm_u8x16_le(pa, wasm_u8x16_min(pb, pc)), mb = wasm_u8x16_le(pb, pc);
  return wasm_i8x16_add(wasm_v128_bitselect(a, wasm_v128_bitselect(b, c, mb), ma), raw);
}
/* b: row 0's loaded from up into bytes 0 to 3, rows 1 to 3 rows 0 to 2's results of LAG steps before (h moved up 4 bytes) */
static inline __attribute__((always_inline)) V b_of(const uint8_t *up, V h) {
  return wasm_v128_load32_lane(up, wasm_i8x16_shuffle(h, h, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27), 0);
}
static inline __attribute__((always_inline)) V ld4(const uint8_t *p0, const uint8_t *p1, const uint8_t *p2, const uint8_t *p3) {
  return wasm_v128_load32_lane(p3, wasm_v128_load32_lane(p2, wasm_v128_load32_lane(p1, wasm_v128_load32_zero(p0), 1), 2), 3);
}
static inline __attribute__((always_inline)) void wave_v(uint8_t *cur, size_t pitch, const uint8_t *prev, uint32_t n,
                                                         const uint32_t bpp, uint32_t k) {
  /* bpp 3: each row's fourth byte stored is its raw byte, as loaded (it is the next pixel's) */
  const V keep = bpp == 3 ? wasm_i32x4_const_splat((int32_t)0xff000000u) : wasm_i32x4_const_splat(0);
  uint8_t z[4] = {0};
  /* row j's pixel x at b[j] + x * bpp; a row past k is the last row's, at its pixel, so the main loop needs no test: it
   * stores rows 3 to 0 in that order, and a live row's store lands after the rows past k that share its bytes */
  uint8_t *o[4];
  for (uint32_t j = 0; j < 4; j++) o[j] = cur + (j < k ? j : k - 1) * pitch;
  const uint32_t wv = n >= 4 ? (n - 4) / bpp + 1 : 0, lo = (k - 1) * LAG, t_end = wv + (k - 1) * LAG;
  /* a: the last step's results; h1: those of the step before (a lag of 2 reads it) */
  V a = wasm_i8x16_const_splat(0), c = a, h1 = a;
  uint32_t t = 0;
  for (int part = 0; part < 3; part++) {
    /* part 0: the steps before every row is inside (t below lo); 1: every row inside (lo to wv); 2: the steps after */
    uint32_t stop = part == 0 ? (lo < t_end ? lo : t_end) : part == 1 ? wv : t_end;
    if (part == 1) {
      if (t >= stop) continue;
      /* one index for every row: row j's pixel t - j' * LAG (j' its row, or the last row for one past k) */
      uint8_t *r0 = o[0], *r1 = o[1] - (k > 1 ? 1 : 0) * LAG * bpp, *r2 = o[2] - (k > 2 ? 2 : k - 1) * LAG * bpp,
              *r3 = o[3] - (k > 3 ? 3 : k - 1) * LAG * bpp;
      const uint8_t *u0 = prev;
      /* the raw bytes are loaded a step ahead, before the stores of the step: a load after them would overlap the 4
       * bytes the step before stored, a store-to-load forward that fails, in the chain from one step to the next */
      size_t i = (size_t)t * bpp, e = (size_t)stop * bpp;
      V next = ld4(r0 + i, r1 + i, r2 + i, r3 + i);
      for (; i < e; i += bpp) {
        V b = b_of(u0 + i, LAG == 1 ? a : h1), raw = next;
        if (i + bpp < e) next = ld4(r0 + i + bpp, r1 + i + bpp, r2 + i + bpp, r3 + i + bpp);
        h1 = a;
        a = paeth_v(a, b, c, raw);
        c = b;
        V st = wasm_v128_bitselect(raw, a, keep);
        wasm_v128_store32_lane(r3 + i, st, 3); wasm_v128_store32_lane(r2 + i, st, 2);
        wasm_v128_store32_lane(r1 + i, st, 1); wasm_v128_store32_lane(r0 + i, st, 0);
      }
      t = stop;
      continue;
    }
    for (; t < stop; t++) {
      int in[4];
      const uint8_t *po[4];
      for (uint32_t j = 0; j < 4; j++) {
        in[j] = j < k && t >= j * LAG && t - j * LAG < wv;   /* a row past k is never stored here */
        po[j] = in[j] ? o[j] + (size_t)(t - j * LAG) * bpp : z;
      }
      V b = b_of(in[0] ? prev + (size_t)t * bpp : z, LAG == 1 ? a : h1), raw = ld4(po[0], po[1], po[2], po[3]);
      h1 = a;
      a = paeth_v(a, b, c, raw);
      c = b;
      V st = wasm_v128_bitselect(raw, a, keep);
      if (in[0]) wasm_v128_store32_lane((void *)po[0], st, 0);
      if (in[1]) wasm_v128_store32_lane((void *)po[1], st, 1);
      if (in[2]) wasm_v128_store32_lane((void *)po[2], st, 2);
      if (in[3]) wasm_v128_store32_lane((void *)po[3], st, 3);
    }
  }
  for (uint32_t j = 0; j < k; j++) {
    const uint8_t *up = j ? o[j - 1] : prev;
    for (uint32_t i = wv * bpp; i < n; i++)
      o[j][i] = (uint8_t)(o[j][i] + (i >= bpp ? paeth(o[j][i - bpp], up[i], up[i - bpp]) : up[i]));
  }
}
/* The wavefront for 1 and 2 bytes a pixel (grey, palette, grey+alpha; docs/image-png-speed.md, "Grey PNG"): one row a lane,
 * 16 rows of 1 byte or 8 of 2, row j + 1 LAG pixels behind row j, as wave_v. A lane's pixel is loaded and stored by
 * itself (a lane load and a lane store a row a step), so no pixel spans another's store and nothing rides along; a load
 * of W bytes stays in the row, so every pixel is in the wave and there is no tail. b is row 0's from prev in lane 0, and
 * row j's the result row j - 1 made LAG steps before, moved up a lane; c the step before's b, a the step before's result.
 * Before its first pixel a row's lanes hold zeros (raw bytes from z, b the upper row's zeros from before it started), so
 * its a and c are zero when it starts; a lane past its last pixel, or past k, is not stored in the parts where rows start
 * and end. In the middle part every row is inside and a lane past k is the last row's, at its pixel, stored before it. */
#define WN_LANES16(X) X(0) X(1) X(2) X(3) X(4) X(5) X(6) X(7) X(8) X(9) X(10) X(11) X(12) X(13) X(14) X(15)
#define WN_LANES8(X) X(0) X(1) X(2) X(3) X(4) X(5) X(6) X(7)
static inline __attribute__((always_inline)) V lane_up(V h, const uint32_t W) {   /* lanes moved up one, zero in */
  const V z = wasm_i8x16_const_splat(0);
  return W == 1 ? wasm_i8x16_shuffle(h, z, 16, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14)
                : wasm_i8x16_shuffle(h, z, 16, 17, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13);
}
static inline __attribute__((always_inline)) void wave_n(uint8_t *cur, size_t pitch, const uint8_t *prev, uint32_t n,
                                                         const uint32_t W, uint32_t k) {
  const uint32_t R = 16 / W, np = n / W, lo0 = (k - 1) * LAG, t_end = np + (k - 1) * LAG;
  uint8_t z[2] = {0};
  uint8_t *o[16];   /* row j's first pixel; a row past k is the last row's */
  for (uint32_t j = 0; j < R; j++) o[j] = cur + (j < k ? j : k - 1) * pitch;
  V a = wasm_i8x16_const_splat(0), c = a, h1 = a;
  uint32_t t = 0;
  for (int part = 0; part < 3; part++) {
    uint32_t stop = part == 0 ? (lo0 < t_end ? lo0 : t_end) : part == 1 ? np : t_end;
    if (part == 1) {
      if (t >= stop) continue;
      /* one index for every row: row j's pixel t - j' * LAG (j' its row, or the last row for one past k) */
      uint8_t *r[16];
      for (uint32_t j = 0; j < R; j++) r[j] = o[j] - (size_t)(j < k ? j : k - 1) * LAG * W;
      for (size_t i = (size_t)t * W, e = (size_t)stop * W; i < e; i += W) {
        V raw = wasm_i8x16_const_splat(0);
        if (W == 1) {
#define WN_LD(j) raw = wasm_v128_load8_lane(r[j] + i, raw, j);
          WN_LANES16(WN_LD)
#undef WN_LD
        } else {
#define WN_LD(j) raw = wasm_v128_load16_lane(r[j] + i, raw, j);
          WN_LANES8(WN_LD)
#undef WN_LD
        }
        V b = W == 1 ? wasm_v128_load8_lane(prev + i, lane_up(LAG == 1 ? a : h1, 1), 0)
                     : wasm_v128_load16_lane(prev + i, lane_up(LAG == 1 ? a : h1, 2), 0);
        h1 = a;
        a = paeth_v(a, b, c, raw);
        c = b;
        if (W == 1) {
#define WN_ST(j) wasm_v128_store8_lane(r[15 - j] + i, a, 15 - j);
          WN_LANES16(WN_ST)
#undef WN_ST
        } else {
#define WN_ST(j) wasm_v128_store16_lane(r[7 - j] + i, a, 7 - j);
          WN_LANES8(WN_ST)
#undef WN_ST
        }
      }
      t = stop;
      continue;
    }
    for (; t < stop; t++) {
      int in[16];
      uint8_t *po[16];
      for (uint32_t j = 0; j < R; j++) {
        in[j] = j < k && t >= j * LAG && t - j * LAG < np;
        po[j] = in[j] ? o[j] + (size_t)(t - j * LAG) * W : z;
      }
      V raw = wasm_i8x16_const_splat(0);
      if (W == 1) {
#define WN_LD(j) raw = wasm_v128_load8_lane(po[j], raw, j);
        WN_LANES16(WN_LD)
#undef WN_LD
      } else {
#define WN_LD(j) raw = wasm_v128_load16_lane(po[j], raw, j);
        WN_LANES8(WN_LD)
#undef WN_LD
      }
      const uint8_t *pb = in[0] ? prev + (size_t)t * W : z;
      V b = W == 1 ? wasm_v128_load8_lane(pb, lane_up(LAG == 1 ? a : h1, 1), 0)
                   : wasm_v128_load16_lane(pb, lane_up(LAG == 1 ? a : h1, 2), 0);
      h1 = a;
      a = paeth_v(a, b, c, raw);
      c = b;
      if (W == 1) {
#define WN_ST(j) if (in[j]) wasm_v128_store8_lane(po[j], a, j);
        WN_LANES16(WN_ST)
#undef WN_ST
      } else {
#define WN_ST(j) if (in[j]) wasm_v128_store16_lane(po[j], a, j);
        WN_LANES8(WN_ST)
#undef WN_ST
      }
    }
  }
}

static void paeth_rows_simd(uint8_t *cur, size_t pitch, const uint8_t *prev, uint32_t n, uint32_t bpp, uint32_t k) {
  if (bpp == 3) wave_v(cur, pitch, prev, n, 3, k);
  else if (bpp == 4) wave_v(cur, pitch, prev, n, 4, k);
  else if (bpp == 1) wave_n(cur, pitch, prev, n, 1, k);
  else wave_n(cur, pitch, prev, n, 2, k);
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
  /* a run's rows: 4 of 3 or 4 bytes a pixel (wave_v), 16 of 1 and 8 of 2 (wave_n: one row a lane) */
  uint32_t cap = bpp == 3 || bpp == 4 ? 4 : bpp == 1 ? 16 : bpp == 2 && !(n & 1) ? 8 : 0;
  uint32_t k = cap ? paeth_run(row, pitch, avail, cap) : 0;
  /* wave_n from 3 rows: at 2 its 16 lanes of work for 2 rows are 1.13 times the scalar rows (the microbench, 1 byte) */
  if (k >= (bpp <= 2 ? 3u : 2u)) { paeth_rows_simd(row + 1, pitch, prev, n, bpp, k); return k; }
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
