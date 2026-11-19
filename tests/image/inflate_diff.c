/* cdom's inflate (src/cdom_image_inflate.c) against zlib, natively, under AddressSanitizer and UBSan
 * (tests/image/run.sh). A seeded corpus (INFLATE_SEED, the one random source): data of many kinds compressed by
 * zlib at every strategy (stored, fixed, dynamic, Huffman only, RLE) and window size, and streams written here by
 * hand (fixed and stored blocks with chosen matches: distance 1 runs, length 258, distances at the 32 KB edge, a
 * distance one past the output). Each stream is inflated by zlib (one call, a large buffer) and by cdom's, from
 * pieces of random sizes, each its own heap block so a read past one is caught, into the smallest buffer the
 * contract allows or a larger one: the two must agree (both OK and the same bytes, or both refuse). Then every
 * stream is cut short at many points (cdom's must answer CORRUPT, as zlib refuses) and damaged at random bits and
 * bytes (cdom's must agree with zlib on each).
 * Usage: inflate_diff [cases=400]; prints "ok inflate_diff: ..." or the first disagreement, and exits 1 on one. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "cdom_image_int.h"

#define INFLATE_SEED 20261119u
static uint64_t rng_state = INFLATE_SEED;
static uint32_t rnd(void) {   /* splitmix64, from the one seed */
  uint64_t z = (rng_state += 0x9e3779b97f4a7c15ull);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull; z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
  return (uint32_t)((z ^ (z >> 31)) >> 32);
}
static uint32_t below(uint32_t n) { return n ? rnd() % n : 0; }

static int fails;
#define CHECK(c, ...) do { if (!(c)) { if (fails++ < 10) { printf("FAIL inflate_diff: " __VA_ARGS__); printf("\n"); } } } while (0)

/* ---- zlib's answer ---- */
static uint8_t *z_out; static size_t z_cap = 1u << 26;
/* 1 and *n when zlib inflates the whole stream and its adler32 matches; 0 when it refuses, -1 when its buffer was
 * too small (not compared) */
static int zlib_inflate(const uint8_t *s, size_t n, size_t *made) {
  z_stream z; memset(&z, 0, sizeof z);
  if (inflateInit(&z) != Z_OK) abort();
  z.next_in = (uint8_t *)s; z.avail_in = (uInt)n; z.next_out = z_out; z.avail_out = (uInt)z_cap;
  int r = inflate(&z, Z_FINISH);
  *made = z_cap - z.avail_out;
  inflateEnd(&z);
  if (r == Z_STREAM_END) return 1;
  if (r == Z_BUF_ERROR && !z.avail_out) return -1;
  return 0;
}

/* ---- cdom's ---- */
typedef struct { uint8_t **p; uint32_t *n; uint32_t k, at; uint8_t *got; size_t got_n, got_cap; int stop_at, greedy; } feed;
static int more(void *c, const uint8_t **p, uint32_t *n) {
  feed *f = c;
  if (f->at == f->k) return 0;
  *p = f->p[f->at]; *n = f->n[f->at]; f->at++;
  return 1;
}
static int64_t out(void *c, uint8_t *b, uint32_t n, int last) {
  feed *f = c;
  if (f->stop_at && !--f->stop_at) return -1;
  uint32_t d = last || f->greedy ? n : n > CD_INFLATE_WINDOW ? n - CD_INFLATE_WINDOW : 0;
  if (f->got_n + d > f->got_cap) { f->got_cap = (f->got_n + d) * 2 + 1024; f->got = realloc(f->got, f->got_cap); }
  if (d) memcpy(f->got + f->got_n, b, d);
  /* the PNG path changes the bytes it lets go, in place: so does this, to show no later match reads them */
  memset(b, 0xa5, d);
  f->got_n += d;
  return d;
}
static cd_inflate dec;
/* cdom's inflate of s[0 .. n) in random pieces into a buffer of cap bytes (0: the smallest allowed) */
static uint32_t cdom_inflate(const uint8_t *s, size_t n, uint32_t cap, feed *f) {
  uint32_t k = 0, most = 1 + (n > 0 ? (uint32_t)n : 0);
  f->p = malloc(sizeof *f->p * most); f->n = malloc(sizeof *f->n * most);
  for (size_t at = 0; at < n;) {
    uint32_t want = below(4) == 0 ? 1 + below(8) : below(4) == 0 ? 1 + below(200) : 1 + below(70000);
    if (want > n - at) want = (uint32_t)(n - at);
    f->p[k] = malloc(want); memcpy(f->p[k], s + at, want); f->n[k] = want; k++; at += want;
    if (below(16) == 0) { f->p[k] = malloc(1); f->n[k] = 0; k++; }   /* an empty piece (an IDAT of 0 bytes) */
  }
  f->k = k; f->at = 0; f->got_n = 0;
  if (!cap) cap = CD_INFLATE_WINDOW + 1314 + below(3) * below(300000);
  uint8_t *buf = malloc(cap);
  uint32_t st = cd_inflate_zlib(&dec, buf, cap, more, out, f);
  free(buf);
  for (uint32_t i = 0; i < k; i++) free(f->p[i]);
  free(f->p); free(f->n);
  return st;
}

