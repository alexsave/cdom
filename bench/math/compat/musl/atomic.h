/* atomic.h for musl's fma.c, which takes only a_clz_64 from it. */
#ifndef BENCH_MATH_COMPAT_ATOMIC_H
#define BENCH_MATH_COMPAT_ATOMIC_H
#include <stdint.h>
static inline int a_clz_64(uint64_t x) { return __builtin_clzll(x); }
#endif
