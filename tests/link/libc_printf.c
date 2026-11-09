/* snprintf and vsnprintf (cdom's stdio.h, src/libc; mini.c's), as a library calls them. run.sh builds it
 * under cdom's flags and freestanding ones, with mini.c and no other libc; run.mjs calls libc_printf,
 * which returns 0, or the line of the first case that is wrong. The cases call through a volatile
 * pointer, so the compiler cannot fold a call it knows into a constant and every one reaches mini.c's
 * code; the one direct call shows the declaration takes a format the compiler checks. */
#include <limits.h>
#include <stdio.h>
#include <string.h>
static char b[64];
static int (*volatile sp)(char *, size_t, const char *, ...) = snprintf;
static const char *volatile null_str = 0;
static int vwrap(char *s, size_t n, const char *f, ...) {
  va_list ap;
  va_start(ap, f);
  int r = vsnprintf(s, n, f, ap);
  va_end(ap);
  return r;
}
static int (*volatile vp)(char *, size_t, const char *, ...) = vwrap;
static int eq(int r, const char *want) {
  size_t k = strlen(want);
  return r == (int)k && memcmp(b, want, k + 1) == 0;
}
static int untouched(size_t from) { /* every byte from `from` on is still the fill */
  for (size_t i = from; i < sizeof b; i++)
    if (b[i] != '#') return 0;
  return 1;
}
#define T(want, ...)                                                                                       \
  do {                                                                                                     \
    memset(b, '#', sizeof b);                                                                              \
    if (!eq(sp(b, sizeof b, __VA_ARGS__), want)) return __LINE__;                                          \
  } while (0)
__attribute__((export_name("libc_printf"))) int libc_printf(void) {
  T("", "");
  T("plain text", "plain text");
  T("0 42 -42", "%d %i %d", 0, 42, -42);
  T("-2147483648 2147483647", "%d %d", INT_MIN, INT_MAX);
  T("4294967295", "%u", UINT_MAX);
  T("-2147483648 4294967295", "%ld %lu", LONG_MIN, ULONG_MAX);
  T("-9223372036854775808 9223372036854775807", "%lld %lld", LLONG_MIN, LLONG_MAX);
  T("18446744073709551615", "%llu", ULLONG_MAX);
  T("ff ABCDEF 0000beef ffffffffffffffff", "%x %X %08x %llx", 255u, 0xabcdefu, 0xbeefu, ULLONG_MAX);
  T("[   42] [42   ] [-0042] [007] [] [  007] [   -7]", "[%5d] [%-5d] [%05d] [%.3d] [%.0d] [%05.3d] [%5d]", 42,
    42, -42, 7, 0, 7, -7);
  T("[42   ] [00042]", "[%-05d] [%0*d]", 42, 5, 42); /* - wins over 0; * reads the width */
  T("[   42] [42   ]", "[%*d] [%*d]", 5, 42, -5, 42);  /* a negative * width left-justifies */
  T("[A] [  A] [A  ]", "[%c] [%3c] [%-3c]", 'A', 'A', 'A');
  T("[hi] [   hi] [hi   ] [he] [hel] [hello]", "[%s] [%5s] [%-5s] [%.2s] [%.*s] [%.*s]", "hi", "hi", "hi", "hello", 3,
    "hello", -1, "hello");
  static const char unterminated[3] = {'a', 'b', 'c'}; /* %.*s reads only what the precision allows */
  T("[ab] [abc]", "[%.*s] [%.3s]", 2, unterminated, unterminated);
  T("(null)", "%s", null_str);
  T("100% %", "100%% %%");
  T("12 44 4464 255", "%zu %hhd %hu %hhu", sizeof(int) * 3, 300, 70000, -1);
  T("0x10 0x0", "%p %p", (void *)0x10, (void *)0);
  T("%f 7", (const char *)"%f %d", 7);         /* no floats: the conversion as it stands, no argument */
  T("%5.2Lf|9", (const char *)"%5.2Lf|%d", 9); /* the same with its flags, width and length */
  T("end %", (const char *)"end %");
  T("end %5", (const char *)"end %5");
  /* n = 0: nothing written (s may be NULL), the whole length returned */
  memset(b, '#', sizeof b);
  if (sp(b, 0, "hello %d", 12345) != 11 || !untouched(0)) return __LINE__;
  if (sp(NULL, 0, "%s", "abc") != 3) return __LINE__;
  /* n = 1: the NUL alone */
  memset(b, '#', sizeof b);
  if (sp(b, 1, "hello") != 5 || b[0] != 0 || !untouched(1)) return __LINE__;
  /* exact fit: the text and its NUL in n bytes, nothing past */
  memset(b, '#', sizeof b);
  if (sp(b, 6, "hello") != 5 || memcmp(b, "hello", 6) || !untouched(6)) return __LINE__;
  /* one short: cut, NUL-terminated, the return the full length */
  memset(b, '#', sizeof b);
  if (sp(b, 5, "hello") != 5 || memcmp(b, "hell", 5) || !untouched(5)) return __LINE__;
  /* cut inside a number, padding and a string */
  memset(b, '#', sizeof b);
  if (sp(b, 4, "%d", -12345) != 6 || memcmp(b, "-12", 4) || !untouched(4)) return __LINE__;
  memset(b, '#', sizeof b);
  if (sp(b, 3, "%8s|", "x") != 9 || memcmp(b, "  ", 3) || !untouched(3)) return __LINE__;
  memset(b, '#', sizeof b);
  if (sp(b, 7, "%lld", LLONG_MIN) != 20 || memcmp(b, "-92233", 7) || !untouched(7)) return __LINE__;
  /* vsnprintf, through a function that takes ... */
  memset(b, '#', sizeof b);
  if (!eq(vp(b, sizeof b, "%s=%04d", "id", 42), "id=0042")) return __LINE__;
  memset(b, '#', sizeof b);
  if (vp(b, 3, "%s=%04d", "id", 42) != 7 || memcmp(b, "id", 3) || !untouched(3)) return __LINE__;
  /* a direct call, as an app writes it */
  int r = snprintf(b, sizeof b, "%u items, %s", 3u, "saved");
  if (r != 14 || memcmp(b, "3 items, saved", 15)) return __LINE__;
  return 0;
}
