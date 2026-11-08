/* An app that replaces malloc and nothing else: cdom's free would get its pointers. It must trap. */
#include <stddef.h>
#include <stdint.h>
void *malloc(size_t n) {
  uintptr_t have = (uintptr_t)__builtin_wasm_memory_size(0) << 16;
  if (__builtin_wasm_memory_grow(0, (n + 65535) >> 16 ? (n + 65535) >> 16 : 1) < 0) return 0;
  return (void *)(have + 16); /* fresh zeroed pages: a header of zeros, a plausible size class */
}
