/* What every arm of the math bench links beside its libm (docs/math-design.md, "Building the arms"): errno, the
 * diagnostics CORE-MATH never reaches (printing does nothing, exit traps), and the functions that are one wasm
 * instruction (sqrt, fabs, floor, ceil, trunc, rint, copysign, fmin, fmax), plus round, which wasm lacks. All weak,
 * so an arm's own definition wins; each arm's module (and each native twin, whose symbols are made local) has its own. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define W __attribute__((weak))
W int errno;
struct bench_math_file { int unused; };
static struct bench_math_file no_file;
W FILE *const stderr = &no_file;
W FILE *const stdout = &no_file;
W int printf(const char *f, ...) { (void)f; return 0; }
W int fprintf(FILE *s, const char *f, ...) { (void)s; (void)f; return 0; }
W int puts(const char *t) { (void)t; return 0; }
W _Noreturn void exit(int c) { (void)c; __builtin_trap(); }
W _Noreturn void abort(void) { __builtin_trap(); }

/* The instructions, in wasm only: natively the host's libm has them (a native build without SSE4.1 would compile
 * __builtin_rint into a call to rint, which here would be this function calling itself). */
#ifdef __wasm__
W double sqrt(double x) { return __builtin_sqrt(x); }
W float sqrtf(float x) { return __builtin_sqrtf(x); }
W double fabs(double x) { return __builtin_fabs(x); }
W float fabsf(float x) { return __builtin_fabsf(x); }
W double floor(double x) { return __builtin_floor(x); }
W float floorf(float x) { return __builtin_floorf(x); }
W double ceil(double x) { return __builtin_ceil(x); }
W float ceilf(float x) { return __builtin_ceilf(x); }
W double trunc(double x) { return __builtin_trunc(x); }
W float truncf(float x) { return __builtin_truncf(x); }
W double rint(double x) { return __builtin_rint(x); }
W float rintf(float x) { return __builtin_rintf(x); }
W double nearbyint(double x) { return __builtin_rint(x); }
W double copysign(double x, double y) { return __builtin_copysign(x, y); }
W float copysignf(float x, float y) { return __builtin_copysignf(x, y); }
/* fmax and fmin: a NaN argument gives the other; between +0 and -0, fmax takes +0 and fmin -0. */
W double fmax(double x, double y) { return x != x ? y : y != y ? x : x > y ? x : x < y ? y : __builtin_signbit(x) ? y : x; }
W double fmin(double x, double y) { return x != x ? y : y != y ? x : x < y ? x : x > y ? y : __builtin_signbit(x) ? x : y; }
W float fmaxf(float x, float y) { return x != x ? y : y != y ? x : x > y ? x : x < y ? y : __builtin_signbit(x) ? y : x; }
W float fminf(float x, float y) { return x != x ? y : y != y ? x : x < y ? x : x > y ? y : __builtin_signbit(x) ? x : y; }
W float nanf(const char *t) { (void)t; return __builtin_nanf(""); }
W double nan(const char *t) { (void)t; return __builtin_nan(""); }
/* round: half away from zero; wasm's nearest rounds half to even. */
W double round(double x) {
  double t = __builtin_trunc(x);
  return __builtin_fabs(x - t) >= 0.5 ? t + __builtin_copysign(1.0, x) : t;
}
W float roundf(float x) {
  float t = __builtin_truncf(x);
  return __builtin_fabsf(x - t) >= 0.5f ? t + __builtin_copysignf(1.0f, x) : t;
}
#endif
