/* errno.h for the math bench's arms: errno is an int each arm defines once (compat.c); EDOM and ERANGE as C has them. */
#ifndef BENCH_MATH_COMPAT_ERRNO_H
#define BENCH_MATH_COMPAT_ERRNO_H
extern int errno;
#define EDOM 33
#define ERANGE 34
#endif
