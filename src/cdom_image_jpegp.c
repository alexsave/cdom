/* cdom_image's progressive JPEG path (docs/image-progressive-speed.md): cdom's own entropy decoder for progressive scans
 * and its own coefficient store, in place of libjpeg-turbo's jdphuff.c and, for a progressive file only, jdcoefct.c.
 * Bit-exact with them: the image out is the one libjpeg-turbo makes, on every file, corrupt ones included.
 *
 * How it plugs in, with no edit to the vendored sources: jdmaster.c calls jinit_phuff_decoder and then
 * jinit_d_coef_controller. Both are here. jdphuff.c is not compiled (cdom.mk); libjpeg's coefficient controller is,
 * under another name (src/cdom_image_ljt_coef.c builds jdcoefct.c as cdom_ljt_d_coef_controller), and
 * jinit_d_coef_controller below hands it every file that is not progressive.
 *
 * What is different from libjpeg's, and why the image is the same:
 *  - The store keeps, per block, only the coefficients the component's IDCT reads (cd_image_idct_reads), in zigzag
 *    order, and a 64-bit mask of which coefficients are nonzero (a refinement scan needs that at every position of its
 *    band, read or not). The IDCT gets a block with those coefficients in place and zeros elsewhere, which it never
 *    reads. At IDCT size 8 every coefficient is kept and the mask is made from the values instead, so the store is
 *    never larger than libjpeg's 128 bytes a block. It is one libjpeg virtual array per component, so libjpeg's
 *    budget (realize_virt_arrays) still decides whether it fits.
 *  - Scans no IDCT needs are not decoded: the markers after the first scan are walked once (the whole file is in
 *    memory), and for each component the decoded coefficients are the closure of the ones its IDCT reads under "a scan
 *    that touches a decoded coefficient makes all it touches decoded". A scan that touches none is jumped over to the
 *    next marker that is not a restart marker, which is where libjpeg's decode of it would end too.
 *  - The bits: a 64-bit buffer, a 10-bit lookahead, and refinement driven by the nonzero mask (the correction bits of
 *    all the nonzero coefficients a step passes are read at once). Where libjpeg sets insufficient_data (the first bit
 *    taken past the end of the scan's data) this sets it too, at the same bit, and every bit past it is a zero, as in
 *    libjpeg; restarts go through libjpeg's own read_restart_marker. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>
#include "cdom_image_int.h"
#define JPEG_INTERNALS
#include "jinclude.h"
#include "jpeglib.h"
#include "jdhuff.h"
#include "jerror.h"

#define ALWAYS static inline __attribute__((always_inline))
#define LOOK 10                                   /* lookahead bits */

/* libjpeg's own coefficient controller (src/cdom_image_ljt_coef.c), for every file that is not progressive */
void cdom_ljt_d_coef_controller(j_decompress_ptr cinfo, boolean need_full_buffer);

/* One component's store: rows of blocks, each row [nonzero masks, 8 bytes a block][values, 2 * k bytes a block]. */
typedef struct {
  jvirt_sarray_ptr arr;
  JSAMPARRAY rows;            /* every row, once the array is realized */
  uint32_t bpr, nrows;        /* blocks a row, rows (padded as libjpeg pads its arrays) */
  uint32_t k;                 /* values kept a block */
  uint32_t vals_off;          /* the values' offset in a row */
  uint8_t has_mask;
  uint64_t need;              /* zigzag positions the IDCT reads */
  uint64_t dec;               /* zigzag positions decoded (need, closed over the scans) */
  int8_t slot[64];            /* zigzag position to value index, or -1 */
  uint8_t nat[64];            /* value index to natural position */
} pcomp;

typedef struct { uint8_t ncomp, comp[MAX_COMPS_IN_SCAN], ss, se, ah, al; } pscan;

typedef struct { uint16_t e[1 << LOOK]; } fast_tbl;   /* (length << 8) | symbol, or 0: longer than LOOK bits */

typedef struct {
  struct jpeg_entropy_decoder pub;        /* first: cinfo->entropy points here */
  struct jpeg_d_coef_controller coef;
  d_derived_tbl *dct[NUM_HUFF_TBLS], *act[NUM_HUFF_TBLS];
  fast_tbl fdc[NUM_HUFF_TBLS], fac;
  int32_t facv[1 << LOOK];                /* AC first: (value << 16) | (run << 8) | bits, symbol and value in one */
  uint16_t rtab[1 << LOOK];               /* AC refinement: bits | run << 5 | (new coefficient: 2 | sign) << 9, or 0 */
  pcomp c[MAX_COMPONENTS];
  pscan *scans;                           /* the walk's scans, or 0 when it gave up (then nothing is skipped) */
  uint32_t nscans, scan_at;               /* scans walked; scans started */
  uint8_t skip, skipped_any;
  JCOEF blk[DCTSIZE2];                    /* the block the IDCT gets */
} pctx;

/* ---- the scan walk ---- */

/* The first 0xFF at or after p[i], or n: eight bytes at a time while there are eight. */
static size_t find_ff(const uint8_t *p, size_t i, size_t n) {
  for (; i + 8 <= n; i += 8) {
    uint64_t w;
    memcpy(&w, p + i, 8);
    if ((~w - 0x0101010101010101ull) & w & 0x8080808080808080ull) break;   /* a 0xFF byte: ~w has a zero byte */
  }
  while (i < n && p[i] != 0xff) i++;
  return i;
}

/* The offset of the first marker that ends a scan's data (0xFF, more 0xFF, then neither 0 nor RSTn), at the first 0xFF
 * of it; n when there is none. */
