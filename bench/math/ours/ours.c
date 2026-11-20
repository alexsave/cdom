/* Our own short maths functions (docs/math-design.md, "Our own functions"): range reduction, then a minimax
 * polynomial whose coefficients bench/math/tools/remez.py fits with sollya (coef.h), and no tables. The target is a
 * max error under 1 ulp on the bench's domains; where one falls short, the doc says so with the measured error.
 * No fused multiply-add anywhere (wasm has none): where a step needs an exact product it splits its operands
 * (Veltkamp and Dekker, two_prod below). Built as the bench's "ours" arm only: this is not cdom's libc.
 * Known limits: sin, cos and tan reduce by pi/2 in four parts with a multiple k under 2^20, so they are accurate
 * for |x| up to about 1.6e6 and lose bits beyond (the bench's large domain stops at 1e6); exp, exp2, pow and
 * friends round twice when the result is subnormal. */
#include <math.h>
#include <stdint.h>
#include "coef.h"

static inline uint64_t asu(double x) { union { double d; uint64_t u; } v = { x }; return v.u; }
static inline double asd(uint64_t u) { union { uint64_t u; double d; } v = { u }; return v.d; }
/* round to the nearest integer, for |x| < 2^51, by the 1.5 * 2^52 trick (no call, the same on every target). */
static inline double round_int(double x) { return (x + 0x1.8p52) - 0x1.8p52; }
/* a = hi + lo exactly (Veltkamp), for |a| < 2^995. */
static inline void split(double a, double *hi, double *lo) {
  double c = 134217729.0 * a;
  *hi = c - (c - a);
  *lo = a - *hi;
}
/* a * b = p + *e exactly (Dekker), barring overflow. */
static inline double two_prod(double a, double b, double *e) {
  double p = a * b, ah, al, bh, bl;
  split(a, &ah, &al);
  split(b, &bh, &bl);
  *e = ((ah * bh - p) + ah * bl + al * bh) + al * bl;
  return p;
}
/* a + b = s + *e exactly (Knuth); fast_two_sum needs |a| >= |b|. */
static inline double two_sum(double a, double b, double *e) {
  double s = a + b, bb = s - a;
  *e = (a - (s - bb)) + (b - bb);
  return s;
}
static inline double fast_two_sum(double a, double b, double *e) {
  double s = a + b;
  *e = b - (s - a);
  return s;
}
static inline double horner(const double *c, int n, double x) {
  double r = c[n - 1];
  for (int i = n - 2; i >= 0; i--) r = c[i] + x * r;
  return r;
}
#define N(a) ((int)(sizeof(a) / sizeof((a)[0])))
/* y * 2^k for an integer k from -1100 to 1100, with one rounding unless the result is subnormal. */
static inline double scale(double y, int k) {
  if (k > 1023) return y * 0x1p1023 * asd((uint64_t)(k - 1023 + 1023) << 52);
  if (k < -1022) return y * asd((uint64_t)(k + 1000 + 1023) << 52) * 0x1p-1000;
  return y * asd((uint64_t)(k + 1023) << 52);
}

/* ---- sin, cos, tan ---- */

/* x - k pi/2 = *hi + *lo, k returned; pi/2 in four parts, 33 + 33 + 33 + 53 bits, so each k * part is exact while
 * |k| < 2^20, and the reduced argument keeps about 100 bits however close x is to a multiple of pi/2. */
