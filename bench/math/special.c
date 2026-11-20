/* The special-value test of the math bench (docs/math-design.md, "Tests"): each arm's results for the special
 * inputs of C23's Annex F (F.10: zeros, infinities, NaN, the overflow and underflow edges, pow's table), plus a
 * few values every good libm gives exactly (cbrt(-8) = -2, hypot(3, 4) = 5, log2(2^k) = k). Built into each
 * arm's module and native twin beside bench.c, it calls the arm through bm_eval, so the browser's arms are tested
 * through the same import a timed call takes.
 *   sp_run()   the number of failures: a case whose result is not the one Annex F gives (NaN for NaN, a zero or
 *              infinity of the right sign, an exact value bit for bit), or a case listed as a known deviation of
 *              this arm (dev below) that no longer deviates, so the list stays true. The report is at sp_text().
 * Where the result is a rounded constant (pi, pi/2, pi/4, 3pi/4: Annex F names the value, not its rounding), it may
 * be either neighbour of the constant: within 1 ulp. A known deviation is listed per arm with its reason: the
 * browser's Math.pow (ECMAScript's pow differs from C's on purpose) and glibc's cbrt (not exact on exact cubes).
 * The arm is SP_ARM_<name> (the Makefile defines it). In wasm these are exports; natively <arm>_sp_run and so on. */
#include <stdint.h>
#include "fns.h"

#ifdef __wasm__
#define SP_SYM(name) name
#define SP_EXPORT(name) __attribute__((export_name(#name))) name
#else
#define SP_CAT2(a, b) a##_##b
#define SP_CAT(a, b) SP_CAT2(a, b)
#define SP_SYM(name) SP_CAT(BM_ARM, name)
#define SP_EXPORT(name) SP_SYM(name)
#endif
double SP_SYM(bm_eval)(int e, double x, double y);

#ifndef BM_JS
#define BM_JS 0
#endif
#ifdef BM_LACK_fmod
#define SP_LACK_FMOD 1
#else
#define SP_LACK_FMOD 0
#endif

#define INF __builtin_inf()
#define NAN_ __builtin_nan("")
#define PI 0x1.921fb54442d18p+1
#define PI_2 0x1.921fb54442d18p+0
#define PI_4 0x1.921fb54442d18p-1
#define PI3_4 0x1.2d97c7f3321d2p+1
#define PIF 0x1.921fb6p+1f
#define PI_2F 0x1.921fb6p+0f
#define PI_4F 0x1.921fb6p-1f
#define PI3_4F 0x1.2d97c8p+1f
#define TD 0x1p-1074          /* the least double subnormal */
#define TF 0x1p-149           /* the least float subnormal */

enum { DEV_JS = 1, DEV_GLIBC = 2 };   /* the arms a case is known to deviate in */
#if defined(SP_ARM_js)
#define SP_ME DEV_JS
#elif defined(SP_ARM_glibc)
#define SP_ME DEV_GLIBC
#else
#define SP_ME 0
#endif
typedef struct { int e; double x, y, want; int dev; const char *why; } sp_case;

/* D: a double case; F: its float entry; B: both, with the same values (each exact in float). */
#define D(f, x, y, w) { 2 * bm_k_##f, x, y, w, 0, #f "(" #x ", " #y ")" },
#define F(f, x, y, w) { 2 * bm_k_##f + 1, x, y, w, 0, #f "f(" #x ", " #y ")" },
#define B(f, x, y, w) D(f, x, y, w) F(f, x, y, w)
/* JS: a pow case where ECMAScript's Math.pow gives NaN on purpose (ECMA-262, Number::exponentiate: a base of +1
 * or -1 with an infinite exponent, and +1 with a NaN exponent, are NaN), and C's pow 1. */
