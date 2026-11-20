/* stdio.h for the math bench's arms: CORE-MATH prints and exits where its rounding test should never fail; here
 * printing does nothing and exit traps (compat.c), so such a failure would show as a trap, never as a wrong value. */
#ifndef BENCH_MATH_COMPAT_STDIO_H
#define BENCH_MATH_COMPAT_STDIO_H
typedef struct bench_math_file FILE;
extern FILE *const stderr;
extern FILE *const stdout;
int printf(const char *, ...);
int fprintf(FILE *, const char *, ...);
int puts(const char *);
#endif
