/* The math bench's accuracy sweep (docs/math-design.md, "Accuracy"): every native arm (each the same C as its wasm
 * module, which tools/twin.mjs checks bit for bit), against MPFR at 160 bits, on the accuracy inputs of gen.h.
 *   accuracy [n=1000000] [entries] [v8dir]   one TSV line per arm, entry and domain: the max and mean error in ulps
 *              of the correctly rounded result's binade, how many results are not the correctly rounded one, and
 *              the input of the max. entries: a comma list (sin,sinf), all by default. v8dir: a directory of the
 *              browser's results on the same inputs (<entry>-<domain>.f64, written by tools/v8dump.mjs), read as
 *              the arm v8 where a file is there.
 *   accuracy sample [n=200] [entries]        the first n inputs of each entry and domain with MPFR's correctly
 *              rounded result and CORE-MATH's, for tools/oracle.py to check against mpmath.
 * An error is |got - exact| / ulp, ulp = 2^(e - 52) (2^(e - 23) for float) where 2^e <= |exact| < 2^(e + 1), e no
 * lower than the least normal's. A result that is infinite, zero or NaN where the correctly rounded one is not the
 * same (or the reverse) counts as an error of 1e9 ulps. */
#define _POSIX_C_SOURCE 200809L
#include <math.h>
#include <mpfr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "arms.h"
#include "fns.h"
#include "gen.h"

#define NAME_ROW(n, arity, doms) #n, #n "f",
static const char *const names[BM_NENTRY] = { BM_FNS(NAME_ROW) };
#define DOMS_ROW(n, arity, doms) doms, doms,
static const int doms[BM_NENTRY] = { BM_FNS(DOMS_ROW) };
#define ARITY_ROW(n, arity, doms) arity, arity,
static const int arity[BM_NENTRY] = { BM_FNS(ARITY_ROW) };
static const char *dom_name(int d) { return d == BM_SMALL ? "small" : d == BM_TYPICAL ? "typical" : "large"; }
static int wanted(const char *list, const char *name) {
  if (!list) return 1;
  size_t n = strlen(name);
  for (const char *p = list; (p = strstr(p, name)); p += n)
    if ((p == list || p[-1] == ',') && (p[n] == ',' || p[n] == 0)) return 1;
  return 0;
}

/* exact = f(x, y) at the precision of r. */
static void ref(int e, double x, double y, mpfr_t r, mpfr_t mx, mpfr_t my) {
  mpfr_set_d(mx, x, MPFR_RNDN);
  mpfr_set_d(my, y, MPFR_RNDN);
  switch (e >> 1) {
  case bm_k_sqrt: mpfr_sqrt(r, mx, MPFR_RNDN); break;
  case bm_k_sin: mpfr_sin(r, mx, MPFR_RNDN); break;
  case bm_k_cos: mpfr_cos(r, mx, MPFR_RNDN); break;
  case bm_k_tan: mpfr_tan(r, mx, MPFR_RNDN); break;
  case bm_k_asin: mpfr_asin(r, mx, MPFR_RNDN); break;
  case bm_k_acos: mpfr_acos(r, mx, MPFR_RNDN); break;
  case bm_k_atan: mpfr_atan(r, mx, MPFR_RNDN); break;
  case bm_k_atan2: mpfr_atan2(r, mx, my, MPFR_RNDN); break;
  case bm_k_exp: mpfr_exp(r, mx, MPFR_RNDN); break;
  case bm_k_exp2: mpfr_exp2(r, mx, MPFR_RNDN); break;
  case bm_k_expm1: mpfr_expm1(r, mx, MPFR_RNDN); break;
  case bm_k_log: mpfr_log(r, mx, MPFR_RNDN); break;
  case bm_k_log2: mpfr_log2(r, mx, MPFR_RNDN); break;
  case bm_k_log10: mpfr_log10(r, mx, MPFR_RNDN); break;
  case bm_k_log1p: mpfr_log1p(r, mx, MPFR_RNDN); break;
  case bm_k_pow: mpfr_pow(r, mx, my, MPFR_RNDN); break;
  case bm_k_sinh: mpfr_sinh(r, mx, MPFR_RNDN); break;
  case bm_k_cosh: mpfr_cosh(r, mx, MPFR_RNDN); break;
  case bm_k_tanh: mpfr_tanh(r, mx, MPFR_RNDN); break;
  case bm_k_cbrt: mpfr_cbrt(r, mx, MPFR_RNDN); break;
  case bm_k_hypot: mpfr_hypot(r, mx, my, MPFR_RNDN); break;
  case bm_k_fmod: mpfr_fmod(r, mx, my, MPFR_RNDN); break;
  }
}
/* The correctly rounded result: a double, or a float (as a double). */
static double rounded(int fl, mpfr_t r) {
  if (!fl) return mpfr_get_d(r, MPFR_RNDN);
  return (double)mpfr_get_flt(r, MPFR_RNDN);
}
/* got's error in ulps of exact (r), whose correct rounding is cr. */
static double ulps(int fl, double got, double cr, mpfr_t r, mpfr_t t) {
  if (isnan(cr) || isinf(cr) || cr == 0 || isnan(got) || isinf(got)) {
    if (isnan(cr)) return isnan(got) ? 0 : 1e9;
    return got == cr && signbit(got) == signbit(cr) ? (cr == 0 && !mpfr_zero_p(r) ? 0.5 : 0) : 1e9;
  }
  long e = mpfr_get_exp(r) - 1;   /* 2^e <= |r| < 2^(e + 1) */
  long lo = fl ? -126 : -1022, p = fl ? 23 : 52;
  if (e < lo) e = lo;
  mpfr_set_d(t, got, MPFR_RNDN);
  mpfr_sub(t, t, r, MPFR_RNDN);
  mpfr_abs(t, t, MPFR_RNDN);
  mpfr_mul_2si(t, t, p - e, MPFR_RNDN);
  return mpfr_get_d(t, MPFR_RNDN);
}

