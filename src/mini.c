/* The program's libc: its string functions (src/libc/string.h declares them) and snprintf and
 * vsnprintf (src/libc/stdio.h). Weak: an app that links its own
 * libc keeps its own, and links cleanly with this file.
 * memcmp and strlen: clang emits calls to them, and string.h sends them here.
 * memcpy, memmove, memset: string.h makes a call one memory.copy or memory.fill in place, so these
 * are reached only where a macro cannot be: a library that takes their address (&memcpy, a table of
 * copy functions) or names them in a file built without cdom's string.h, under -ffreestanding. They
 * are the same instructions, and a program that never reaches them carries none of them (the linker
 * drops what nothing calls). */
#include <stdarg.h>
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

/* snprintf and vsnprintf (stdio.h says what they take). One pass over the format: every byte of the
 * result goes through put, which stores it only while it fits before the NUL and always counts it,
 * so the count is the C99 return whatever n is. Integers are converted as unsigned long long (a
 * wasm i64), the sign kept apart, so INT_MIN and LLONG_MIN need no special case. */
typedef struct {
  char *s;
  size_t n, len;
} cd_printf_out;
static void cd_printf_put(cd_printf_out *o, char c) {
  if (o->len + 1 < o->n) o->s[o->len] = c;
  o->len++;
}
static void cd_printf_pad(cd_printf_out *o, char c, int k) {
  while (k-- > 0) cd_printf_put(o, c);
}
__attribute__((weak, no_builtin)) int vsnprintf(char *restrict s, size_t n, const char *restrict f, va_list ap) {
  cd_printf_out o = {s, n, 0};
  for (; *f; f++) {
    if (*f != '%') {
      cd_printf_put(&o, *f);
      continue;
    }
    const char *spec = f++;
    int left = 0, zero = 0, width = 0, prec = -1, size = 0; /* size: -2 hh, -1 h, 0, 1 l, 2 ll, 3 z */
    for (;; f++) {
      if (*f == '-') left = 1;
      else if (*f == '0') zero = 1;
      else break;
    }
    if (*f == '*') {
      width = va_arg(ap, int);
      if (width < 0) left = 1, width = -width;
      f++;
    } else
      while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
    if (*f == '.') {
      f++;
      prec = 0;
      if (*f == '*') {
        prec = va_arg(ap, int);
        if (prec < 0) prec = -1; /* a negative precision is taken as none */
        f++;
      } else
        while (*f >= '0' && *f <= '9') prec = prec * 10 + (*f++ - '0');
    }
    if (*f == 'h') size = *++f == 'h' ? (f++, -2) : -1;
    else if (*f == 'l') size = *++f == 'l' ? (f++, 2) : 1;
    else if (*f == 'z') size = 3, f++;
    char buf[24]; /* the digits of an unsigned long long (20), or %c's byte */
    const char *str = buf, *pre = "";
    int len = 0, zeros = 0;
    unsigned long long u = 0;
    unsigned base = 10;
    const char *digits = "0123456789abcdef";
    switch (*f) {
    case 'd':
    case 'i': {
      long long v = size == 2 ? va_arg(ap, long long)
                  : size == 1 ? va_arg(ap, long)
                  : size == 3 ? (long long)va_arg(ap, size_t)
                              : va_arg(ap, int);
      if (size == -1) v = (short)v;
      if (size == -2) v = (signed char)v;
      if (v < 0) pre = "-";
      u = v < 0 ? 0ull - (unsigned long long)v : (unsigned long long)v;
      goto integer;
    }
    case 'X': digits = "0123456789ABCDEF"; /* fall through */
    case 'x': base = 16; /* fall through */
    case 'u':
      u = size == 2 ? va_arg(ap, unsigned long long)
        : size == 1 ? va_arg(ap, unsigned long)
        : size == 3 ? va_arg(ap, size_t)
                    : va_arg(ap, unsigned);
      if (size == -1) u = (unsigned short)u;
      if (size == -2) u = (unsigned char)u;
      goto integer;
    case 'p':
      u = (unsigned long long)(size_t)va_arg(ap, void *);
      base = 16, pre = "0x";
    integer:
      len = sizeof buf;
      while (u) buf[--len] = digits[u % base], u /= base;
      str = buf + len, len = (int)sizeof buf - len;
      if (len == 0 && prec != 0) buf[sizeof buf - 1] = '0', str = buf + sizeof buf - 1, len = 1;
      if (prec > len) zeros = prec - len;
      else if (zero && !left && prec < 0) {
        int k = width - len - (int)strlen(pre);
        if (k > 0) zeros = k;
      }
      break;
    case 'c':
      buf[0] = (char)va_arg(ap, int), len = 1;
      break;
    case 's':
      str = va_arg(ap, const char *);
      if (!str) str = "(null)";
      while ((prec < 0 || len < prec) && str[len]) len++;
      break;
    case '%':
      cd_printf_put(&o, '%');
      continue;
    default: /* not a conversion stdio.h lists: its text as it stands, no argument taken */
      while (spec < f) cd_printf_put(&o, *spec++);
      if (!*f) goto done; /* a % that ends the format */
      cd_printf_put(&o, *f);
      continue;
    }
    int body = (int)strlen(pre) + zeros + len;
    if (!left) cd_printf_pad(&o, ' ', width - body);
    while (*pre) cd_printf_put(&o, *pre++);
    cd_printf_pad(&o, '0', zeros);
    for (int i = 0; i < len; i++) cd_printf_put(&o, str[i]);
    if (left) cd_printf_pad(&o, ' ', width - body);
  }
done:
  if (n) s[o.len < n ? o.len : n - 1] = 0;
  return o.len > 0x7fffffff ? -1 : (int)o.len;
}
__attribute__((weak)) int snprintf(char *restrict s, size_t n, const char *restrict f, ...) {
  va_list ap;
  va_start(ap, f);
  int r = vsnprintf(s, n, f, ap);
  va_end(ap);
  return r;
}
