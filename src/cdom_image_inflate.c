/* cdom_image's inflate: a zlib stream (RFC 1950 around RFC 1951) read from pieces (a PNG's IDAT payloads) and
 * written into one bounded buffer, which the caller's sink empties from the front; the buffer always keeps the
 * last 32 KB, the most a distance reaches, so nothing ever holds the whole output. cdom_image_int.h has the
 * contract. Written for speed on wasm32 as well as natively: a 64-bit bit buffer refilled without a branch, an
 * 11-bit first-level table whose entries hold one or two literals, a length, the end of the block or a pointer to a
 * second level, wide match copies, and a fast loop that runs while the input and the buffer have room for its worst
 * case, with a checked loop (one symbol at a time, input across pieces, the end of the input) for the rest.
 * What is a valid stream is zlib's (inflate 1.3.1): the same header checks, the same rules on incomplete and
 * over-subscribed codes, a distance past the output so far is an error, and the adler32 must match. */
#include <stddef.h>
#include <string.h>
#include "cdom_image_int.h"
#ifdef __wasm_simd128__
#include <wasm_simd128.h>
#endif

/* ---- adler32 ---- */

#define ADLER_MOD 65521u
#define ADLER_NMAX 5552u   /* the most bytes before s2 can pass 2^32 (zlib's NMAX) */

#ifdef __wasm_simd128__
/* 16 bytes a step: s1 gains their sum; s2 gains 16 x s1 before the step plus the bytes weighted 16 down to 1. The
 * s1-before sums are kept per lane in p (multiplied by 16 at the end), the weighted sums in v2. */
static uint32_t adler_blocks(uint32_t *a, uint32_t *b, const uint8_t *s, size_t n) {
  uint32_t s1 = *a, s2 = *b;
  const v128_t w_lo = wasm_i16x8_make(16, 15, 14, 13, 12, 11, 10, 9), w_hi = wasm_i16x8_make(8, 7, 6, 5, 4, 3, 2, 1);
  size_t done = 0;
  while (n - done >= 16) {
    size_t k = (n - done) / 16;
    if (k > ADLER_NMAX / 16) k = ADLER_NMAX / 16;
    v128_t v1 = wasm_i32x4_splat(0), v2 = wasm_i32x4_splat(0), p = wasm_i32x4_splat(0);
    for (size_t c = 0; c < k; c++, s += 16) {
      v128_t x = wasm_v128_load(s);
      p = wasm_i32x4_add(p, v1);
      v1 = wasm_i32x4_add(v1, wasm_u32x4_extadd_pairwise_u16x8(wasm_u16x8_extadd_pairwise_u8x16(x)));
      v2 = wasm_i32x4_add(v2, wasm_i32x4_dot_i16x8(wasm_u16x8_extend_low_u8x16(x), w_lo));
      v2 = wasm_i32x4_add(v2, wasm_i32x4_dot_i16x8(wasm_u16x8_extend_high_u8x16(x), w_hi));
    }
    uint64_t h1 = (uint64_t)wasm_i32x4_extract_lane(v1, 0) + (uint32_t)wasm_i32x4_extract_lane(v1, 1) +
                  (uint32_t)wasm_i32x4_extract_lane(v1, 2) + (uint32_t)wasm_i32x4_extract_lane(v1, 3);
    uint64_t hp = (uint64_t)(uint32_t)wasm_i32x4_extract_lane(p, 0) + (uint32_t)wasm_i32x4_extract_lane(p, 1) +
                  (uint32_t)wasm_i32x4_extract_lane(p, 2) + (uint32_t)wasm_i32x4_extract_lane(p, 3);
    uint64_t h2 = (uint64_t)(uint32_t)wasm_i32x4_extract_lane(v2, 0) + (uint32_t)wasm_i32x4_extract_lane(v2, 1) +
                  (uint32_t)wasm_i32x4_extract_lane(v2, 2) + (uint32_t)wasm_i32x4_extract_lane(v2, 3);
    h1 = (uint32_t)h1;
    s2 = (uint32_t)((s2 + (uint64_t)s1 * (16 * k) + 16 * hp + h2) % ADLER_MOD);
    s1 = (uint32_t)((s1 + h1) % ADLER_MOD);
    done += 16 * k;
  }
  *a = s1; *b = s2;
  return (uint32_t)done;
}
#endif

