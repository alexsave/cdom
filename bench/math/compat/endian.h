/* endian.h for musl's libm.h: wasm32, x86-64 and arm64 are all little-endian. */
#ifndef BENCH_MATH_COMPAT_ENDIAN_H
#define BENCH_MATH_COMPAT_ENDIAN_H
#define __LITTLE_ENDIAN 1234
#define __BIG_ENDIAN 4321
#define __BYTE_ORDER __LITTLE_ENDIAN
#endif
