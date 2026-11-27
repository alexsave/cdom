/* sys/types.h for FreeBSD msun's math_private.h: the BSD integer names it uses. */
#ifndef BENCH_MATH_COMPAT_SYS_TYPES_H
#define BENCH_MATH_COMPAT_SYS_TYPES_H
#include <stdint.h>
typedef uint8_t u_int8_t;
typedef uint16_t u_int16_t;
typedef uint32_t u_int32_t;
typedef uint64_t u_int64_t;
typedef uint32_t __uint32_t;
typedef int32_t __int32_t;
typedef uint64_t __uint64_t;
typedef int64_t __int64_t;
#endif
