/* stdlib.h for the math bench's arms: exit and abort trap (compat.c). size_t and the allocation functions are
 * declared because clang's x86intrin.h (CORE-MATH's native x86-64 twin reads the SSE status through it) includes
 * mm_malloc.h, which names them; no arm calls them. */
#ifndef BENCH_MATH_COMPAT_STDLIB_H
#define BENCH_MATH_COMPAT_STDLIB_H
#include <stddef.h>
_Noreturn void exit(int);
_Noreturn void abort(void);
void *malloc(size_t);
void free(void *);
int posix_memalign(void **, size_t, size_t);
#endif