static long compared, refused_both, cut_checked, damaged_checked;
/* zlib and cdom on one stream: they must agree */
static void agree(const uint8_t *s, size_t n, const char *what, int expect_ok) {
  size_t zn;
  int zr = zlib_inflate(s, n, &zn);
  if (zr < 0) return;
  feed f = {0};
  uint32_t st = cdom_inflate(s, n, 0, &f);
  if (zr == 1) {
    CHECK(st == CD_INFLATE_OK, "%s: zlib inflates %zu bytes, cdom answers %u", what, zn, st);
    if (st == CD_INFLATE_OK) {
      CHECK(f.got_n == zn && (!zn || !memcmp(f.got, z_out, zn)), "%s: %zu bytes from cdom, %zu from zlib, or they differ", what, f.got_n, zn);
      compared++;
    }
  } else {
    CHECK(st == CD_INFLATE_CORRUPT, "%s: zlib refuses, cdom answers %u (%zu bytes)", what, st, f.got_n);
    refused_both++;
  }
  if (expect_ok >= 0) CHECK(zr == expect_ok, "%s: zlib answers %d, the test expected %d", what, zr, expect_ok);
  free(f.got);
}

/* ---- data ---- */
static void fill(uint8_t *d, size_t n, uint32_t kind) {
  switch (kind % 8) {
    case 0: for (size_t i = 0; i < n; i++) d[i] = (uint8_t)rnd(); break;                  /* random: stored or Huffman */
    case 1: for (size_t i = 0; i < n; i++) d[i] = (uint8_t)(below(4) ? 'a' + below(3) : rnd()); break;
    case 2: { uint8_t c = (uint8_t)rnd(); for (size_t i = 0; i < n; i++) { if (!below(300)) c = (uint8_t)rnd(); d[i] = c; } break; }  /* runs: distance 1 */
    case 3: { uint32_t p = 1 + below(40); for (size_t i = 0; i < n; i++) d[i] = i < p ? (uint8_t)rnd() : below(500) ? d[i - p] : (uint8_t)rnd(); break; }
    case 4: { /* a block repeated far back: matches at long distances, near 32 KB */
      uint32_t b = 30000 + below(2800);
      for (size_t i = 0; i < n; i++) d[i] = i < b ? (uint8_t)rnd() : below(2000) ? d[i - b] : (uint8_t)rnd();
      break;
    }
    case 5: for (size_t i = 0; i < n; i++) d[i] = (uint8_t)((i * 7 + (i >> 5)) ^ (below(8) == 0 ? rnd() : 0)); break;
    case 6: { /* rows of a grainy picture, Paeth-like small residuals: what PNG streams look like */
      for (size_t i = 0; i < n; i++) { int v = (int)below(9) - 4; d[i] = (uint8_t)(below(50) ? v : (int)rnd()); }
      break;
    }
    default: memset(d, 0, n); if (n) d[n / 2] = 1; break;                              /* long runs of 258 */
  }
}

