/* simd/jsimdconst.h: what libjpeg-turbo's decoder files include as "../simd/jsimdconst.h" when WITH_SIMD is on
 * (cdom.mk, CDOM_IMAGE_SIMD=1). The upstream header names the CPU features its x86, Arm and other SIMD ports probe for
 * at run time; wasm SIMD is not probed (a module that has SIMD instructions needs them to instantiate), so this has
 * the two things the decoder's files read: the architecture constants their #if lines compare SIMD_ARCHITECTURE with
 * (jconfigint.h gives NONE: no architecture's own tables or dispatch applies), and JSIMD_UNDEFINED, which
 * jpeg_create_decompress stores in the master's simd_support. */
#ifndef CDOM_JSIMDCONST_H
#define CDOM_JSIMDCONST_H

#define NONE        0
#define X86_64      1
#define I386        2
#define ARM64       3
#define ARM         4
#define POWERPC     5
#define MIPS        6
#define MIPS64      7
#define LOONGARCH   8

#define JSIMD_UNDEFINED  0x80000000

#endif