static size_t scan_end(const uint8_t *p, size_t n) {
  size_t i = 0;
  for (;;) {
    size_t s = find_ff(p, i, n), j = s + 1;
    if (s >= n) return n;
    while (j < n && p[j] == 0xff) j++;
    if (j >= n) return n;
    if (p[j] != 0 && (p[j] < 0xd0 || p[j] > 0xd7)) return s;
    i = j + 1;
  }
}

/* The zigzag positions a scan can write: libjpeg's AC loops write up to 15 past Se on corrupt data (and refinement one
 * past it), to position 63 at most. */
static uint64_t touched(uint32_t ss, uint32_t se) {
  if (ss == 0) return 1;
  uint32_t hi = se + 15 > 63 ? 63 : se + 15;
  return (hi == 63 ? ~0ull : (2ull << hi) - 1) & ~((1ull << ss) - 1);
}

/* A band libjpeg's start_pass accepts (a scan it refuses stops the decode, so nothing after it matters) */
static int scan_ok(const pscan *q) {
  return q->ss <= q->se && q->se <= 63 && (q->ss != 0 || q->se == 0) && (q->ss == 0 || q->ncomp == 1);
}

/* Walks the file's markers from the first scan's data, as libjpeg's read_markers will: every scan's components and
 * band. Leaves x->scans 0 at anything it does not fully follow (a marker it does not know, a scan header libjpeg would
 * refuse), and then nothing is skipped. */
static void walk(j_decompress_ptr cinfo, pctx *x) {
  const uint8_t *p = cinfo->src->next_input_byte;
  size_t n = cinfo->src->bytes_in_buffer;
  uint32_t cap = CD_IMAGE_SCANS_MAX + 2, ns = 0;
  pscan *s = (*cinfo->mem->alloc_small)((j_common_ptr)cinfo, JPOOL_IMAGE, cap * sizeof *s);
  s[0].ncomp = (uint8_t)cinfo->comps_in_scan;
  for (int i = 0; i < cinfo->comps_in_scan; i++) s[0].comp[i] = (uint8_t)cinfo->cur_comp_info[i]->component_index;
  s[0].ss = (uint8_t)cinfo->Ss; s[0].se = (uint8_t)cinfo->Se; s[0].ah = (uint8_t)cinfo->Ah; s[0].al = (uint8_t)cinfo->Al;
  if (!scan_ok(&s[0])) return;
  ns = 1;
  size_t at = scan_end(p, n);
  for (;;) {
    if (at >= n) break;                                   /* the file ends: libjpeg reads its own EOI there */
    while (at < n && p[at] == 0xff) at++;
    if (at >= n) break;
    uint32_t m = p[at++];
    if (m == 0) { at = scan_end(p + at, n - at) + at; continue; }   /* cannot follow 0xFF: next_marker skips it */
    if (m == 0xd9) break;                                 /* EOI */
    if (m == 0x01 || (m >= 0xd0 && m <= 0xd7)) goto next; /* TEM, RSTn: no length */
    if (at + 2 > n) break;
    uint32_t len = (uint32_t)p[at] << 8 | p[at + 1];
    if (m == 0xda) {                                      /* SOS: as get_sos reads it */
      if (at + 3 > n) break;
      uint32_t c = p[at + 2];
      if (len != c * 2 + 6 || c < 1 || c > MAX_COMPS_IN_SCAN || at + len > n || ns >= cap) return;
      pscan *q = &s[ns];
      int cur[MAX_COMPS_IN_SCAN] = {-1, -1, -1, -1};
      for (uint32_t i = 0; i < c; i++) {
        uint32_t id = p[at + 3 + 2 * i];
        int found = -1;
        for (int ci = 0; ci < cinfo->num_components && ci < MAX_COMPS_IN_SCAN; ci++)
          if ((uint32_t)cinfo->comp_info[ci].component_id == id && cur[ci] < 0) { found = ci; break; }
        if (found < 0) return;
        for (uint32_t pi = 0; pi < i; pi++) if (cur[pi] == found) return;
        cur[i] = found;
        q->comp[i] = (uint8_t)found;
      }
      q->ncomp = (uint8_t)c;
      q->ss = p[at + 3 + 2 * c]; q->se = p[at + 4 + 2 * c]; q->ah = p[at + 5 + 2 * c] >> 4; q->al = p[at + 5 + 2 * c] & 15;
      if (!scan_ok(q)) return;
      ns++;
      at += len;
      at = scan_end(p + at, n - at) + at;
      continue;
    }
    if (!((m >= 0xe0 && m <= 0xef) || m == 0xfe || m == 0xc4 || m == 0xdb || m == 0xdd || m == 0xcc || m == 0xdc)) return;
    if (len < 2 || at + len > n) break;                   /* libjpeg reaches the end of the file in it */
    at += len;
  next:
    while (at < n && p[at] != 0xff) at++;                 /* next_marker: bytes before a marker are skipped */
  }
  x->scans = s;
  x->nscans = ns;
}

/* ---- the entropy decoder: start of a scan ---- */

static void build_fast(fast_tbl *f, const d_derived_tbl *d) {
  for (uint32_t v = 0; v < (1u << LOOK); v++) {
    f->e[v] = 0;
    for (int l = 1; l <= LOOK; l++) {
      long code = (long)(v >> (LOOK - l));
      if (code <= d->maxcode[l]) { f->e[v] = (uint16_t)(l << 8 | d->pub->huffval[(int)(code + d->valoffset[l])]); break; }
    }
  }
}

ALWAYS int extend(int r, int s) { return r < (1 << (s - 1)) ? r + (int)((~0u << s) + 1) : r; }

