/* The functions of the math bench (docs/math-design.md), in one X-macro list so every part of the bench (each
 * arm's module, the native driver, the accuracy sweep, the tests, the JS glue through bm_name) numbers them the
 * same: function k is entry 2k (double) and 2k + 1 (float, its name with an f).
 *   X(name, arity, domains): arity 1 or 2; domains a mask of BM_SMALL, BM_TYPICAL and BM_LARGE (gen.h).
 * sqrt is the control: it is a wasm instruction (f64.sqrt), so every arm should tie on it. */
#ifndef BENCH_MATH_FNS_H
#define BENCH_MATH_FNS_H
#define BM_SMALL 1
#define BM_TYPICAL 2
#define BM_LARGE 4
#define BM_TRIG (BM_SMALL | BM_TYPICAL | BM_LARGE)
#define BM_FNS(X) \
  X(sqrt, 1, BM_TYPICAL) X(sin, 1, BM_TRIG) X(cos, 1, BM_TRIG) X(tan, 1, BM_TRIG) X(asin, 1, BM_TYPICAL) \
  X(acos, 1, BM_TYPICAL) X(atan, 1, BM_TYPICAL) X(atan2, 2, BM_TYPICAL) X(exp, 1, BM_TYPICAL) X(exp2, 1, BM_TYPICAL) \
  X(expm1, 1, BM_TYPICAL) X(log, 1, BM_TYPICAL) X(log2, 1, BM_TYPICAL) X(log10, 1, BM_TYPICAL) X(log1p, 1, BM_TYPICAL) \
  X(pow, 2, BM_TYPICAL) X(sinh, 1, BM_TYPICAL) X(cosh, 1, BM_TYPICAL) X(tanh, 1, BM_TYPICAL) X(cbrt, 1, BM_TYPICAL) \
  X(hypot, 2, BM_TYPICAL) X(fmod, 2, BM_TYPICAL)
#define BM_COUNT_ONE(name, arity, doms) + 1
#define BM_NFN (0 BM_FNS(BM_COUNT_ONE))
#define BM_NENTRY (2 * BM_NFN)
#define BM_ENUM_ONE(name, arity, doms) bm_k_##name,
enum { BM_FNS(BM_ENUM_ONE) };
#endif
