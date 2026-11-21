/* One arm of the math bench (docs/math-design.md): this file, built with an arm's libm (or, for the browser's
 * arms, with every function an import), is that arm's module in wasm and its object natively. It holds the timed
 * loops, the inputs, and the entry points the hosts call:
 *   bm_n()                 the number of entries (fns.h: entry 2k is function k in double, 2k + 1 in float)
 *   bm_name(e)             the entry's name, a NUL-terminated string in memory
 *   bm_doms(e)             its domains, a mask of BM_SMALL BM_TYPICAL BM_LARGE (fns.h); 0 when this arm lacks it
 *   bm_fill(e, d)          the 4096 timed inputs of entry e in domain d, from the one generator (gen.h)
 *   bm_run(e, reps)        reps passes over them, each call's result summed; the sum is returned, and every host
 *                          prints the sums at the end, so no loop can be optimized away
 *   bm_eval(e, x, y)       one call (the tests and the accuracy sweep)
 *   bm_digest(e, d, n)     a hash of the result bits of n inputs (the wasm-equals-native check), NaNs as one NaN
 *   bm_dump_begin(e, d), bm_dump_next(n)  the results of the accuracy inputs, in chunks (the browser's arms, whose
 *                          results the native sweep cannot compute)
 * In wasm these are exports of those names; natively each is <arm>_<name> (BM_ARM), so one program links every arm.
 * The browser's arms: BM_JS=1 calls an import per value (math.sin), BM_JS=2 one import per pass over the 4096
 * inputs (math.sin_n, which fills an array of results that the loop then sums). An arm that lacks a function
 * (CORE-MATH has no fmod) is built with -DBM_LACK_<name>. */
#include <math.h>
#include <stdint.h>
#include "fns.h"
#include "gen.h"

#define BM_N 4096
#define BM_DUMP_MAX 65536

#ifdef __wasm__
#define BM_SYM(name) name
#define BM_EXPORT(name) __attribute__((export_name(#name))) name
#else
#define BM_CAT2(a, b) a##_##b
#define BM_CAT(a, b) BM_CAT2(a, b)
#define BM_SYM(name) BM_CAT(BM_ARM, name)
#define BM_EXPORT(name) BM_SYM(name)
#endif

static double A[BM_N], B[BM_N];
static float AF[BM_N], BF[BM_N];
#if BM_JS == 2
static double OUT[BM_N];
static float OUTF[BM_N];
#endif
static double DUMP[BM_DUMP_MAX];
static bm_rng dump_rng;
static int dump_e, dump_d;

#ifndef BM_JS
#define BM_JS 0
#endif