static void build_acfirst(pctx *x) {
  for (uint32_t v = 0; v < (1u << LOOK); v++) {
    uint32_t e = x->fac.e[v], len = e >> 8, r = (e >> 4) & 15, s = e & 15;
    x->facv[v] = 0;
    if (e && s && len + s <= LOOK) {
      int val = extend((int)((v >> (LOOK - len - s)) & ((1u << s) - 1)), (int)s);
      x->facv[v] = (int32_t)((uint32_t)val << 16 | r << 8 | (len + s));
    }
  }
}

static void build_refine(pctx *x) {
  for (uint32_t v = 0; v < (1u << LOOK); v++) {
    uint32_t e = x->fac.e[v], len = e >> 8, r = (e >> 4) & 15, z = e & 15;
    x->rtab[v] = 0;
    if (!e) continue;
    if (z == 1 && len + 1 <= LOOK) x->rtab[v] = (uint16_t)((len + 1) | r << 5 | (2 | (v >> (LOOK - len - 1) & 1)) << 9);
    else if (z == 0 && r == 15) x->rtab[v] = (uint16_t)(len | 15 << 5);
  }
}

static boolean no_mcu(j_decompress_ptr cinfo, JBLOCKROW *b) { (void)b; ERREXIT(cinfo, JERR_NOT_COMPILED); return FALSE; }

/* jdphuff.c's start_pass_phuff_decoder, the same checks and the same warnings, then this decoder's own setup. */
static void start_pass(j_decompress_ptr cinfo) {
  pctx *x = (pctx *)cinfo->entropy;
  int is_dc = cinfo->Ss == 0, bad = 0;
  if (is_dc) { if (cinfo->Se != 0) bad = 1; }
  else { if (cinfo->Ss > cinfo->Se || cinfo->Se >= DCTSIZE2) bad = 1; if (cinfo->comps_in_scan != 1) bad = 1; }
  if (cinfo->Ah != 0 && cinfo->Al != cinfo->Ah - 1) bad = 1;
  if (cinfo->Al > 13) bad = 1;
  if (bad) ERREXIT4(cinfo, JERR_BAD_PROGRESSION, cinfo->Ss, cinfo->Se, cinfo->Ah, cinfo->Al);
  for (int ci = 0; ci < cinfo->comps_in_scan; ci++) {
    int cindex = cinfo->cur_comp_info[ci]->component_index;
    int *cb = &cinfo->coef_bits[cindex][0], *prev = &cinfo->coef_bits[cindex + cinfo->num_components][0];
    if (!is_dc && cb[0] < 0) WARNMS2(cinfo, JWRN_BOGUS_PROGRESSION, cindex, 0);
    for (int k = MIN(cinfo->Ss, 1); k <= MAX(cinfo->Se, 9); k++) prev[k] = cinfo->input_scan_number > 1 ? cb[k] : 0;
    for (int k = cinfo->Ss; k <= cinfo->Se; k++) {
      int expected = cb[k] < 0 ? 0 : cb[k];
      if (cinfo->Ah != expected) WARNMS2(cinfo, JWRN_BOGUS_PROGRESSION, cindex, k);
      cb[k] = cinfo->Al;
    }
  }
  for (int ci = 0; ci < cinfo->comps_in_scan; ci++) {
    jpeg_component_info *cp = cinfo->cur_comp_info[ci];
    int tbl = is_dc ? cp->dc_tbl_no : cp->ac_tbl_no;     /* jpeg_make_d_derived_tbl's own refusal, before the index */
    if ((!is_dc || cinfo->Ah == 0) && (tbl < 0 || tbl >= NUM_HUFF_TBLS)) ERREXIT1(cinfo, JERR_NO_HUFF_TABLE, tbl);
    if (is_dc) {
      if (cinfo->Ah == 0) {
        jpeg_make_d_derived_tbl(cinfo, TRUE, cp->dc_tbl_no, &x->dct[cp->dc_tbl_no]);
        build_fast(&x->fdc[cp->dc_tbl_no], x->dct[cp->dc_tbl_no]);
      }
    } else {
      jpeg_make_d_derived_tbl(cinfo, FALSE, cp->ac_tbl_no, &x->act[cp->ac_tbl_no]);
      build_fast(&x->fac, x->act[cp->ac_tbl_no]);
      if (cinfo->Ah == 0) build_acfirst(x); else build_refine(x);
    }
  }
  x->pub.insufficient_data = FALSE;
  /* Skip or decode: the walk must have seen this scan as libjpeg read it; a scan it did not see is decoded, and when
   * an earlier scan was skipped or a store has no mask for it, the decode cannot be libjpeg's, so it stops. */
  uint32_t i = x->scan_at++;
  x->skip = 0;
  if (x->scans) {
    const pscan *q = i < x->nscans ? &x->scans[i] : 0;
    int same = q && q->ncomp == cinfo->comps_in_scan && q->ss == cinfo->Ss && q->se == cinfo->Se && q->ah == cinfo->Ah && q->al == cinfo->Al;
    for (int ci = 0; same && ci < cinfo->comps_in_scan; ci++) same = q->comp[ci] == cinfo->cur_comp_info[ci]->component_index;
    if (same) {
      if (!is_dc) x->skip = !(touched((uint32_t)cinfo->Ss, (uint32_t)cinfo->Se) & x->c[cinfo->cur_comp_info[0]->component_index].dec);
    } else {
      if (x->skipped_any) ERREXIT(cinfo, JERR_NOTIMPL);
      x->scans = 0;
    }
  }
  if (!x->skip && !is_dc) {
    pcomp *pc = &x->c[cinfo->cur_comp_info[0]->component_index];
    if (!pc->has_mask && pc->k < DCTSIZE2) ERREXIT(cinfo, JERR_NOTIMPL);
  }
  x->skipped_any |= x->skip;
}

