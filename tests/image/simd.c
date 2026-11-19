/* The differential test of the image module's hand-written wasm SIMD loops (tests/image/simd.sh builds it, tests/image/simd.mjs
 * runs it in node): every SIMD loop against its scalar counterpart, on random and on edge inputs, bit for bit.
 *
 *   idct4, idct2   jsimd_idct_4x4 and jsimd_idct_2x2 (src/jsimd/jsimd_wasm.c) against libjpeg's own jpeg_idct_4x4 and
 *                  jpeg_idct_2x2 (jidctred.c, built here without SIMD): random coefficient blocks, quantization tables
 *                  and output columns, including the extreme values that make the C's 32-bit arithmetic wrap and the
 *                  blocks whose columns or rows hold only a DC term (the C's shortcuts)
 *   ycc            jsimd_color_deconvert against libjpeg's own YCbCr to RGBA (jdcolor.c, built here without SIMD, reached
 *                  through jinit_color_deconverter): widths 1 to 64 and more, random rows
 *   ycc_all        the same on every (y, cb, cr) of 8 bits: 16,777,216 pixels
 *   unfilter       cd_image_unfilter_simd against cd_image_unfilter_scalar: every filter (and two that are none), 1, 2,
 *                  3, 4, 6 and 8 bytes a pixel, widths 0 to 64 pixels and some larger, random and edge bytes
 *   rgba           cd_image_rgb_to_rgba_simd against the scalar: widths 0 to 64 and some larger
 *   across, down   the scaler's two row loops: random sizes from 1 to 64 pixels and more, edge weights and sums
 *
 * Every input and output buffer is placed so that its last byte is the last byte of the module's memory (and the other one
 * far below), so a read or a write past the end is a trap, not a quiet wrong byte; a write before the start or past the
 * end is also looked for in canary bytes around every output. One seed makes every random input: SEED.
 * The module has no imports; run(loop) answers the number of cases that differed, and first_case, cases and detail say
 * where the first one was. */
#include <stdint.h>
#include <string.h>
#include "cdom_image_int.h"
#define JPEG_INTERNALS
#include "jinclude.h"
#include "jpeglib.h"
#include "jdct.h"
#include "simd/jsimddct.h"
#include "simd/jsimd.h"

#define EXPORT(n) __attribute__((export_name(n)))
#define SEED 20261119u                  /* the one named seed: change this line for another set of inputs */

static uint64_t rng_state;
static uint32_t rnd(void) {            /* splitmix64's output, 32 bits */
  uint64_t z = (rng_state += 0x9e3779b97f4a7c15ull);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
  return (uint32_t)((z ^ (z >> 31)) >> 16);
}
static uint32_t below(uint32_t n) { return rnd() % n; }
/* an edge-heavy byte: 0, 1, 127, 128, 254, 255 a third of the time */
static uint8_t byte_edge(void) { static const uint8_t e[] = {0, 1, 127, 128, 254, 255}; return below(3) ? (uint8_t)rnd() : e[below(6)]; }

static int neq(const void *a, const void *b, uint32_t n) {
  const uint8_t *x = a, *y = b;
  for (uint32_t i = 0; i < n; i++) if (x[i] != y[i]) return 1;
  return 0;
}

static uint32_t cases, first_case, detail;
static void note(uint32_t what) { if (!detail) { detail = what; first_case = cases; } }

/* ---- buffers at the end of memory ---- */

#define PAGE 65536u
#define CANARY 0xA5
static uint8_t *mem_end(void) { return (uint8_t *)((uintptr_t)__builtin_wasm_memory_size(0) * PAGE); }
/* A buffer of n bytes whose end is the end of memory (top) or well below it, with CANARY bytes of 32 before and after
 * unless it is at the very top (nothing after: a write past it traps). Contents: the canary, to be filled in. */
