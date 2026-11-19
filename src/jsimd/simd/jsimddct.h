/* simd/jsimddct.h: libjpeg-turbo's SIMD dispatch for the inverse DCT (jddctmgr.c includes it as "../simd/jsimddct.h"
 * when WITH_SIMD is on), answered by src/jsimd/jsimd_wasm.c: the reduced-size 4x4 and 2x2 IDCTs, the two that the
 * image module's decodes use (a decode at 1/8 to 4/8 of the size; jidctred.c's). The full-size IDCTs and the
 * 1x1, 3x3 and the others answer a constant 0: libjpeg's own C runs, and the branch that names the missing routine is
 * dead code. */
#ifndef CDOM_JSIMDDCT_H
#define CDOM_JSIMDDCT_H

EXTERN(int) jsimd_set_idct_2x2(j_decompress_ptr cinfo);
EXTERN(int) jsimd_set_idct_4x4(j_decompress_ptr cinfo);
EXTERN(void) jsimd_idct_2x2(j_decompress_ptr cinfo, jpeg_component_info *compptr, JCOEFPTR coef_block,
                            JSAMPARRAY output_buf, JDIMENSION output_col);
EXTERN(void) jsimd_idct_4x4(j_decompress_ptr cinfo, jpeg_component_info *compptr, JCOEFPTR coef_block,
                            JSAMPARRAY output_buf, JDIMENSION output_col);

#define jsimd_set_idct_islow(cinfo)  0
#define jsimd_set_idct_ifast(cinfo)  0
#define jsimd_set_idct_float(cinfo)  0
EXTERN(void) jsimd_idct_islow(j_decompress_ptr cinfo, jpeg_component_info *compptr, JCOEFPTR coef_block,
                              JSAMPARRAY output_buf, JDIMENSION output_col);
EXTERN(void) jsimd_idct_ifast(j_decompress_ptr cinfo, jpeg_component_info *compptr, JCOEFPTR coef_block,
                              JSAMPARRAY output_buf, JDIMENSION output_col);
EXTERN(void) jsimd_idct_float(j_decompress_ptr cinfo, jpeg_component_info *compptr, JCOEFPTR coef_block,
                              JSAMPARRAY output_buf, JDIMENSION output_col);

#endif
