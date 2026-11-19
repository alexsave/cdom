/* The inflate candidates in wasm (docs/image-png-speed.md, P2), driven by bench/image/inflate/run.mjs in Chrome.
 * The page puts a PNG's bytes in memory (room), then run(arm, p, n) inflates its zlib stream from the IDAT
 * payloads (pieces, as the PNG path reads them) into a bounded buffer and hands the bytes on in rows, as the PNG
 * path does before its unfilter: tinfl and the zlibs copy each row out of their output buffer into a row (the
 * old path's memcpy), cdom's inflate visits each row in place in its buffer (the new path). Answers the bytes made,
 * or 0 on an error. Arms: 0 tinfl, 1 cdom, 2 zlib 1.3.1, 3 zlib-ng 2.2.4 generic (2 and 3 when built in), 9 cdom's
 * adler32 alone. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "cdom_image_miniz.h"
#include "cdom_image_int.h"
#define EXPORT(n) __attribute__((export_name(n)))

/* a bump heap: the page runs one arm per load */
extern unsigned char __heap_base;
static uintptr_t brk;
void *cd_alloc_try(uint32_t n) {
  if (!brk) brk = (uintptr_t)&__heap_base;
  brk = (brk + 15) & ~(uintptr_t)15;
  uintptr_t p = brk; brk += n;
  size_t have = __builtin_wasm_memory_size(0) * 65536;
  if (brk > have && __builtin_wasm_memory_grow(0, (brk - have + 65535) / 65536) == (size_t)-1) return 0;
  return (void *)p;
}
void cd_free(void *p) { (void)p; }
void *cd_realloc_try(void *p, uint32_t n) { (void)p; (void)n; return 0; }
void *malloc(size_t n) { return cd_alloc_try((uint32_t)n); }
void *calloc(size_t n, size_t k) { void *p = cd_alloc_try((uint32_t)(n * k)); if (p) memset(p, 0, n * k); return p; }
void free(void *p) { (void)p; }
void *aligned_alloc(size_t a, size_t n) { (void)a; if (!brk) brk = (uintptr_t)&__heap_base; brk = (brk + 63) & ~(uintptr_t)63; return cd_alloc_try((uint32_t)n); }
EXPORT("room") void *room(uint32_t n) { return cd_alloc_try(n); }
EXPORT("heap") uint32_t heap(void) { return (uint32_t)brk; }

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static const uint8_t *file; static uint32_t file_n; static uint64_t at;
static int more(void *c, const uint8_t **p, uint32_t *n) {
  (void)c;
  while (at + 12 <= file_n) {
    uint32_t l = be32(file + at);
    if (at + 12 + (uint64_t)l > file_n) return 0;
    const uint8_t *t = file + at + 4;
    at += 12 + (uint64_t)l;
    if (!memcmp(t, "IDAT", 4)) { *p = t + 4; *n = l; return 1; }
    if (!memcmp(t, "IEND", 4)) return 0;
  }
  return 0;
}
static uint32_t row, rows_seen; static uint8_t *rowbuf; static uint32_t have; static uint64_t made;
static volatile uint32_t sink_sum;
/* the old path's hand-on: bytes into a row, each finished row looked at once */
static void rows_copy(const uint8_t *s, size_t n) {
  made += n;
  while (n) {
    size_t t = row - have; if (t > n) t = n;
    memcpy(rowbuf + have, s, t); have += (uint32_t)t; s += t; n -= t;
    if (have == row) { sink_sum += rowbuf[0]; rows_seen++; have = 0; }
  }
}
/* the new path's: rows visited in place, those wholly 32 KB behind (or all, at the end) */
static uint32_t row_at;
static int64_t rows_in_place(void *c, uint8_t *b, uint32_t n, int last) {
  (void)c;
  uint32_t lim = last ? n : n > CD_INFLATE_WINDOW ? n - CD_INFLATE_WINDOW : 0;
  while (row_at + row <= lim) { sink_sum += b[row_at]; rows_seen++; row_at += row; made += row; }
  uint32_t drop = row_at >= row ? row_at - row : 0;   /* keep the row above, as the unfilter needs */
  if (last) { made += n - row_at; drop = n; }
  row_at -= drop;
  return drop;
}

