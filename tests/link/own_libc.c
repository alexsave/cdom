/* An app's own libc, strong: links next to mini.c's weak string functions. */
#include <stddef.h>
__attribute__((no_builtin)) int memcmp(const void *a, const void *b, size_t n) {
  const unsigned char *x = a, *y = b;
  for (size_t i = 0; i < n; i++) if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
  return 0;
}
__attribute__((no_builtin)) size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }
__attribute__((no_builtin)) void *memcpy(void *d, const void *s, size_t n) {
  for (size_t i = 0; i < n; i++) ((unsigned char *)d)[i] = ((const unsigned char *)s)[i];
  return d;
}
__attribute__((no_builtin)) void *memmove(void *d, const void *s, size_t n) {
  unsigned char *x = d;
  const unsigned char *y = s;
  if (x < y) for (size_t i = 0; i < n; i++) x[i] = y[i];
  else for (size_t i = n; i--;) x[i] = y[i];
  return d;
}
__attribute__((no_builtin)) void *memset(void *d, int c, size_t n) {
  for (size_t i = 0; i < n; i++) ((unsigned char *)d)[i] = (unsigned char)c;
  return d;
}
__attribute__((export_name("own_libc"))) int own_libc(void) { return 1; }