/* ---- the bits ---- */

typedef struct {
  const uint8_t *p;           /* the source's bytes not read yet */
  size_t n;
  uint64_t buf;               /* the next bit at the top */
  int bits;                   /* bits in buf */
  int real;                   /* bits in buf that came from the file; the rest are zeros after a marker */
  j_decompress_ptr cinfo;
} br;

static int next_byte(br *b) {
  if (!b->n) {
    struct jpeg_source_mgr *s = b->cinfo->src;
    s->next_input_byte = b->p; s->bytes_in_buffer = 0;
    if (!(*s->fill_input_buffer)(b->cinfo)) ERREXIT(b->cinfo, JERR_NOTIMPL);   /* cdom's source never suspends */
    b->p = s->next_input_byte; b->n = s->bytes_in_buffer;
  }
  b->n--;
  return *b->p++;
}

/* jpeg_fill_bit_buffer's bytes: 0xFF 0x00 is 0xFF (and more 0xFF before the 0x00 are taken as one); 0xFF then anything
 * else is the marker that ends the data, and from there the buffer is filled with zeros. */
static void refill(br *b) {
  while (b->bits <= 56) {
    if (b->cinfo->unread_marker) { b->bits = 64; return; }
    if (b->n >= 8) {
      uint64_t w;
      memcpy(&w, b->p, 8);
      if (!((~w - 0x0101010101010101ull) & w & 0x8080808080808080ull)) {   /* no 0xFF byte (~w has no zero byte) */
        w = __builtin_bswap64(w);
        int k = (64 - b->bits) >> 3, sh = 64 - b->bits - 8 * k;
        b->buf |= (w >> b->bits) >> sh << sh;
        b->p += k; b->n -= (size_t)k; b->bits += 8 * k; b->real += 8 * k;
        continue;
      }
    }
    int c = next_byte(b);
    if (c == 0xff) {
      do c = next_byte(b); while (c == 0xff);
      if (c) { b->cinfo->unread_marker = c; b->bits = 64; return; }
      c = 0xff;
    }
    b->buf |= (uint64_t)c << (56 - b->bits);
    b->bits += 8; b->real += 8;
  }
}

ALWAYS void ensure(br *b, int n) { if (b->bits < n) refill(b); }

/* refill's common case on a loop's local copy of the buffer: eight bytes with no 0xFF among them (and no marker met
 * yet), taken as whole bytes; 0 when it is not that case and refill must run. */
ALWAYS int refill_fast(br *b, uint64_t *buf, int *bits, int *real) {
  if (b->n < 8 || b->cinfo->unread_marker) return 0;
  uint64_t w;
  memcpy(&w, b->p, 8);
  if ((~w - 0x0101010101010101ull) & w & 0x8080808080808080ull) return 0;
  w = __builtin_bswap64(w);
  int k = (64 - *bits) >> 3, sh = 64 - *bits - 8 * k;
  *buf |= (w >> *bits) >> sh << sh;
  b->p += k; b->n -= (size_t)k; *bits += 8 * k; *real += 8 * k;
  return 1;
}

/* The first bit taken past the file's bits (take, take_any, and the loop below) */
static void crossed(br *b) {
  if (!b->cinfo->entropy->insufficient_data) { WARNMS(b->cinfo, JWRN_HIT_MARKER); b->cinfo->entropy->insufficient_data = TRUE; }
  b->real = 0;
}

/* Takes n bits (1 to 32), which must be in the buffer. The first one taken past the file's bits is where libjpeg
 * warns and sets insufficient_data. */
ALWAYS uint32_t take(br *b, int n) {
  uint32_t v = (uint32_t)(b->buf >> (64 - n));
  b->buf <<= n; b->bits -= n;
  if (n > b->real) crossed(b);
  else b->real -= n;
  return v;
}

ALWAYS uint32_t get(br *b, int n) { ensure(b, n); return take(b, n); }

/* n bits, 0 to 63, the first taken at the top */
ALWAYS uint64_t get_wide(br *b, int n) {
  if (n <= 32) return n ? get(b, n) : 0;
  uint64_t hi = get(b, 32);
  return hi << (n - 32) | get(b, n - 32);
}

/* A Huffman symbol: the lookahead, or jpeg_huff_decode's bit-by-bit rule (a code longer than 16 bits is 17 bits taken,
 * a warning, and the symbol 0). */
ALWAYS int decode(br *b, const fast_tbl *f, const d_derived_tbl *d) {
  ensure(b, 32);
  uint32_t e = f->e[b->buf >> (64 - LOOK)];
  if (e) { take(b, (int)(e >> 8)); return (int)(e & 0xff); }
  int l = LOOK + 1;
  long code = (long)(b->buf >> (64 - l));
  while (l <= 16 && code > d->maxcode[l]) { l++; code = (long)(b->buf >> (64 - l)); }
  if (l > 16) { take(b, 17); WARNMS(b->cinfo, JWRN_HUFF_BAD_CODE); return 0; }
  take(b, l);
  return d->pub->huffval[(int)(code + d->valoffset[l])];
}

/* ---- the store ---- */

ALWAYS uint64_t *mask_at(const pcomp *pc, uint32_t row, uint32_t col) { return (uint64_t *)(void *)pc->rows[row] + col; }
ALWAYS int16_t *vals_at(const pcomp *pc, uint32_t row, uint32_t col) { return (int16_t *)(void *)(pc->rows[row] + pc->vals_off) + (size_t)col * pc->k; }

