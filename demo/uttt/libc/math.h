/* math.h for foolish's uttt_code.c, whose uttt_ideal_bits calls log2 (cdom's libc has no math.h). The replay never
 * calls uttt_ideal_bits, so the linker drops it and log2 is never needed: nothing defines it. */
#ifndef UTTT_REPLAY_MATH_H
#define UTTT_REPLAY_MATH_H
double log2(double x);
#endif
