/* The heap's large blocks (cdom.h, memory; docs/file-read-design.md, the predictions), built uncapped by
 * tests/build.sh and run by tools/alloc.mjs in Node, a fresh instance per case. Each case answers the
 * memory.grow pages it took (the module's pages after, less those before), so a block that costs more than
 * its size, or one that is not reused once freed, shows as pages. */
#include "../src/cdom.h"
#define EXPORT(n) __attribute__((export_name(n)))
void cd_main(void) {}

#define PHOTO 36578304u /* a 12 MP picture as RGB, 4032 x 3024 x 3: what raceclub's resize bench decoded */
#define FILE20 20000000u

static uint32_t pages(void) { return (uint32_t)__builtin_wasm_memory_size(0); }
/* Every byte written, so the block is really there and an optimizer cannot drop it. */
static uint8_t *take(uint32_t n) {
  uint8_t *p = cd_alloc(n);
  __builtin_memset(p, 0xa5, n);
  return p;
}
/* S1: 36 MB taken and freed, twice. */
EXPORT("s1") uint32_t s1(void) {
  uint32_t p0 = pages();
  for (int i = 0; i < 2; i++) cd_free(take(PHOTO));
  return pages() - p0;
}
/* S2: 36 MB, then 20 MB, then 36 MB, each freed before the next. */
EXPORT("s2") uint32_t s2(void) {
  uint32_t p0 = pages();
  cd_free(take(PHOTO));
  cd_free(take(FILE20));
  cd_free(take(PHOTO));
  return pages() - p0;
}
/* S3: 36 MB freed with a small block above it, then 30 MB: the freed block is not at the top. */
EXPORT("s3") uint32_t s3(void) {
  uint32_t p0 = pages();
  uint8_t *a = take(PHOTO);
  uint8_t *small = take(100);
  cd_free(a);
  uint8_t *b = take(30000000u);
  uint32_t grown = pages() - p0;
  cd_free(b);
  cd_free(small);
  return b == a ? grown : grown | 0x80000000u; /* the high bit: b did not take a's place */
}
/* S4: a buffer grown by doubling from 1 MiB to 64 MiB with cd_realloc, then freed. */
EXPORT("s4") uint32_t s4(void) {
  uint32_t p0 = pages();
  uint8_t *p = 0;
  for (uint32_t n = 1u << 20; n <= 1u << 26; n <<= 1) {
    p = cd_realloc(p, n);
    __builtin_memset(p, 0x5a, n);
  }
  cd_free(p);
  return pages() - p0;
}
/* S5: two large blocks freed out of order merge, so a block of both their sizes fits where they were. */
EXPORT("s5") uint32_t s5(void) {
  uint32_t p0 = pages();
  uint8_t *a = take(FILE20), *b = take(FILE20), *top = take(100);
  cd_free(a);
  cd_free(b);
  uint8_t *c = take(2 * FILE20);
  uint32_t grown = pages() - p0;
  cd_free(c);
  cd_free(top);
  return c == a ? grown : grown | 0x80000000u;
}
/* S6: small blocks after a large one is freed at the top take its room, so the memory does not grow. */
EXPORT("s6") uint32_t s6(void) {
  cd_free(take(PHOTO));
  uint32_t p0 = pages();
  for (int i = 0; i < 100000; i++) take(200); /* 100,000 blocks of 256 bytes: 25.6 MB, never freed */
  return pages() - p0;
}
/* The bytes of an answer (cdom.c, cd_bytes_buf): tools/alloc.mjs answers two requests of 20 MB as the shim
 * does (room from cd_bytes_buf, the bytes copied in, then cd_net_http), and then the app takes 20 MB of its
 * own; the pages all that took. */
#include "../src/cdom_net.h"
static uint32_t answered;
static void got(void *, uint32_t status, const uint8_t *p, uint32_t len) {
  answered += status == 200 && len == FILE20 && p[0] == 7 && p[len - 1] == 7;
}
EXPORT("ask") void ask(void) { cd_http_cb(got, 0, "GET", 3, "/big", 4, "", 0, 0, 0, 0); }
EXPORT("answered") uint32_t answered_n(void) { return answered; }
EXPORT("pages") uint32_t pages_now(void) { return pages(); }
EXPORT("take20") void take20(void) { take(FILE20); }