static uint8_t *place(uint32_t n, int top, int slot) {
  uint8_t *end = top ? mem_end() : mem_end() - (uintptr_t)(slot + 1) * 0x10000;
  uint8_t *p = end - n;
  memset(p - 32, CANARY, 32 + n);
  if (!top) memset(end, CANARY, 32);
  return p;
}
static int canary_ok(const uint8_t *p, uint32_t n, int top) {
  for (int i = 1; i <= 32; i++) if (p[-i] != CANARY) return 0;
  if (!top) for (uint32_t i = 0; i < 32; i++) if (p[n + i] != CANARY) return 0;
  return 1;
}

/* ---- the IDCTs ---- */

/* libjpeg's range limit table (jdmaster.c's prepare_range_limit_table), the table IDCT_range_limit indexes. */
static uint8_t range_table[5 * 256 + 128];
static void make_range_table(struct jpeg_decompress_struct *c) {
  uint8_t *t = range_table + 256;
  c->sample_range_limit = t;
  memset(t - 256, 0, 256);
  for (int i = 0; i <= 255; i++) t[i] = (uint8_t)i;
  t += 128;
  for (int i = 128; i < 512; i++) t[i] = 255;
  memset(t + 512, 0, 512 - 128);
  memcpy(t + 1024 - 128, c->sample_range_limit, 128);
}

extern void jpeg_idct_4x4(j_decompress_ptr, jpeg_component_info *, JCOEFPTR, JSAMPARRAY, JDIMENSION);
extern void jpeg_idct_2x2(j_decompress_ptr, jpeg_component_info *, JCOEFPTR, JSAMPARRAY, JDIMENSION);

static void fill_block(JCOEF *b, int kind) {
  for (int i = 0; i < 64; i++) b[i] = 0;
  switch (kind) {
    case 0: break;                                              /* all zero */
    case 1: b[0] = (JCOEF)rnd(); break;                         /* a DC term, any 16-bit value */
    case 2: b[0] = (JCOEF)(below(4096) - 2048); break;
    case 3: for (int i = 0; i < 64; i++) if (!below(6)) b[i] = (JCOEF)(below(257) - 128); break;      /* sparse, small */
    case 4: for (int i = 0; i < 64; i++) b[i] = (JCOEF)(below(2049) - 1024); break;                   /* dense, a valid file's range */
    case 5: for (int i = 0; i < 64; i++) b[i] = (JCOEF)rnd(); break;                                  /* any 16 bits: the C wraps */
    case 6: for (int i = 0; i < 64; i++) { static const int16_t e[] = {0, 1, -1, 32767, -32768, 255, -256, 2047, -2048}; b[i] = e[below(9)]; } break;
    case 7:                                                     /* columns that are DC only, the others dense: the shortcut per column */
      for (int c = 0; c < 8; c++) {
        int dc_only = (int)below(2);
        for (int r = 0; r < 8; r++) if (r == 0 || !dc_only) b[r * 8 + c] = (JCOEF)(kind == 7 ? (int)below(65536) - 32768 : 0);
      }
      break;
    case 8:                                                     /* rows 0 of columns, with AC only in a few columns */
      for (int c = 0; c < 8; c++) { b[c] = (JCOEF)rnd(); if (below(3) == 0) for (int r = 1; r < 8; r++) b[r * 8 + c] = (JCOEF)rnd(); }
      break;
    case 9: for (int i = 0; i < 64; i++) b[i] = (JCOEF)((below(2) ? 1 : -1) * (int)below(40)); break;  /* a few small values everywhere */
    case 10: for (int r = 0; r < 8; r++) b[r * 8] = (JCOEF)rnd(); break;                              /* column 0 only: every row of the work array is DC only */
    case 11: for (int c = 0; c < 8; c++) b[c] = (JCOEF)rnd(); break;                                  /* row 0 only */
    default: b[below(64)] = (JCOEF)rnd(); b[below(64)] = (JCOEF)(below(2) ? 32767 : -32768); break;    /* one or two single huge terms */
  }
}
static void fill_quant(int *q, int kind) {
  for (int i = 0; i < 64; i++)
    q[i] = kind == 0 ? 1 + (int)below(255)                      /* 8-bit table */
         : kind == 1 ? (int)below(65536)                        /* 16-bit table (a file may have one), 0 included */
         : kind == 2 ? 255
         : kind == 3 ? 1
         : (int)(rnd() % 8 == 0 ? 0 : 1 + below(40));
}