#ifdef WITH_ZLIB
#include "zlib.h"
static voidpf z_alloc(voidpf o, uInt n, uInt k) { (void)o; return calloc(n, k); }   /* Z_SOLO: no default */
static void z_free(voidpf o, voidpf p) { (void)o; free(p); }
#endif
#ifdef WITH_ZNG
#include "zlib-ng.h"
#endif

EXPORT("run") uint32_t run(uint32_t arm, const uint8_t *p, uint32_t n) {
  file = p; file_n = n; at = 8; made = 0; rows_seen = 0; have = 0; row_at = 0;
  uint32_t w = be32(p + 16), bd = p[24], ct = p[25];
  uint32_t ch = ct == 2 ? 3 : ct == 6 ? 4 : ct == 4 ? 2 : 1;
  row = (w * ch * bd + 7) / 8 + 1;
  if (!rowbuf) rowbuf = cd_alloc_try(row);
  const uint8_t *in; uint32_t in_n;
  if (arm == 0) {
    static tinfl_decompressor inf; static uint8_t dict[TINFL_LZ_DICT_SIZE];
    tinfl_init(&inf);
    uint32_t ofs = 0;
    while (more(0, &in, &in_n)) {
      size_t pos = 0;
      for (;;) {
        size_t i_n = in_n - pos, o_n = TINFL_LZ_DICT_SIZE - ofs;
        tinfl_status st = tinfl_decompress(&inf, in + pos, &i_n, dict, dict + ofs, &o_n, TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_HAS_MORE_INPUT);
        pos += i_n;
        rows_copy(dict + ofs, o_n);
        ofs = (ofs + (uint32_t)o_n) & (TINFL_LZ_DICT_SIZE - 1);
        if (st == TINFL_STATUS_DONE) return (uint32_t)made;
        if (st < 0) return 0;
        if (st == TINFL_STATUS_NEEDS_MORE_INPUT && pos >= in_n) break;
      }
    }
    return 0;
  }
  if (arm == 1) {
    static cd_inflate z; static uint8_t *buf;
    uint32_t cap = CD_INFLATE_WINDOW + 2 * row + 262144;
    if (!buf) buf = cd_alloc_try(cap);
    return cd_inflate_zlib(&z, buf, cap, more, rows_in_place, 0) == CD_INFLATE_OK ? (uint32_t)made : 0;
  }
#ifdef WITH_ZLIB
  if (arm == 2) {
    static uint8_t o[65536]; z_stream z; memset(&z, 0, sizeof z);
    z.zalloc = z_alloc; z.zfree = z_free;
    if (inflateInit(&z) != Z_OK) return 0;
    int r = Z_OK;
    while (r == Z_OK && more(0, &in, &in_n)) {
      z.next_in = (uint8_t *)in; z.avail_in = in_n;
      do { z.next_out = o; z.avail_out = sizeof o; r = inflate(&z, Z_NO_FLUSH); rows_copy(o, sizeof o - z.avail_out); }
      while (r == Z_OK && (z.avail_in || !z.avail_out));
    }
    return r == Z_STREAM_END ? (uint32_t)made : 0;
  }
#endif
#ifdef WITH_ZNG
  if (arm == 3) {
    static uint8_t o[65536]; zng_stream z; memset(&z, 0, sizeof z);
    if (zng_inflateInit(&z) != Z_OK) return 0;
    int r = Z_OK;
    while (r == Z_OK && more(0, &in, &in_n)) {
      z.next_in = in; z.avail_in = in_n;
      do { z.next_out = o; z.avail_out = sizeof o; r = zng_inflate(&z, Z_NO_FLUSH); rows_copy(o, sizeof o - z.avail_out); }
      while (r == Z_OK && (z.avail_in || !z.avail_out));
    }
    return r == Z_STREAM_END ? (uint32_t)made : 0;
  }
#endif
  if (arm == 9) {   /* adler32 alone: 140 times over one 256 KB block (36.7 MB), hot, as the inflate takes it */
    static uint8_t *blk;
    if (!blk) blk = cd_alloc_try(262144);
    for (uint32_t i = 0; i < 262144; i++) blk[i] = p[(i * 7919u) % n];
    uint32_t a = 1;
    for (int k = 0; k < 140; k++) a = cd_image_adler32(a, blk, 262144);
    sink_sum += a;
    return 140u * 262144u;
  }
  return 0;
}