static int rem_pio2(double x, double *hi, double *lo) {
  double fn = round_int(x * INVPIO2);
  double t = x - fn * PIO2_1, e1, w;
  double r = two_sum(t, -fn * PIO2_2, &e1);
  w = e1 - fn * PIO2_3 - fn * PIO2_4;
  *hi = fast_two_sum(r, w, lo);
  /* the quadrant: fn itself while it fits an int, else (a reduction that has lost every bit anyway) fn mod 4 */
  return fabs(fn) < 0x1p30 ? (int)fn : (int)(fn - 4.0 * floor(fn * 0.25));
}
/* sin(x + y), |x| <= pi/4, |y| tiny beside x. */
static inline double sin_k(double x, double y) {
  double z = x * x, v = z * x;
  return x + (y - 0.5 * z * y + v * horner(SIN, N(SIN), z));
}
/* cos(x + y), |x| <= pi/4: 1 - z/2 is rounded once and its error carried. */
static inline double cos_k(double x, double y) {
  double z = x * x, hz = 0.5 * z, w = 1.0 - hz;
  return w + (((1.0 - w) - hz) + (z * z * horner(COS, N(COS), z) - x * y));
}
double sin(double x) {
  double ax = fabs(x), hi, lo;
  if (ax < 0x1p-26) return x;
  if (ax <= PIO4_HI) return sin_k(x, 0);
  if (!(ax < INFINITY)) return x - x;
  int n = rem_pio2(x, &hi, &lo);
  switch (n & 3) {
  case 0: return sin_k(hi, lo);
  case 1: return cos_k(hi, lo);
  case 2: return -sin_k(hi, lo);
  default: return -cos_k(hi, lo);
  }
}
double cos(double x) {
  double ax = fabs(x), hi, lo;
  if (ax < 0x1p-27) return 1.0;
  if (ax <= PIO4_HI) return cos_k(x, 0);
  if (!(ax < INFINITY)) return x - x;
  int n = rem_pio2(x, &hi, &lo);
  switch (n & 3) {
  case 0: return cos_k(hi, lo);
  case 1: return -sin_k(hi, lo);
  case 2: return -cos_k(hi, lo);
  default: return sin_k(hi, lo);
  }
}
/* tan(x + y) (odd 0) or -1/tan(x + y) (odd 1), |x| <= pi/4. Past 0.6745, x becomes pi/4 - x and the result comes
 * from tan(pi/4 - u) = (1 - tan u)/(1 + tan u), so the polynomial only spans |x| <= 0.6745. */
static double tan_k(double x, double y, int odd) {
  int big = fabs(x) >= 0.6745, neg = x < 0;
  if (big) {
    if (neg) { x = -x; y = -y; }
    x = (PIO4_HI - x) + (PIO4_LO - y);
    y = 0;
  }
  double z = x * x, v = z * x * horner(TAN, N(TAN), z);
  double r = y + (z * y + v), w = x + r;
  if (big) {
    double s = odd ? -1.0 : 1.0;
    double t = s - 2.0 * (x - (w * w / (w + s) - r));
    return neg ? -t : t;
  }
  if (!odd) return w;
  /* -1/w with w = x + r: a and w cut to 26 bits, so their product is exact, and one correction step. */
  double wz = asd(asu(w) & 0xfffffffff8000000ull), vv = r - (wz - x);
  double a = -1.0 / w, t = asd(asu(a) & 0xfffffffff8000000ull);
  double s = 1.0 + t * wz;
  return t + a * (s + t * vv);
}
double tan(double x) {
  double ax = fabs(x), hi, lo;
  if (ax < 0x1p-27) return x;
  if (ax <= PIO4_HI) return tan_k(x, 0, 0);
  if (!(ax < INFINITY)) return x - x;
  int n = rem_pio2(x, &hi, &lo);
  return tan_k(hi, lo, n & 1);
}

/* ---- asin, acos, atan, atan2 ---- */

/* asin(x) - x for |x| <= 1/2, as x * z * P(z). */
static inline double asin_tail(double x, double z) { return x * z * horner(ASIN, N(ASIN), z); }
/* sqrt(z) = s + *lo to about 2^-100. */
static inline double sqrt_dd(double z, double *lo) {
  double s = sqrt(z), e, p = two_prod(s, s, &e);
  *lo = ((z - p) - e) / (s + s);
  return s;
}
double asin(double x) {
  double ax = fabs(x), r;
  if (!(ax < 1.0)) return ax == 1.0 ? copysign(PIO2_HI, x) : (x - x) / (x - x);
  if (ax < 0x1p-26) return x;
  if (ax <= 0.5) return x + asin_tail(x, x * x);
  /* asin(x) = pi/2 - 2 asin(s), s = sqrt((1 - x)/2), exact: 1 - x by Sterbenz, the halving by scaling. */
  double z = (1.0 - ax) * 0.5, slo, s = sqrt_dd(z, &slo);
  if (ax >= 0.975) r = PIO2_HI - (2.0 * (s + (slo + asin_tail(s, z))) - PIO2_LO);
  else {
    /* pi/4 - 2w is exact for w = s cut to 26 bits (Sterbenz over most of the range): no cancellation is lost. */
    double w = asd(asu(s) & 0xfffffffff8000000ull), c = (s - w) + slo;
    r = (PIO4_HI - 2.0 * w) + (PIO4_HI - (2.0 * (c + asin_tail(s, z)) - PIO2_LO));
  }
  return copysign(r, x);
}
double acos(double x) {
  double ax = fabs(x);
  if (!(ax < 1.0)) return x == 1.0 ? 0.0 : x == -1.0 ? PI_HI : (x - x) / (x - x);
  if (ax < 0x1p-57) return PIO2_HI;
  if (ax <= 0.5) return PIO2_HI - (x - (PIO2_LO - asin_tail(x, x * x)));
  double z = (1.0 - ax) * 0.5, slo, s = sqrt_dd(z, &slo);
  if (x < 0) return PI_HI - 2.0 * (s + (slo + asin_tail(s, z) - PIO2_LO));
  return 2.0 * (s + (slo + asin_tail(s, z)));
}
/* atan(q + ql) = returned + *lo, q >= 0, ql tiny beside q; on [0, 7/16] the polynomial, past it
 * atan(c) + atan((q - c)/(1 + cq)) for c = 1/2, 1, 3/2 and infinity. ql enters as ql / (1 + q^2), atan's derivative. */
