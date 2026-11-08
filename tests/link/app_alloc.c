/* An app's own allocator, as a kernel brings one: a bump from __heap_base with a size header and
 * a free that does nothing. Strong, all four, so cdom must allocate from it. */
#include <stddef.h>
#include <stdint.h>
extern unsigned char __heap_base;
static uintptr_t brk;
uint32_t app_mallocs;
void *malloc(size_t n) {
  if (!brk) brk = ((uintptr_t)&__heap_base + 15) & ~(uintptr_t)15;
  uintptr_t p = brk + 16, end = p + ((n + 15) & ~(size_t)15);
  uintptr_t have = (uintptr_t)__builtin_wasm_memory_size(0) << 16;
  if (end > have && __builtin_wasm_memory_grow(0, (end - have + 65535) >> 16) < 0) return 0;
  ((uint32_t *)p)[-1] = (uint32_t)n;
  brk = end;
  app_mallocs++;
  return (void *)p;
}
void free(void *p) { (void)p; }
void *calloc(size_t n, size_t sz) {
  unsigned char *p = malloc(n * sz);
  if (p) __builtin_memset(p, 0, n * sz);
  return p;
}
void *realloc(void *p, size_t n) {
  if (!p) return malloc(n);
  uint32_t old = ((uint32_t *)p)[-1];
  if (old >= n) return p;
  void *q = malloc(n);
  if (q) __builtin_memcpy(q, p, old);
  return q;
}
__attribute__((export_name("app_mallocs"))) uint32_t get_app_mallocs(void) { return app_mallocs; }
