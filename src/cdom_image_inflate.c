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

#ifndef __wasm_simd128__
/* 8 bytes a step in a 64-bit word, two words a turn: the even and odd bytes spread into 16-bit lanes, and one
 * multiply sums a word's bytes (into the top lane) and another their weights 8 down to 1 (8 b0 + 7 b1 + ... + b7:
 * the top lane of e x (2, 4, 6, 8) plus o x (1, 3, 5, 7), each lane under 2^16, so nothing carries into the top).
 * s2 gains 8 x s1 before each word, summed apart in t. */
static uint32_t adler_words(uint32_t *a, uint32_t *b, const uint8_t *s, size_t n) {
  const uint64_t m = 0x00ff00ff00ff00ffull, ones = 0x0001000100010001ull;
  const uint64_t ce = 2 | 4ull << 16 | 6ull << 32 | 8ull << 48, co = 1 | 3ull << 16 | 5ull << 32 | 7ull << 48;
  uint32_t s1 = *a, s2 = *b;
  size_t done = 0;
  while (n - done >= 16) {
    size_t k = (n - done) / 16;
    if (k > ADLER_NMAX / 16) k = ADLER_NMAX / 16;
    uint64_t t = 0, w2 = 0;
    for (size_t i = 0; i < k; i++, s += 16) {
      uint64_t x, y;
      memcpy(&x, s, 8); memcpy(&y, s + 8, 8);
      uint64_t xe = x & m, xo = (x >> 8) & m, ye = y & m, yo = (y >> 8) & m;
      uint32_t sx = (uint32_t)(((xe + xo) * ones) >> 48), sy = (uint32_t)(((ye + yo) * ones) >> 48);
      w2 += ((xe * ce + xo * co) >> 48) + ((ye * ce + yo * co) >> 48);
      t += 2 * (uint64_t)s1 + sx;     /* s1 before the first word, twice, and the first word's bytes once more */
      s1 += sx + sy;
    }
    s2 = (uint32_t)((s2 + 8 * t + w2) % ADLER_MOD);
    s1 %= ADLER_MOD;
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
#else
  size_t v = adler_words(&s1, &s2, s, n);
#endif
  s += v; n -= v;
  for (; n; n--) { s1 += *s++; s2 += s1; }      /* under 16 bytes */
  return (s2 % ADLER_MOD) << 16 | s1 % ADLER_MOD;
}

/* ---- the tables ----
 * An entry is 32 bits. Bits 0-5 are the bits it consumes (under 32, so a shift by the whole entry, which wasm and
 * x86 take modulo 64, shifts by them), bits 6-7 its class, the rest by class:
 *   literal  8-15 a literal, 16-23 a second one, 24-31 how many (1 or 2): one 16-bit store writes both
 *   length   8-11 the code's own bits (0-5 counts them and the extra bits), 16-31 the base; a distance likewise
 *   other    12-15 END (the end of the block), SUB (a second level: 16-31 its start, 8-11 its index bits) or BAD (a
 *            code that is not in the alphabet: zlib's "invalid literal/length code", "invalid distance code")
 * The code-length code's entries are the bits and, at 16-31, the symbol. */
enum { C_LIT = 0, C_LEN = 1, C_OTHER = 2 };
enum { O_END = 1, O_SUB = 2, O_BAD = 3 };
#define E_BITS(e) ((e) & 63u)
#define E_CLASS(e) (((e) >> 6) & 3u)
#define E_OTHER(e) (((e) >> 12) & 15u)
#define LIT1(bits, a) ((uint32_t)(bits) | (uint32_t)(a) << 8 | 1u << 24)
#define LIT2(bits, a, b) ((uint32_t)(bits) | (uint32_t)(a) << 8 | (uint32_t)(b) << 16 | 2u << 24)
#define LENE(bits, c, base) ((uint32_t)(bits) | C_LEN << 6 | (uint32_t)(c) << 8 | (uint32_t)(base) << 16)
#define OTHER(bits, k, sb, start) ((uint32_t)(bits) | C_OTHER << 6 | (uint32_t)(sb) << 8 | (uint32_t)(k) << 12 | (uint32_t)(start) << 16)
#define BAD OTHER(1, O_BAD, 0, 0)

#define LBITS 11        /* the literal/length table's first level (12 measured no faster: its tables take twice as long to build) */
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
  if (dist) return sym < 30 ? LENE(c + dist_extra[sym], c, dist_base[sym]) : BAD;
  if (sym < 256) return LIT1(c, sym);
  if (sym == 256) return OTHER(c, O_END, 0, 0);
  if (sym < 286) return LENE(c + len_extra[sym - 257], c, len_base[sym - 257]);
  return BAD;
}

