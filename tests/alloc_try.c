/* cd_alloc_try and cd_realloc_try (cdom.h, memory), built by tests/build.sh with -DCDOM_HEAP_PAGES_MAX
 * so the heap runs out of room at 40 pages (2.5 MB), and run by tools/alloc.mjs in Node, a fresh
 * instance per case (a trap leaves the heap mid-call). With -DOWN_ALLOC the app brings its own four
 * allocator functions, which refuse everything, and the _try forms must answer what they answer. */
#include "../src/cdom.h"
#include <stddef.h>
void *malloc(size_t n);
void *realloc(void *p, size_t n);
#define EXPORT(n) __attribute__((export_name(n)))
void cd_main(void) {}

#ifdef OWN_ALLOC
static uint32_t asked;
void *malloc(size_t) { asked++; return 0; }
void free(void *) {}
void *realloc(void *, size_t) { asked++; return 0; }
void *calloc(size_t, size_t) { asked++; return 0; }
EXPORT("own") uint32_t own(void) { /* both answer 0 and both asked the app's allocator */
  uint32_t before = asked;
  void *a = cd_alloc_try(16), *b = cd_realloc_try(0, 16);
  return !a && !b && asked == before + 2;
}
#else
EXPORT("pages") uint32_t pages(void) { return (uint32_t)__builtin_wasm_memory_size(0); }
/* Blocks of 64 KB until there is no room: how many came, the pages then, and that one more after a
 * free comes from the freed block. */
EXPORT("fill") uint32_t fill(void) {
  void *last = 0;
  uint32_t n = 0;
  for (void *p; (p = cd_alloc_try(65536 - 16)); n++) last = p;
  if (!last || cd_alloc_try(65536 - 16)) return 0;
  cd_free(last);
  void *again = cd_alloc_try(65536 - 16);
  return again == last ? n : 0;
}
/* A grow that does not fit leaves the block as it was: still there, its bytes kept, and freeable. */
EXPORT("grow") uint32_t grow(void) {
  uint8_t *p = cd_alloc_try(100);
  if (!p) return 0;
  for (int i = 0; i < 100; i++) p[i] = (uint8_t)i;
  uint8_t *q = cd_realloc_try(p, 64u << 20); /* 64 MB: past the cap */
  if (q) return 0;
  for (int i = 0; i < 100; i++) if (p[i] != (uint8_t)i) return 0;
  uint8_t *r = cd_realloc_try(p, 200);       /* a grow that fits moves it, bytes and all */
  if (!r) return 0;
  for (int i = 0; i < 100; i++) if (r[i] != (uint8_t)i) return 0;
  cd_free(r);
  return 1;
}
EXPORT("huge") uint32_t huge(void) { return cd_alloc_try(0x80000000u) == 0; } /* past what a size can be */
/* What must still trap at the cap: malloc (and cd_alloc on it), realloc. */
/* Each block is kept in a volatile, or clang, which knows malloc, drops an allocation nothing uses. */
static void *volatile kept;
EXPORT("malloc_full") void malloc_full(void) { for (;;) { kept = malloc(65536 - 16); if (!kept) return; } }
EXPORT("alloc_full") void alloc_full(void) { for (;;) kept = cd_alloc(65536 - 16); }
EXPORT("realloc_full") void realloc_full(void) {
  kept = malloc(16);
  for (uint32_t n = 1u << 16; n; n <<= 1) { kept = realloc(kept, n); if (!kept) return; }
}
#endif
