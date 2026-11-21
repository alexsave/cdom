/* The math bench's one random generator and its inputs (docs/math-design.md, "Inputs"). Every random input of the
 * bench, in every arm, in wasm and natively, in the timing, the accuracy sweep and the wasm-equals-native check,
 * comes from splitmix64 seeded from MATH_SEED: the stream of (purpose, entry, domain) starts at
 * MATH_SEED + golden * (1 + its index), so each stream is the same whichever order the bench asks for them in.
 * Header-only (static functions), so each arm's module and the native tools compile the same text. */
#ifndef BENCH_MATH_GEN_H
#define BENCH_MATH_GEN_H
#include <stdint.h>
#include "fns.h"

#define MATH_SEED 0x6d6174682d62656eull   /* the bench's one seed: "math-ben" */
#define BM_PURPOSE_TIME 0                  /* the 4096 timed inputs */
#define BM_PURPOSE_ACCURACY 1              /* the accuracy sweep's inputs */
#define BM_PURPOSE_TWIN 2                  /* the wasm-equals-native check's inputs */

typedef struct { uint64_t s; } bm_rng;

static inline uint64_t bm_next(bm_rng *g) {
  uint64_t z = (g->s += 0x9e3779b97f4a7c15ull);
  z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
  z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
  return z ^ (z >> 31);
}
/* A double uniform in [0, 1). */
static inline double bm_unit(bm_rng *g) { return (double)(bm_next(g) >> 11) * 0x1p-53; }

static inline bm_rng bm_stream(int purpose, int entry, int domain) {
  bm_rng g = { MATH_SEED + 0x9e3779b97f4a7c15ull * (uint64_t)(1 + (purpose * BM_NENTRY + entry) * 8 + domain) };
  return g;
}

/* 2^e for an integer e in [-1074, 1023], without libm. */
static inline double bm_pow2i(int e) {
  union { double d; uint64_t u; } v;
  if (e < -1022) { v.u = 1ull << (e + 1074); return v.d; }
  v.u = (uint64_t)(e + 1023) << 52;
  return v.d;
}
/* 2^t for a real t, by its integer part and 2^frac between 1 and 2 by a short series: an input generator, so it
 * needs no accuracy, only to spread values evenly over the binades. */
static inline double bm_exp2r(double t) {
  int e = (int)t;
  if (t < e) e--;
  double f = t - e, p = 1, term = 1;
  for (int i = 1; i < 20; i++) { term *= f * 0.6931471805599453 / i; p += term; }
  return p * bm_pow2i(e);
}
enum { U, L, S };   /* uniform in [a, b]; log-uniform 2^[a, b]; that with a random sign */
static inline double bm_draw(bm_rng *g, int kind, double a, double b) {
  double u = bm_unit(g);
  if (kind == U) return a + (b - a) * u;
  double v = bm_exp2r(a + (b - a) * u);
  return kind == S && (bm_next(g) & 1) ? -v : v;
}

/* One input (x, and y for a function of two) of entry e (2k, 2k + 1 for the float) in domain d (BM_SMALL,
 * BM_TYPICAL, BM_LARGE), drawn from g; a float entry's inputs are rounded to float. */
static inline void bm_input(bm_rng *g, int e, int d, double *x, double *y) {
  int k = e >> 1, fl = e & 1;
  const double pi4 = 0.78539816339744830962;
  double xx = 0, yy = 0;
  switch (k) {
  case 0: xx = bm_draw(g, L, fl ? -120 : -1000, fl ? 120 : 1000); break;                     /* sqrt */
  case 1: case 2: case 3:                                                                       /* sin cos tan */
    xx = d == BM_SMALL ? bm_draw(g, U, -pi4, pi4) : d == BM_TYPICAL ? bm_draw(g, U, -100, 100) : bm_draw(g, U, -1e6, 1e6); break;
  case 4: case 5: xx = bm_draw(g, U, -1, 1); break;                                             /* asin acos */
  case 6: xx = bm_draw(g, U, -100, 100); break;                                                 /* atan */
  case 7: xx = bm_draw(g, U, -100, 100); yy = bm_draw(g, U, -100, 100); break;                  /* atan2(y = xx, x = yy) */
  case 8: xx = fl ? bm_draw(g, U, -87, 88) : bm_draw(g, U, -700, 700); break;                   /* exp */
  case 9: xx = fl ? bm_draw(g, U, -125, 127) : bm_draw(g, U, -1000, 1000); break;               /* exp2 */
  case 10: xx = bm_draw(g, U, -10, 10); break;                                                  /* expm1 */
  case 11: case 12: case 13: xx = bm_draw(g, L, fl ? -120 : -1000, fl ? 120 : 1000); break;    /* log log2 log10 */
  case 14: xx = bm_draw(g, U, -0.9999, 10); break;                                              /* log1p */
  case 15: xx = bm_draw(g, L, fl ? -4 : -10, fl ? 4 : 10); yy = bm_draw(g, U, fl ? -20 : -50, fl ? 20 : 50); break; /* pow */
  case 16: case 17: case 18: xx = bm_draw(g, U, -20, 20); break;                                /* sinh cosh tanh */
  case 19: xx = bm_draw(g, S, fl ? -120 : -1000, fl ? 120 : 1000); break;                      /* cbrt */
  case 20: xx = bm_draw(g, U, -1000, 1000); yy = bm_draw(g, U, -1000, 1000); break;             /* hypot */
  case 21: xx = bm_draw(g, U, -1e6, 1e6); yy = bm_draw(g, U, 0.5, 100); break;                  /* fmod */
  }
  if (fl) { xx = (float)xx; yy = (float)yy; }
  *x = xx; *y = yy;
}
#endif