/* GL: glibc's cbrt is not correctly rounded (its own manual lists cbrt at 1 ulp), so an exact cube can miss. */
#define DGL(f, x, y, w) { 2 * bm_k_##f, x, y, w, DEV_GLIBC, #f "(" #x ", " #y ")" },
#define BJS(f, x, y, w) { 2 * bm_k_##f, x, y, w, DEV_JS, #f "(" #x ", " #y ")" }, { 2 * bm_k_##f + 1, x, y, w, DEV_JS, #f "f(" #x ", " #y ")" },

static const sp_case cases[] = {
  /* sqrt (the control) */
  B(sqrt, -0.0, 0, -0.0) B(sqrt, 0.0, 0, 0.0) B(sqrt, -1.0, 0, NAN_) B(sqrt, INF, 0, INF) B(sqrt, NAN_, 0, NAN_) B(sqrt, 4.0, 0, 2.0)
  /* F.10.1.6 sin, .7 tan, .5 cos */
  B(sin, 0.0, 0, 0.0) B(sin, -0.0, 0, -0.0) B(sin, INF, 0, NAN_) B(sin, -INF, 0, NAN_) B(sin, NAN_, 0, NAN_)
  D(sin, TD, 0, TD) D(sin, -TD, 0, -TD) F(sin, TF, 0, TF)
  B(cos, 0.0, 0, 1.0) B(cos, -0.0, 0, 1.0) B(cos, INF, 0, NAN_) B(cos, -INF, 0, NAN_) B(cos, NAN_, 0, NAN_) D(cos, TD, 0, 1.0)
  B(tan, 0.0, 0, 0.0) B(tan, -0.0, 0, -0.0) B(tan, INF, 0, NAN_) B(tan, -INF, 0, NAN_) B(tan, NAN_, 0, NAN_) D(tan, -TD, 0, -TD)
  /* F.10.1.2 asin, .1 acos, .3 atan */
  B(asin, 0.0, 0, 0.0) B(asin, -0.0, 0, -0.0) B(asin, 2.0, 0, NAN_) B(asin, -2.0, 0, NAN_) B(asin, INF, 0, NAN_) B(asin, NAN_, 0, NAN_)
  D(asin, 1.0, 0, PI_2) D(asin, -1.0, 0, -PI_2) F(asin, 1.0, 0, PI_2F) D(asin, TD, 0, TD)
  B(acos, 1.0, 0, 0.0) B(acos, 2.0, 0, NAN_) B(acos, -2.0, 0, NAN_) B(acos, -INF, 0, NAN_) B(acos, NAN_, 0, NAN_)
  D(acos, -1.0, 0, PI) F(acos, -1.0, 0, PIF) D(acos, 0.0, 0, PI_2)
  B(atan, 0.0, 0, 0.0) B(atan, -0.0, 0, -0.0) B(atan, NAN_, 0, NAN_) D(atan, INF, 0, PI_2) D(atan, -INF, 0, -PI_2)
  F(atan, INF, 0, PI_2F) F(atan, -INF, 0, -PI_2F) D(atan, -TD, 0, -TD)
  /* F.10.1.4 atan2(y, x) */
  D(atan2, 0.0, -0.0, PI) D(atan2, -0.0, -0.0, -PI) D(atan2, 0.0, 0.0, 0.0) D(atan2, -0.0, 0.0, -0.0)
  D(atan2, 0.0, -1.0, PI) D(atan2, -0.0, -1.0, -PI) D(atan2, 0.0, 1.0, 0.0) D(atan2, -0.0, 1.0, -0.0)
  D(atan2, -1.0, 0.0, -PI_2) D(atan2, -1.0, -0.0, -PI_2) D(atan2, 1.0, 0.0, PI_2) D(atan2, 1.0, -0.0, PI_2)
  D(atan2, 1.0, -INF, PI) D(atan2, -1.0, -INF, -PI) D(atan2, 1.0, INF, 0.0) D(atan2, -1.0, INF, -0.0)
  D(atan2, INF, 1.0, PI_2) D(atan2, -INF, 1.0, -PI_2) D(atan2, INF, -INF, PI3_4) D(atan2, -INF, -INF, -PI3_4)
  D(atan2, INF, INF, PI_4) D(atan2, -INF, INF, -PI_4) D(atan2, NAN_, 1.0, NAN_) D(atan2, 1.0, NAN_, NAN_)
  F(atan2, 0.0, -0.0, PIF) F(atan2, -0.0, -0.0, -PIF) F(atan2, 0.0, 0.0, 0.0) F(atan2, -0.0, 0.0, -0.0)
  F(atan2, -1.0, 0.0, -PI_2F) F(atan2, 1.0, -INF, PIF) F(atan2, -1.0, INF, -0.0) F(atan2, INF, -INF, PI3_4F)
  F(atan2, -INF, INF, -PI_4F) F(atan2, NAN_, 1.0, NAN_)
  /* F.10.3.1 exp, .2 exp2, .3 expm1, with the overflow and underflow edges */
  B(exp, 0.0, 0, 1.0) B(exp, -0.0, 0, 1.0) B(exp, -INF, 0, 0.0) B(exp, INF, 0, INF) B(exp, NAN_, 0, NAN_)
  D(exp, 710.0, 0, INF) D(exp, -746.0, 0, 0.0) D(exp, TD, 0, 1.0) D(exp, 0x1.62e42fefa39efp+9, 0, 0x1.fffffffffff2ap+1023)
  F(exp, 89.0, 0, INF) F(exp, -104.0, 0, 0.0) F(exp, TF, 0, 1.0)
  B(exp2, 0.0, 0, 1.0) B(exp2, -0.0, 0, 1.0) B(exp2, -INF, 0, 0.0) B(exp2, INF, 0, INF) B(exp2, NAN_, 0, NAN_)
  D(exp2, 1024.0, 0, INF) D(exp2, 1023.0, 0, 0x1p1023) D(exp2, -1074.0, 0, TD) D(exp2, -1075.0, 0, 0.0) D(exp2, -1022.0, 0, 0x1p-1022)
  F(exp2, 128.0, 0, INF) F(exp2, 127.0, 0, 0x1p127) F(exp2, -149.0, 0, TF) F(exp2, -150.0, 0, 0.0) B(exp2, 3.0, 0, 8.0)
  B(expm1, 0.0, 0, 0.0) B(expm1, -0.0, 0, -0.0) B(expm1, -INF, 0, -1.0) B(expm1, INF, 0, INF) B(expm1, NAN_, 0, NAN_)
  D(expm1, 710.0, 0, INF) F(expm1, 89.0, 0, INF) D(expm1, -TD, 0, -TD) F(expm1, TF, 0, TF) B(expm1, -100.0, 0, -1.0)
  /* F.10.3.7 log, .8 log10, .9 log1p, .10 log2 */
  B(log, 0.0, 0, -INF) B(log, -0.0, 0, -INF) B(log, 1.0, 0, 0.0) B(log, -1.0, 0, NAN_) B(log, -INF, 0, NAN_) B(log, INF, 0, INF)
  B(log, NAN_, 0, NAN_) D(log, TD, 0, -0x1.74385446d71c3p+9) F(log, TF, 0, -0x1.9d1da0p+6f)
  B(log2, 0.0, 0, -INF) B(log2, -0.0, 0, -INF) B(log2, 1.0, 0, 0.0) B(log2, -1.0, 0, NAN_) B(log2, INF, 0, INF) B(log2, NAN_, 0, NAN_)
  D(log2, TD, 0, -1074.0) F(log2, TF, 0, -149.0) D(log2, 0x1p1023, 0, 1023.0) B(log2, 8.0, 0, 3.0)
  B(log10, 0.0, 0, -INF) B(log10, -0.0, 0, -INF) B(log10, 1.0, 0, 0.0) B(log10, -1.0, 0, NAN_) B(log10, INF, 0, INF) B(log10, NAN_, 0, NAN_)
  B(log1p, 0.0, 0, 0.0) B(log1p, -0.0, 0, -0.0) B(log1p, -1.0, 0, -INF) B(log1p, -2.0, 0, NAN_) B(log1p, INF, 0, INF)
  B(log1p, -INF, 0, NAN_) B(log1p, NAN_, 0, NAN_) D(log1p, TD, 0, TD) F(log1p, -TF, 0, -TF)
  /* F.10.4.5 pow */
  B(pow, 0.0, -3.0, INF) B(pow, -0.0, -3.0, -INF) B(pow, 0.0, -2.0, INF) B(pow, -0.0, -2.0, INF) B(pow, -0.0, -0.5, INF)
  B(pow, 0.0, -INF, INF) B(pow, -0.0, -INF, INF) B(pow, 0.0, 3.0, 0.0) B(pow, -0.0, 3.0, -0.0) B(pow, -0.0, 2.0, 0.0)
  B(pow, -0.0, 0.5, 0.0) BJS(pow, -1.0, INF, 1.0) BJS(pow, -1.0, -INF, 1.0) BJS(pow, 1.0, NAN_, 1.0) BJS(pow, 1.0, INF, 1.0)
  B(pow, 1.0, -7.5, 1.0) B(pow, NAN_, 0.0, 1.0) B(pow, NAN_, -0.0, 1.0) B(pow, INF, 0.0, 1.0)
  B(pow, -2.0, 0.5, NAN_) B(pow, -2.0, 1.5, NAN_) B(pow, 0.5, -INF, INF) B(pow, -0.5, -INF, INF) B(pow, 2.0, -INF, 0.0)
  B(pow, -2.0, -INF, 0.0) B(pow, 0.5, INF, 0.0) B(pow, -0.5, INF, 0.0) B(pow, 2.0, INF, INF) B(pow, -2.0, INF, INF)
  B(pow, -INF, -3.0, -0.0) B(pow, -INF, -2.0, 0.0) B(pow, -INF, 3.0, -INF) B(pow, -INF, 2.0, INF) B(pow, INF, -1.0, 0.0)
  B(pow, INF, 1.0, INF) B(pow, NAN_, 1.0, NAN_) B(pow, 2.0, NAN_, NAN_) B(pow, -2.0, 3.0, -8.0) B(pow, 2.0, 10.0, 1024.0)
  D(pow, 10.0, 400.0, INF) D(pow, -10.0, 401.0, -INF) D(pow, 10.0, -400.0, 0.0) D(pow, 2.0, -1074.0, TD)
  F(pow, 10.0, 40.0, INF) F(pow, -10.0, 41.0, -INF) F(pow, 2.0, -149.0, TF)
  /* F.10.2.5 sinh, .4 cosh, .6 tanh */
  B(sinh, 0.0, 0, 0.0) B(sinh, -0.0, 0, -0.0) B(sinh, INF, 0, INF) B(sinh, -INF, 0, -INF) B(sinh, NAN_, 0, NAN_)
  D(sinh, 711.0, 0, INF) D(sinh, -711.0, 0, -INF) F(sinh, 90.0, 0, INF) D(sinh, TD, 0, TD)
  B(cosh, 0.0, 0, 1.0) B(cosh, -0.0, 0, 1.0) B(cosh, INF, 0, INF) B(cosh, -INF, 0, INF) B(cosh, NAN_, 0, NAN_) D(cosh, 711.0, 0, INF)
  B(tanh, 0.0, 0, 0.0) B(tanh, -0.0, 0, -0.0) B(tanh, INF, 0, 1.0) B(tanh, -INF, 0, -1.0) B(tanh, NAN_, 0, NAN_)
  B(tanh, 40.0, 0, 1.0) D(tanh, -TD, 0, -TD)
  /* F.10.4.1 cbrt, .3 hypot */
  B(cbrt, 0.0, 0, 0.0) B(cbrt, -0.0, 0, -0.0) B(cbrt, INF, 0, INF) B(cbrt, -INF, 0, -INF) B(cbrt, NAN_, 0, NAN_)
  B(cbrt, -8.0, 0, -2.0) DGL(cbrt, 27.0, 0, 3.0) F(cbrt, 27.0, 0, 3.0) DGL(cbrt, TD, 0, 0x1p-358)
  B(hypot, 3.0, 0.0, 3.0) B(hypot, -3.0, -0.0, 3.0) B(hypot, INF, NAN_, INF) B(hypot, NAN_, -INF, INF) B(hypot, NAN_, 1.0, NAN_)
  B(hypot, 3.0, 4.0, 5.0) D(hypot, 0x1p1023, 0x1p1023, 0x1.6a09e667f3bcdp+1023) D(hypot, 0x1.fffffffffffffp+1023, 0x1p1023, INF) D(hypot, 0x1p1022, 0x1p1022, 0x1.6a09e667f3bcdp+1022)
  D(hypot, TD, TD, TD) F(hypot, 0x1p127, 0x1p127, 0x1.6a09e6p+127f) F(hypot, 0x1.fffffep+127f, 0x1p127, INF)
  /* F.10.7.1 fmod */
  B(fmod, 0.0, 3.0, 0.0) B(fmod, -0.0, 3.0, -0.0) B(fmod, INF, 3.0, NAN_) B(fmod, 3.0, 0.0, NAN_) B(fmod, 3.0, INF, 3.0)
  B(fmod, -3.0, -INF, -3.0) B(fmod, NAN_, 3.0, NAN_) B(fmod, 3.0, NAN_, NAN_) B(fmod, -5.5, 2.0, -1.5) B(fmod, 5.0, -3.0, 2.0)
  B(fmod, 0x1p100, 3.0, 1.0) D(fmod, TD, 1.0, TD)
};