static void idct_cases(int n4) {
  struct jpeg_decompress_struct cinfo;
  jpeg_component_info comp;
  memset(&cinfo, 0, sizeof cinfo);
  make_range_table(&cinfo);
  static JCOEF coef[64] __attribute__((aligned(16)));
  static int quant[64] __attribute__((aligned(16)));
  const int rows = n4 ? 4 : 2, px = n4 ? 4 : 2;
  for (uint32_t k = 0; k < 65000; k++) {
    cases = k;
    fill_block(coef, (int)(k % 13));
    fill_quant(quant, (int)((k / 13) % 5));
    uint32_t col = below(9);                                    /* the output column: any, so the stores are unaligned */
    int top = k % 3 == 0;                                       /* which one is at the top of memory: the last output row, the coefficients or the table */
    uint32_t span = col + (uint32_t)px;                         /* a row ends at its last pixel: at the top, a store past it traps */
    uint8_t *ra[4], *rb[4];
    JSAMPROW pa[4], pb[4];
    for (int r = 0; r < rows; r++) {
      ra[r] = place(span, 0, 2 * r);
      rb[r] = place(span, top && r == rows - 1, 2 * r + 1);
      pa[r] = ra[r]; pb[r] = rb[r];
    }
    /* the coefficients and the table are read by both calls, one of them at the top of memory (a read past it traps) */
    JCOEF *cc = coef; int *qq = quant;
    if (k % 3 == 1) { cc = (JCOEF *)place(128, 1, 20); memcpy(cc, coef, 128); }
    else if (k % 3 == 2) { qq = (int *)place(256, 1, 21); memcpy(qq, quant, 256); }
    comp.dct_table = qq;
    (n4 ? jpeg_idct_4x4 : jpeg_idct_2x2)(&cinfo, &comp, cc, pa, col);
    (n4 ? jsimd_idct_4x4 : jsimd_idct_2x2)(&cinfo, &comp, cc, pb, col);
    for (int r = 0; r < rows; r++) {
      if (neq(ra[r], rb[r], span)) note(1 + (uint32_t)r);       /* the pixels, and every other byte of the row, equal */
      if (!canary_ok(rb[r], span, top && r == rows - 1)) note(10 + (uint32_t)r);
    }
  }
}

/* ---- YCbCr to RGBA ---- */

static void *arena_alloc(j_common_ptr c, int pool, size_t n) {
  static uint8_t arena[1 << 16], *at = arena;
  (void)c; (void)pool;
  void *p = at;
  at += (n + 15) & ~(size_t)15;
  return p;
}
static void err_exit(j_common_ptr c) { (void)c; __builtin_trap(); }

static void (*ycc_ref)(j_decompress_ptr, JSAMPIMAGE, JDIMENSION, JSAMPARRAY, int);
static struct jpeg_decompress_struct ycc_cinfo;
static void ycc_setup(void) {
  static struct jpeg_memory_mgr mem;
  static struct jpeg_error_mgr err;
  memset(&ycc_cinfo, 0, sizeof ycc_cinfo);
  make_range_table(&ycc_cinfo);
  mem.alloc_small = arena_alloc;
  err.error_exit = err_exit;
  ycc_cinfo.mem = &mem;
  ycc_cinfo.err = &err;
  ycc_cinfo.num_components = 3;
  ycc_cinfo.jpeg_color_space = JCS_YCbCr;
  ycc_cinfo.out_color_space = JCS_EXT_RGBA;
  ycc_cinfo.data_precision = 8;
  jinit_color_deconverter(&ycc_cinfo);
  ycc_ref = ycc_cinfo.cconvert->color_convert;
}