static double atan_pos(double q, double ql, double *lo) {
  static const double ahi[4] = { ATAN_HI0, ATAN_HI1, ATAN_HI2, ATAN_HI3 }, alo[4] = { ATAN_LO0, ATAN_LO1, ATAN_LO2, ATAN_LO3 };
  int id;
  double x, corr = ql / (1.0 + q * q);
  if (q < 0.4375) { id = -1; x = q; }
  else if (q < 0.6875) { id = 0; x = (2.0 * q - 1.0) / (2.0 + q); }
  else if (q < 1.1875) { id = 1; x = (q - 1.0) / (q + 1.0); }
  else if (q < 2.4375) { id = 2; x = (q - 1.5) / (1.0 + 1.5 * q); }
  else { id = 3; x = -1.0 / q; }
  double z = x * x, tail = x * z * horner(ATAN, N(ATAN), z);
  if (id < 0) { *lo = tail + corr; return x; }
  double e, h = fast_two_sum(ahi[id], x, &e);
  *lo = e + (tail + (alo[id] + corr));
  return h;
}
double atan(double x) {
  double ax = fabs(x);
  if (ax != ax) return x + x;
  if (ax >= 0x1p66) return copysign(PIO2_HI, x);
  if (ax < 0x1p-27) return x;
  double lo, hi = atan_pos(ax, 0, &lo);
  return copysign(hi + lo, x);
}
double atan2(double y, double x) {
  if (x != x || y != y) return x + y;
  double ax = fabs(x), ay = fabs(y), z;
  if (ay == 0) return signbit(x) ? copysign(PI_HI, y) : y;
  if (ax == 0) return copysign(PIO2_HI, y);
  if (ax == INFINITY) {
    if (ay == INFINITY) return copysign(signbit(x) ? 3 * PIO4_HI : PIO4_HI, y);
    return signbit(x) ? copysign(PI_HI, y) : copysign(0.0, y);
  }
  if (ay == INFINITY) return copysign(PIO2_HI, y);
  int ex = (int)(asu(ax) >> 52), ey = (int)(asu(ay) >> 52);
  if (ex == 0) ex = 1;
  if (ey == 0) ey = 1;
  if (ey - ex > 60) return copysign(PIO2_HI, y);
  if (ex - ey > 60) return signbit(x) ? copysign(PI_HI, y) : y / x;
  if (ax > 0x1p900 || ay > 0x1p900) { ax *= 0x1p-600; ay *= 0x1p-600; }
  else if (ax < 0x1p-900 || ay < 0x1p-900) { ax *= 0x1p600; ay *= 0x1p600; }
  /* q + ql = ay / ax to about 2^-106: the division's error from an exact product. */
  double q = ay / ax, e, p = two_prod(q, ax, &e), ql = ((ay - p) - e) / ax;
  double lo, hi = atan_pos(q, ql, &lo);
  if (signbit(x)) {
    double e, t = two_sum(PI_HI, -hi, &e);
    z = t + (e + (PI_LO - lo));
  } else z = hi + lo;
  return copysign(z, y);
}

/* ---- exp, exp2, expm1 ---- */

