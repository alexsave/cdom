/* Included before each FreeBSD msun file (-include): what FreeBSD's own math.h would have brought in, sys/cdefs.h
 * and the __double_t and __float_t its math_private.h names. */
#ifndef BENCH_MATH_COMPAT_MSUN_H
#define BENCH_MATH_COMPAT_MSUN_H
#include <sys/cdefs.h>
typedef double __double_t;
typedef float __float_t;
void sincos(double, double *, double *);
void sincosf(float, float *, float *);
#endif