static char text[16384];
static int len;
static void put(const char *s) { while (*s && len < (int)sizeof text - 1) text[len++] = *s++; }
static void put_hex(uint64_t v) {
  char b[19] = "0x";
  for (int i = 0; i < 16; i++) b[2 + i] = "0123456789abcdef"[(v >> (60 - 4 * i)) & 15];
  b[18] = 0;
  put(b);
}
static uint64_t bits(int fl, double v) {
  union { double d; uint64_t u; } b = { v };
  union { float f; uint32_t u; } c = { (float)v };
  return fl ? c.u : b.u;
}
static int nan_(double v) { return v != v; }
/* want is pi, pi/2, pi/4 or 3pi/4 (rounded, either sign): got may be within 1 ulp of it. */
static int near_pi(int fl, double got, double want) {
  static const double d[] = { PI, PI_2, PI_4, PI3_4 };
  static const float f[] = { PIF, PI_2F, PI_4F, PI3_4F };
  double a = want < 0 ? -want : want;
  for (int i = 0; i < 4; i++)
    if (fl ? a == f[i] : a == d[i]) {
      int64_t g = (int64_t)bits(fl, got), w = (int64_t)bits(fl, want);
      return g - w <= 1 && w - g <= 1;
    }
  return 0;
}

int SP_EXPORT(sp_run)(void) {
  int fails = 0;
  len = 0;
  for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    const sp_case *c = &cases[i];
    int fl = c->e & 1;
    if (SP_LACK_FMOD && c->e >> 1 == bm_k_fmod) continue;
    double got = SP_SYM(bm_eval)(c->e, c->x, c->y);
    int ok = nan_(c->want) ? nan_(got) : !nan_(got) && (bits(fl, got) == bits(fl, c->want) || near_pi(fl, got, c->want));
    int dev = (c->dev & SP_ME) != 0;
    if (ok == !dev) continue;
    fails++;
    put(dev ? "NOW RIGHT, listed as a deviation: " : "FAIL ");
    put(c->why);
    put(": got ");
    put_hex(bits(fl, got));
    put(", want ");
    put_hex(bits(fl, c->want));
    put("\n");
  }
  text[len] = 0;
  return fails;
}
const char *SP_EXPORT(sp_text)(void) { return text; }
int SP_EXPORT(sp_count)(void) { return (int)(sizeof cases / sizeof cases[0]); }
