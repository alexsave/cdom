/* One heap, whoever brings it. cdom's allocations (cd_alloc) and the app's (malloc, calloc) are
 * interleaved at sizes from 1 byte to 64 KB, every block is filled with its own byte, and then
 * every block is checked: a second allocator over the same bytes would overwrite a neighbour.
 * Built twice by tests/link/run.sh: with cdom's weak malloc, and with app_alloc.c's. */
#include "../../src/cdom.h"
#include <stddef.h>
void *malloc(size_t n);
void *calloc(size_t n, size_t sz);
void free(void *p);

void cd_on_event(const cd_event_rec *e) { (void)e; }
void cd_main(void) {}

#define N 600
static uint8_t *blk[N];
static uint32_t len[N];

/* One block from cd_alloc, given back: with an app's malloc and cdom's free this must trap. */
__attribute__((export_name("free_one"))) int free_one(void) {
  void *p = cd_alloc(24);
  cd_free(p);
  return 0;
}

__attribute__((export_name("run"))) int run(void) {
  uint32_t rng = 99;
  for (int round = 0; round < 3; round++) {
    for (int i = 0; i < N; i++) {
      rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
      uint32_t n = 1 + (rng >> 4) % ((rng & 7) == 0 ? 65536 : 300);
      int kind = (int)(rng % 3);
      blk[i] = kind == 0 ? cd_alloc(n) : kind == 1 ? malloc(n) : calloc(n, 1);
      len[i] = n;
      if (!blk[i]) return -1;
      for (uint32_t k = 0; k < n; k++) blk[i][k] = (uint8_t)i;
      /* free some, so free lists are reused across the two callers */
      if (i % 5 == 4) { if (i % 2) cd_free(blk[i - 2]); else free(blk[i - 2]); blk[i - 2] = 0; }
      /* and grow some through cd_realloc, which must keep the bytes */
      if (i % 7 == 6 && blk[i - 1]) {
        blk[i - 1] = cd_realloc(blk[i - 1], len[i - 1] * 2 + 1);
        for (uint32_t k = len[i - 1]; k < len[i - 1] * 2 + 1; k++) blk[i - 1][k] = (uint8_t)(i - 1);
        len[i - 1] = len[i - 1] * 2 + 1;
      }
    }
    for (int i = 0; i < N; i++) {
      if (!blk[i]) continue;
      for (uint32_t k = 0; k < len[i]; k++) if (blk[i][k] != (uint8_t)i) return 1000 * round + i + 1;
      cd_free(blk[i]);
      blk[i] = 0;
    }
  }
  /* cdom's own tables come from the same heap: intern enough strings to grow them */
  char s[8] = "s000000";
  for (int i = 0; i < 3000; i++) { s[4] = (char)('0' + i / 100 % 10); s[5] = (char)('0' + i / 10 % 10); s[6] = (char)('0' + i % 10); s[3] = (char)('0' + i / 1000); cd_intern(s, 7); }
  return 0;
}