/* the nonzero mask of a block kept whole (k is 64, values in zigzag order) */
static uint64_t mask_of(const int16_t *v) {
  uint64_t m = 0;
  for (int i = 0; i < DCTSIZE2; i++) m |= (uint64_t)(v[i] != 0) << i;
  return m;
}

/* A first scan's coefficient at zigzag position k (past 63: 63, as jpeg_natural_order's tail). */
ALWAYS void put(const pcomp *pc, uint64_t *m, int16_t *v, int k, int val, int al) {
  int z = k < 64 ? k : 63;
  int16_t cv = (int16_t)(uint16_t)((uint32_t)val << al);
  if (pc->has_mask) *m = (*m & ~(1ull << z)) | ((uint64_t)(cv != 0) << z);
  if (pc->slot[z] >= 0) v[pc->slot[z]] = cv;
}

/* ---- the scans ---- */

typedef struct {
  br b;
  pctx *x;
  uint32_t eobrun;
  uint32_t rtg;               /* MCUs left in this restart interval */
  int last_dc[MAX_COMPS_IN_SCAN];
} scan;

/* jdphuff.c's process_restart */
static void restart(scan *s) {
  j_decompress_ptr cinfo = s->b.cinfo;
  cinfo->marker->discarded_bytes += (unsigned)(s->b.real / 8);
  s->b.buf = 0; s->b.bits = s->b.real = 0;
  cinfo->src->next_input_byte = s->b.p; cinfo->src->bytes_in_buffer = s->b.n;
  if (!(*cinfo->marker->read_restart_marker)(cinfo)) ERREXIT(cinfo, JERR_NOTIMPL);
  s->b.p = cinfo->src->next_input_byte; s->b.n = cinfo->src->bytes_in_buffer;
  for (int ci = 0; ci < cinfo->comps_in_scan; ci++) s->last_dc[ci] = 0;
  s->eobrun = 0;
  s->rtg = cinfo->restart_interval;
  if (cinfo->unread_marker == 0) cinfo->entropy->insufficient_data = FALSE;
}

static void ac_first_block(scan *s, const pcomp *pc, uint64_t *m, int16_t *v, int ss, int se, int al) {
  br *b = &s->b;
  const pctx *x = s->x;
  const d_derived_tbl *d = x->act[s->b.cinfo->cur_comp_info[0]->ac_tbl_no];
  uint64_t buf = b->buf;                                  /* the bit buffer in locals, back in *b around every call */
  int bits = b->bits, real = b->real;
  for (int k = ss; k <= se; k++) {
    if (bits < 32 && !refill_fast(b, &buf, &bits, &real)) { b->buf = buf; b->bits = bits; b->real = real; refill(b); buf = b->buf; bits = b->bits; real = b->real; }
    int32_t fe = x->facv[buf >> (64 - LOOK)];
    if (fe && (fe & 31) <= real) {                        /* symbol and value in one, all of it from the file */
      int n = fe & 31;
      buf <<= n; bits -= n; real -= n;
      k += (fe >> 8) & 15;
      put(pc, m, v, k, fe >> 16, al);
      continue;
    }
    b->buf = buf; b->bits = bits; b->real = real;
    int sym = decode(b, &x->fac, d), r = sym >> 4, z = sym & 15;
    if (z) { k += r; put(pc, m, v, k, extend((int)get(b, z), z), al); }
    else if (r == 15) k += 15;
    else { uint32_t e = 1u << r; if (r) e += get(b, r); s->eobrun = e - 1; buf = b->buf; bits = b->bits; real = b->real; break; }
    buf = b->buf; bits = b->bits; real = b->real;
  }
  b->buf = buf; b->bits = bits; b->real = real;
}

/* The bits of `bits` (n of them, the first at the top) put at the positions of `at` (n set bits, the lowest first). */
ALWAYS uint64_t deposit(uint64_t bits, uint64_t at, int n) {
  if (n == 1) return (bits & 1) << __builtin_ctzll(at);
  uint64_t out = 0;
  while (at) { n--; out |= (bits >> n & 1) << __builtin_ctzll(at); at &= at - 1; }
  return out;
}

/* A refinement scan's block. The correction bits are gathered in a mask (cm) and the new coefficients in two (nw, ng),
 * and the stored values change once, at the end: libjpeg's per-coefficient updates commute, since a position is passed
 * once a block, and the only one that can be both corrected and made new (63, when the band ends there) takes its
 * correction first and its new value last, as here. */