double exp(double x) {
  if (x != x) return x + x;
  if (x > 0x1.62e42fefa39efp+9) return INFINITY;
  if (x < -0x1.74910d52d3051p+9) return 0.0;
  if (fabs(x) < 0x1p-54) return 1.0 + x;
  double fn = round_int(x * INVLN2), hi = x - fn * LN2_HI, lo = fn * LN2_LO, r = hi - lo, c = (hi - r) - lo;
  double e, s = fast_two_sum(1.0, r, &e);
  double y = s + (e + (c + r * r * horner(EXP, N(EXP), r)));
  return scale(y, (int)fn);
}
/* 2^(r + rl), |r| <= 1/2 + tiny: 1 + r ln2 in double-double, then r^2 P(r) (pow takes it too). */
static inline double exp2_k(double r, double rl) {
  double ae, ah = two_prod(r, LN2D_HI, &ae), al = ae + r * LN2D_LO + rl * LN2D_HI;
  double tail = r * r * horner(EXP2 + 1, N(EXP2) - 1, r);
  double be, bh = fast_two_sum(1.0, ah, &be);
  return bh + (be + (al + tail));
}
double exp2(double x) {
  if (x != x) return x + x;
  if (x >= 1024.0) return INFINITY;
  if (x < -1080.0) return 0.0;
  if (fabs(x) < 0x1p-54) return 1.0 + x;
  double fn = round_int(x), r = x - fn;
  return scale(exp2_k(r, 0), (int)fn);
}
/* expm1(r) for |r| <= ln2/2. */
static inline double expm1_k(double r) { return r + (0.5 * r * r + r * r * r * horner(EXPM1, N(EXPM1), r)); }
double expm1(double x) {
  if (x != x) return x + x;
  if (x > 0x1.62e42fefa39efp+9) return INFINITY;
  if (x < -37.5) return -1.0;
  double ax = fabs(x);
  if (ax < 0x1p-54) return x;
  if (ax <= 0.3465) return expm1_k(x);
  double fn = round_int(x * INVLN2), hi = x - fn * LN2_HI, lo = fn * LN2_LO, r = hi - lo, c = (hi - r) - lo;
  int k = (int)fn;
  /* e + el = expm1(r + c): the tail beside r rounded once, r and the sum kept apart */
  double tail = 0.5 * r * r + r * r * r * horner(EXPM1, N(EXPM1), r), el, e = fast_two_sum(r, tail, &el);
  el += c + c * e;
  e = fast_two_sum(e, el, &el);
  if (k >= -52 && k <= 52) {
    /* 2^k - 1 and 2^k e are exact; their sum is kept exact too, so the result rounds once */
    double tk = asd((uint64_t)(k + 1023) << 52), se, sh = two_sum(tk - 1.0, tk * e, &se);
    return sh + (se + tk * el);
  }
  if (k < 0) return scale(1.0 + e, k) - 1.0;
  return scale(1.0 + e, k);
}

/* ---- log, log2, log10, log1p ---- */

