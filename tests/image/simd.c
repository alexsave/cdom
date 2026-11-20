/* The differential test of the image module's hand-written wasm SIMD loops (tests/image/simd.sh builds it, tests/image/simd.mjs
 * runs it in node): every SIMD loop against its scalar counterpart, on random and on edge inputs, bit for bit.
 *
 *   idct4, idct2   jsimd_idct_4x4 and jsimd_idct_2x2 (src/jsimd/jsimd_wasm.c) against libjpeg's own jpeg_idct_4x4 and
 *                  jpeg_idct_2x2 (jidctred.c, built here without SIMD): random coefficient blocks, quantization tables
 *                  and output columns, including the extreme values that make the C's 32-bit arithmetic wrap and the
 *                  blocks whose columns or rows hold only a DC term (the C's shortcuts)
 *   unread         every IDCT the decoder can pick (jpeg_idct_1x1 to 7x7 and islow, scalar, and the SIMD 2x2 and 4x4)
 *                  against cd_image_idct_reads, the table the progressive store keeps by: changing every coefficient
 *                  outside it, to any value, changes no output byte; and every coefficient inside it changes the
 *                  output in some case (the table is no larger than it must be)
 *   ycc            jsimd_color_deconvert against libjpeg's own YCbCr to RGBA (jdcolor.c, built here without SIMD, reached
 *                  through jinit_color_deconverter): widths 1 to 64 and more, random rows
 *   ycc_all        the same on every (y, cb, cr) of 8 bits: 16,777,216 pixels
 *   unfilter       cd_image_unfilter_simd against cd_image_unfilter_scalar: every filter (and two that are none), 1, 2,
 *                  3, 4, 6 and 8 bytes a pixel, widths 0 to 64 pixels and some larger, random and edge bytes
 *   runs           cd_image_unfilter_run_simd (the Paeth wavefronts over runs of up to 4 rows of 3 and 4 bytes a pixel,
 *                  16 of 1 and 8 of 2), and _scalar, against cd_image_unfilter_scalar row by row: plans of Paeth runs of
 *                  1 to 5 rows (3 and 4 bytes a pixel) or 1 to 17 (1 and 2) with None, Sub, Up and Avg rows between, 1 to
 *                  4 bytes a pixel, widths 1 to 9 pixels and 4031 and 4032, a zero or a random row
 *                  above the first, handed over a random number of rows at a time
 *   rgba           cd_image_rgb_to_rgba_simd against the scalar: widths 0 to 64 and some larger
 *   div            the scaler's divide, in doubles: sums up to 255 t, t up to 65500 squared, and the sums at the edges of the quotient
 *   across grey    cd_image_scaler_across_grey (the PNG path's grey read) against across on the pixels as RGBA: sw 1 to
 *                  70 and wide rows, every dw from 1 to sw's edges; each grey sum equals the three RGBA ones
 *   across, down   the scaler's two row loops: random sizes from 1 to 64 pixels and more, edge weights and sums, and an
 *                  output that starts as garbage (across writes every sum, down adds to what is there)
 *   adler32        cd_image_adler32 (src/cdom_image_inflate.c, whose 16-byte blocks are SIMD here) against a byte at a time
 *                  with the modulo at every byte: every length 0 to 64, lengths about one, two and three times zlib's NMAX
 *                  (5552, the most bytes between two modulos) with 0 to 15 bytes of tail, random lengths up to 100,000,
 *                  random and all-255 bytes (the largest sums), and starting values with s1 and s2 at their largest
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

/* ---- the coefficients each IDCT reads ---- */

extern void jpeg_idct_1x1(j_decompress_ptr, jpeg_component_info *, JCOEFPTR, JSAMPARRAY, JDIMENSION);
extern void jpeg_idct_3x3(j_decompress_ptr, jpeg_component_info *, JCOEFPTR, JSAMPARRAY, JDIMENSION);
extern void jpeg_idct_5x5(j_decompress_ptr, jpeg_component_info *, JCOEFPTR, JSAMPARRAY, JDIMENSION);
extern void jpeg_idct_6x6(j_decompress_ptr, jpeg_component_info *, JCOEFPTR, JSAMPARRAY, JDIMENSION);
extern void jpeg_idct_7x7(j_decompress_ptr, jpeg_component_info *, JCOEFPTR, JSAMPARRAY, JDIMENSION);
extern void jpeg_idct_islow(j_decompress_ptr, jpeg_component_info *, JCOEFPTR, JSAMPARRAY, JDIMENSION);