static void ycc_case(uint32_t w, uint32_t nrows, int kind, int top) {
  /* three input planes of nrows rows of w bytes (each plane its own buffer), and the outputs of nrows rows of w pixels */
  static uint8_t *in[3][3];
  JSAMPROW oa[3], ob[3];
  uint8_t *outa[3], *outb[3];
  JSAMPROW ip[3][3];
  for (int p = 0; p < 3; p++)
    for (uint32_t r = 0; r < nrows; r++) {
      in[p][r] = place(w, top && p == 2 && r == nrows - 1, 22 + (int)(p * 3 + r));
      for (uint32_t i = 0; i < w; i++) in[p][r][i] = kind == 0 ? (uint8_t)rnd() : kind == 1 ? byte_edge() : kind == 2 ? (p == 0 ? 255 : 0) : 128;
      ip[p][r] = in[p][r];
    }
  for (uint32_t r = 0; r < nrows; r++) {
    outa[r] = place(w * 4, 0, 40 + (int)r); outb[r] = place(w * 4, !top && r == nrows - 1, 50 + (int)r);
    oa[r] = outa[r]; ob[r] = outb[r];
  }
  JSAMPARRAY planes[3] = {ip[0], ip[1], ip[2]};
  ycc_cinfo.output_width = w;
  JSAMPROW *oap = oa, *obp = ob;
  ycc_ref(&ycc_cinfo, planes, 0, oap, (int)nrows);
  jsimd_color_deconvert(&ycc_cinfo, planes, 0, obp, (int)nrows);
  for (uint32_t r = 0; r < nrows; r++) {
    if (neq(outa[r], outb[r], w * 4)) note(1);
    if (!canary_ok(outb[r], w * 4, !top && r == nrows - 1)) note(2);
  }
}

static void ycc_random(void) {
  ycc_setup();
  for (uint32_t k = 0; k < 6000; k++) {
    cases = k;
    uint32_t w = k < 3200 ? 1 + k % 64 : 1 + below(1500);       /* every width 1 to 64 (fifty times each), then up to 1500 */
    ycc_case(w, 1 + below(3), (int)(k % 4), (int)(k & 1));
  }
}

/* every (y, cb, cr): a row of 256 pixels is every y, for one (cb, cr) */
static void ycc_all(void) {
  ycc_setup();
  static uint8_t y[256 + 16], cb[256 + 16], cr[256 + 16], oa[256 * 4 + 16], ob[256 * 4 + 16];
  for (int i = 0; i < 256; i++) y[i] = (uint8_t)i;
  JSAMPROW py[1] = {y}, pcb[1] = {cb}, pcr[1] = {cr}, ra[1] = {oa}, rb[1] = {ob};
  JSAMPARRAY planes[3] = {py, pcb, pcr};
  ycc_cinfo.output_width = 256;
  for (uint32_t c = 0; c < 65536; c++) {
    cases = c;
    memset(cb, (int)(c >> 8), 256); memset(cr, (int)(c & 255), 256);
    ycc_ref(&ycc_cinfo, planes, 0, ra, 1);
    jsimd_color_deconvert(&ycc_cinfo, planes, 0, rb, 1);
    if (neq(oa, ob, 256 * 4)) note(1);
  }
}

/* ---- the PNG row filters and the RGB expansion ---- */