uint32_t cd_image_adler32(uint32_t adler, const uint8_t *s, size_t n) {
  uint32_t s1 = adler & 0xffff, s2 = adler >> 16;
#ifdef __wasm_simd128__
  size_t v = adler_blocks(&s1, &s2, s, n);
  s += v; n -= v;
#endif
  while (n) {
    size_t k = n < ADLER_NMAX ? n : ADLER_NMAX;
    n -= k;
    for (; k >= 8; k -= 8, s += 8) {
      s1 += s[0]; s2 += s1; s1 += s[1]; s2 += s1; s1 += s[2]; s2 += s1; s1 += s[3]; s2 += s1;
      s1 += s[4]; s2 += s1; s1 += s[5]; s2 += s1; s1 += s[6]; s2 += s1; s1 += s[7]; s2 += s1;
    }
    for (; k; k--) { s1 += *s++; s2 += s1; }
    s1 %= ADLER_MOD; s2 %= ADLER_MOD;
  }
  return s2 << 16 | s1;
}

/* ---- the tables ----
 * An entry is 32 bits: bits 0-4 the bits it consumes, 5-7 its kind, 8-15 and 16-31 by kind:
 *   LIT1  16-23 the literal                                 LIT2  16-23 the first literal, 8-15 the second
 *   LEN   16-31 the base length, 8-11 the code's own bits (0-4 counts them and the extra bits)
 *   DIST  as LEN, for a distance                            END   the end of the block
 *   SUB   16-31 the second level's start, 8-11 its index bits (0-4 is the first level's bits)
 *   BAD   a code that is not in the alphabet (zlib: "invalid literal/length code", "invalid distance code") */
enum { K_LIT1 = 0, K_LIT2 = 1, K_LEN = 2, K_END = 3, K_SUB = 4, K_BAD = 5, K_DIST = 6 };
#define E_BITS(e) ((e) & 31u)
#define E_KIND(e) (((e) >> 5) & 7u)
#define ENT(bits, kind, lo, hi) ((uint32_t)(bits) | (uint32_t)(kind) << 5 | (uint32_t)(lo) << 8 | (uint32_t)(hi) << 16)

#define LBITS 11        /* the literal/length table's first level */
#define DBITS 9         /* the distance table's first level */

static const uint16_t len_base[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83,
                                      99, 115, 131, 163, 195, 227, 258};
static const uint8_t len_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5,
                                      5, 0};
static const uint16_t dist_base[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
                                       1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const uint8_t dist_extra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11,
                                       12, 12, 13, 13};

/* The entry a symbol's code decodes to, its code's own length c (the bits it consumes, for a second-level entry, are
 * c less the first level's). */
static uint32_t sym_entry(uint32_t sym, uint32_t c, int dist) {
  if (dist) return sym < 30 ? ENT(c + dist_extra[sym], K_DIST, c, dist_base[sym]) : ENT(c, K_BAD, 0, 0);
  if (sym < 256) return ENT(c, K_LIT1, 0, sym);
  if (sym == 256) return ENT(c, K_END, 0, 0);
  if (sym < 286) return ENT(c + len_extra[sym - 257], K_LEN, c, len_base[sym - 257]);
  return ENT(c, K_BAD, 0, 0);
}

static uint32_t rev(uint32_t code, uint32_t len) {   /* the code's bits reversed: deflate sends a code's top bit first */
  uint32_t r = 0;
  for (uint32_t i = 0; i < len; i++) r = r << 1 | ((code >> i) & 1);
  return r;
}