typedef void (*idct_fn)(j_decompress_ptr, jpeg_component_info *, JCOEFPTR, JSAMPARRAY, JDIMENSION);

static void idct_run(idct_fn f, int size, struct jpeg_decompress_struct *cinfo, jpeg_component_info *comp, JCOEF *c, uint8_t out[8][8]) {
  JSAMPROW rows[8];
  memset(out, 0, 64);
  for (int r = 0; r < size; r++) rows[r] = out[r];
  f(cinfo, comp, c, rows, 0);
}

static void unread_cases(void) {
  static const struct { int size; idct_fn f; } t[] = {
    {1, jpeg_idct_1x1}, {2, jpeg_idct_2x2}, {2, jsimd_idct_2x2}, {3, jpeg_idct_3x3}, {4, jpeg_idct_4x4},
    {4, jsimd_idct_4x4}, {5, jpeg_idct_5x5}, {6, jpeg_idct_6x6}, {7, jpeg_idct_7x7}, {8, jpeg_idct_islow}};
  struct jpeg_decompress_struct cinfo;
  jpeg_component_info comp;
  memset(&cinfo, 0, sizeof cinfo);
  make_range_table(&cinfo);
  static JCOEF coef[64], other[64];
  static int quant[64];
  uint8_t a[8][8], b[8][8];
  for (uint32_t e = 0; e < sizeof t / sizeof t[0]; e++) {
    uint64_t reads = cd_image_idct_reads(t[e].size), seen = 0;
    for (uint32_t k = 0; k < 6000; k++) {
      cases = e * 6000 + k;
      fill_block(coef, (int)(k % 13));
      fill_quant(quant, (int)((k / 13) % 5));
      comp.dct_table = quant;
      idct_run(t[e].f, t[e].size, &cinfo, &comp, coef, a);
      memcpy(other, coef, sizeof coef);                 /* every coefficient outside the table: any value */
      for (int i = 0; i < 64; i++) if (!(reads >> i & 1)) other[i] = (JCOEF)rnd();
      idct_run(t[e].f, t[e].size, &cinfo, &comp, other, b);
      if (neq(a, b, 64)) note(1 + e);
      memcpy(other, coef, sizeof coef);                 /* one coefficient inside it: changed */
      int j;
      do j = (int)below(64); while (!(reads >> j & 1));
      other[j] = (JCOEF)(other[j] + 1 + (int)below(300));
      idct_run(t[e].f, t[e].size, &cinfo, &comp, other, b);
      if (neq(a, b, 64)) seen |= 1ull << j;
    }
    if (seen != reads) note(100 + e);                   /* a coefficient in the table that never mattered */
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

/* PNG rows as the sink hands them over (cd_image_unfilter_run): a block of rows, each its filter byte and n bytes, the
 * filters a plan of Paeth runs of 1 to 5 rows with rows of the other filters between. The reference unfilters it row by
 * row with cd_image_unfilter_scalar; the runs (the SIMD one; and the scalar one, which is a row at a time) are driven as the sink does,
 * a random number of rows at a time (the rows past the window). Every byte is compared, and the rows each call answers
 * against the run the plan has there. The block's last byte is the end of memory (a read past the last row traps). */
static void runs_case(uint32_t bpp, uint32_t n, const uint8_t *plan, uint32_t rows, int zero_prev, int scalar_run) {
  const uint32_t pitch = n + 1, size = pitch * rows;
  uint8_t *ref = place(size, 0, 40), *got = place(size, 1, 0);
  uint8_t *prev = place(n + 1, 0, 50) + 1;
  for (uint32_t i = 0; i < n; i++) prev[i] = zero_prev ? 0 : byte_edge();
  for (uint32_t r = 0; r < rows; r++) {
    ref[r * pitch] = got[r * pitch] = plan[r];
    for (uint32_t i = 1; i < pitch; i++) ref[r * pitch + i] = got[r * pitch + i] = below(4) ? (uint8_t)rnd() : byte_edge();
  }
  for (uint32_t r = 0; r < rows; r++)
    if (!cd_image_unfilter_scalar(plan[r], ref + r * pitch + 1, r ? ref + (r - 1) * pitch + 1 : prev, n, bpp)) note(5);
  uint32_t done = 0;
  while (done < rows) {
    uint32_t avail = below(3) ? 1 + below(6) : rows - done;
    if (avail > rows - done) avail = rows - done;
    uint32_t want = 0;
    const uint32_t cap = bpp >= 3 ? 4 : bpp == 1 ? 16 : n % 2 ? 0 : 8;   /* a run's rows: wave_v 4, wave_n 16 or 8 */
    while (want < avail && want < cap && plan[done + want] == 4) want++;
    if (want < (bpp <= 2 ? 3u : 2u) || scalar_run) want = 1;   /* wave_n from 3 rows, wave_v from 2 */
    uint8_t *row = got + done * pitch;
    const uint8_t *above = done ? row - pitch + 1 : prev;
    uint32_t k = scalar_run ? cd_image_unfilter_run_scalar(row, pitch, above, n, bpp, avail)
                            : cd_image_unfilter_run_simd(row, pitch, above, n, bpp, avail);
    if (k != want) { note(4); break; }
    done += k;
  }
  if (neq(ref, got, size)) note(1);
}
static void runs_all(void) {
  static const uint32_t widths[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 4031, 4032};
  uint32_t k = 0;
  uint8_t plan[128];
  for (uint32_t b = 1; b <= 4; b++)
    for (uint32_t wi = 0; wi < sizeof widths / sizeof widths[0]; wi++)
      for (int rep = 0; rep < (widths[wi] > 9 ? 6 : 40); rep++) {
        /* a plan: runs of Paeth of each length 1 to most (5 for 3 and 4 bytes a pixel, whose runs are 4 rows; 17 for 1
         * and 2, whose runs are 16 and 8) in a rotating order, 0 to 2 rows of None, Sub, Up or Avg between (rep 0 and 1:
         * no rows between, so runs meet; rep 2: one run of most alone) */
        const uint32_t most = b >= 3 ? 5 : 17, cap_rows = b >= 3 ? 20 : 100;
        uint32_t rows = 0;
        if (rep == 2) { for (; rows < most; rows++) plan[rows] = 4; }
        else for (uint32_t r = 0; r < 5 && rows < cap_rows; r++) {
          uint32_t len = 1 + (r * (most > 5 ? 4 : 1) + (uint32_t)rep) % most;
          for (uint32_t i = 0; i < len; i++) plan[rows++] = 4;
          uint32_t gap = rep < 2 ? 0 : below(3);
          for (uint32_t i = 0; i < gap; i++) plan[rows++] = (uint8_t)below(4);
        }
        uint32_t n = widths[wi] * b + (rep % 7 == 6 ? below(b) : 0);   /* mostly whole pixels; a few not */
        cases = k++;
        runs_case(b, n, plan, rows, rep % 3 == 0, 0);
        cases = k++;
        runs_case(b, n, plan, rows, rep % 3 == 0, 1);
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
      /* h: dw * 3 sums, not zero (garbage the loops must not add to: across writes every sum), and one more word, which
       * neither may touch */
      uint32_t words = dw * 3 + 1;
      uint32_t *ha = (uint32_t *)place(words * 4, 0, 81), *hb = (uint32_t *)place(words * 4, !top, 82);
      for (uint32_t i = 0; i < words; i++) ha[i] = hb[i] = rnd();
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
    for (uint32_t j = 0; j < words; j++) ha[j] = hb[j] = rnd();
    cd_image_scaler_across_scalar(ha, px, sw, dw);
    cd_image_scaler_across_simd(hb, px, sw, dw);
    if (neq(ha, hb, words * 4)) note(1);
  }
}

/* The grey read (cd_image_scaler_across_grey, scalar in both builds) against across on the same pixels as RGBA (R = G =
 * B): each of its dw sums equals the three across makes for that output pixel, and it writes no word past them. The
 * grey row's last byte is the end of memory, so a read past the row traps. */
static void across_grey_all(void) {
  uint32_t k = 0;
  for (uint32_t i = 0; i < 2500; i++) {
    cases = k++;
    uint32_t sw = i < 2100 ? 1 + i / 30 : 1 + below(4032), dw = 1 + below(sw);   /* sw 1 to 70, then wide rows */
    if (i % 30 == 0) dw = sw;
    if (i % 30 == 1) dw = 1;
    uint8_t *g = place(sw, 1, 86), *px = place(sw * 4, 0, 87);
    for (uint32_t j = 0; j < sw; j++) {
      g[j] = i % 4 == 0 || i >= 2100 ? byte_edge() : (uint8_t)rnd();
      px[4 * j] = px[4 * j + 1] = px[4 * j + 2] = g[j]; px[4 * j + 3] = (uint8_t)rnd();
    }
    uint32_t *ha = (uint32_t *)place(dw * 3 * 4, 0, 88), *hb = (uint32_t *)place((dw + 1) * 4, 0, 89);
    for (uint32_t j = 0; j <= dw; j++) hb[j] = rnd();
    hb[dw] = 0xC0FFEE11u;
    cd_image_scaler_across_scalar(ha, px, sw, dw);
    cd_image_scaler_across_grey(hb, g, sw, dw);
    for (uint32_t j = 0; j < dw; j++)
      if (hb[j] != ha[3 * j] || ha[3 * j] != ha[3 * j + 1] || ha[3 * j] != ha[3 * j + 2]) { note(1); break; }
    if (hb[dw] != 0xC0FFEE11u) note(2);
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

static void div_all(void) {
  uint32_t k = 0;
  for (uint32_t n = 0; n <= 70; n++)
    for (int rep = 0; rep < 30; rep++) {
      cases = k++;
      uint64_t sw = rep < 3 ? 65500 : 1 + below(65500), sh = rep < 3 ? 65500 : rep < 6 ? 1 + below(4) : 1 + below(65500);
      uint64_t t = sw * sh;                                         /* what the scaler divides by, up to 65500 squared */
      int top = rep & 1;
      uint64_t *a = (uint64_t *)place(n * 8, top, 100);
      uint8_t *oa = place(n, 0, 101), *ob = place(n, !top, 102);
      for (uint32_t i = 0; i < n; i++) {
        uint64_t v;
        switch (rnd() % 4) {
          case 0: v = ((uint64_t)rnd() << 32 | rnd()) % (255 * t + 1); break;       /* anywhere up to 255 t */
          case 1: { uint64_t q = below(256);                       /* at a boundary of the quotient: 2a + t is q * 2t, less 1, plus 1 */
                    uint64_t base = q * t >= (t + 1) / 2 ? q * t - (t + 1) / 2 : 0;
                    v = base + below(5); if (v > 255 * t) v = 255 * t; break; }
          case 2: v = below(2) ? 0 : 255 * t; break;
          default: v = 255 * t - below(3) < 255 * t ? 255 * t - below(3) : 255 * t; break;
        }
        a[i] = v;
      }
      cd_image_scaler_div_scalar(oa, a, n, t);
      cd_image_scaler_div_simd(ob, a, n, t);
      if (neq(oa, ob, n)) note(1);
      if (!canary_ok(ob, n, !top)) note(2);
    }
}

/* ---- adler32 ---- */

static uint32_t adler_bytes(uint32_t a, const uint8_t *s, uint32_t n) {   /* the definition, one byte at a time */
  uint32_t s1 = a & 0xffff, s2 = a >> 16;
  for (uint32_t i = 0; i < n; i++) { s1 = (s1 + s[i]) % 65521u; s2 = (s2 + s1) % 65521u; }
  return s2 << 16 | s1;
}
static void adler_case(uint32_t n, int kind) {
  uint8_t *s = place(n, 1, 0);
  for (uint32_t i = 0; i < n; i++) s[i] = kind == 0 ? (uint8_t)rnd() : kind == 1 ? 255 : byte_edge();
  uint32_t a = below(3) == 0 ? 1u : below(2) ? 65520u << 16 | 65520u : below(65521) << 16 | below(65521);
  if (cd_image_adler32(a, s, n) != adler_bytes(a, s, n)) note(1);
}
static void adler_all(void) {
  uint32_t k = 0;
  for (uint32_t n = 0; n <= 64; n++)
    for (int rep = 0; rep < 12; rep++) { cases = k++; adler_case(n, rep % 3); }
  for (uint32_t m = 1; m <= 3; m++)                                  /* about m x NMAX, and its tails of 0 to 15 bytes */
    for (int d = -48; d <= 48; d++)
      for (int kind = 0; kind < 2; kind++) { cases = k++; adler_case(m * 5552u + (uint32_t)d, kind); }
  for (uint32_t t = 0; t < 16; t++)                                  /* whole blocks, then a tail of t */
    for (int rep = 0; rep < 8; rep++) { cases = k++; adler_case(16 * (1 + below(2000)) + t, rep % 3); }
  for (int rep = 0; rep < 200; rep++) { cases = k++; adler_case(below(100001), rep % 3); }
  cases = k++; adler_case(100000, 1);                                /* 18 NMAX runs of 255s */
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
    case 8: div_all(); break;
    case 9: adler_all(); break;
    case 10: unread_cases(); break;
    case 11: runs_all(); break;
    case 12: across_grey_all(); break;
    default: return 0xffffffffu;
  }
  (void)fails;
  return detail;                                                   /* 0: every case equal; else what differed in the first case that did */
}
EXPORT("first_case") uint32_t get_first_case(void) { return first_case; }
EXPORT("last_case") uint32_t get_last_case(void) { return cases; }