/* The calls: a function of the arm's libm, or the browser's through an import. */
#if BM_JS
#define BM_IMP(name, ret, ...) __attribute__((import_module("math"), import_name(#name))) ret js_##name(__VA_ARGS__);
#define BM_IMP1(name) BM_IMP(name, double, double) BM_IMP(name##f, float, float)
#define BM_IMP2(name) BM_IMP(name, double, double, double) BM_IMP(name##f, float, float, float)
#define BM_IMPORT(name, arity, doms) BM_IMP##arity(name)                                                         \
  BM_IMP(name##_n, void, const double *, const double *, double *, int)                                         \
  BM_IMP(name##f_n, void, const float *, const float *, float *, int)
BM_FNS(BM_IMPORT)
#define BM_CALL1(name, x) js_##name(x)
#define BM_CALL2(name, x, y) js_##name(x, y)
#else
/* Natively an arm's functions are <arm>_sin and so on (the Makefile's rename header, which this file is not built
 * with); glibc's are libm's own names. sqrt, the instruction, keeps its name in every arm. */
#if defined(__wasm__) || defined(BM_HOST_LIBM)
#define BM_FN(name) name
#else
#define BM_FN(name) BM_CAT(BM_ARM, name)
#define BM_PROTO(name, arity, doms) BM_PROTO##arity(name)
#define BM_PROTO1(name) double BM_FN(name)(double); float BM_FN(name##f)(float);
#define BM_PROTO2(name) double BM_FN(name)(double, double); float BM_FN(name##f)(float, float);
static inline double BM_FN(sqrt)(double x) { return __builtin_sqrt(x); }
static inline float BM_FN(sqrtf)(float x) { return __builtin_sqrtf(x); }
#ifdef BM_LACK_fmod
static inline double BM_FN(fmod)(double x, double y) { (void)x; (void)y; return 0; }
static inline float BM_FN(fmodf)(float x, float y) { (void)x; (void)y; return 0; }
#endif
BM_FNS(BM_PROTO)
#endif
#define BM_CALL1(name, x) BM_FN(name)(x)
#define BM_CALL2(name, x, y) BM_FN(name)(x, y)
#endif

/* A function of one argument in a binary of two takes only x; the macros give each arity its own call. */
#define BM_ARITY1(name, x, y) BM_CALL1(name, x)
#define BM_ARITY2(name, x, y) BM_CALL2(name, x, y)
#define BM_ONE(arity, name, x, y) BM_ARITY##arity(name, x, y)

/* The timed loops, one per entry, each a direct call of the function. */
#if BM_JS == 2
#define BM_LOOPS(name, arity, doms)                                                                              \
  static double run_##name(int reps) {                                                                          \
    double s = 0;                                                                                               \
    for (int r = 0; r < reps; r++) { js_##name##_n(A, B, OUT, BM_N); for (int i = 0; i < BM_N; i++) s += OUT[i]; } \
    return s;                                                                                                   \
  }                                                                                                             \
  static double run_##name##f(int reps) {                                                                       \
    double s = 0;                                                                                               \
    for (int r = 0; r < reps; r++) { js_##name##f_n(AF, BF, OUTF, BM_N); for (int i = 0; i < BM_N; i++) s += OUTF[i]; } \
    return s;                                                                                                   \
  }
#else
#define BM_LOOPS(name, arity, doms)                                                                              \
  static double run_##name(int reps) {                                                                          \
    double s = 0;                                                                                               \
    for (int r = 0; r < reps; r++) for (int i = 0; i < BM_N; i++) s += BM_ONE(arity, name, A[i], B[i]);         \
    return s;                                                                                                   \
  }                                                                                                             \
  static double run_##name##f(int reps) {                                                                       \
    double s = 0;                                                                                               \
    for (int r = 0; r < reps; r++) for (int i = 0; i < BM_N; i++) s += BM_ONE(arity, name##f, AF[i], BF[i]);    \
    return s;                                                                                                   \
  }
#endif

/* A lacking function is never called: its loops and calls give 0, and bm_doms says 0. */
#ifdef BM_LACK_fmod
#define fmod(x, y) 0.0
#define fmodf(x, y) 0.0f
#define js_fmod(x, y) 0.0
#define js_fmodf(x, y) 0.0f
#define BM_HAS_fmod 0
#else
#define BM_HAS_fmod 1
#endif
#define BM_HAS(name) BM_HAS_##name
#define BM_HAS_sqrt 1
#define BM_HAS_sin 1
#define BM_HAS_cos 1
#define BM_HAS_tan 1
#define BM_HAS_asin 1
#define BM_HAS_acos 1
#define BM_HAS_atan 1
#define BM_HAS_atan2 1
#define BM_HAS_exp 1
#define BM_HAS_exp2 1
#define BM_HAS_expm1 1
#define BM_HAS_log 1
#define BM_HAS_log2 1
#define BM_HAS_log10 1
#define BM_HAS_log1p 1
#define BM_HAS_pow 1
#define BM_HAS_sinh 1
#define BM_HAS_cosh 1
#define BM_HAS_tanh 1
#define BM_HAS_cbrt 1
#define BM_HAS_hypot 1

BM_FNS(BM_LOOPS)

typedef double (*bm_loop)(int);
#define BM_TABLE_LOOP(name, arity, doms) run_##name, run_##name##f,
static const bm_loop loops[BM_NENTRY] = { BM_FNS(BM_TABLE_LOOP) };
#define BM_TABLE_NAME(name, arity, doms) #name, #name "f",
static const char *const names[BM_NENTRY] = { BM_FNS(BM_TABLE_NAME) };
#define BM_TABLE_DOMS(name, arity, doms) BM_HAS(name) ? doms : 0, BM_HAS(name) ? doms : 0,
static const int doms[BM_NENTRY] = { BM_FNS(BM_TABLE_DOMS) };

int BM_EXPORT(bm_n)(void) { return BM_NENTRY; }
const char *BM_EXPORT(bm_name)(int e) { return e >= 0 && e < BM_NENTRY ? names[e] : ""; }
int BM_EXPORT(bm_doms)(int e) { return e >= 0 && e < BM_NENTRY ? doms[e] : 0; }

void BM_EXPORT(bm_fill)(int e, int d) {
  bm_rng g = bm_stream(BM_PURPOSE_TIME, e, d);
  for (int i = 0; i < BM_N; i++) {
    double x, y;
    bm_input(&g, e, d, &x, &y);
    A[i] = x; B[i] = y; AF[i] = (float)x; BF[i] = (float)y;
  }
}

double BM_EXPORT(bm_run)(int e, int reps) { return e >= 0 && e < BM_NENTRY && doms[e] ? loops[e](reps) : 0; }

#define BM_EVAL_CASE(name, arity, doms)                                                                          \
  case 2 * bm_k_##name: return BM_ONE(arity, name, x, y);                                                       \
  case 2 * bm_k_##name + 1: return BM_ONE(arity, name##f, (float)x, (float)y);
double BM_EXPORT(bm_eval)(int e, double x, double y) {
  switch (e) { BM_FNS(BM_EVAL_CASE) }
  return 0;
}

/* The result's bits: a float result as its float bits, every NaN as one. */
static uint64_t bm_bits(int e, double v) {
  union { double d; uint64_t u; } b = { v };
  union { float f; uint32_t u; } c = { (float)v };
  if (v != v) return 0x7ff8000000000000ull;
  return e & 1 ? c.u : b.u;
}

uint64_t BM_EXPORT(bm_digest)(int e, int d, int n) {
  bm_rng g = bm_stream(BM_PURPOSE_TWIN, e, d);
  uint64_t h = 0xcbf29ce484222325ull;
  for (int i = 0; i < n; i++) {
    double x, y;
    bm_input(&g, e, d, &x, &y);
    h = (h ^ bm_bits(e, BM_SYM(bm_eval)(e, x, y))) * 0x100000001b3ull;
  }
  return h;
}

void BM_EXPORT(bm_dump_begin)(int e, int d) { dump_rng = bm_stream(BM_PURPOSE_ACCURACY, e, d); dump_e = e; dump_d = d; }
double *BM_EXPORT(bm_dump_next)(int n) {
  if (n > BM_DUMP_MAX) n = BM_DUMP_MAX;
  for (int i = 0; i < n; i++) {
    double x, y;
    bm_input(&dump_rng, dump_e, dump_d, &x, &y);
    DUMP[i] = BM_SYM(bm_eval)(dump_e, x, y);
  }
  return DUMP;
}
