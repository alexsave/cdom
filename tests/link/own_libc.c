/* An app's own libc, strong: links next to mini.c's weak pair. */
#include <stddef.h>
int memcmp(const void *a, const void *b, size_t n) {
  const unsigned char *x = a, *y = b;
  for (size_t i = 0; i < n; i++) if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
  return 0;
}
size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
__attribute__((export_name("own_libc"))) int own_libc(void) { return 1; }