/* x = 2^k m, m in [sqrt2/2, sqrt2); returns m - 1 (exact) and sets k. x positive and finite. */
static inline double reduce_log(double x, int *k) {
  uint64_t u = asu(x);
  int kk = 0;
  if (u < 0x0010000000000000ull) { x *= 0x1p54; u = asu(x); kk = -54; }
  uint32_t hx = (uint32_t)(u >> 32) + (0x3ff00000 - 0x3fe6a09e);
  kk += (int)(hx >> 20) - 0x3ff;
  hx = (hx & 0x000fffff) + 0x3fe6a09e;
  *k = kk;
  return asd(((uint64_t)hx << 32) | (u & 0xffffffffull)) - 1.0;
}
/* log(1 + f) - f + f^2/2 as s (f^2/2 + R), with s = f/(2 + f): the part of the series past f - f^2/2. */
static inline double log_tail(double f, double hfsq) {
  double s = f / (2.0 + f), z = s * s;
  return s * (hfsq + z * horner(LOG, N(LOG), z));
}
double log(double x) {
  if (x != x) return x + x;
  if (x == 0) return -INFINITY;
  if (x < 0) return (x - x) / 0.0;
  if (x == INFINITY) return x;
  int k;
  double f = reduce_log(x, &k), hfsq = 0.5 * f * f, dk = k;
  return dk * LN2_HI - ((hfsq - (log_tail(f, hfsq) + dk * LN2_LO)) - f);
}
/* log(1 + f) = *hi + *lo to about 2^-60, hi with no rounding error carried. */
static inline double log1p_dd(double f, double *lo) {
  double hfsq = 0.5 * f * f, hi = f - hfsq;
  *lo = ((f - hi) - hfsq) + log_tail(f, hfsq);
  return hi;
}
double log2(double x) {
  if (x != x) return x + x;
  if (x == 0) return -INFINITY;
  if (x < 0) return (x - x) / 0.0;
  if (x == INFINITY) return x;
  int k;
  double f = reduce_log(x, &k), lo, hi = log1p_dd(f, &lo), pe;
  double ph = two_prod(hi, IVLN2_HI, &pe), pl = pe + hi * IVLN2_LO + lo * IVLN2_HI, e1;
  double s = two_sum((double)k, ph, &e1);
  return s + (e1 + pl);
}
double log10(double x) {
  if (x != x) return x + x;
  if (x == 0) return -INFINITY;
  if (x < 0) return (x - x) / 0.0;
  if (x == INFINITY) return x;
  int k;
  double f = reduce_log(x, &k), lo, hi = log1p_dd(f, &lo), pe, dk = k;
  double ph = two_prod(hi, IVLN10_HI, &pe), pl = pe + hi * IVLN10_LO + lo * IVLN10_HI, e1;
  double s = two_sum(dk * LOG10_2HI, ph, &e1);
  return s + (e1 + (pl + dk * LOG10_2LO));
}
double log1p(double x) {
  if (x != x) return x + x;
  if (x == INFINITY) return x;
  if (x <= -1.0) return x == -1.0 ? -INFINITY : (x - x) / 0.0;
  if (fabs(x) < 0x1p-54) return x;
  /* 1 + x = u + c exactly; log1p(x) = log(u) + c/u to well under an ulp. */
  double u = 1.0 + x, c = x >= 1.0 ? 1.0 - (u - x) : x - (u - 1.0);
  int k;
  double f = reduce_log(u, &k), hfsq = 0.5 * f * f, dk = k;
  return dk * LN2_HI - ((hfsq - (log_tail(f, hfsq) + (dk * LN2_LO + c / u))) - f);
}

/* ---- pow ---- */

/* 0: y is not an integer; 1: an odd one; 2: an even one. */
static int int_kind(double y) {
  if (fabs(y) >= 0x1p53) return 2;
  if (round_int(y) != y) return 0;
  return ((int64_t)y & 1) ? 1 : 2;
}
/* log2(x) = *lo + returned hi, to about 2^-65 relative; x positive and finite. */
static double log2_dd(double x, double *lo) {
  int k;
  double f = reduce_log(x, &k), e;
  /* s = f / (2 + f) as s_hi + s_lo */
  double dl, d = fast_two_sum(2.0, f, &dl);
  double sh = f / d, p = two_prod(sh, d, &e), sl = (((f - p) - e) - sh * dl) / d;
  /* s^2, s^3 and (2/3) s^3 in double-double; s^5 P(s^2) in double */
  double ze, zh = two_prod(sh, sh, &ze), zl = ze + 2.0 * sh * sl;
  double ce, ch = two_prod(zh, sh, &ce), cl = ce + zh * sl + zl * sh;
  double te, th = two_prod(ch, TWO3_HI, &te), tl = te + ch * TWO3_LO + cl * TWO3_HI;
  double q = ch * zh * horner(POWLOG, N(POWLOG), zh);
  double ue, uh = two_sum(2.0 * sh, th, &ue), ul = ue + (2.0 * sl + (tl + q));
  double ll, lh = fast_two_sum(uh, ul, &ll);
  /* times 1/ln2, plus k */
  double ge, gh = two_prod(lh, IVLN2_HI, &ge), gl = ge + lh * IVLN2_LO + ll * IVLN2_HI;
  double le, l = two_sum((double)k, gh, &le);
  return fast_two_sum(l, le + gl, lo);
}
double pow(double x, double y) {
  if (y == 0 || x == 1.0) return 1.0;
  if (x != x || y != y) return x + y;
  int yk = int_kind(y);
  if (x == 0) {
    if (y < 0) return yk == 1 ? copysign(INFINITY, x) : INFINITY;
    return yk == 1 ? x : 0.0;
  }
  double ax = fabs(x);
  if (fabs(y) == INFINITY) {
    if (ax == 1.0) return 1.0;
    return (ax < 1.0) == (y < 0) ? INFINITY : 0.0;
  }
  if (ax == INFINITY) {
    double r = y < 0 ? 0.0 : INFINITY;
    return x < 0 && yk == 1 ? -r : r;
  }
  double sign = 1.0;
  if (x < 0) {
    if (yk == 0) return (x - x) / (x - x);
    if (yk == 1) sign = -1.0;
  }
  if (fabs(y) > 0x1p64) return sign * ((ax < 1.0) == (y < 0) ? INFINITY : 0.0);
  double ll, lh = log2_dd(ax, &ll), we;
  double wh = two_prod(y, lh, &we), wl = we + y * ll;
  if (wh > 1024.5) return sign * INFINITY;
  if (wh < -1080.0) return sign * 0.0;
  /* 2^(wh + wl) = 2^k 2^r, |r| <= 1/2 + tiny; 2^r = 1 + r ln2 + r^2 P(r), r ln2 in double-double. */
  double fn = round_int(wh), rl, r = fast_two_sum(wh - fn, wl, &rl);
  return sign * scale(exp2_k(r, rl), (int)fn);
}

