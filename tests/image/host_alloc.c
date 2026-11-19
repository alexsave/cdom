/* The heap of the image module's native builds (tests/image/test_image.c, tests/image/fuzz_*.c): cd_alloc_try,
 * cd_realloc_try and cd_free with cdom's contract (0 when there is no room; cd_free(0) does nothing), over malloc,
 * counting what is live. host_room is a heap's room in bytes: once a block would take the live total past it, that
 * block and every later one answer 0 until host_reset, as cdom's heap stays full once the memory cannot grow (the
 * review's item 4: a heap that fails one block and then succeeds hides a leak). */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "host_alloc.h"

size_t host_live, host_peak, host_blocks, host_calls, host_room = SIZE_MAX;
int host_full;

typedef struct { size_t n; size_t pad; } hdr;   /* 16 bytes, so the block keeps malloc's alignment */

void host_reset(size_t room) { host_room = room; host_full = 0; host_peak = host_live; host_calls = 0; }

void *cd_alloc_try(uint32_t n) {
  host_calls++;
  if (host_full || n > host_room - host_live || host_live + n > host_room) { host_full = 1; return 0; }
  hdr *h = malloc(sizeof(hdr) + n);
  if (!h) return 0;
  h->n = n;
  host_live += n; host_blocks++;
  if (host_live > host_peak) host_peak = host_live;
  return h + 1;
}
void cd_free(void *p) {
  if (!p) return;
  hdr *h = (hdr *)p - 1;
  host_live -= h->n; host_blocks--;
  free(h);
}
void *cd_realloc_try(void *p, uint32_t n) {
  if (!p) return cd_alloc_try(n);
  hdr *h = (hdr *)p - 1;
  void *q = cd_alloc_try(n);
  if (!q) return 0;
  memcpy(q, p, h->n < n ? h->n : n);
  cd_free(p);
  return q;
}