static void ac_refine_block(scan *s, const pcomp *pc, uint64_t *m, int16_t *v, int ss, int se, int p1) {
  br *b = &s->b;
  const pctx *x = s->x;
  const d_derived_tbl *d = x->act[s->b.cinfo->cur_comp_info[0]->ac_tbl_no];
  uint64_t nz = pc->has_mask ? *m : mask_of(v);
  uint64_t band = (se == 63 ? ~0ull : (2ull << se) - 1) & ~((1ull << ss) - 1), cm = 0, nw = 0, ng = 0;
  int k = ss;
  /* the bit buffer in locals, back in *b around every call */
  uint64_t buf = b->buf;
  int bits = b->bits, real = b->real;
#define OUT() (b->buf = buf, b->bits = bits, b->real = real)
#define IN() (buf = b->buf, bits = b->bits, real = b->real)
#define DROP(n) do { int n_ = (n); buf <<= n_; bits -= n_; if (n_ > real) { OUT(); crossed(b); IN(); } else real -= n_; } while (0)
  if (s->eobrun == 0) {
    while (k <= se) {
      if (bits < 32 && !refill_fast(b, &buf, &bits, &real)) { OUT(); refill(b); IN(); }
      uint32_t e = x->rtab[buf >> (64 - LOOK)];
      int r, nv;
      if (e) {                                            /* a run and a new coefficient with its sign, or a ZRL */
        DROP((int)(e & 31));
        r = (int)(e >> 5 & 15);
        nv = (int)(e >> 9 & 3);                           /* 0: none, 2: negative, 3: positive */
      } else {
        OUT();
        int sym = decode(b, &x->fac, d), z = sym & 15;
        r = sym >> 4; nv = 0;
        if (z) {
          if (z != 1) WARNMS(b->cinfo, JWRN_HUFF_BAD_CODE);
          nv = 2 | (int)get(b, 1);
        } else if (r != 15) {
          uint32_t eob = 1u << r;
          if (r) eob += get(b, r);
          s->eobrun = eob;
          IN();
          break;
        }
        IN();
      }
      /* pass the nonzero coefficients from k on, with their correction bits, and r zero ones; stop at the next zero.
       * Branch-free for runs up to 3 and up to 4 correction bits, the common case; the chain from one symbol to the
       * next is then the lookup, the zero search, a popcount and a shift. */
      uint64_t rng = band & (~0ull << k), zs = ~nz & rng;
      if (r <= 3) {
        uint64_t z1 = zs & (zs - 1), z2 = z1 & (z1 - 1), z3 = z2 & (z2 - 1);
        zs = r == 0 ? zs : r == 1 ? z1 : r == 2 ? z2 : z3;
      } else for (; r > 0 && zs; r--) zs &= zs - 1;
      int to = zs ? __builtin_ctzll(zs) : se + 1;
      uint64_t passed = nz & rng & ((zs & (0 - zs)) - 1);
      int n = __builtin_popcountll(passed);
      if (n > 57) { OUT(); cm |= deposit(get_wide(b, n), passed, n); IN(); }
      else {
        if (n > bits && !(refill_fast(b, &buf, &bits, &real) && n <= bits)) { OUT(); refill(b); IN(); }
        uint64_t w = buf, l0 = passed & (0 - passed), a1 = passed ^ l0, l1 = a1 & (0 - a1), a2 = a1 ^ l1;
        uint64_t l2 = a2 & (0 - a2), a3 = a2 ^ l2, l3 = a3 & (0 - a3), a4 = a3 ^ l3;
        DROP(n);
        cm |= (l0 & (0 - (w >> 63))) | (l1 & (0 - (w >> 62 & 1))) | (l2 & (0 - (w >> 61 & 1))) | (l3 & (0 - (w >> 60 & 1)));
        if (a4) cm |= deposit(w << 4 >> (64 - (n - 4)), a4, n - 4);
      }
      k = to;
      if (nv) {
        uint64_t bit = 1ull << (k < 64 ? k : 63);
        nz |= bit; nw |= bit;
        ng = (ng & ~bit) | (nv == 2 ? bit : 0);
      }
      k++;
    }
  }
  OUT();
#undef OUT
#undef IN
#undef DROP
  if (s->eobrun > 0) {
    if (k <= se) {
      uint64_t passed = nz & band & (~0ull << k);
      int n = __builtin_popcountll(passed);
      if (n) cm |= deposit(get_wide(b, n), passed, n);
    }
    s->eobrun--;
  }
  for (uint64_t q = cm & pc->need; q; q &= q - 1) {
    int16_t *c = &v[pc->slot[__builtin_ctzll(q)]];
    if ((*c & p1) == 0) *c = (int16_t)(*c + (*c >= 0 ? p1 : -p1));
  }
  for (uint64_t q = nw & pc->need; q; q &= q - 1) {
    int j = __builtin_ctzll(q);
    v[pc->slot[j]] = (int16_t)(ng >> j & 1 ? -p1 : p1);
  }
  if (pc->has_mask) *m = nz;
}

