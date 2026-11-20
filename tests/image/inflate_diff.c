/* cdom's inflate (src/cdom_image_inflate.c) against zlib, natively, under AddressSanitizer and UBSan
 * (tests/image/run.sh). A seeded corpus (INFLATE_SEED, the one random source): data of many kinds compressed by
 * zlib at every strategy (stored, fixed, dynamic, Huffman only, RLE) and window size, and streams written here by
 * hand (fixed and stored blocks with chosen matches: distance 1 runs, length 258, distances at the 32 KB edge, a
 * distance one past the output). Each stream is inflated by zlib (one call, a large buffer) and by cdom's, pushed
 * (cd_inflate_put) in pieces each its own heap block so a read past one is caught, into the smallest buffer the
 * contract allows or a larger one: the two must agree (both OK and the same bytes, or both refuse). The pieces: the
 * whole stream in one, every byte its own, 7 bytes, 4093 bytes, and random sizes from the seed (with empty pieces,
 * and last given with the final piece or in a put of its own); agree() runs every way, the rest take turns. Then every
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
#define CHECK(c, ...) do { if (!(c)) { if (fails++ < 60) { printf("FAIL inflate_diff: " __VA_ARGS__); printf("\n"); fflush(stdout); } } } while (0)

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
static int64_t out(void *c, uint8_t *b, uint32_t n, int last);
/* The pieces, pushed in order: last with the final one, or (half the time, from the seed) in an empty put of its own. */
static uint32_t push(cd_inflate *z, uint8_t *buf, uint32_t cap, feed *f) {
  cd_inflate_begin(z, buf, cap, out, f);
  int apart = !f->k || below(2);
  uint32_t st = CD_INFLATE_MORE;
  for (f->at = 0; f->at < f->k && st == CD_INFLATE_MORE; f->at++)
    st = cd_inflate_put(z, f->p[f->at], f->n[f->at], !apart && f->at + 1 == f->k);
  if (st == CD_INFLATE_MORE && apart) st = cd_inflate_put(z, 0, 0, 1);
  if (st == CD_INFLATE_MORE) { CHECK(0, "a put with last answered MORE"); st = CD_INFLATE_CORRUPT; }
  if (cd_inflate_put(z, (const uint8_t *)"zz", 2, 1) != st) CHECK(0, "a put after the end answered another status");
  return st;
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
/* The ways to cut a stream into pieces: random sizes, whole, then fixed sizes of 1, 7 and 4093 bytes. */
enum { P_RANDOM, P_WHOLE, P_1, P_7, P_4093, P_WAYS };
static const char *way_name[P_WAYS] = {"random pieces", "whole", "1-byte pieces", "7-byte pieces", "4093-byte pieces"};
static int way = -1;          /* the way the next cdom_inflate cuts; -1: each call the next in turn */
static uint32_t turn;
static long by_way[P_WAYS];   /* streams pushed each way */
/* cdom's inflate of s[0 .. n) in pieces into a buffer of cap bytes (0: the smallest allowed) */
static uint32_t cdom_inflate(const uint8_t *s, size_t n, uint32_t cap, feed *f) {
  int w = way >= 0 ? way : (int)(turn++ % P_WAYS);
  by_way[w]++;
  uint32_t k = 0, most = 1 + 2 * (n > 0 ? (uint32_t)n : 0);
  f->p = malloc(sizeof *f->p * most); f->n = malloc(sizeof *f->n * most);
  for (size_t at = 0; at < n;) {
    uint32_t want = w == P_WHOLE ? (uint32_t)n : w == P_1 ? 1 : w == P_7 ? 7 : w == P_4093 ? 4093
                  : below(4) == 0 ? 1 + below(8) : below(4) == 0 ? 1 + below(200) : 1 + below(70000);
    if (want > n - at) want = (uint32_t)(n - at);
    f->p[k] = malloc(want); memcpy(f->p[k], s + at, want); f->n[k] = want; k++; at += want;
    if (w == P_RANDOM && below(16) == 0) { f->p[k] = malloc(1); f->n[k] = 0; k++; }   /* an empty piece (an IDAT of 0 bytes) */
  }
  f->k = k; f->at = 0; f->got_n = 0;
  if (!cap) cap = CD_INFLATE_WINDOW + 1314 + below(3) * below(300000);
  uint8_t *buf = malloc(cap);
  uint32_t st = push(&dec, buf, cap, f);
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
  for (way = 0; way < P_WAYS; way++) {
    feed f = {0};
    uint32_t st = cdom_inflate(s, n, 0, &f);
    if (zr == 1) {
      CHECK(st == CD_INFLATE_OK, "%s: zlib inflates %zu bytes, cdom answers %u (%s)", what, zn, st, way_name[way]);
      if (st == CD_INFLATE_OK) {
        CHECK(f.got_n == zn && (!zn || !memcmp(f.got, z_out, zn)), "%s, %s: %zu bytes from cdom, %zu from zlib, or they differ", what, way_name[way], f.got_n, zn);
        compared++;
      }
    } else {
      CHECK(st == CD_INFLATE_CORRUPT, "%s: zlib refuses, cdom answers %u (%zu bytes; %s)", what, st, f.got_n, way_name[way]);
      refused_both++;
    }
    free(f.got);
  }
  way = -1;
  if (expect_ok >= 0) CHECK(zr == expect_ok, "%s: zlib answers %d, the test expected %d", what, zr, expect_ok);
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
  uint32_t st = cdom_inflate(s, n, CD_INFLATE_CAP_MIN, &f);   /* a buffer under the output, so the sink is handed bytes before the end */
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

/* ---- the distance check, in each loop: a match one past the output so far. As one piece into a large buffer, the
 * match is met in the fast loop (the checked loop only runs in the last 32 bytes of a piece or the last 290 of the
 * buffer); as pieces of one byte, in the checked loop. The 16 bytes before the buffer are 'Z': a decoder that let the
 * match through would read them and make "Zab", and the stream's adler32 is of that output, so the only refusal left
 * is the distance check itself. A distance equal to the output so far is fine. ---- */
static void dist_edge(void) {
  static const uint8_t lit[10] = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j'};
  for (int ok = 0; ok < 2; ok++)
    for (int bytes = 0; bytes < 2; bytes++) {
      const char *loop = bytes ? "checked loop" : "fast loop";
      bw w = {0};
      uint8_t o[100];
      size_t n = 0;
      put(&w, 0x78, 8); put(&w, 0x01, 8); put(&w, 1, 1); put(&w, 1, 2);
      for (int i = 0; i < 10; i++) { fixed_sym(&w, lit[i]); o[n++] = lit[i]; }
      fixed_match(&w, 3, ok ? 10 : 11);
      if (ok) for (int i = 0; i < 3; i++, n++) o[n] = o[n - 10];
      else { o[n++] = 'Z'; o[n++] = 'a'; o[n++] = 'b'; }
      for (int i = 0; i < 64; i++) { uint8_t c = (uint8_t)('A' + i % 26); fixed_sym(&w, c); o[n++] = c; }
      fixed_sym(&w, 256);
      trailer(&w, o, n);
      size_t zn;
      int zr = zlib_inflate(w.b, w.n, &zn);
      CHECK(zr == ok, "%s, a distance %s the output: zlib answers %d", loop, ok ? "equal to" : "one past", zr);
      uint32_t k = bytes ? (uint32_t)w.n : 1;
      uint8_t **pp = malloc(sizeof *pp * k);
      uint32_t *pn = malloc(sizeof *pn * k);
      for (uint32_t i = 0; i < k; i++) {
        pn[i] = bytes ? 1 : (uint32_t)w.n;
        pp[i] = malloc(pn[i]); memcpy(pp[i], w.b + (bytes ? i : 0), pn[i]);
      }
      feed f = {0};
      f.p = pp; f.n = pn; f.k = k;
      uint32_t cap = 1u << 20;
      uint8_t *mem = malloc(cap + 16);
      memset(mem, 'Z', 16);
      uint32_t st = push(&dec, mem + 16, cap, &f);
      if (ok) CHECK(st == CD_INFLATE_OK && f.got_n == n && !memcmp(f.got, o, n), "%s, a distance equal to the output: cdom answers %u", loop, st);
      else CHECK(st == CD_INFLATE_CORRUPT, "%s, a distance one past the output: cdom answers %u, not CORRUPT", loop, st);
      for (uint32_t i = 0; i < k; i++) free(pp[i]);
      free(pp); free(pn); free(mem); free(f.got); free(w.b);
    }
}

/* ---- dynamic headers written symbol by symbol: a code-length code of the caller's (cl, lengths of 0 to 18) and the
 * code lengths as ops (a code-length symbol and its extra bits), so that repeats, a bad HLIT or HDIST and the rest
 * can be written; the block holds three of one literal and the end, both of one bit ---- */
typedef struct { uint8_t sym, extra; } clop;
static void dyn_ops(bw *w, uint32_t btype, uint32_t hlit, uint32_t hdist, const uint8_t *cl, const clop *ops, uint32_t nops) {
  static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
  uint32_t cc[19] = {0};
  canon(cl, 19, cc);
  put(w, 1, 1); put(w, btype, 2);
  put(w, hlit, 5); put(w, hdist, 5); put(w, 19 - 4, 4);
  for (int i = 0; i < 19; i++) put(w, cl[order[i]], 3);
  for (uint32_t i = 0; i < nops; i++) {
    put_rev(w, cc[ops[i].sym], cl[ops[i].sym]);
    if (ops[i].sym == 16) put(w, ops[i].extra, 2);
    else if (ops[i].sym == 17) put(w, ops[i].extra, 3);
    else if (ops[i].sym == 18) put(w, ops[i].extra, 7);
  }
}
/* runs of zeros as code 18s (11 to 138) and a 17 (3 to 10) for the rest; under 3 as single zeros */
static uint32_t zeros(clop *ops, uint32_t k, uint32_t n) {
  while (n >= 11) { uint32_t r = n > 138 ? 138 : n; if (n - r && n - r < 3) r = n - 3 < 11 ? r : n - 3; ops[k].sym = 18; ops[k].extra = (uint8_t)(r - 11); k++; n -= r; }
  if (n >= 3) { ops[k].sym = 17; ops[k].extra = (uint8_t)(n - 3); k++; n = 0; }
  while (n--) { ops[k].sym = 0; ops[k].extra = 0; k++; }
  return k;
}
/* one dynamic case: the literal lit and the end at one bit each (so lit's code is 0 when lit < 256, the end's 1),
 * three of lit then the end, and the adler32 of those three; expect: zlib's answer */
static void dyn_case(const char *what, uint32_t btype, uint32_t hlit, uint32_t hdist, const clop *ops, uint32_t nops, uint8_t lit, int expect) {
  static const uint8_t cl[19] = {[0] = 2, [1] = 2, [16] = 2, [17] = 3, [18] = 3};   /* complete: 3/4 + 2/8 */
  bw w = {0};
  put(&w, 0x78, 8); put(&w, 0x01, 8);
  dyn_ops(&w, btype, hlit, hdist, cl, ops, nops);
  for (int i = 0; i < 3; i++) put(&w, 0, 1);
  put(&w, 1, 1);
  uint8_t o[3] = {lit, lit, lit};
  trailer(&w, o, 3);
  agree(w.b, w.n, what, expect);
  if (expect == 1) {
    feed f = {0};
    uint32_t st = cdom_inflate(w.b, w.n, 0, &f);
    CHECK(st == CD_INFLATE_OK && f.got_n == 3 && !memcmp(f.got, o, 3), "%s: not the bytes written", what);
    free(f.got);
  }
  free(w.b);
}
/* the code lengths of HLIT + 257 literal/length codes and HDIST + 1 distance codes: 'a' and the end one bit each,
 * one distance code of one bit, every other length 0; first16: a code 16 first (a repeat of nothing) */
static uint32_t lens_ops(clop *ops, uint32_t nlen, uint32_t ndist, int first16) {
  uint32_t k = 0;
  if (first16) { ops[k].sym = 16; ops[k].extra = 0; k++; k = zeros(ops, k, 'a' - 3); }
  else k = zeros(ops, k, 'a');
  ops[k].sym = 1; ops[k].extra = 0; k++;                     /* 'a' */
  k = zeros(ops, k, 256 - 'a' - 1);
  ops[k].sym = 1; ops[k].extra = 0; k++;                     /* the end */
  k = zeros(ops, k, nlen - 257);
  ops[k].sym = 1; ops[k].extra = 0; k++;                     /* distance code 0 */
  return zeros(ops, k, ndist - 1);
}
static void dynamic_headers(void) {
  clop ops[64];
  char what[120];
  uint32_t k = lens_ops(ops, 257, 1, 0);
  dyn_case("dynamic ops: HLIT 0, HDIST 0 (a good block)", 2, 0, 0, ops, k, 'a', 1);
  dyn_case("block type 3 (the good block's bits)", 3, 0, 0, ops, k, 'a', 0);
  for (uint32_t hlit = 29; hlit <= 31; hlit++) {             /* 286 codes are allowed; 287 and 288 are not */
    k = lens_ops(ops, 257 + hlit, 1, 0);
    snprintf(what, sizeof what, "dynamic: HLIT %u (%u literal/length codes)", hlit, 257 + hlit);
    dyn_case(what, 2, hlit, 0, ops, k, 'a', hlit <= 29);
  }
  for (uint32_t hdist = 29; hdist <= 31; hdist++) {          /* 30 distance codes are allowed; 31 and 32 are not */
    k = lens_ops(ops, 257, hdist + 1, 0);
    snprintf(what, sizeof what, "dynamic: HDIST %u (%u distance codes)", hdist, hdist + 1);
    dyn_case(what, 2, 0, hdist, ops, k, 'a', hdist <= 29);
  }
  k = lens_ops(ops, 257, 1, 1);
  dyn_case("dynamic: a code 16 first (a repeat of no length)", 2, 0, 0, ops, k, 'a', 0);
  /* a repeat one past HLIT + HDIST: 255 zeros, 255's length 1, then a 16 repeating it three times over 256, 257 and
   * 258 (one past the 258 lengths); the stream is otherwise good */
  k = zeros(ops, 0, 255);
  ops[k].sym = 1; ops[k].extra = 0; k++;
  ops[k].sym = 16; ops[k].extra = 0; k++;
  dyn_case("dynamic: a repeat one past HLIT + HDIST", 2, 0, 0, ops, k, 255, 0);
  /* the review's case: HLIT 0 and HDIST 0 (258 lengths), then code 18 repeating 138 zeros three times (414), which
   * would run past the decoder's 316 lengths */
  for (k = 0; k < 3; k++) { ops[k].sym = 18; ops[k].extra = 127; }
  dyn_case("dynamic: three repeats of 138 zeros over 258 lengths", 2, 0, 0, ops, 3, 'a', 0);
}

/* ---- the zlib header: a good fixed block after headers zlib refuses: CM not 8, CINFO over 7, FCHECK wrong, FDICT
 * set (FCHECK right in all but its own case); each refused ---- */
static void header_case(const char *what, uint32_t cmf, int fdict, int fcheck_ok, int expect) {
  uint32_t flg = fdict ? 32 : 0;
  while ((cmf * 256 + flg) % 31) flg++;                      /* the smallest FCHECK that makes it a multiple of 31 */
  if (!fcheck_ok) flg++;
  bw w = {0};
  put(&w, cmf, 8); put(&w, flg, 8); put(&w, 1, 1); put(&w, 1, 2);
  static const uint8_t o[5] = {'h', 'e', 'l', 'l', 'o'};
  for (int i = 0; i < 5; i++) fixed_sym(&w, o[i]);
  fixed_sym(&w, 256);
  trailer(&w, o, 5);
  agree(w.b, w.n, what, expect);
  free(w.b);
}
/* a stored block of "hello" whose NLEN is LEN's complement or not, the adler32 of "hello" after it: only the NLEN
 * check stands between a wrong one and OK */
static void stored_case(const char *what, int nlen_ok) {
  bw w = {0};
  static const uint8_t o[5] = {'h', 'e', 'l', 'l', 'o'};
  put(&w, 0x78, 8); put(&w, 0x01, 8); put(&w, 1, 1); put(&w, 0, 2); align(&w);
  put(&w, 5, 16); put(&w, nlen_ok ? (~5u & 0xffff) : (~5u & 0xffff) ^ 0x100, 16);
  for (int i = 0; i < 5; i++) put(&w, o[i], 8);
  trailer(&w, o, 5);
  agree(w.b, w.n, what, nlen_ok);
  free(w.b);
}
static void headers(void) {
  stored_case("a stored block, NLEN right", 1);
  stored_case("a stored block, NLEN wrong", 0);
  header_case("header 78 01 (a good one)", 0x78, 0, 1, 1);
  header_case("header CM 7", 0x77, 0, 1, 0);
  header_case("header CINFO 8", 0x88, 0, 1, 0);
  header_case("header FCHECK wrong", 0x78, 0, 0, 0);
  header_case("header FDICT set", 0x78, 1, 1, 0);
}

/* ---- the fixed code's literal/length symbols 286 and 287 and distances 30 and 31: they have codes but are not in
 * the alphabet. "abc", then the symbol (a length 3 for the distances, then the end); the adler32 is of what a decoder
 * would make that took 286 and 287 as the end and 30 and 31 as distance 1 ("abc", or "abcccc"), so only the refusal
 * of the symbol itself stands between the stream and OK ---- */
static void fixed_bad_syms(void) {
  char what[80];
  for (uint32_t s = 284; s <= 287; s++) {                    /* 284 and 285 are lengths: the good controls */
    bw w = {0};
    uint8_t o[300] = {'a', 'b', 'c'};
    size_t n = 3;
    put(&w, 0x78, 8); put(&w, 0x01, 8); put(&w, 1, 1); put(&w, 1, 2);
    for (int i = 0; i < 3; i++) fixed_sym(&w, o[i]);
    fixed_sym(&w, s);
    if (s < 286) {                                           /* 284: 227 + 5 bits (here 0); 285: 258; distance 1 */
      uint32_t len = s == 284 ? 227 : 258;
      if (s == 284) put(&w, 0, 5);
      put_rev(&w, 0, 5);
      for (uint32_t i = 0; i < len; i++, n++) o[n] = o[n - 1];
    }
    if (s < 286) fixed_sym(&w, 256);                        /* none after 286 and 287: to a decoder that took them as the end, the adler32 follows */
    trailer(&w, o, n);
    snprintf(what, sizeof what, "fixed literal/length symbol %u", s);
    agree(w.b, w.n, what, s < 286);
    free(w.b);
  }
  for (uint32_t d = 29; d <= 31; d++) {                      /* 29 is 24577 + 13 bits: the control, refused too (too far) */
    bw w = {0};
    uint8_t o[6] = {'a', 'b', 'c', 'c', 'c', 'c'};
    put(&w, 0x78, 8); put(&w, 0x01, 8); put(&w, 1, 1); put(&w, 1, 2);
    for (int i = 0; i < 3; i++) fixed_sym(&w, o[i]);
    fixed_sym(&w, 257);
    put_rev(&w, d, 5);
    if (d == 29) put(&w, 0, 13);
    fixed_sym(&w, 256);
    trailer(&w, o, 6);
    snprintf(what, sizeof what, "fixed distance code %u", d);
    agree(w.b, w.n, what, 0);
    free(w.b);
  }
}

/* ---- the largest tables zlib's examples/enough.c finds (enough 286 11 15: 2340 entries; enough 30 9 15: 592), as
 * CD_INFLATE_LT and CD_INFLATE_DT hold: a code of each such shape must still build. Literal/length: the end, 'a',
 * 'b', length 4 (258) and 0 at 1 to 5 bits, then of the other 281 symbols 1 at 12 bits, 229 at 13, 49 at 14 and 2 at
 * 15. Distance: distance 2 (code 1) and six others at 1 to 7 bits, then 1 at 10, 9 at 11, 9 at 12, 1 at 13, 1 at 14
 * and 2 at 15 ---- */
static void worst_tables(void) {
  uint8_t ll[286], dl[30];
  ll[256] = 1; ll['a'] = 2; ll['b'] = 3; ll[258] = 4; ll[0] = 5;
  static const uint8_t lcount[4][2] = {{12, 1}, {13, 229}, {14, 49}, {15, 2}};
  uint32_t s = 1, c = 0;
  for (int g = 0; g < 4; g++)
    for (uint32_t j = 0; j < lcount[g][1]; j++) {
      while (s == 'a' || s == 'b' || s == 256 || s == 258) s++;
      ll[s++] = lcount[g][0]; c++;
    }
  CHECK(c == 281 && s == 286, "worst tables: %u literal/length codes placed, up to %u", c, s);
  dl[1] = 1; dl[0] = 2;
  for (uint32_t i = 2; i < 7; i++) dl[i] = (uint8_t)(i + 1);
  static const uint8_t dcount[6][2] = {{10, 1}, {11, 9}, {12, 9}, {13, 1}, {14, 1}, {15, 2}};
  s = 7;
  for (int g = 0; g < 6; g++) for (uint32_t j = 0; j < dcount[g][1]; j++) dl[s++] = dcount[g][0];
  CHECK(s == 30, "worst tables: %u distance codes", s);
  dynamic_case("dynamic: the largest tables (2340 and 592 entries)", ll, dl, 30, 1, 1);
}

int main(int argc, char **argv) {
  int cases = argc > 1 ? atoi(argv[1]) : 400;
  z_out = malloc(z_cap);
  char what[200];
  dist_edge();   /* first: a decoder that took the bad match would read before its buffer in later cases */
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
  dynamic_headers();
  headers();
  fixed_bad_syms();
  worst_tables();
  greedy();
  /* a distance of exactly the output so far is fine; one more is not */
  for (int ok = 0; ok < 2; ok++) {
    bw w = {0};
    static const uint8_t lit[10] = {'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j'};
    put(&w, 0x78, 8); put(&w, 0x01, 8); put(&w, 1, 1); put(&w, 1, 2);
    for (int i = 0; i < 10; i++) fixed_sym(&w, lit[i]);
    fixed_match(&w, 5, ok ? 10 : 11);
    fixed_sym(&w, 256);
    uint8_t o[15]; memcpy(o, lit, 10); memcpy(o + 10, lit, 5);
    trailer(&w, o, 15);
    agree(w.b, w.n, ok ? "distance = output" : "distance = output + 1", ok);
    free(w.b);
  }
  printf("%s inflate_diff: %ld streams equal to zlib's output, %ld refused by both, %ld cuts CORRUPT, %ld damaged agreeing (seed %u);"
         " pushed whole %ld, in 1-byte pieces %ld, 7 %ld, 4093 %ld, random %ld\n",
         fails ? "FAIL" : "ok  ", compared, refused_both, cut_checked, damaged_checked, INFLATE_SEED,
         by_way[P_WHOLE], by_way[P_1], by_way[P_7], by_way[P_4093], by_way[P_RANDOM]);
  return fails != 0;
}
