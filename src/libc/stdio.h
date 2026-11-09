/* stdio.h for a cdom app's program (see string.h beside it: the program's libc is cdom's). It offers
 * snprintf and vsnprintf, and size_t and va_list, nothing more: there is no FILE, no printf, no stream.
 * Both are real functions, weak, in mini.c (link it), so a program with its own libc keeps its own,
 * and a program that never calls them carries none of their code (the linker drops what nothing calls).
 *
 * C99 semantics: the return is the length the whole result has (not counting the NUL), whatever n is;
 * at most n bytes are written, the last of them a NUL whenever n > 0, so a return >= n means the text
 * was cut; n = 0 writes nothing and s may be NULL. A result longer than INT_MAX returns -1.
 *
 * The conversions: %d %i %u %x %X %c %s %p %%, with the flags - (left-justify) and 0 (pad with zeros),
 * a width, a precision (digits or *: for %s the most bytes taken from the string, for an integer the
 * fewest digits), and the length modifiers hh h l ll z. %s of NULL writes "(null)".
 * No floating point (%f %e %g %a), no %n, no flags + # or space: cd_fmt and cd_fmt_len (cdom.h) write
 * numbers with decimals. A conversion this list does not have is written as it stands (its text, so it
 * shows), and takes no argument. */
#ifndef CDOM_LIBC_STDIO_H
#define CDOM_LIBC_STDIO_H
#define __need_size_t
#define __need_NULL
#include <stddef.h>
#include <stdarg.h>

__attribute__((format(printf, 3, 4))) int snprintf(char *restrict s, size_t n, const char *restrict fmt, ...);
__attribute__((format(printf, 3, 0))) int vsnprintf(char *restrict s, size_t n, const char *restrict fmt, va_list ap);
#endif