/* ---- sinh, cosh, tanh ---- */

/* exp(x) = returned + *lo for |x| < 709, with no subnormal result; expm1(x) the same for |x| < 36, so sinh, cosh
 * and tanh keep the bits a rounded exp or expm1 would lose. */
static double exp_dd(double x, double *lo) {
  double fn = round_int(x * INVLN2), hi = x - fn * LN2_HI, l = fn * LN2_LO, r = hi - l, c = (hi - r) - l;
  double e, s = fast_two_sum(1.0, r, &e), yl, y = fast_two_sum(s, e + (c + r * r * horner(EXP, N(EXP), r)), &yl);
  double tk = scale(1.0, (int)fn);
  *lo = yl * tk;
  return y * tk;
}
static double expm1_dd(double x, double *lo) {
  if (fabs(x) <= 0.3465) {
    double tail = 0.5 * x * x + x * x * x * horner(EXPM1, N(EXPM1), x);
    return fast_two_sum(x, tail, lo);
  }
  double fn = round_int(x * INVLN2), hi = x - fn * LN2_HI, l = fn * LN2_LO, r = hi - l, c = (hi - r) - l;
  double tail = 0.5 * r * r + r * r * r * horner(EXPM1, N(EXPM1), r), el, e = fast_two_sum(r, tail, &el);
  el += c + c * e;
  e = fast_two_sum(e, el, &el);
  double tk = scale(1.0, (int)fn), se, sh = two_sum(tk - 1.0, tk * e, &se);
  return fast_two_sum(sh, se + tk * el, lo);
}
/* (ah + al) / (bh + bl) = returned + *lo, to about 2^-100 relative. */
static double div_dd(double ah, double al, double bh, double bl, double *lo) {
  double q = ah / bh, pe, p = two_prod(q, bh, &pe);
  *lo = (((ah - p) - pe) + al - q * bl) / bh;
  return q;
}
double sinh(double x) {
  double ax = fabs(x), h = copysign(0.5, x);
  if (!(ax < INFINITY)) return x + x;
  if (ax < 0x1p-28) return x;
  if (ax < 22) {
    /* sinh = (t + t/(t + 1)) / 2, t = expm1(|x|), all in double-double */
    double tl, t = expm1_dd(ax, &tl), dl, d = two_sum(t, 1.0, &dl), ul, u = div_dd(t, tl, d, dl, &ul);
    double se, s = two_sum(t, u, &se);
    return h * (s + (se + (tl + ul)));
  }
  if (ax < 0x1.62e42fefa39efp+9) return h * exp(ax);
  if (ax <= 0x1.633ce8fb9f87dp+9) { double w = exp(0.5 * ax); return (h * w) * w; }
  return copysign(INFINITY, x);
}
double cosh(double x) {
  double ax = fabs(x), t;
  if (ax != ax) return x * x;
  if (ax < 0.3465) {
    if (ax < 0x1p-55) return 1.0;
    t = expm1(ax);
    return 1.0 + (t * t) / (2.0 * (1.0 + t));
  }
  if (ax < 22) {
    /* cosh = (t + 1/t) / 2, t = exp(|x|), in double-double */
    double tl, th = exp_dd(ax, &tl), ql, q = div_dd(1.0, 0.0, th, tl, &ql), se, s = two_sum(th, q, &se);
    return 0.5 * (s + (se + (tl + ql)));
  }
  if (ax < 0x1.62e42fefa39efp+9) return 0.5 * exp(ax);
  if (ax <= 0x1.633ce8fb9f87dp+9) { double w = exp(0.5 * ax); return (0.5 * w) * w; }
  return INFINITY;
}
double tanh(double x) {
  double ax = fabs(x), t, z;
  if (ax != ax) return x + x;
  if (ax < 0x1p-55) return x;
  if (ax >= 22) return copysign(1.0, x);
  if (ax >= 1) { t = expm1(2.0 * ax); z = 1.0 - 2.0 / (t + 2.0); }
  else {
    /* tanh = -t / (t + 2), t = expm1(-2|x|), in double-double */
    double tl, th = expm1_dd(-2.0 * ax, &tl), dl, d = fast_two_sum(2.0, th, &dl), zl;
    z = div_dd(-th, -tl, d, dl, &zl);
    z += zl;
  }
  return copysign(z, x);
}

