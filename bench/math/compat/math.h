/* math.h for the math bench's arms (bench/math/README is docs/math-design.md): the declarations each vendored libm and
 * ours are built against, in wasm and natively alike, so the two builds of an arm compile the same text. It is not
 * cdom's: src/libc/ has no math.h, and this one never leaves bench/math/. */
#ifndef BENCH_MATH_COMPAT_MATH_H
#define BENCH_MATH_COMPAT_MATH_H
#include <float.h>

typedef double double_t;
typedef float float_t;

#define NAN (__builtin_nanf(""))
#define INFINITY (__builtin_inff())
#define HUGE_VAL (__builtin_huge_val())
#define HUGE_VALF (__builtin_huge_valf())
#define HUGE_VALL (__builtin_huge_vall())
#define MATH_ERRNO 1
#define MATH_ERREXCEPT 2
#define math_errhandling 2
#define FP_ILOGBNAN (-1 - 0x7fffffff)
#define FP_ILOGB0 FP_ILOGBNAN
#define FP_NAN 0
#define FP_INFINITE 1
#define FP_ZERO 2
#define FP_SUBNORMAL 3
#define FP_NORMAL 4
#define fpclassify(x) __builtin_fpclassify(FP_NAN, FP_INFINITE, FP_NORMAL, FP_SUBNORMAL, FP_ZERO, x)
#define isnan(x) __builtin_isnan(x)
#define isinf(x) __builtin_isinf(x)
#define isfinite(x) __builtin_isfinite(x)
#define isnormal(x) __builtin_isnormal(x)
#define signbit(x) __builtin_signbit(x)
#define isunordered(x, y) __builtin_isunordered(x, y)
#define isless(x, y) __builtin_isless(x, y)
#define islessequal(x, y) __builtin_islessequal(x, y)
#define isgreater(x, y) __builtin_isgreater(x, y)
#define isgreaterequal(x, y) __builtin_isgreaterequal(x, y)
#define islessgreater(x, y) __builtin_islessgreater(x, y)
#define M_E 2.7182818284590452354
#define M_LN2 0.69314718055994530942
#define M_LN10 2.30258509299404568402
#define M_PI 3.14159265358979323846
#define M_PI_2 1.57079632679489661923
#define M_PI_4 0.78539816339744830962
#define M_SQRT2 1.41421356237309504880

#define BENCH_MATH_D1(n) double n(double); float n##f(float); long double n##l(long double);
#define BENCH_MATH_D2(n) double n(double, double); float n##f(float, float); long double n##l(long double, long double);
BENCH_MATH_D1(sin) BENCH_MATH_D1(cos) BENCH_MATH_D1(tan) BENCH_MATH_D1(asin) BENCH_MATH_D1(acos) BENCH_MATH_D1(atan)
BENCH_MATH_D2(atan2) BENCH_MATH_D1(exp) BENCH_MATH_D1(exp2) BENCH_MATH_D1(expm1) BENCH_MATH_D1(log) BENCH_MATH_D1(log2)
BENCH_MATH_D1(log10) BENCH_MATH_D1(log1p) BENCH_MATH_D2(pow) BENCH_MATH_D1(sinh) BENCH_MATH_D1(cosh) BENCH_MATH_D1(tanh)
BENCH_MATH_D1(cbrt) BENCH_MATH_D2(hypot) BENCH_MATH_D2(fmod) BENCH_MATH_D1(sqrt) BENCH_MATH_D1(fabs) BENCH_MATH_D1(floor)
BENCH_MATH_D1(ceil) BENCH_MATH_D1(trunc) BENCH_MATH_D1(rint) BENCH_MATH_D1(nearbyint) BENCH_MATH_D1(round)
BENCH_MATH_D2(copysign) BENCH_MATH_D2(fmax) BENCH_MATH_D2(fmin) BENCH_MATH_D1(asinh) BENCH_MATH_D1(acosh) BENCH_MATH_D1(atanh)
double fma(double, double, double); float fmaf(float, float, float);
double scalbn(double, int); float scalbnf(float, int); double ldexp(double, int); float ldexpf(float, int);
double frexp(double, int *); float frexpf(float, int *); int ilogb(double); int ilogbf(float);
double modf(double, double *); float modff(float, float *); double nan(const char *); float nanf(const char *);
long lrint(double); long lrintf(float);
#undef BENCH_MATH_D1
#undef BENCH_MATH_D2
#endif