/* The table for n code lengths (lens) into t: a first level of 2^root entries, then second levels. 0 for a code zlib
 * refuses: over-subscribed, or incomplete unless it is one code of one bit (or, for the code-length code, any
 * incomplete code or none at all). dist: the symbols are distances; codes: the code-length code (each entry is then
 * the symbol and its length, ENT(len, 0, 0, sym), with no second level since its codes are at most 7 bits). */
static int build(const uint8_t *lens, uint32_t n, uint32_t *t, uint32_t tn, uint32_t root, int dist, int codes) {
  uint16_t count[16] = {0}, offs[16], sorted[288];
  for (uint32_t i = 0; i < n; i++) count[lens[i]]++;
  count[0] = 0;
  uint32_t max = 15;
  while (max && !count[max]) max--;
  int32_t left = 1;
  for (uint32_t l = 1; l <= 15; l++) { left = left * 2 - count[l]; if (left < 0) return 0; }   /* over-subscribed */
  if (max == 0) {                          /* no codes: every entry is BAD (zlib waits for a use to fail) */
    if (codes) return 0;                   /* zlib then reads every length as 0 and fails for want of an end code */
    for (uint32_t i = 0; i < (1u << root); i++) t[i] = ENT(1, K_BAD, 0, 0);
    return 1;
  }
  if (left > 0 && (codes || max != 1)) return 0;               /* incomplete */
  offs[1] = 0;
  for (uint32_t l = 1; l < 15; l++) offs[l + 1] = (uint16_t)(offs[l] + count[l]);
  for (uint32_t i = 0; i < n; i++) if (lens[i]) sorted[offs[lens[i]]++] = (uint16_t)i;
  /* incomplete (one code of one bit): the other half of the table is BAD */
  for (uint32_t i = 0; i < (1u << root); i++) t[i] = ENT(1, K_BAD, 0, 0);
  uint32_t code = 0, k = 0, next_sub = 1u << root;
  uint32_t sub_prefix = ~0u, sub_start = 0, sub_bits = 0;
  for (uint32_t l = 1; l <= max; l++) {
    for (uint32_t j = 0; j < count[l]; j++, k++) {
      uint32_t sym = sorted[k], r = rev(code, l);
      if (codes) {
        for (uint32_t i = r; i < (1u << root); i += 1u << l) t[i] = ENT(l, 0, 0, sym);
      } else if (l <= root) {
        uint32_t e = sym_entry(sym, l, dist);
        for (uint32_t i = r; i < (1u << root); i += 1u << l) t[i] = e;
      } else {
        uint32_t prefix = r & ((1u << root) - 1);
        if (prefix != sub_prefix) {
          /* a new second level: its size is what the codes under this prefix need, from this length on (the codes
           * come in increasing length, so this code is its shortest; count the rest the way zlib does) */
          uint32_t b = l - root;
          int32_t room = 1 << b;
          uint32_t ll = l;
          room -= count[ll] - j;
          while (room > 0 && ll < max) { ll++; b++; room = room * 2 - count[ll]; }
          sub_prefix = prefix; sub_start = next_sub; sub_bits = b;
          next_sub += 1u << b;
          if (next_sub > tn) return 0;     /* not reached (zlib's enough: 2340 and 592 entries), but checked */
          t[prefix] = ENT(root, K_SUB, b, sub_start);
        }
        uint32_t e = sym_entry(sym, l - root, dist);
        for (uint32_t i = r >> root; i < (1u << sub_bits); i += 1u << (l - root)) t[sub_start + i] = e;
      }
      code++;
    }
    code <<= 1;
  }
  if (!codes && !dist) {
    /* two literals in one entry where both codes fit the first level. From the top down, so the entry looked up for
     * the second (index i >> l1, below i) is still a one-symbol entry. */
    for (uint32_t i = (1u << root); i-- > 0;) {
      uint32_t e = t[i];
      if (E_KIND(e) != K_LIT1) continue;
      uint32_t l1 = E_BITS(e), e2 = t[i >> l1];
      if (E_KIND(e2) == K_LIT1 && l1 + E_BITS(e2) <= root) t[i] = ENT(l1 + E_BITS(e2), K_LIT2, e2 >> 16, e >> 16);
    }
  }
  return 1;
}