/* Decodes MCUs [i, i + cnt) of the scan (no restart marker among them). */
static void run_mcus(scan *s, uint32_t i, uint32_t cnt) {
  j_decompress_ptr cinfo = s->b.cinfo;
  pctx *x = s->x;
  uint32_t per_row = cinfo->MCUs_per_row, row = i / per_row, col = i % per_row, end = i + cnt;
  int ss = cinfo->Ss, se = cinfo->Se, al = cinfo->Al, ah = cinfo->Ah;
  if (ss) {                                               /* AC: one block an MCU, one component */
    const pcomp *pc = &x->c[cinfo->cur_comp_info[0]->component_index];
    while (i < end) {
      if (cinfo->entropy->insufficient_data) return;      /* the rest of the interval is left as it is */
      uint32_t go = 1;
      if (ah == 0 && s->eobrun) {                         /* blocks in an EOB run: nothing to do */
        go = s->eobrun < end - i ? s->eobrun : end - i;
        s->eobrun -= go;
      } else if (ah == 0) ac_first_block(s, pc, mask_at(pc, row, col), vals_at(pc, row, col), ss, se, al);
      else ac_refine_block(s, pc, mask_at(pc, row, col), vals_at(pc, row, col), ss, se, 1 << al);
      i += go; col += go;
      while (col >= per_row) { col -= per_row; row++; }
    }
    return;
  }
  /* DC: every block of the MCU, at the offsets libjpeg's consume_data gives them (by component, then row, then column) */
  const pcomp *bpc[D_MAX_BLOCKS_IN_MCU];
  uint32_t by[D_MAX_BLOCKS_IN_MCU], bx[D_MAX_BLOCKS_IN_MCU], mh[D_MAX_BLOCKS_IN_MCU], mw[D_MAX_BLOCKS_IN_MCU];
  int bci[D_MAX_BLOCKS_IN_MCU], nb = cinfo->blocks_in_MCU;
  for (int blkn = 0, first = 0; blkn < nb; blkn++) {
    int ci = cinfo->MCU_membership[blkn];
    jpeg_component_info *cp = cinfo->cur_comp_info[ci];
    if (blkn && ci != bci[blkn - 1]) first = 0;
    bci[blkn] = ci; bpc[blkn] = &x->c[cp->component_index];
    mh[blkn] = (uint32_t)cp->MCU_height; mw[blkn] = (uint32_t)cp->MCU_width;
    by[blkn] = (uint32_t)first / mw[blkn]; bx[blkn] = (uint32_t)first % mw[blkn];
    first++;
  }
  for (; i < end; i++) {
    if (ah == 0 && cinfo->entropy->insufficient_data) { if (++col == per_row) { col = 0; row++; } continue; }
    for (int blkn = 0; blkn < nb; blkn++) {
      int ci = bci[blkn];
      int16_t *v = vals_at(bpc[blkn], row * mh[blkn] + by[blkn], col * mw[blkn] + bx[blkn]);
      if (ah == 0) {
        jpeg_component_info *cp = cinfo->cur_comp_info[ci];
        int t = decode(&s->b, &x->fdc[cp->dc_tbl_no], x->dct[cp->dc_tbl_no]);
        if (t) t = extend((int)get(&s->b, t), t);
        int last = s->last_dc[ci];
        if ((last >= 0 && t > INT_MAX - last) || (last < 0 && t < INT_MIN - last)) ERREXIT(cinfo, JERR_BAD_DCT_COEF);
        t += last;
        s->last_dc[ci] = t;
        v[0] = (int16_t)(uint16_t)((uint32_t)t << al);
      } else if (get(&s->b, 1)) v[0] = (int16_t)(v[0] | (1 << al));
    }
    if (++col == per_row) { col = 0; row++; }
  }
}

#ifdef CDOM_IMAGE_TESTING
uint32_t cd_image_jpegp_skipped;                          /* scans skipped, for tests/image/prog.c */
#endif

/* consume_data: the whole scan in one call. */
static int consume(j_decompress_ptr cinfo) {
  pctx *x = (pctx *)cinfo->entropy;
  struct jpeg_source_mgr *src = cinfo->src;
  if (x->skip) {                                          /* to the marker that ends the scan */
#ifdef CDOM_IMAGE_TESTING
    cd_image_jpegp_skipped++;
#endif
    size_t e = scan_end(src->next_input_byte, src->bytes_in_buffer);
    src->next_input_byte += e; src->bytes_in_buffer -= e;
  } else {
    scan s;
    memset(&s, 0, sizeof s);
    s.b.p = src->next_input_byte; s.b.n = src->bytes_in_buffer; s.b.cinfo = cinfo;
    s.x = x;
    s.rtg = cinfo->restart_interval;
    uint32_t total = cinfo->MCU_rows_in_scan * cinfo->MCUs_per_row, i = 0;
    while (i < total) {
      uint32_t cnt = total - i;
      if (cinfo->restart_interval) {
        if (s.rtg == 0) restart(&s);
        if (cnt > s.rtg) cnt = s.rtg;
        s.rtg -= cnt;
      }
      run_mcus(&s, i, cnt);
      i += cnt;
    }
    src->next_input_byte = s.b.p; src->bytes_in_buffer = s.b.n;
  }
  cinfo->input_iMCU_row = cinfo->total_iMCU_rows;
  (*cinfo->inputctl->finish_input_pass)(cinfo);
  return JPEG_SCAN_COMPLETED;
}

/* ---- the coefficient controller ---- */

static void start_input_pass(j_decompress_ptr cinfo) {
  pctx *x = (pctx *)cinfo->entropy;
  cinfo->input_iMCU_row = 0;
  for (int ci = 0; ci < cinfo->num_components; ci++)
    if (!x->c[ci].rows)
      x->c[ci].rows = (*cinfo->mem->access_virt_sarray)((j_common_ptr)cinfo, x->c[ci].arr, 0, x->c[ci].nrows, TRUE);
}

static void start_output_pass(j_decompress_ptr cinfo) { cinfo->output_iMCU_row = 0; }

/* jdcoefct.c's decompress_data, each block made whole (zeros where the IDCT does not read) for the IDCT */
static int decompress(j_decompress_ptr cinfo, JSAMPIMAGE out) {
  pctx *x = (pctx *)cinfo->entropy;
  JDIMENSION last_row = cinfo->total_iMCU_rows - 1;
  while (cinfo->input_scan_number < cinfo->output_scan_number ||
         (cinfo->input_scan_number == cinfo->output_scan_number && cinfo->input_iMCU_row <= cinfo->output_iMCU_row))
    if ((*cinfo->inputctl->consume_input)(cinfo) == JPEG_SUSPENDED) return JPEG_SUSPENDED;
  jpeg_component_info *cp = cinfo->comp_info;
  for (int ci = 0; ci < cinfo->num_components; ci++, cp++) {
    if (!cp->component_needed) continue;
    const pcomp *pc = &x->c[ci];
    int rows = cp->v_samp_factor;
    if (cinfo->output_iMCU_row == last_row) { rows = (int)(cp->height_in_blocks % (JDIMENSION)cp->v_samp_factor); if (!rows) rows = cp->v_samp_factor; }
    inverse_DCT_method_ptr idct = cinfo->idct->inverse_DCT[ci];
    JSAMPARRAY op = out[ci];
    memset(x->blk, 0, sizeof x->blk);
    for (int r = 0; r < rows; r++) {
      uint32_t brow = cinfo->output_iMCU_row * (uint32_t)cp->v_samp_factor + (uint32_t)r;
      JDIMENSION oc = 0;
      for (JDIMENSION bn = cinfo->master->first_MCU_col[ci]; bn <= cinfo->master->last_MCU_col[ci]; bn++) {
        const int16_t *v = vals_at(pc, brow, bn);
        for (uint32_t i = 0; i < pc->k; i++) x->blk[pc->nat[i]] = v[i];
        (*idct)(cinfo, cp, x->blk, op, oc);
        oc += (JDIMENSION)cp->DCT_h_scaled_size;
      }
      op += cp->DCT_h_scaled_size;
    }
  }
  if (++cinfo->output_iMCU_row < cinfo->total_iMCU_rows) return JPEG_ROW_COMPLETED;
  return JPEG_SCAN_COMPLETED;
}

