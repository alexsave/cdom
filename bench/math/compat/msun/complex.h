/* complex.h for FreeBSD msun's k_exp.c and k_expf.c, the two files that hold __ldexp_exp (cosh and sinh call it)
 * beside the complex __ldexp_cexp, which the bench never calls. */
#ifndef _COMPLEX_H
#define _COMPLEX_H
#define complex _Complex
#define I (__extension__ 1.0iF)
static inline double creal(double _Complex z) { return __real__ z; }
static inline double cimag(double _Complex z) { return __imag__ z; }
static inline float crealf(float _Complex z) { return __real__ z; }
static inline float cimagf(float _Complex z) { return __imag__ z; }
#endif