/* ---- cbrt, hypot, fmod ---- */

/* cbrt of a positive normal x above 2^-900: a first guess from the exponent divided by 3 (about 5 bits), a
 * polynomial in r = t^3/x (about 21), one Newton step (about 42), and a last one whose residual x - t^3 is exact. */
static double cbrt_pos(double ax) {
  double t = asd(((asu(ax) >> 32) / 3 + 715094163ull) << 32);
  double r = (t * t) * (t / ax);
  t = t * horner(CBRT, N(CBRT), r);
  t = t - (t * t * t - ax) / (3.0 * t * t);
  double e1, p = two_prod(t, t, &e1), e2, c = two_prod(p, t, &e2);
  double res = (ax - c) - (e2 + e1 * t);
  return t + res / (3.0 * t * t);
}
double cbrt(double x) {
  double ax = fabs(x);
  if (ax == 0 || !(ax < INFINITY)) return x + x;
  if (ax < 0x1p-900) return copysign(cbrt_pos(ax * 0x1p900) * 0x1p-300, x);
  return copysign(cbrt_pos(ax), x);
}
double hypot(double x, double y) {
  double ax = fabs(x), ay = fabs(y), sc = 1.0;
  if (ax == INFINITY || ay == INFINITY) return INFINITY;
  if (ax != ax || ay != ay) return ax + ay;
  if (ax < ay) { double t = ax; ax = ay; ay = t; }
  if (ay == 0) return ax;
  if ((int)(asu(ax) >> 52) - (int)(asu(ay) >> 52) > 60) return ax + ay;
  if (ax > 0x1p500) { ax *= 0x1p-600; ay *= 0x1p-600; sc = 0x1p600; }
  else if (ay < 0x1p-500) { ax *= 0x1p600; ay *= 0x1p600; sc = 0x1p-600; }
  double al, ah = two_prod(ax, ax, &al), bl, bh = two_prod(ay, ay, &bl), e;
  double s = two_sum(ah, bh, &e), l = e + al + bl;
  double r = sqrt(s), rl, rh = two_prod(r, r, &rl);
  r = r + (((s - rh) - rl) + l) / (r + r);
  return r * sc;
}
/* Exact: the significands' remainder, up to 11 bits of the exponent gap at a time through a 64-bit remainder. */
double fmod(double x, double y) {
  uint64_t ux = asu(x), uy = asu(y), sx = ux & 0x8000000000000000ull, ax = ux & ~sx, ay = uy & 0x7fffffffffffffffull;
  if (ay == 0 || ax >= 0x7ff0000000000000ull || ay > 0x7ff0000000000000ull) return (x * y) / (x * y);
  if (ax < ay) return x;
  if (ax == ay) return asd(sx);
  int ex = (int)(ax >> 52), ey = (int)(ay >> 52);
  uint64_t mx = ax & 0x000fffffffffffffull, my = ay & 0x000fffffffffffffull;
  if (ex) mx |= 1ull << 52; else { int s = __builtin_clzll(mx) - 11; mx <<= s; ex = 1 - s; }
  if (ey) my |= 1ull << 52; else { int s = __builtin_clzll(my) - 11; my <<= s; ey = 1 - s; }
  int d = ex - ey;
  mx %= my;
  while (d > 0 && mx) {
    int s = d < 11 ? d : 11;
    mx = (mx << s) % my;
    d -= s;
  }
  if (mx == 0) return asd(sx);
  int s = __builtin_clzll(mx) - 11, e = ey - s;
  mx <<= s;
  if (e <= 0) { mx >>= 1 - e; e = 0; }
  return asd(sx | ((uint64_t)e << 52 | (mx & 0x000fffffffffffffull)));
}
