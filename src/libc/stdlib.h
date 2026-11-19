/* stdlib.h for a cdom app's program (see string.h beside it: the program's libc is cdom's). It offers the
 * heap and the two integer absolutes, and size_t and NULL, nothing more: no getenv, no exit, no qsort.
 *   malloc, calloc, realloc, free: cdom's heap (cdom.c defines them, weak, so an app that brings its own
 *     allocator defines all four, strong; cdom.h, "memory"). In a cdom build malloc, calloc and realloc trap
 *     when the memory cannot grow: code that must answer "no room" instead calls cd_alloc_try (cdom.h).
 *   abs, labs: inline functions (real ones, so &abs and a call through (abs)(x) work too), with C's
 *     undefined result for INT_MIN and LONG_MIN. */
#ifndef CDOM_LIBC_STDLIB_H
#define CDOM_LIBC_STDLIB_H
#define __need_size_t
#define __need_NULL
#include <stddef.h>

void *malloc(size_t n);
void *calloc(size_t n, size_t size);
void *realloc(void *p, size_t n);
void free(void *p);

static inline int abs(int v) { return v < 0 ? -v : v; }
static inline long labs(long v) { return v < 0 ? -v : v; }
#endif