static void unfilter_case(uint32_t f, uint32_t bpp, uint32_t n, int kind, int top_cur) {
  uint8_t *ca = place(n, 0, 60), *cb = place(n, top_cur, 61);
  uint8_t *pa = place(n + 1, !top_cur, 62) + 1;                  /* prev, from index 0 (a byte before it is part of the row above) */
  /* prev has n bytes; the buffer placed is one longer so that prev[-1] exists: both calls get the same prev */
  for (uint32_t i = 0; i < n; i++) {
    uint8_t v = kind == 0 ? (uint8_t)rnd() : kind == 1 ? byte_edge() : kind == 2 ? 255 : kind == 3 ? 0 : (uint8_t)(i & 1 ? 255 : 0);
    ca[i] = cb[i] = v;
    pa[i] = kind == 0 ? (uint8_t)rnd() : kind == 1 ? byte_edge() : kind == 2 ? 255 : kind == 3 ? 0 : (uint8_t)(i & 1 ? 0 : 255);
  }
  int ra = cd_image_unfilter_scalar(f, ca, pa, n, bpp), rb = cd_image_unfilter_simd(f, cb, pa, n, bpp);
  if (ra != rb) note(3);
  if (neq(ca, cb, n)) note(1);
  if (!canary_ok(cb, n, top_cur)) note(2);
}

static void unfilter_all(void) {
  static const uint32_t bpps[] = {1, 2, 3, 4, 6, 8};
  uint32_t k = 0;
  for (uint32_t f = 0; f <= 6; f++)
    for (uint32_t b = 0; b < 6; b++)
      for (uint32_t w = 0; w <= 64; w++)                          /* widths in pixels; n is w * bpp, and w+1 bytes more at some */
        for (int rep = 0; rep < 8; rep++) {
          cases = k++;
          uint32_t n = w * bpps[b] + (rep >= 6 ? below(bpps[b]) : 0);   /* mostly whole pixels; a few not */
          unfilter_case(f == 6 ? 255 : f, bpps[b], n, rep % 5, rep & 1);
        }
  for (uint32_t i = 0; i < 3000; i++) {                           /* and long rows, as a photo's */
    cases = k++;
    uint32_t bpp = bpps[below(6)], n = bpp * (65 + below(800));
    unfilter_case(below(5), bpp, n, (int)(i % 5), (int)(i & 1));
  }
}

static void rgba_all(void) {
  uint32_t k = 0;
  for (uint32_t w = 0; w <= 64 + 40; w++)
    for (int rep = 0; rep < 40; rep++) {
      cases = k++;
      uint32_t ww = w < 65 ? w : 65 + below(2000);
      int top = rep & 1;
      uint8_t *in = place(ww * 3, top, 70), *oa = place(ww * 4, 0, 71), *ob = place(ww * 4, !top, 72);
      for (uint32_t i = 0; i < ww * 3; i++) in[i] = rep % 5 == 0 ? byte_edge() : (uint8_t)rnd();
      cd_image_rgb_to_rgba_scalar(in, oa, ww);
      cd_image_rgb_to_rgba_simd(in, ob, ww);
      if (neq(oa, ob, ww * 4)) note(1);
      if (!canary_ok(ob, ww * 4, !top)) note(2);
    }
}

/* ---- the scaler's two row loops ---- */

