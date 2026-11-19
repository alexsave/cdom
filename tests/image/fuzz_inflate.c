/* libFuzzer target: any bytes as a zlib stream into cdom's inflate (src/cdom_image_inflate.c) and into zlib, the two
 * compared (tests/image/fuzz.sh): when zlib inflates the whole stream, cdom's must give OK (not CORRUPT, not STOPPED) and the same bytes; when
 * zlib refuses it, cdom's must answer CORRUPT. The input is cut into pieces at points the data's first byte picks,
 * each piece its own heap block, so a read past one is caught; the buffer is the smallest the contract allows. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "cdom_image_int.h"

#define OUT_MAX (1u << 22)   /* streams that make more are not compared */
static uint8_t *zbuf, *got;
static size_t got_n;
static uint8_t **pp; static uint32_t *pn, pk, pat;
static int more(void *c, const uint8_t **p, uint32_t *n) { (void)c; if (pat == pk) return 0; *p = pp[pat]; *n = pn[pat]; pat++; return 1; }
static int64_t out(void *c, uint8_t *b, uint32_t n, int last) {
  (void)c;
  uint32_t d = last ? n : n > CD_INFLATE_WINDOW ? n - CD_INFLATE_WINDOW : 0;
  if (got_n + d > OUT_MAX) return -1;
  memcpy(got + got_n, b, d); memset(b, 0x5a, d);
  got_n += d;
  return d;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  static cd_inflate z;
  if (!zbuf) { zbuf = malloc(OUT_MAX); got = malloc(OUT_MAX); }
  if (size < 1) return 0;
  uint32_t cut = data[0] % 7 + 1;            /* pieces of 1, 2, ... 7 times a stride of the data's own picking */
  data++; size--;
  z_stream zs; memset(&zs, 0, sizeof zs);
  if (inflateInit(&zs) != Z_OK) abort();
  zs.next_in = (uint8_t *)data; zs.avail_in = (uInt)size; zs.next_out = zbuf; zs.avail_out = OUT_MAX;
  int zr = inflate(&zs, Z_FINISH);
  size_t zn = OUT_MAX - zs.avail_out;
  inflateEnd(&zs);
  pk = 0; pat = 0;
  pp = malloc(sizeof *pp * (size + 1)); pn = malloc(sizeof *pn * (size + 1));
  for (size_t at = 0; at < size;) {
    uint32_t n = (uint32_t)(cut * 37u % 4096u + 1u) * (pk % 3 ? 1u : cut);
    if (n > size - at) n = (uint32_t)(size - at);
    pp[pk] = malloc(n); memcpy(pp[pk], data + at, n); pn[pk] = n; pk++; at += n;
  }
  size_t cap = CD_INFLATE_WINDOW + 1314;
  uint8_t *buf = malloc(cap);
  got_n = 0;
  uint32_t st = cd_inflate_zlib(&z, buf, (uint32_t)cap, more, out, 0);
  free(buf);
  for (uint32_t i = 0; i < pk; i++) free(pp[i]);
  free(pp); free(pn);
  if (zr == Z_STREAM_END) {
    if (st == CD_INFLATE_OK && (got_n != zn || memcmp(got, zbuf, zn))) __builtin_trap();   /* different bytes */
    if (st != CD_INFLATE_OK) __builtin_trap();                                              /* refused or stopped on a good one */
  } else if (!(zr == Z_BUF_ERROR && !zs.avail_out)) {
    if (st == CD_INFLATE_OK) __builtin_trap();                                              /* took a bad one */
  }
  return 0;
}
