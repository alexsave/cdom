/* sys/cdefs.h for FreeBSD msun: the macros its sources use. __weak_reference makes nothing: the long double
 * aliases (sinl for sin where long double is double) are not part of the bench. */
#ifndef BENCH_MATH_COMPAT_SYS_CDEFS_H
#define BENCH_MATH_COMPAT_SYS_CDEFS_H
#define __FBSDID(s) struct bench_math_fbsdid
#define __weak_reference(sym, alias) struct bench_math_weak_ref
#define __strong_reference(sym, alias) struct bench_math_strong_ref
#define __CONCAT1(x, y) x ## y
#define __CONCAT(x, y) __CONCAT1(x, y)
#define __XSTRING(x) #x
#define __inline inline
#define __always_inline __attribute__((__always_inline__))
#define __unused __attribute__((__unused__))
#define __predict_true(e) __builtin_expect((e), 1)
#define __predict_false(e) __builtin_expect((e), 0)
#define __BSD_VISIBLE 1
#define __ISO_C_VISIBLE 2011
#endif
