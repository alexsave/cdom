/* What miniz's inflate section (third_party/miniz-3.1.2/tinfl.h and tinfl.c, cut whole from miniz.h and miniz.c)
 * takes from the rest of miniz, written for cdom: the same types and macros as miniz.h's, but for the heap
 * (cdom's cd_alloc_try, through MZ_MALLOC; only the unused one-shot helpers allocate) and the 64-bit bit buffer
 * (every target cdom builds for has 64-bit integer registers, wasm32 among them). The build includes it ahead of
 * tinfl.c (-include); cdom_image_png.c includes it for the declarations. */
#ifndef CDOM_IMAGE_MINIZ_H
#define CDOM_IMAGE_MINIZ_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "cdom_image_int.h"
void *cd_realloc_try(void *p, uint32_t n);

#define MINIZ_EXPORT
typedef unsigned char mz_uint8;
typedef int16_t mz_int16;
typedef uint16_t mz_uint16;
typedef uint32_t mz_uint32;
typedef uint32_t mz_uint;
typedef uint64_t mz_uint64;
typedef int mz_bool;
#define MZ_FALSE (0)
#define MZ_TRUE (1)
#define MZ_MACRO_END while (0)
#define MZ_ASSERT(x) ((void)0)
#define MZ_MALLOC(x) cd_alloc_try((uint32_t)(x))
#define MZ_FREE(x) cd_free(x)
#define MZ_REALLOC(p, x) cd_realloc_try((p), (uint32_t)(x))
#define MZ_MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MZ_MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MZ_CLEAR_ARR(obj) memset((obj), 0, sizeof(obj))
#define MZ_READ_LE16(p) ((mz_uint32)(((const mz_uint8 *)(p))[0]) | ((mz_uint32)(((const mz_uint8 *)(p))[1]) << 8U))
#define MZ_READ_LE32(p) ((mz_uint32)(((const mz_uint8 *)(p))[0]) | ((mz_uint32)(((const mz_uint8 *)(p))[1]) << 8U) | \
                         ((mz_uint32)(((const mz_uint8 *)(p))[2]) << 16U) | ((mz_uint32)(((const mz_uint8 *)(p))[3]) << 24U))
#define MZ_FORCEINLINE inline __attribute__((__always_inline__))
#define MZ_UINT8_MAX 255
#define MINIZ_HAS_64BIT_REGISTERS 1
#define MINIZ_USE_UNALIGNED_LOADS_AND_STORES 0
#define MINIZ_LITTLE_ENDIAN 1
#include "tinfl.h"
#endif
