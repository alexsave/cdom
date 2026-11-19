/* simd/jsimd.h: libjpeg-turbo's SIMD dispatch for the decoder's colour conversion and upsampling (jdcolor.c and
 * jdsample.c include it as "../simd/jsimd.h" when WITH_SIMD is on), answered by src/jsimd/jsimd_wasm.c.
 * Each jsimd_set_* asks "does a SIMD routine take this case?" and answers 1 or 0; when 1, libjpeg calls the
 * routine the matching jsimd_* names. What has no wasm routine here answers a constant 0, so libjpeg's own C runs
 * and the branch that names the missing routine is dead code (nothing is linked for it). */
#ifndef CDOM_JSIMD_H
#define CDOM_JSIMD_H
#include "jsimdconst.h"

/* YCbCr to RGBA and RGBX (8 bits): src/jsimd/jsimd_wasm.c. */
EXTERN(int) jsimd_set_ycc_rgb(j_decompress_ptr cinfo);
EXTERN(void) jsimd_color_deconvert(j_decompress_ptr cinfo, JSAMPIMAGE input_buf, JDIMENSION input_row,
                                   JSAMPARRAY output_buf, int num_rows);
#define jsimd_set_ycc_rgb565(cinfo)  0

/* The upsamplers: none (at the bench's scales libjpeg gives the chroma its own larger IDCT, so none runs). */
#define jsimd_set_h2v1_fancy_upsample(cinfo)  0
#define jsimd_set_h2v1_upsample(cinfo)  0
#define jsimd_set_h1v2_fancy_upsample(cinfo)  0
#define jsimd_set_h2v2_fancy_upsample(cinfo)  0
#define jsimd_set_h2v2_upsample(cinfo)  0
EXTERN(void) jsimd_h2v1_fancy_upsample(j_decompress_ptr cinfo, jpeg_component_info *compptr, JSAMPARRAY input_data,
                                       JSAMPARRAY *output_data_ptr);
EXTERN(void) jsimd_h2v1_upsample(j_decompress_ptr cinfo, jpeg_component_info *compptr, JSAMPARRAY input_data,
                                 JSAMPARRAY *output_data_ptr);
EXTERN(void) jsimd_h1v2_fancy_upsample(j_decompress_ptr cinfo, jpeg_component_info *compptr, JSAMPARRAY input_data,
                                       JSAMPARRAY *output_data_ptr);
EXTERN(void) jsimd_h2v2_fancy_upsample(j_decompress_ptr cinfo, jpeg_component_info *compptr, JSAMPARRAY input_data,
                                       JSAMPARRAY *output_data_ptr);
EXTERN(void) jsimd_h2v2_upsample(j_decompress_ptr cinfo, jpeg_component_info *compptr, JSAMPARRAY input_data,
                                 JSAMPARRAY *output_data_ptr);

#endif
