/* features.h for musl's sources (its src/include/features.h, cut to what src/math uses). */
#ifndef BENCH_MATH_COMPAT_FEATURES_H
#define BENCH_MATH_COMPAT_FEATURES_H
#define hidden __attribute__((__visibility__("hidden")))
#define weak_alias(old, new) extern __typeof(old) new __attribute__((__weak__, __alias__(#old)))
#endif