/* The table for n code lengths (lens) into t: a first level of 2^root entries, then second levels. 0 for a code zlib
 * refuses: over-subscribed, or incomplete unless it is one code of one bit (or, for the code-length code, any
 * incomplete code or none at all). dist: the symbols are distances; codes: the code-length code (each entry is then
 * the symbol and its length, with no second level since its codes are at most 7 bits). */
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
    for (uint32_t i = 0; i < (1u << root); i++) t[i] = BAD;
    return 1;
  }
  if (left > 0 && (codes || max != 1)) return 0;               /* incomplete */
  offs[1] = 0;
  for (uint32_t l = 1; l < 15; l++) offs[l + 1] = (uint16_t)(offs[l] + count[l]);
  for (uint32_t i = 0; i < n; i++) if (lens[i]) sorted[offs[lens[i]]++] = (uint16_t)i;
  /* incomplete (one code of one bit): the other half of the table is BAD; a complete code fills every entry */
  if (left > 0) for (uint32_t i = 0; i < (1u << root); i++) t[i] = BAD;
  /* r: the next code, its bits reversed (deflate sends a code's top bit first), counted up in that order the way
   * zlib does; a longer code appends a 0, which reversed is the top bit, so r carries over */
  uint32_t r = 0, k = 0, next_sub = 1u << root;
  uint32_t sub_prefix = ~0u, sub_start = 0, sub_bits = 0;
  for (uint32_t l = 1; l <= max; l++) {
    for (uint32_t j = 0; j < count[l]; j++, k++) {
      uint32_t sym = sorted[k];
      if (codes) {
        for (uint32_t i = r; i < (1u << root); i += 1u << l) t[i] = l | sym << 16;
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
          t[prefix] = OTHER(root, O_SUB, b, sub_start);
        }
        uint32_t e = sym_entry(sym, l - root, dist);
        for (uint32_t i = r >> root; i < (1u << sub_bits); i += 1u << (l - root)) t[sub_start + i] = e;
      }
      uint32_t inc = 1u << (l - 1);
      while (r & inc) inc >>= 1;
      r = inc ? (r & (inc - 1)) + inc : 0;
    }
  }
  if (!codes && !dist) {
    /* two literals in one entry where both codes fit the first level. From the top down, so the entry looked up for
     * the second (index i >> l1, below i) is still a one-symbol entry. */
    for (uint32_t i = (1u << root); i-- > 0;) {
      uint32_t e = t[i], l1 = E_BITS(e), e2 = t[i >> l1];
      /* without branches: e and e2 literals (each one literal: neither is paired yet), their bits together in the
       * level */
      uint32_t pair = (((e | e2) & 0xc0u) == 0) & (l1 + E_BITS(e2) <= root);
      t[i] = pair ? LIT2(l1 + E_BITS(e2), (e >> 8) & 255, (e2 >> 8) & 255) : e;
    }
  }
  return 1;
}

/* ---- the stream ---- */

typedef cd_inflate bits;   /* the input's fields of the state */
typedef cd_inflate sink;   /* the output's */

/* The next byte; past the end of the input (only when it is the last), a zero counted in pad. A put never reads
 * past its end otherwise: it decodes only while CD_INFLATE_LOOK bytes are ahead. */