/* ---- the stream ---- */

typedef struct {
  uint64_t bb;             /* the bit buffer: the next bits of the stream from bit 0; nothing above bl */
  uint32_t bl;             /* its bits */
  uint32_t pad;            /* zero bytes put in past the end of the input (each 8 bits of bb) */
  const uint8_t *in, *end; /* the piece being read */
  cd_inflate_in more;
  void *ctx;
  int done;                /* no more pieces */
} bits;

/* The next byte, from this piece or the next; past the end, a zero counted in pad. */
static uint32_t next_byte(bits *s) {
  while (s->in == s->end) {
    const uint8_t *p; uint32_t n;
    if (s->done || !s->more(s->ctx, &p, &n)) { s->done = 1; s->pad++; return 0; }
    s->in = p; s->end = p + n;
  }
  return *s->in++;
}
/* At least n bits (n <= 56) in the buffer. */
static inline void need(bits *s, uint32_t n) {
  while (s->bl < n) { s->bb |= (uint64_t)next_byte(s) << s->bl; s->bl += 8; }
}
/* 1 when what was taken reaches into the padding: the stream was cut. */
static inline int cut(const bits *s) { return s->bl < 8 * s->pad; }
static inline uint32_t take(bits *s, uint32_t n) {
  need(s, n);
  uint32_t v = (uint32_t)(s->bb & ((1ull << n) - 1));
  s->bb >>= n; s->bl -= n;
  return v;
}

typedef struct {
  uint8_t *buf;            /* the output buffer: buf[0 .. pos) made and not dropped */
  uint32_t cap, pos;
  uint32_t adl;            /* buf[0 .. adl) is in the adler32 */
  uint32_t adler;
  uint64_t total;          /* bytes made since the start */
  cd_inflate_out out;
  void *ctx;
} sink;

/* Hands buf[0 .. pos) to the sink and drops what it lets go; 0 when the sink stops. */
static int flush(sink *o, int last) {
  o->adler = cd_image_adler32(o->adler, o->buf + o->adl, o->pos - o->adl);
  o->adl = o->pos;
  uint32_t can = last ? o->pos : o->pos > CD_INFLATE_WINDOW ? o->pos - CD_INFLATE_WINDOW : 0;
  int64_t drop = o->out(o->ctx, o->buf, o->pos, last);
  if (drop < 0 || (uint64_t)drop > can) return 0;
  if (drop) { memmove(o->buf, o->buf + drop, o->pos - (uint32_t)drop); o->pos -= (uint32_t)drop; o->adl = o->pos; }
  return 1;
}

/* Room the fast loop needs: in the buffer, the longest match and a wide copy's overshoot; in the piece, four
 * refills of 8 bytes. */
#define FAST_OUT (258 + 32)
#define FAST_IN 32

static inline uint64_t load64(const uint8_t *p) { uint64_t v; memcpy(&v, p, 8); return v; }   /* little-endian */

/* A match: len bytes from dist back, at o (room for len plus 31 bytes of overshoot). */
static inline void copy_match(uint8_t *o, uint32_t dist, uint32_t len) {
  const uint8_t *s = o - dist;
  uint8_t *e = o + len;
  if (dist >= 16) {
    do { memcpy(o, s, 16); o += 16; s += 16; } while (o < e);
  } else if (dist >= 8) {
    do { memcpy(o, s, 8); o += 8; s += 8; } while (o < e);
  } else if (dist == 1) {
    memset(o, s[0], len);
  } else {
    do { *o++ = *s++; } while (o < e);
  }
}

