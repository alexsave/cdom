/* The program's string functions (src/libc/string.h declares them). Weak: an app that links its own
 * libc keeps its own, and links cleanly with this file.
 * memcmp and strlen: clang emits calls to them, and string.h sends them here.
 * memcpy, memmove, memset: string.h makes a call one memory.copy or memory.fill in place, so these
 * are reached only where a macro cannot be: a library that takes their address (&memcpy, a table of
 * copy functions) or names them in a file built without cdom's string.h, under -ffreestanding. They
 * are the same instructions, and a program that never reaches them carries none of them (the linker
 * drops what nothing calls). */
#include <stddef.h>
#ifndef __wasm_bulk_memory__
#error "cdom needs wasm bulk memory (-mbulk-memory): memcpy, memmove and memset are memory.copy and memory.fill"
#endif
__attribute__((weak, no_builtin)) int memcmp(const void *a, const void *b, size_t n) {
  const unsigned char *x = a, *y = b;
  for (size_t i = 0; i < n; i++)
    if (x[i] != y[i]) return x[i] - y[i];
  return 0;
}
__attribute__((weak, no_builtin)) size_t strlen(const char *s) {
  size_t n = 0;
  while (s[n]) n++;
  return n;
}
/* With bulk memory the builtins lower to the instructions, never to a call (no recursion). */
__attribute__((weak)) void *memcpy(void *restrict d, const void *restrict s, size_t n) { return __builtin_memcpy(d, s, n); }
__attribute__((weak)) void *memmove(void *d, const void *s, size_t n) { return __builtin_memmove(d, s, n); }
__attribute__((weak)) void *memset(void *d, int c, size_t n) { return __builtin_memset(d, c, n); }
