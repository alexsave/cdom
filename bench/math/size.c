/* The size modules (docs/math-design.md, "Size"): built with an arm's objects and measured after wasm-opt -O3.
 * SIZE_empty exports one function that calls nothing, SIZE_sin one that calls sin, SIZE_all one per function the
 * arm has (double and float); the size an arm adds is a module's bytes minus the empty one's. */
#include <math.h>
#include "fns.h"
#define EXPORT(name) __attribute__((export_name(#name)))
#ifndef BM_JS
#define BM_JS 0
#endif
#if BM_JS
#define BM_IMP(name, ret, ...) __attribute__((import_module("math"), import_name(#name))) ret js_##name(__VA_ARGS__);
BM_IMP(sin, double, double)
#define CALL_SIN(x) js_sin(x)
#else
#define CALL_SIN(x) sin(x)
#endif
#if defined(SIZE_empty)
EXPORT(f) double f(double x) { return x; }
#elif defined(SIZE_sin)
EXPORT(f) double f(double x) { return CALL_SIN(x); }
#elif defined(SIZE_all)
#if BM_JS
#define BM_IMPS(name, arity, doms) BM_IMP##arity(name)
#define BM_IMP1(name) BM_IMP(name##_1, double, double) BM_IMP(name##f_1, float, float)
#define BM_IMP2(name) BM_IMP(name##_1, double, double, double) BM_IMP(name##f_1, float, float, float)
BM_FNS(BM_IMPS)
#define CALL(name) js_##name##_1
#else
#define CALL(name) name
#endif
#define ONE1(name) EXPORT(f_##name) double f_##name(double x) { return CALL(name)(x); } \
  EXPORT(f_##name##f) float f_##name##f(float x) { return CALL(name##f)(x); }
#define ONE2(name) EXPORT(f_##name) double f_##name(double x, double y) { return CALL(name)(x, y); } \
  EXPORT(f_##name##f) float f_##name##f(float x, float y) { return CALL(name##f)(x, y); }
#ifdef BM_LACK_fmod
#define fmod(x, y) (x)
#define fmodf(x, y) (x)
#endif
#define ONE(name, arity, doms) ONE##arity(name)
BM_FNS(ONE)
#endif