/* The body of one Huffman block, codes in lt and dt. 1 at its end, 0 on a bad stream or a stopped sink (*st). */
static int huff_block(bits *s, sink *o, const uint32_t *lt, const uint32_t *dt, uint32_t *st) {
  for (;;) {
    /* the fast loop: no checks of the input or the buffer inside */
    if (o->cap - o->pos >= FAST_OUT && s->end - s->in >= FAST_IN) {
      uint64_t bb = s->bb;
      uint32_t bl = s->bl;
      const uint8_t *in = s->in, *in_lim = s->end - FAST_IN;
      uint8_t *out = o->buf + o->pos, *out_lim = o->buf + o->cap - FAST_OUT;
      uint8_t *out0 = out;
      int end = 0, bad = 0;
#define REFILL() do { bb |= load64(in) << bl; in += (63 - bl) >> 3; bl |= 56; } while (0)
      while (out <= out_lim && in <= in_lim) {
        REFILL();                                   /* 56 bits or more: a length and a distance take at most 48 */
        uint32_t e = lt[bb & ((1u << LBITS) - 1)];
        if (E_KIND(e) <= K_LIT2) {                  /* one or two literals, and the next entry as long as it is */
          out[0] = (uint8_t)(e >> 16); out[1] = (uint8_t)(e >> 8); out += 1 + E_KIND(e);
          bb >>= E_BITS(e); bl -= E_BITS(e);        /* 11 bits at most taken: 45 left */
          e = lt[bb & ((1u << LBITS) - 1)];
          if (E_KIND(e) <= K_LIT2) {
            out[0] = (uint8_t)(e >> 16); out[1] = (uint8_t)(e >> 8); out += 1 + E_KIND(e);
            bb >>= E_BITS(e); bl -= E_BITS(e);      /* 34 left */
            e = lt[bb & ((1u << LBITS) - 1)];
            if (E_KIND(e) <= K_LIT2) {
              out[0] = (uint8_t)(e >> 16); out[1] = (uint8_t)(e >> 8); out += 1 + E_KIND(e);
              bb >>= E_BITS(e); bl -= E_BITS(e);
              continue;
            }
          }
          if (bl < 48) REFILL();
        }
        if (E_KIND(e) == K_SUB) {
          bb >>= LBITS; bl -= LBITS;
          e = lt[(e >> 16) + (bb & ((1u << ((e >> 8) & 15)) - 1))];
          if (E_KIND(e) == K_LIT1) { *out++ = (uint8_t)(e >> 16); bb >>= E_BITS(e); bl -= E_BITS(e); continue; }
          if (bl < 33) REFILL();                    /* a length and a distance with the first level taken */
        }
        if (E_KIND(e) == K_LEN) {
          uint32_t c = (e >> 8) & 15, n = E_BITS(e);
          uint32_t len = (e >> 16) + (uint32_t)((bb >> c) & ((1u << (n - c)) - 1));
          bb >>= n; bl -= n;
          uint32_t d = dt[bb & ((1u << DBITS) - 1)];
          if (E_KIND(d) == K_SUB) {
            bb >>= DBITS; bl -= DBITS;
            d = dt[(d >> 16) + (bb & ((1u << ((d >> 8) & 15)) - 1))];
          }
          if (E_KIND(d) != K_DIST) { bad = 1; break; }
          c = (d >> 8) & 15; n = E_BITS(d);
          uint32_t dist = (d >> 16) + (uint32_t)((bb >> c) & ((1u << (n - c)) - 1));
          bb >>= n; bl -= n;
          if (dist > o->total + (uint64_t)(out - out0)) { bad = 1; break; }   /* before the start of the output */
          copy_match(out, dist, len);
          out += len;
          continue;
        }
        if (E_KIND(e) == K_END) { bb >>= E_BITS(e); bl -= E_BITS(e); end = 1; break; }
        bad = 1; break;                             /* K_BAD */
      }
#undef REFILL
      s->bb = bb; s->bl = bl; s->in = in;
      o->total += (uint64_t)(out - out0);
      o->pos = (uint32_t)(out - o->buf);
      if (bad) return 0;
      if (end) return 1;
      continue;
    }
    if (o->cap - o->pos < FAST_OUT) {
      /* a sink that keeps so much that the longest match has no room is stopped */
      if (!flush(o, 0) || o->cap - o->pos < FAST_OUT) { *st = CD_INFLATE_STOPPED; return 0; }
      continue;
    }
    /* one symbol, checked: the input may cross a piece or end (a length code and its extra bits take 20 at most) */
    need(s, 20);
    uint32_t e = lt[s->bb & ((1u << LBITS) - 1)];
    if (E_KIND(e) == K_SUB) {
      s->bb >>= LBITS; s->bl -= LBITS;
      need(s, 20);
      e = lt[(e >> 16) + (s->bb & ((1u << ((e >> 8) & 15)) - 1))];
    }
    uint32_t kind = E_KIND(e);
    if (kind == K_BAD) return 0;
    if (kind == K_LIT1 || kind == K_LIT2) {
      s->bb >>= E_BITS(e); s->bl -= E_BITS(e);
      if (cut(s)) return 0;
      o->buf[o->pos++] = (uint8_t)(e >> 16);
      if (kind == K_LIT2) o->buf[o->pos++] = (uint8_t)(e >> 8);
      o->total += 1 + kind;
      continue;
    }
    if (kind == K_END) { s->bb >>= E_BITS(e); s->bl -= E_BITS(e); return !cut(s); }
    /* a length (the code and its extra bits are in the buffer: need(15) and 15 bits at most in all) */
    uint32_t c = (e >> 8) & 15, n = E_BITS(e);
    uint32_t len = (e >> 16) + (uint32_t)((s->bb >> c) & ((1u << (n - c)) - 1));
    s->bb >>= n; s->bl -= n;
    need(s, 15);
    uint32_t d = dt[s->bb & ((1u << DBITS) - 1)];
    if (E_KIND(d) == K_SUB) {
      s->bb >>= DBITS; s->bl -= DBITS;
      need(s, 15);
      d = dt[(d >> 16) + (s->bb & ((1u << ((d >> 8) & 15)) - 1))];
    }
    if (E_KIND(d) != K_DIST) return 0;
    c = (d >> 8) & 15; n = E_BITS(d);
    s->bb >>= c; s->bl -= c;
    uint32_t dist = (d >> 16) + take(s, n - c);
    if (cut(s)) return 0;
    if (dist > o->total) return 0;
    uint8_t *w = o->buf + o->pos;
    for (uint32_t i = 0; i < len; i++) w[i] = w[(int32_t)i - (int32_t)dist];
    o->pos += len; o->total += len;
  }
}

