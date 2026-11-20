/* Our float functions: each is our double function on the float, rounded once more to float. In wasm a double
 * operation costs what a float one does, and a result within 0.5 + 2^-29 ulp of the float's correctly rounded one
 * rounds to it but in the rarest ties, so this is the short way to floats (docs/math-design.md).
 * no_builtin: clang would turn (float)fmod((double)x, (double)y) into fmodf(x, y), this function calling itself. */
#include <math.h>
#define F1(name) __attribute__((no_builtin)) float name##f(float x) { return (float)name((double)x); }
#define F2(name) __attribute__((no_builtin)) float name##f(float x, float y) { return (float)name((double)x, (double)y); }
F1(sin) F1(cos) F1(tan) F1(asin) F1(acos) F1(atan) F2(atan2) F1(exp) F1(exp2) F1(expm1) F1(log) F1(log2) F1(log10)
F1(log1p) F2(pow) F1(sinh) F1(cosh) F1(tanh) F1(cbrt) F2(hypot) F2(fmod)
