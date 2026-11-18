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
/* S3: 36 MB freed with a small block above it, then 30 MB: the freed block is not at the top. Then the
 * 30 MB freed: it merges with the rest of the 36 MB it was split from, so 36 MB fits there again. */
EXPORT("s3") uint32_t s3(void) {
  uint32_t p0 = pages();
  uint8_t *a = take(PHOTO);
  uint8_t *small = take(100);
  cd_free(a);
  uint8_t *b = take(30000000u);
  cd_free(b);
  uint8_t *c = take(PHOTO);
  uint32_t grown = pages() - p0;
  cd_free(c);
  cd_free(small);
  return b == a && c == a ? grown : grown | 0x80000000u; /* the high bit: b or c did not take a's place */
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
/* S7: three 20 MB blocks, the outer two freed first, then the middle one, which merges with both. */
EXPORT("s7") uint32_t s7(void) {
  uint32_t p0 = pages();
  uint8_t *a = take(FILE20), *b = take(FILE20), *c = take(FILE20), *top = take(100);
  cd_free(a);
  cd_free(c);
  cd_free(b);
  uint8_t *d = take(3 * FILE20);
  uint32_t grown = pages() - p0;
  cd_free(d);
  cd_free(top);
  return d == a ? grown : grown | 0x80000000u;
}
/* S8: a free run right under the top block, then the top block freed: both go back to the top, so
 * 25.6 MB of small blocks fit in their 40 MB. */
EXPORT("s8") uint32_t s8(void) {
  uint8_t *a = take(FILE20), *b = take(FILE20);
  cd_free(a);
  cd_free(b);
  uint32_t p0 = pages();
  for (int i = 0; i < 100000; i++) take(200);
  return pages() - p0;
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
 * own; the pages all that took. It delivers 20 MB twice the same way through every other export that takes
 * cd_bytes_buf's block (a message, an encoded image, a stored value, a path), which reach the modules' weak
 * hooks. */
#include "../src/cdom_net.h"
static uint32_t answered;
static void got(void *, uint32_t status, const uint8_t *p, uint32_t len) {
  answered += status == 200 && len == FILE20 && p[0] == 7 && p[len - 1] == 7;
}
EXPORT("ask") void ask(void) { cd_http_cb(got, 0, "GET", 3, "/big", 4, "", 0, 0, 0, 0); }
EXPORT("answered") uint32_t answered_n(void) { return answered; }
EXPORT("pages") uint32_t pages_now(void) { return pages(); }
EXPORT("take20") void take20(void) { take(FILE20); }
/* Blocks of every kind taken, grown and freed in a seeded order (cd_rand, seeded by cd_start), each filled
 * with a byte of its own and checked before it goes: 0 when every block held its bytes, else the step
 * that found another block's bytes in one (two blocks that overlap, or a run handed out twice). */
#define SLOTS 64
static uint8_t *blk[SLOTS];
static uint32_t len[SLOTS];
static uint8_t mark[SLOTS];
static int intact(int k) {
  for (uint32_t i = 0; i < len[k]; i += 997) if (blk[k][i] != mark[k]) return 0;
  return !len[k] || blk[k][len[k] - 1] == mark[k];
}
static uint32_t size_of(uint32_t r) { /* small blocks, blocks around the 64 KiB edge, and large ones */
  switch (r % 4) {
  case 0: return 1 + (r >> 2) % 5000;
  case 1: return 65000 + (r >> 2) % 1100;
  default: return 70000 + (r >> 2) % 3000000;
  }
}
EXPORT("churn") uint32_t churn(uint32_t steps) {
  for (uint32_t s = 1; s <= steps; s++) {
    uint32_t r = cd_rand(), k = r % SLOTS, op = (r >> 8) % 3, n = size_of(cd_rand());
    if (blk[k] && !intact(k)) return s;
    if (op == 0 && blk[k]) { cd_free(blk[k]); blk[k] = 0; continue; }
    if (op == 1 && blk[k]) { /* grown or shrunk: the bytes it had stay */
      uint32_t keep = len[k] < n ? len[k] : n;
      blk[k] = cd_realloc(blk[k], n);
      for (uint32_t i = 0; i < keep; i += 997) if (blk[k][i] != mark[k]) return s;
    } else {
      cd_free(blk[k]);
      blk[k] = cd_alloc(n);
    }
    len[k] = n;
    mark[k] = (uint8_t)(s * 31 + k);
    __builtin_memset(blk[k], mark[k], n);
  }
  for (int k = 0; k < SLOTS; k++) {
    if (blk[k] && !intact(k)) return steps + 1;
    cd_free(blk[k]);
    blk[k] = 0;
  }
  return 0;
}