/* The code lengths of a dynamic block, then its tables. */
static int dynamic(bits *s, uint32_t *lt, uint32_t *dt) {
  static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
  uint32_t nlen = take(s, 5) + 257, ndist = take(s, 5) + 1, ncode = take(s, 4) + 4;
  if (nlen > 286 || ndist > 30) return 0;
  uint8_t lens[286 + 30];
  memset(lens, 0, 19);
  for (uint32_t i = 0; i < ncode; i++) lens[order[i]] = (uint8_t)take(s, 3);
  uint32_t ct[128];
  if (cut(s) || !build(lens, 19, ct, 128, 7, 0, 1)) return 0;
  for (uint32_t i = 0; i < nlen + ndist;) {
    need(s, 7 + 7);
    uint32_t e = ct[s->bb & 127];
    s->bb >>= E_BITS(e); s->bl -= E_BITS(e);
    uint32_t sym = e >> 16, rep, v;
    if (sym < 16) { lens[i++] = (uint8_t)sym; continue; }
    if (sym == 16) { if (!i) return 0; v = lens[i - 1]; rep = 3 + take(s, 2); }
    else if (sym == 17) { v = 0; rep = 3 + take(s, 3); }
    else { v = 0; rep = 11 + take(s, 7); }
    if (i + rep > nlen + ndist) return 0;
    while (rep--) lens[i++] = (uint8_t)v;
  }
  if (cut(s) || !lens[256]) return 0;                       /* zlib: "invalid code -- missing end-of-block" */
  return build(lens, nlen, lt, CD_INFLATE_LT, LBITS, 0, 0) && build(lens + nlen, ndist, dt, CD_INFLATE_DT, DBITS, 1, 0);
}