/* ---- the two entry points jdmaster.c calls ---- */

void jinit_phuff_decoder(j_decompress_ptr cinfo) {
  pctx *x = (*cinfo->mem->alloc_small)((j_common_ptr)cinfo, JPOOL_IMAGE, sizeof(pctx));
  memset(x, 0, sizeof *x);
  cinfo->entropy = &x->pub;
  x->pub.start_pass = start_pass;
  x->pub.decode_mcu = no_mcu;
  cinfo->coef_bits = (int (*)[DCTSIZE2])(*cinfo->mem->alloc_small)((j_common_ptr)cinfo, JPOOL_IMAGE,
                                                                  (size_t)cinfo->num_components * 2 * DCTSIZE2 * sizeof(int));
  for (int i = 0; i < cinfo->num_components * DCTSIZE2; i++) (&cinfo->coef_bits[0][0])[i] = -1;
}

/* The zigzag positions an IDCT of this size reads. */
static uint64_t need_zz(int size) {
  uint64_t nat = cd_image_idct_reads(size), z = 0;
  for (int k = 0; k < DCTSIZE2; k++) if (nat >> jpeg_natural_order[k] & 1) z |= 1ull << k;
  return z;
}

void jinit_d_coef_controller(j_decompress_ptr cinfo, boolean need_full_buffer) {
  if (!cinfo->progressive_mode || !need_full_buffer || cinfo->buffered_image || cinfo->entropy->start_pass != start_pass) {
    cdom_ljt_d_coef_controller(cinfo, need_full_buffer);
    return;
  }
  if (cinfo->data_precision != 8) ERREXIT1(cinfo, JERR_BAD_PRECISION, cinfo->data_precision);
  pctx *x = (pctx *)cinfo->entropy;
  walk(cinfo, x);
  jpeg_component_info *cp = cinfo->comp_info;
  for (int ci = 0; ci < cinfo->num_components; ci++, cp++) {
    pcomp *pc = &x->c[ci];
    pc->need = need_zz(cp->DCT_h_scaled_size);
    pc->dec = x->scans ? pc->need : ~0ull;
    for (int changed = 1; changed && x->scans;) {        /* the closure over the scans */
      changed = 0;
      for (uint32_t i = 0; i < x->nscans; i++)
        for (int j = 0; j < x->scans[i].ncomp; j++) {
          uint64_t t = touched(x->scans[i].ss, x->scans[i].se);
          if (x->scans[i].comp[j] == ci && (t & pc->dec) && (t & ~pc->dec)) { pc->dec |= t; changed = 1; }
        }
    }
    pc->k = (uint32_t)__builtin_popcountll(pc->need);
    pc->has_mask = pc->k < DCTSIZE2 && (pc->dec & ~1ull);
    memset(pc->slot, -1, sizeof pc->slot);
    for (int z = 0, i = 0; z < DCTSIZE2; z++)
      if (pc->need >> z & 1) { pc->slot[z] = (int8_t)i; pc->nat[i] = (uint8_t)jpeg_natural_order[z]; i++; }
    /* the size libjpeg gives its arrays (jdcoefct.c), and at least the MCUs of an interleaved scan */
    uint32_t w = (uint32_t)jround_up((long)cp->width_in_blocks, (long)cp->h_samp_factor);
    uint32_t h = (uint32_t)jround_up((long)cp->height_in_blocks, (long)cp->v_samp_factor);
    uint32_t wi = (uint32_t)jdiv_round_up((long)cinfo->image_width, (long)(cinfo->max_h_samp_factor * DCTSIZE)) * (uint32_t)cp->h_samp_factor;
    uint32_t hi = cinfo->total_iMCU_rows * (uint32_t)cp->v_samp_factor;
    pc->bpr = w > wi ? w : wi;
    pc->nrows = h > hi ? h : hi;
    uint64_t row = (uint64_t)pc->bpr * (pc->has_mask ? 8 : 0) + (uint64_t)pc->bpr * pc->k * 2;
    row = (row + 7) & ~7ull;
    if (row > 0xffffffffull) ERREXIT(cinfo, JERR_WIDTH_OVERFLOW);
    pc->vals_off = pc->has_mask ? pc->bpr * 8 : 0;
    pc->arr = (*cinfo->mem->request_virt_sarray)((j_common_ptr)cinfo, JPOOL_IMAGE, TRUE, (JDIMENSION)row, pc->nrows, pc->nrows);
  }
  x->coef.start_input_pass = start_input_pass;
  x->coef.consume_data = consume;
  x->coef.start_output_pass = start_output_pass;
  x->coef.decompress_data = decompress;
  x->coef.coef_arrays = 0;
  cinfo->coef = &x->coef;
}