static uint32_t next_byte(bits *s) {
  if (s->in == s->end) { s->pad++; return 0; }
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
/* The bytes ahead of the bit buffer; 1 when a put may go on reading: LOOK of them, or the last of the input. */
static inline uint32_t ahead(const bits *s) { return (uint32_t)(s->end - s->in); }
static inline int may_read(const bits *s) { return s->last || ahead(s) >= CD_INFLATE_LOOK; }

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
/* An entry's one or two literals, as one 16-bit store (the second byte of a LIT1 is junk, overwritten next) */
static inline void lits(uint8_t *o, uint32_t e) { uint16_t v = (uint16_t)(e >> 8); memcpy(o, &v, 2); }

/* A match: len bytes from dist back, at o (room for len plus 31 bytes of overshoot). */
static inline void copy_match(uint8_t *o, uint32_t dist, uint32_t len) {
  const uint8_t *s = o - dist;
  uint8_t *e = o + len;
  if (dist >= 16) {
    memcpy(o, s, 16); memcpy(o + 16, s + 16, 16);   /* 32 bytes first: 88% of p12.png's matches are no longer */
    for (o += 32, s += 32; o < e; o += 16, s += 16) memcpy(o, s, 16);
  } else if (dist >= 8) {
    do { memcpy(o, s, 8); o += 8; s += 8; } while (o < e);
  } else if (dist == 1) {
    memset(o, s[0], len);
  } else {
    do { *o++ = *s++; } while (o < e);
  }
}

/* The body of one Huffman block, codes in lt and dt. 1 at its end, 0 on a bad stream, a stopped sink (*st STOPPED),
 * or the input's end short of LOOK bytes ahead when it is not the last (*st MORE: the next put goes on from here). */
static int huff_block(bits *s, sink *o, const uint32_t *lt, const uint32_t *dt, uint32_t *st) {
  const uint32_t lm = (1u << LBITS) - 1, dm = (1u << DBITS) - 1;
  for (;;) {
    /* the fast loop: no checks of the input or the buffer inside but one, on out. Each turn starts with 56 bits
     * or more in the buffer and the next entry e looked up. It leaves look bytes of input ahead: its worst case at
     * the last piece, or LOOK before it (the input the slower loop below may still need). */
    const uint32_t look = s->last ? FAST_IN : CD_INFLATE_LOOK;
    if (o->cap - o->pos >= FAST_OUT && ahead(s) >= look) {
      uint64_t bb = s->bb;
      uint32_t bl = s->bl;     /* each entry's whole value is taken off it: only its low 6 bits are the count */
      const uint8_t *in = s->in;
      uint8_t *const start = o->buf, *out = start + o->pos, *out_lim = start + o->cap - FAST_OUT;
      /* one limit: a turn refills once (7 bytes at most) and writes a byte or more, so with out at most the input's
       * room past look over 8 from here, each refill's 8-byte load stays in the piece and look bytes stay ahead */
      size_t turns = (size_t)(ahead(s) - look) / 8;
      if ((size_t)(out_lim - out) > turns) out_lim = out + turns;
#define REFILL() do { bb |= load64(in) << (bl & 63); in += (63 - (bl & 63)) >> 3; bl = (bl & 63) | 56; } while (0)
#define SAVE() do { s->bb = bb; s->bl = bl & 63; s->in = in; o->total += (uint64_t)(out - (start + o->pos)); o->pos = (uint32_t)(out - start); } while (0)
      REFILL();
      uint32_t e = lt[bb & lm];
      while (out <= out_lim) {
        if (E_CLASS(e) == C_LIT) {
          /* up to three literal entries, each looked up as soon as the one before is, before the stores; each LBITS
           * bits at most: 33, and 11 more for the entry after. The index comes from the low 32 bits of the buffer
           * before, shifted by the entry (its low 5 bits are the count): 11 and 11 more fit, and V8 masks a 64-bit
           * shift's count (an and on the chain from one lookup to the next) but not a 32-bit one; the 64-bit
           * shift, by the entry too, is beside the chain */
          uint32_t e2 = lt[((uint32_t)bb >> (e & 31)) & lm];
          uint64_t b = bb >> (e & 63);
          lits(out, e); out += e >> 24; bl -= e;
          if (E_CLASS(e2) == C_LIT) {
            uint32_t e3 = lt[((uint32_t)b >> (e2 & 31)) & lm];
            b >>= e2 & 63;
            lits(out, e2); out += e2 >> 24; bl -= e2;
            if (E_CLASS(e3) == C_LIT) {
              e = lt[((uint32_t)b >> (e3 & 31)) & lm];
              b >>= e3 & 63;
              lits(out, e3); out += e3 >> 24; bl -= e3;
              bb = b;
              REFILL();
              continue;
            }
            e = e3;
          } else e = e2;
          bb = b;
          REFILL();
        }
        if (E_CLASS(e) != C_LEN) {
          uint32_t k = E_OTHER(e);
          if (k == O_SUB) {                           /* a code longer than the first level */
            bb >>= LBITS; bl -= LBITS;
            e = lt[(e >> 16) + (bb & ((1u << ((e >> 8) & 15)) - 1))];
            if (E_CLASS(e) == C_LIT) {
              *out++ = (uint8_t)(e >> 8);
              bb >>= e & 63; bl -= e;
              REFILL();
              e = lt[bb & lm];
              continue;
            }
            if (E_CLASS(e) != C_LEN) k = E_OTHER(e);
          }
          if (E_CLASS(e) != C_LEN) {
            if (k != O_END) return 0;
            bb >>= e & 63; bl -= e;
            SAVE();
            return 1;
          }
        }
        /* a length (up to 20 bits with a second level), then a distance (up to 28): 56 are there. The extra bits are
         * the entry's bits below its count (under 32) above its code's own */
        uint32_t len = (e >> 16) + (((uint32_t)bb & ((1u << (e & 31)) - 1)) >> ((e >> 8) & 15));
        bb >>= e & 63; bl -= e;
        uint32_t d = dt[bb & dm];
        if (E_CLASS(d) != C_LEN) {
          if (E_OTHER(d) != O_SUB) return 0;
          bb >>= DBITS; bl -= DBITS;
          d = dt[(d >> 16) + (bb & ((1u << ((d >> 8) & 15)) - 1))];
          if (E_CLASS(d) != C_LEN) return 0;
        }
        uint32_t dist = (d >> 16) + (((uint32_t)bb & ((1u << (d & 31)) - 1)) >> ((d >> 8) & 15));
        bb >>= d & 63; bl -= d;
        /* before the start of the output: while nothing was dropped the output starts at start; once something was,
         * the sink kept the window (flush allows no more), so no distance reaches past start */
        if (dist > (uint32_t)(out - start)) return 0;
        REFILL();
        e = lt[bb & lm];                              /* the next entry's load overlaps the copy */
        copy_match(out, dist, len);
        out += len;
      }
      SAVE();
#undef REFILL
#undef SAVE
      continue;
    }
    if (o->cap - o->pos < FAST_OUT) {
      /* a sink that keeps so much that the longest match has no room is stopped */
      if (!flush(o, 0) || o->cap - o->pos < FAST_OUT) { *st = CD_INFLATE_STOPPED; return 0; }
      continue;
    }
    /* one symbol, checked: the input may end (a length code and its extra bits take 20 at most) */
    if (!may_read(s)) { *st = CD_INFLATE_MORE; return 0; }
    need(s, 20);
    uint32_t e = lt[s->bb & lm];
    if (E_CLASS(e) == C_OTHER && E_OTHER(e) == O_SUB) {
      s->bb >>= LBITS; s->bl -= LBITS;
      need(s, 20);
      e = lt[(e >> 16) + (s->bb & ((1u << ((e >> 8) & 15)) - 1))];
    }
    if (E_CLASS(e) == C_LIT) {
      s->bb >>= E_BITS(e); s->bl -= E_BITS(e);
      if (cut(s)) return 0;
      o->buf[o->pos++] = (uint8_t)(e >> 8);
      if (e >> 24 == 2) o->buf[o->pos++] = (uint8_t)(e >> 16);
      o->total += e >> 24;
      continue;
    }
    if (E_CLASS(e) != C_LEN) {
      if (E_OTHER(e) != O_END) return 0;
      s->bb >>= E_BITS(e); s->bl -= E_BITS(e);
      return !cut(s);
    }
    uint32_t c = (e >> 8) & 15, n = E_BITS(e);
    uint32_t len = (e >> 16) + (uint32_t)((s->bb >> c) & ((1u << (n - c)) - 1));
    s->bb >>= n; s->bl -= n;
    need(s, 15);
    uint32_t d = dt[s->bb & dm];
    if (E_CLASS(d) != C_LEN) {
      if (E_OTHER(d) != O_SUB) return 0;
      s->bb >>= DBITS; s->bl -= DBITS;
      need(s, 15);
      d = dt[(d >> 16) + (s->bb & ((1u << ((d >> 8) & 15)) - 1))];
      if (E_CLASS(d) != C_LEN) return 0;
    }
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

enum { S_HEADER, S_BLOCK, S_HUFF, S_STORED, S_ADLER, S_DONE };

void cd_inflate_begin(cd_inflate *z, uint8_t *buf, uint32_t cap, cd_inflate_out out, void *ctx) {
  _Static_assert(CD_INFLATE_CAP_MIN == CD_INFLATE_WINDOW + FAST_OUT + 1024, "the header's floor is the fast loop's room and 1 KB");
  _Static_assert(CD_INFLATE_LOOK >= FAST_IN, "the look-ahead covers the fast loop's");
  z->bb = 0; z->bl = 0; z->pad = 0; z->in = z->end = 0; z->last = 0;
  z->buf = buf; z->cap = cap; z->pos = 0; z->adl = 0; z->adler = 1; z->total = 0; z->out = out; z->ctx = ctx;
  z->stage = S_HEADER; z->final = 0; z->stored = 0; z->status = CD_INFLATE_CORRUPT; z->carry_n = 0;
  if (cap < CD_INFLATE_CAP_MIN) z->stage = S_DONE;
}

/* The stream from z->in to z->end, as far as it goes: from the stage it stopped at, MORE when it stops for want of
 * input (at a block's header, between two symbols, in a stored block, before the adler32), else its final answer. */
static uint32_t run(cd_inflate *z) {
  bits *s = z;
  sink *o = z;
  const uint32_t bad = CD_INFLATE_CORRUPT;
  for (;;) {
    switch (z->stage) {
      case S_HEADER: {
        if (!may_read(s)) return CD_INFLATE_MORE;
        uint32_t cmf = take(s, 8), flg = take(s, 8);
        if (cut(s) || (cmf & 15) != 8 || (cmf >> 4) > 7 || (cmf * 256 + flg) % 31 || (flg & 32)) return bad;
        z->stage = S_BLOCK;
        break;
      }
      case S_BLOCK: {
        if (!may_read(s)) return CD_INFLATE_MORE;
        z->final = take(s, 1);
        uint32_t type = take(s, 2);
        if (cut(s)) return bad;
        if (type == 0) {                                     /* stored: to a byte, LEN, NLEN, the bytes */
          s->bb >>= s->bl & 7; s->bl &= ~7u;
          s->bb &= (1ull << s->bl) - 1;   /* the bytes below are taken past the bit buffer: no read-ahead bits above it */
          uint32_t len = take(s, 16), nlen = take(s, 16);
          if (cut(s) || len != (~nlen & 0xffff)) return bad;
          z->stored = len;
          z->stage = S_STORED;
        } else if (type == 3) {
          return bad;
        } else {
          if (type == 1) fixed(z->lt, z->dt);
          else if (!dynamic(s, z->lt, z->dt)) return bad;
          z->stage = S_HUFF;
        }
        break;
      }
      case S_HUFF: {
        uint32_t st = bad;
        if (!huff_block(s, o, z->lt, z->dt, &st)) return st;
        z->stage = z->final ? S_ADLER : S_BLOCK;
        break;
      }
      case S_STORED:
        while (z->stored) {
          if (o->cap - o->pos < FAST_OUT && (!flush(o, 0) || o->cap - o->pos < FAST_OUT)) return CD_INFLATE_STOPPED;
          uint32_t n = 0, room = o->cap - o->pos;
          if (s->bl) { o->buf[o->pos] = (uint8_t)s->bb; s->bb >>= 8; s->bl -= 8; n = 1; }
          else if (s->in == s->end) {
            if (!s->last) return CD_INFLATE_MORE;
            return bad;                                      /* cut inside a stored block */
          } else {
            n = (uint32_t)(s->end - s->in);
            if (n > z->stored) n = z->stored;
            if (n > room) n = room;
            memcpy(o->buf + o->pos, s->in, n);
            s->in += n;
          }
          o->pos += n; o->total += n; z->stored -= n;
        }
        z->stage = z->final ? S_ADLER : S_BLOCK;
        break;
      case S_ADLER: {                                        /* the adler32, big-endian, from the next whole byte */
        if (!s->last && ahead(s) < 8) return CD_INFLATE_MORE;
        s->bb >>= s->bl & 7; s->bl &= ~7u;
        uint32_t a = take(s, 8) << 24;
        a |= take(s, 8) << 16; a |= take(s, 8) << 8; a |= take(s, 8);
        if (cut(s)) return bad;
        if (!flush(o, 1)) return CD_INFLATE_STOPPED;
        return a == o->adler ? CD_INFLATE_OK : bad;
      }
      default:
        return z->status;
    }
  }
}

/* run over p[0 .. n): the bytes it used, through *used; the answer is run's (a final one ends the stream). */
static uint32_t run_on(cd_inflate *z, const uint8_t *p, uint32_t n, int last, uint32_t *used) {
  z->in = p; z->end = p + n; z->last = last;
  uint32_t st = run(z);
  *used = (uint32_t)(z->in - p);
  z->in = z->end = 0;
  if (st != CD_INFLATE_MORE) { z->status = st; z->stage = S_DONE; }
  return st;
}

/* The carry holds a tail the stream could not use yet (under LOOK bytes). New bytes join it until it holds
 * CD_INFLATE_CARRY (or the input ends), so each run over it takes LOOK or more; once what it leaves lies in the
 * new bytes, the run goes on over those where they are, and keeps their own tail in the carry. */
uint32_t cd_inflate_put(cd_inflate *z, const uint8_t *p, uint32_t n, int last) {
  static const uint8_t none[1];
  uint32_t used, st;
  if (z->stage == S_DONE) return z->status;
  if (!p) p = none;                                         /* n is 0: no arithmetic on a null pointer */
  while (z->carry_n) {
    uint32_t k = CD_INFLATE_CARRY - z->carry_n;
    if (k > n) k = n;
    if (k) memcpy(z->carry + z->carry_n, p, k);
    z->carry_n += k; p += k; n -= k;
    int fin = last && !n;
    if (z->carry_n < CD_INFLATE_CARRY && !fin) return CD_INFLATE_MORE;
    st = run_on(z, z->carry, z->carry_n, fin, &used);
    if (st != CD_INFLATE_MORE) return st;
    uint32_t r = z->carry_n - used;                         /* under LOOK: those bytes are the last r it holds */
    if (r <= k) { p -= r; n += r; z->carry_n = 0; }        /* all in the new bytes: go on there */
    else { memmove(z->carry, z->carry + used, r); z->carry_n = r; }
  }
  if (!last && n < CD_INFLATE_CARRY) {                      /* too few to run on: into the carry */
    if (n) memcpy(z->carry, p, n);
    z->carry_n = n;
    return CD_INFLATE_MORE;
  }
  st = run_on(z, p, n, last, &used);
  if (st != CD_INFLATE_MORE) return st;
  z->carry_n = n - used;
  if (z->carry_n) memcpy(z->carry, p + used, z->carry_n);
  return CD_INFLATE_MORE;
}