typedef struct { double max, sum, wx, wy; long wrong, n; } stat;

int main(int argc, char **argv) {
  int sample = argc > 1 && !strcmp(argv[1], "sample");
  int a0 = sample ? 2 : 1;
  long n = argc > a0 ? atol(argv[a0]) : sample ? 200 : 1000000;
  const char *list = argc > a0 + 1 && strcmp(argv[a0 + 1], "all") ? argv[a0 + 1] : NULL;
  const char *v8dir = !sample && argc > a0 + 2 ? argv[a0 + 2] : NULL;
  mpfr_t r, t, mx, my;
  mpfr_inits2(160, r, t, mx, my, (mpfr_ptr)0);
  mpfr_set_emin(mpfr_get_emin_min());
  mpfr_set_emax(mpfr_get_emax_max());
  if (!sample) printf("arm\tentry\tdomain\tn\tmax_ulp\tmean_ulp\tnot_cr\tworst_x\tworst_y\n");
  for (int e = 0; e < BM_NENTRY; e++)
    for (int d = 1; d <= 4; d <<= 1) {
      if (!(doms[e] & d) || !wanted(list, names[e])) continue;
      int fl = e & 1;
      stat st[BM_NARMS + 1];
      memset(st, 0, sizeof st);
      FILE *v8 = NULL;
      if (v8dir) {
        char path[4096];
        snprintf(path, sizeof path, "%s/%s-%s.f64", v8dir, names[e], dom_name(d));
        v8 = fopen(path, "rb");
      }
      bm_rng g = bm_stream(BM_PURPOSE_ACCURACY, e, d);
      for (long i = 0; i < n; i++) {
        double x, y;
        bm_input(&g, e, d, &x, &y);
        ref(e, x, y, r, mx, my);
        double cr = rounded(fl, r);
        if (sample) {
          double c = bm_arms[2].doms(e) & d ? bm_arms[2].eval(e, x, y) : NAN;
          printf("%s\t%s\t%a\t%a\t%a\t%a\n", names[e], dom_name(d), x, arity[e] == 2 ? y : 0.0, cr, c);
          continue;
        }
        for (int a = 0; a <= BM_NARMS; a++) {
          double got;
          if (a == BM_NARMS) {
            if (!v8 || fread(&got, sizeof got, 1, v8) != 1) continue;
          } else {
            if (!(bm_arms[a].doms(e) & d)) continue;
            got = bm_arms[a].eval(e, x, y);
          }
          if (fl) got = (float)got;
          double u = ulps(fl, got, cr, r, t);
          stat *s = &st[a];
          s->n++;
          s->sum += u;
          if (!(got == cr || (isnan(got) && isnan(cr)))) s->wrong++;
          if (u > s->max) { s->max = u; s->wx = x; s->wy = y; }
        }
      }
      if (v8) fclose(v8);
      if (sample) continue;
      for (int a = 0; a <= BM_NARMS; a++) {
        stat *s = &st[a];
        if (!s->n) continue;
        printf("%s\t%s\t%s\t%ld\t%.4f\t%.5f\t%ld\t%a\t%a\n", a == BM_NARMS ? "v8" : bm_arms[a].name, names[e], dom_name(d), s->n,
               s->max, s->sum / s->n, s->wrong, s->wx, arity[e] == 2 ? s->wy : 0.0);
      }
      fflush(stdout);
    }
  mpfr_clears(r, t, mx, my, (mpfr_ptr)0);
  return 0;
}
