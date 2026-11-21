/* fenv.h for the math bench's arms: wasm has one rounding mode (to nearest) and no exception flags, so every arm
 * is built as if the environment were always that: fegetround answers FE_TONEAREST, the flag functions do nothing
 * (compat.c), and the native twins are built against the same header, so the two builds take the same paths. */
#ifndef BENCH_MATH_COMPAT_FENV_H
#define BENCH_MATH_COMPAT_FENV_H
#define FE_INVALID 1
#define FE_DIVBYZERO 4
#define FE_OVERFLOW 8
#define FE_UNDERFLOW 16
#define FE_INEXACT 32
#define FE_ALL_EXCEPT 61
#define FE_TONEAREST 0
#define FE_DOWNWARD 0x400
#define FE_UPWARD 0x800
#define FE_TOWARDZERO 0xc00
typedef unsigned short fexcept_t;
typedef struct { unsigned int x; } fenv_t;
static inline int fegetround(void) { return FE_TONEAREST; }
static inline int fesetround(int r) { return r != FE_TONEAREST; }
static inline int feraiseexcept(int e) { (void)e; return 0; }
static inline int feclearexcept(int e) { (void)e; return 0; }
static inline int fetestexcept(int e) { (void)e; return 0; }
static inline int fegetexceptflag(fexcept_t *f, int e) { (void)e; *f = 0; return 0; }
static inline int fesetexceptflag(const fexcept_t *f, int e) { (void)f; (void)e; return 0; }
static inline int feholdexcept(fenv_t *e) { e->x = 0; return 0; }
static inline int feupdateenv(const fenv_t *e) { (void)e; return 0; }
static inline int fegetenv(fenv_t *e) { e->x = 0; return 0; }
static inline int fesetenv(const fenv_t *e) { (void)e; return 0; }
#endif