static void across_all(void) {
  uint32_t k = 0;
  for (uint32_t sw = 1; sw <= 70; sw++)
    for (int rep = 0; rep < 30; rep++) {
      cases = k++;
      uint32_t dw = 1 + below(sw);                                 /* the scaler never enlarges: dw is 1 to sw */
      if (rep == 0) dw = sw;
      if (rep == 1) dw = 1;
      int top = rep & 1;
      uint8_t *px = place(sw * 4, top, 80);
      for (uint32_t i = 0; i < sw * 4; i++) px[i] = rep % 4 == 0 ? byte_edge() : (uint8_t)rnd();
      /* h: dw * 3 sums, zero, and one more word, which the SIMD body may read and write but not change */
      uint32_t words = dw * 3 + 1;
      uint32_t *ha = (uint32_t *)place(words * 4, 0, 81), *hb = (uint32_t *)place(words * 4, !top, 82);
      for (uint32_t i = 0; i < words; i++) ha[i] = hb[i] = 0;
      ha[dw * 3] = hb[dw * 3] = 0xC0FFEE11u;
      cd_image_scaler_across_scalar(ha, px, sw, dw);
      cd_image_scaler_across_simd(hb, px, sw, dw);
      if (neq(ha, hb, words * 4)) note(1);
      if (!canary_ok((uint8_t *)hb, words * 4, !top)) note(2);
    }
  for (uint32_t i = 0; i < 400; i++) {                               /* wide rows, with big weights: sw up to 4032, dw up to sw */
    cases = k++;
    uint32_t sw = 1 + below(4032), dw = 1 + below(sw);
    uint8_t *px = place(sw * 4, 0, 83);
    for (uint32_t j = 0; j < sw * 4; j++) px[j] = byte_edge();
    uint32_t words = dw * 3 + 1;
    uint32_t *ha = (uint32_t *)place(words * 4, 0, 84), *hb = (uint32_t *)place(words * 4, 1, 85);
    for (uint32_t j = 0; j < words; j++) ha[j] = hb[j] = 0;
    cd_image_scaler_across_scalar(ha, px, sw, dw);
    cd_image_scaler_across_simd(hb, px, sw, dw);
    if (neq(ha, hb, words * 4)) note(1);
  }
}

static void down_all(void) {
  uint32_t k = 0;
  static const uint64_t wgts[] = {0, 1, 2, 255, 256, 65535, 65536, 0xffffffffull, 0x100000000ull, 0x100000005ull, 0xffffffffffffffffull};
  for (uint32_t n = 0; n <= 70; n++)
    for (int rep = 0; rep < 24; rep++) {
      cases = k++;
      uint64_t wgt = rep < 11 ? wgts[rep] : rep < 18 ? below(65536) : ((uint64_t)rnd() << 16) ^ rnd();
      int top = rep & 1;
      uint32_t *h = (uint32_t *)place(n * 4, top, 90);
      uint64_t *aa = (uint64_t *)place(n * 8, 0, 91), *ab = (uint64_t *)place(n * 8, !top, 92);
      for (uint32_t i = 0; i < n; i++) {
        h[i] = rep % 3 == 0 ? 0xffffffffu : rep % 3 == 1 ? rnd() * 65537u : (uint32_t)below(65535 * 255 + 1);
        uint64_t a = ((uint64_t)rnd() << 32 | rnd()) >> (rep % 4 * 8);
        aa[i] = ab[i] = rep % 5 == 0 ? ~0ull - below(5) : a;       /* up against 2^64: the sum wraps the same */
      }
      cd_image_scaler_down_scalar(aa, h, wgt, n);
      cd_image_scaler_down_simd(ab, h, wgt, n);
      if (neq(aa, ab, n * 8)) note(1);
      if (!canary_ok((uint8_t *)ab, n * 8, !top)) note(2);
    }
}

/* ---- the entry ---- */

EXPORT("run") uint32_t run(uint32_t loop) {
  rng_state = (uint64_t)SEED * 0x2545F4914F6CDD1Dull + loop;
  cases = first_case = detail = 0;
  uint32_t fails = 0;
  switch (loop) {
    case 0: idct_cases(1); break;
    case 1: idct_cases(0); break;
    case 2: ycc_random(); break;
    case 3: ycc_all(); break;
    case 4: unfilter_all(); break;
    case 5: rgba_all(); break;
    case 6: across_all(); break;
    case 7: down_all(); break;
    default: return 0xffffffffu;
  }
  (void)fails;
  return detail;                                                   /* 0: every case equal; else what differed in the first case that did */
}
EXPORT("first_case") uint32_t get_first_case(void) { return first_case; }
EXPORT("last_case") uint32_t get_last_case(void) { return cases; }
