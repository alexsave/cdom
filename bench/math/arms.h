/* The native arms of the math bench (docs/math-design.md): each is bench.c and special.c built against that arm's
 * libm, their entry points named <arm>_<name> (BM_ARM in bench.c), so the native driver (native.c) and the
 * accuracy sweep (accuracy.c) link every arm into one program. glibc is the host's libm, measured only. */
#ifndef BENCH_MATH_ARMS_H
#define BENCH_MATH_ARMS_H
#include <stdint.h>
#define BM_NATIVE_ARMS(X) X(musl) X(msun) X(core) X(ours) X(glibc)
#define BM_DECLARE(a)                                                                                             \
  int a##_bm_n(void); const char *a##_bm_name(int); int a##_bm_doms(int); void a##_bm_fill(int, int);             \
  double a##_bm_run(int, int); double a##_bm_eval(int, double, double); uint64_t a##_bm_digest(int, int, int);    \
  int a##_sp_run(void); const char *a##_sp_text(void);
BM_NATIVE_ARMS(BM_DECLARE)
typedef struct {
  const char *name;
  int (*doms)(int);
  void (*fill)(int, int);
  double (*run)(int, int);
  double (*eval)(int, double, double);
  uint64_t (*digest)(int, int, int);
  int (*sp_run)(void);
  const char *(*sp_text)(void);
} bm_arm;
/* The host libm's arm is glibc on Linux; on a Mac it is the system's libm, so it is named for that. */
#ifdef __APPLE__
#define BM_LABEL(a) (sizeof #a == sizeof "glibc" && #a[0] == 'g' ? "applelibm" : #a)
#else
#define BM_LABEL(a) #a
#endif
#define BM_ARM_ROW(a) { BM_LABEL(a), a##_bm_doms, a##_bm_fill, a##_bm_run, a##_bm_eval, a##_bm_digest, a##_sp_run, a##_sp_text },
static const bm_arm bm_arms[] = { BM_NATIVE_ARMS(BM_ARM_ROW) };
#define BM_NARMS ((int)(sizeof bm_arms / sizeof bm_arms[0]))
#endif