/* ---- hand-written streams: a bit writer, fixed-Huffman blocks and stored blocks ---- */
typedef struct { uint8_t *b; size_t n, cap; uint64_t acc; uint32_t bits; } bw;
static void put(bw *w, uint32_t v, uint32_t n) {
  w->acc |= (uint64_t)v << w->bits; w->bits += n;
  while (w->bits >= 8) {
    if (w->n == w->cap) { w->cap = w->cap * 2 + 64; w->b = realloc(w->b, w->cap); }
    w->b[w->n++] = (uint8_t)w->acc; w->acc >>= 8; w->bits -= 8;
  }
}
static void put_rev(bw *w, uint32_t code, uint32_t n) { uint32_t r = 0; for (uint32_t i = 0; i < n; i++) r |= ((code >> i) & 1) << (n - 1 - i); put(w, r, n); }
static void align(bw *w) { if (w->bits & 7) put(w, 0, 8 - (w->bits & 7)); }
static void fixed_sym(bw *w, uint32_t s) {
  if (s < 144) put_rev(w, 0x30 + s, 8); else if (s < 256) put_rev(w, 0x190 + s - 144, 9);
  else if (s < 280) put_rev(w, s - 256, 7); else put_rev(w, 0xc0 + s - 280, 8);
}
static const uint16_t lb[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const uint8_t le[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const uint16_t db[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const uint8_t de[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
static void fixed_match(bw *w, uint32_t len, uint32_t dist) {
  uint32_t i = 28; while (lb[i] > len) i--;
  if (len == 258) i = 28;
  fixed_sym(w, 257 + i); put(w, len - lb[i], le[i]);
  uint32_t j = 29; while (db[j] > dist) j--;
  put_rev(w, j, 5); put(w, dist - db[j], de[j]);
}
static uint32_t adler(const uint8_t *d, size_t n) { return (uint32_t)adler32(1, d, (uInt)n); }
static void trailer(bw *w, const uint8_t *d, size_t n) {
  align(w);
  uint32_t a = adler(d, n);
  put(w, a >> 24, 8); put(w, (a >> 16) & 255, 8); put(w, (a >> 8) & 255, 8); put(w, a & 255, 8);
}
/* a stream of literals and matches, as fixed blocks of random sizes, some stored blocks between; the expected
 * output in *d */
static void handmade(bw *w, uint8_t **d, size_t *dn, int far_edge, int bad_dist) {
  size_t cap = 400000, n = 0;
  uint8_t *o = malloc(cap);
  put(w, 0x78, 8); put(w, 0x01, 8);
  int blocks = 1 + (int)below(6);
  for (int b = 0; b < blocks; b++) {
    int last = b == blocks - 1;
    if (below(4) == 0) {                                    /* stored */
      put(w, last, 1); put(w, 0, 2); align(w);
      uint32_t len = below(3) ? below(300) : below(70000);
      if (len > 65535) len = 65535;
      if (n + len > cap) len = (uint32_t)(cap - n);
      put(w, len & 0xff, 8); put(w, len >> 8, 8); put(w, ~len & 0xff, 8); put(w, (~len >> 8) & 0xff, 8);
      for (uint32_t i = 0; i < len; i++) { o[n] = (uint8_t)rnd(); put(w, o[n], 8); n++; }
      continue;
    }
    put(w, last, 1); put(w, 1, 2);
    uint32_t syms = 1 + below(3000);
    for (uint32_t i = 0; i < syms && n + 300 < cap; i++) {
      uint32_t r = below(10);
      if (n == 0 || r < 4) { uint8_t c = (uint8_t)rnd(); fixed_sym(w, c); o[n++] = c; continue; }
      uint32_t len = r == 4 ? 258 : r == 5 ? 3 : 3 + below(256);
      uint32_t most = n < 32768 ? (uint32_t)n : 32768, dist;
      if (r == 6) dist = 1;
      else if (r == 7 && far_edge && n >= 32768) dist = 32768 - below(3);
      else dist = 1 + below(most);
      if (dist > most) dist = most;
      fixed_match(w, len, dist);
      for (uint32_t k = 0; k < len; k++, n++) o[n] = o[n - dist];
    }
    if (bad_dist && last) { fixed_match(w, 3, (uint32_t)(n < 32768 ? n + 1 : 32768)); if (n >= 32768) bad_dist = 0; }
    fixed_sym(w, 256);
  }
  trailer(w, o, n);
  *d = o; *dn = n;
}

/* every way of cutting s short (or a sample of them), and random damage: cdom must agree with zlib */
static void cut_and_damage(const uint8_t *s, size_t n, const char *what) {
  char name[200];
  size_t step = n < 400 ? 1 : n / 150;
  for (size_t at = 0; at < n; at += step) {
    size_t zn;
    if (zlib_inflate(s, at, &zn) == 1) continue;   /* cut inside data past the stream's end: still whole */
    feed f = {0};
    uint32_t st = cdom_inflate(s, at, 0, &f);
    CHECK(st == CD_INFLATE_CORRUPT, "%s cut at %zu of %zu: cdom answers %u", what, at, n, st);
    free(f.got);
    cut_checked++;
  }
  uint8_t *d = malloc(n ? n : 1);
  for (int k = 0; k < 12; k++) {
    memcpy(d, s, n);
    uint32_t flips = 1 + below(4);
    for (uint32_t i = 0; i < flips && n; i++) {
      size_t at = below((uint32_t)n);
      if (below(2)) d[at] ^= (uint8_t)(1u << below(8)); else d[at] = (uint8_t)rnd();
    }
    snprintf(name, sizeof name, "%s damaged %d", what, k);
    agree(d, n, name, -1);
    damaged_checked++;
  }
  free(d);
}

/* the sink stops: STOPPED, and nothing after */
static void stopped(const uint8_t *s, size_t n) {
  feed f = {0};
  f.stop_at = 1;
  uint32_t st = cdom_inflate(s, n, 0, &f);
  CHECK(st == CD_INFLATE_STOPPED, "a sink that stops: cdom answers %u", st);
  free(f.got);
}

/* a sink that drops the window too (all it is handed): STOPPED, since the window must stay */
static void greedy(void) {
  uLong dn = 300000, n = compressBound(dn);
  uint8_t *d = malloc(dn), *s = malloc(n);
  fill(d, dn, 1);
  if (compress2(s, &n, d, dn, 6) != Z_OK) abort();
  feed f = {0};
  f.greedy = 1;
  uint32_t st = cdom_inflate(s, n, 0, &f);
  CHECK(st == CD_INFLATE_STOPPED, "a sink that drops the window: cdom answers %u", st);
  free(f.got);
  free(d); free(s);
}

/* ---- hand-written dynamic blocks: the code-length code gives 0 to 15 four bits each, and the lengths follow with
 * no repeats, so any set of lengths can be written, complete or not ---- */
static void canon(const uint8_t *len, uint32_t n, uint32_t *code) {   /* deflate's canonical codes */
  uint32_t count[16] = {0}, next[16];
  for (uint32_t i = 0; i < n; i++) count[len[i]]++;
  count[0] = 0;
  uint32_t c = 0;
  for (uint32_t l = 1; l < 16; l++) { c = (c + count[l - 1]) << 1; next[l] = c; }
  for (uint32_t i = 0; i < n; i++) if (len[i]) code[i] = next[len[i]]++;
}
static void dyn_header(bw *w, const uint8_t *ll, uint32_t nlen, const uint8_t *dl, uint32_t ndist) {
  static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
  put(w, 1, 1); put(w, 2, 2);
  put(w, nlen - 257, 5); put(w, ndist - 1, 5); put(w, 19 - 4, 4);
  for (int i = 0; i < 19; i++) put(w, order[i] < 16 ? 4 : 0, 3);
  for (uint32_t i = 0; i < nlen; i++) put_rev(w, ll[i], 4);       /* symbol v's code is v: 16 codes of 4 bits */
  for (uint32_t i = 0; i < ndist; i++) put_rev(w, dl[i], 4);
}
/* one dynamic block: 'a' 'b' 'a' 'b', then (when match) a match of 4 at distance 2, then the end; expect: zlib's answer */
static void dynamic_case(const char *what, const uint8_t *ll, const uint8_t *dl, uint32_t ndist, int match, int expect) {
  bw w = {0};
  uint32_t lc[286] = {0}, dc[30] = {0};
  canon(ll, 286, lc); canon(dl, ndist, dc);
  put(&w, 0x78, 8); put(&w, 0x01, 8);
  dyn_header(&w, ll, 286, dl, ndist);
  const char *t = "abab";
  for (int i = 0; i < 4; i++) put_rev(&w, lc[(uint8_t)t[i]], ll[(uint8_t)t[i]]);
  uint8_t o[8]; memcpy(o, "abab", 4);
  size_t n = 4;
  if (match) {   /* length 4 is code 258; distance 2 is code 1 */
    put_rev(&w, lc[258], ll[258]); put_rev(&w, dc[1], dl[1]);
    memcpy(o + 4, "abab", 4); n = 8;
  }
  put_rev(&w, lc[256], ll[256]);
  trailer(&w, o, n);
  agree(w.b, w.n, what, expect);
  if (expect == 1) {
    feed f = {0};
    uint32_t st = cdom_inflate(w.b, w.n, 0, &f);
    CHECK(st == CD_INFLATE_OK && f.got_n == n && !memcmp(f.got, o, n), "%s: not the bytes written", what);
    free(f.got);
  }
  free(w.b);
}
static void dynamic_codes(void) {
  uint8_t ll[286], dl[30];
  /* complete: a, b, the end and length 4 at 2 bits each; one distance code of one bit (incomplete, as zlib allows) */
  memset(ll, 0, sizeof ll); memset(dl, 0, sizeof dl);
  ll['a'] = ll['b'] = ll[256] = ll[258] = 2; dl[1] = 1;
  dynamic_case("dynamic: one distance code of one bit", ll, dl, 2, 1, 1);
  /* incomplete literal/length code (three codes of 2 bits): zlib refuses it */
  ll[258] = 0;
  dynamic_case("dynamic: an incomplete literal/length code", ll, dl, 2, 0, 0);
  /* incomplete distance code of two bits (one code of 2 bits): refused */
  ll[258] = 2; dl[1] = 2;
  dynamic_case("dynamic: an incomplete distance code", ll, dl, 2, 1, 0);
  /* over-subscribed: five codes of 2 bits */
  ll[258] = 2; ll['c'] = 2; dl[1] = 1;
  dynamic_case("dynamic: an over-subscribed code", ll, dl, 2, 1, 0);
  /* no end-of-block code */
  memset(ll, 0, sizeof ll); ll['a'] = ll['b'] = 1;
  dynamic_case("dynamic: no end-of-block code", ll, dl, 2, 0, 0);
}

int main(int argc, char **argv) {
  int cases = argc > 1 ? atoi(argv[1]) : 400;
  z_out = malloc(z_cap);
  char what[200];
  /* (the sink checks) */
/* the code's own checks of its tables: adler32 against zlib's on odd lengths */
  for (int i = 0; i < 200; i++) {
    size_t n = below(3) ? below(100) : below(200000);
    uint8_t *d = malloc(n + 1); fill(d, n, rnd());
    uint32_t a0 = below(2) ? 1 : rnd() % 65521 | (rnd() % 65521) << 16;
    CHECK(cd_image_adler32(a0, d, n) == (uint32_t)adler32(a0, d, (uInt)n), "adler32 of %zu bytes", n);
    free(d);
  }
  for (int c = 0; c < cases; c++) {
    size_t n = below(5) == 0 ? below(64) : below(3) ? below(100000) : below(1200000);
    uint8_t *d = malloc(n + 1);
    uint32_t kind = rnd();
    fill(d, n, kind);
    int level = (int)below(10), strat = (int)below(5), wbits = 9 + (int)below(7);
    int zs[5] = {Z_DEFAULT_STRATEGY, Z_FILTERED, Z_HUFFMAN_ONLY, Z_RLE, Z_FIXED};
    z_stream z; memset(&z, 0, sizeof z);
    if (deflateInit2(&z, level, Z_DEFLATED, wbits, 1 + (int)below(9), zs[strat]) != Z_OK) abort();
    size_t cap = deflateBound(&z, (uLong)n) + 64;
    uint8_t *s = malloc(cap);
    z.next_in = d; z.avail_in = (uInt)n; z.next_out = s; z.avail_out = (uInt)cap;
    /* a full flush or a sync flush part way: an empty stored block in the middle */
    if (n > 10 && below(4) == 0) { z.avail_in = (uInt)(n / 2); deflate(&z, below(2) ? Z_SYNC_FLUSH : Z_FULL_FLUSH); z.avail_in = (uInt)(n - n / 2); }
    if (deflate(&z, Z_FINISH) != Z_STREAM_END) abort();
    size_t sn = cap - z.avail_out;
    deflateEnd(&z);
    snprintf(what, sizeof what, "zlib case %d (%zu bytes, kind %u, level %d, strategy %d, window %d)", c, n, kind % 8, level, strat, wbits);
    agree(s, sn, what, 1);
    if (c % 8 == 0) { if (sn > 4000) sn = 4000 + below(2000); cut_and_damage(s, sn, what); }
    if (c == 0) stopped(s, sn);
    free(s); free(d);
  }
  for (int c = 0; c < cases / 4; c++) {
    bw w = {0};
    uint8_t *d; size_t dn;
    int bad = below(5) == 0;
    handmade(&w, &d, &dn, 1, bad);
    snprintf(what, sizeof what, "hand-made %d (%zu bytes%s)", c, dn, bad ? ", a distance past the start" : "");
    agree(w.b, w.n, what, bad ? -1 : 1);
    if (!bad) {
      feed f = {0};
      uint32_t st = cdom_inflate(w.b, w.n, 0, &f);
      CHECK(st == CD_INFLATE_OK && f.got_n == dn && !memcmp(f.got, d, dn), "%s: not the bytes written", what);
      free(f.got);
    }
    if (c % 4 == 0) cut_and_damage(w.b, w.n < 6000 ? w.n : 6000, what);
    free(w.b); free(d);
  }
  dynamic_codes();
  greedy();
  /* a distance of exactly the output so far is fine; one more is not */
  for (int ok = 0; ok < 2; ok++) {
    bw w = {0};
    uint8_t lit[10] = "abcdefghij";
    put(&w, 0x78, 8); put(&w, 0x01, 8); put(&w, 1, 1); put(&w, 1, 2);
    for (int i = 0; i < 10; i++) fixed_sym(&w, lit[i]);
    fixed_match(&w, 5, ok ? 10 : 11);
    fixed_sym(&w, 256);
    uint8_t o[15]; memcpy(o, lit, 10); memcpy(o + 10, lit, 5);
    trailer(&w, o, 15);
    agree(w.b, w.n, ok ? "distance = output" : "distance = output + 1", ok);
    free(w.b);
  }
  printf("%s inflate_diff: %ld streams equal to zlib's output, %ld refused by both, %ld cuts CORRUPT, %ld damaged agreeing (seed %u)\n",
         fails ? "FAIL" : "ok  ", compared, refused_both, cut_checked, damaged_checked, INFLATE_SEED);
  return fails != 0;
}