static void fixed(uint32_t *lt, uint32_t *dt) {
  uint8_t lens[288];
  for (uint32_t i = 0; i < 288; i++) lens[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
  build(lens, 288, lt, CD_INFLATE_LT, LBITS, 0, 0);
  for (uint32_t i = 0; i < 30; i++) lens[i] = 5;
  lens[30] = lens[31] = 5;                                   /* 30 and 31 have codes, and are BAD */
  build(lens, 32, dt, CD_INFLATE_DT, DBITS, 1, 0);
}

uint32_t cd_inflate_zlib(cd_inflate *z, uint8_t *buf, uint32_t cap, cd_inflate_in more, cd_inflate_out out, void *ctx) {
  bits s = {0, 0, 0, 0, 0, more, ctx, 0};
  sink o = {buf, cap, 0, 0, 1, 0, out, ctx};
  uint32_t st = CD_INFLATE_CORRUPT;
  if (cap < CD_INFLATE_WINDOW + FAST_OUT + 1024) return CD_INFLATE_CORRUPT;
  uint32_t cmf = take(&s, 8), flg = take(&s, 8);
  if (cut(&s) || (cmf & 15) != 8 || (cmf >> 4) > 7 || (cmf * 256 + flg) % 31 || (flg & 32)) return st;
  for (;;) {
    uint32_t last = take(&s, 1), type = take(&s, 2);
    if (cut(&s)) return st;
    if (type == 0) {                                         /* stored: to a byte, LEN, NLEN, the bytes */
      s.bb >>= s.bl & 7; s.bl &= ~7u;
      s.bb &= (1ull << s.bl) - 1;   /* the bytes below are taken past the bit buffer: no read-ahead bits above it */
      uint32_t len = take(&s, 16), nlen = take(&s, 16);
      if (cut(&s) || len != (~nlen & 0xffff)) return st;
      while (len) {
        if (o.cap - o.pos < FAST_OUT && (!flush(&o, 0) || o.cap - o.pos < FAST_OUT)) return CD_INFLATE_STOPPED;
        uint32_t n = 0, room = o.cap - o.pos;
        if (s.bl) { o.buf[o.pos] = (uint8_t)s.bb; s.bb >>= 8; s.bl -= 8; n = 1; }
        else {
          if (s.in == s.end) { o.buf[o.pos] = (uint8_t)next_byte(&s); if (s.pad) return st; n = 1; }
          else {
            n = (uint32_t)(s.end - s.in);
            if (n > len) n = len;
            if (n > room) n = room;
            memcpy(o.buf + o.pos, s.in, n);
            s.in += n;
          }
        }
        o.pos += n; o.total += n; len -= n;
      }
    } else if (type == 3) {
      return st;
    } else {
      if (type == 1) fixed(z->lt, z->dt);
      else if (!dynamic(&s, z->lt, z->dt)) return st;
      if (!huff_block(&s, &o, z->lt, z->dt, &st)) return st;
    }
    if (last) break;
  }
  /* the adler32, big-endian, from the next whole byte */
  s.bb >>= s.bl & 7; s.bl &= ~7u;
  uint32_t a = take(&s, 8) << 24;
  a |= take(&s, 8) << 16; a |= take(&s, 8) << 8; a |= take(&s, 8);
  if (cut(&s)) return st;
  if (!flush(&o, 1)) return CD_INFLATE_STOPPED;
  return a == o.adler ? CD_INFLATE_OK : st;
}
