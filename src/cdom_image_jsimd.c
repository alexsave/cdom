/* The image module's libjpeg-turbo SIMD dispatch, the decoder's half (CDOM_IMAGE_SIMD=simde, cdom.mk).
 *
 * libjpeg-turbo's own dispatch is simd/jsimd.c: one jsimd_set_* function per accelerated step, which checks the build and
 * the CPU and, if the SIMD routine may run, stores it where the decoder looks (cinfo->idct->idct_simd, ...) and returns the
 * instruction set. This file is that file's Arm branch for the steps the decoder takes and nothing else, since jsimd.c also
 * holds the encoder's steps and the vendored tree (third_party/libjpeg-turbo-3.2.1) is the decoder's. Each function here
 * has jsimd.c's checks and jsimd.c's assignment, in the same order; the Arm NEON routines they name are libjpeg-turbo's
 * simd/arm/*.c, built for wasm32 over SIMDe (WITH_SIMDE), which turns each NEON intrinsic into wasm simd128.
 *
 * What differs from jsimd.c, all of it for wasm: jpeg_simd_cpu_support is a constant (a module that is built with
 * -msimd128 runs only where simd128 does, and wasm has no CPU to ask), there is no environment (JSIMD_FORCENONE and the
 * others are NO_GETENV's business), and cd_image_simd_wired, a bit for each step wired at least once, which a test reads
 * to know that the SIMD routines are the ones that ran (a decode that quietly fell back to the C code is bit-exact too).
 * The bit is set at setup, once per step per decode, never in a loop. */
#define JPEG_INTERNALS
#include "jinclude.h"
#include "jpeglib.h"
#include "../simd/jsimdint.h"
#include "../simd/jsimddct.h"
#include "../simd/jsimd.h"

#ifndef WITH_SIMDE
#error "cdom_image_jsimd.c is the SIMD build's file (CDOM_IMAGE_SIMD=simde)"
#endif

/* The steps, one bit each (tests/image/wasm.c names them). */
enum {
  W_YCC_RGB = 1 << 0, W_YCC_RGB565 = 1 << 1, W_H2V1_UP = 1 << 2, W_H2V2_UP = 1 << 3, W_H2V1_FANCY = 1 << 4,
  W_H2V2_FANCY = 1 << 5, W_H1V2_FANCY = 1 << 6, W_H2V1_MERGED = 1 << 7, W_H2V2_MERGED = 1 << 8, W_IDCT_ISLOW = 1 << 9,
  W_IDCT_IFAST = 1 << 10, W_IDCT_2X2 = 1 << 11, W_IDCT_4X4 = 1 << 12
};
unsigned int cd_image_simd_wired;
#ifdef CDOM_IMAGE_SIMD_TEST   /* tests/image/wasm.mk only: the scalar routines on demand, and the decode options the module never sets */
unsigned int cd_image_simd_off;         /* nonzero: every jsimd_set_* answers JSIMD_NONE, so libjpeg's own C code runs */
unsigned int cd_image_simd_test_mode;   /* bit 0: JDCT_IFAST; bit 1: no fancy upsampling (so merged upsampling), cdom_image_jpeg.c */
#define SIMD_OFF (cd_image_simd_off != 0)
#else
#define SIMD_OFF 0
#endif

HIDDEN unsigned int jpeg_simd_cpu_support(void) { return JSIMD_NEON; }

/* The checks every step of jsimd.c makes first: 8-bit samples and a 4-byte JDIMENSION (the others it makes are below). */
#define BUILD_OK (BITS_IN_JSAMPLE == 8 && sizeof(JDIMENSION) == 4 && !SIMD_OFF)
#define IDCT_OK (BUILD_OK && sizeof(JCOEF) == 2 && sizeof(ISLOW_MULT_TYPE) == 2 && cinfo->idct)

HIDDEN unsigned int jsimd_set_ycc_rgb(j_decompress_ptr cinfo) {
  if (!BUILD_OK || (RGB_PIXELSIZE != 3 && RGB_PIXELSIZE != 4) || !cinfo->cconvert) return JSIMD_NONE;
  SET_SIMD_EXTRGB_COLOR_DECONVERTER(neon);
  cd_image_simd_wired |= W_YCC_RGB;
  return JSIMD_NEON;
}
HIDDEN unsigned int jsimd_set_ycc_rgb565(j_decompress_ptr cinfo) {
  if (!BUILD_OK || !cinfo->cconvert) return JSIMD_NONE;
  cinfo->cconvert->color_convert_simd = jsimd_ycc_rgb565_convert_neon;
  cd_image_simd_wired |= W_YCC_RGB565;
  return JSIMD_NEON;
}
HIDDEN void jsimd_color_deconvert(j_decompress_ptr cinfo, JSAMPIMAGE input_buf, JDIMENSION input_row,
                                  JSAMPARRAY output_buf, int num_rows) {
  cinfo->cconvert->color_convert_simd(cinfo->output_width, input_buf, input_row, output_buf, num_rows);
}

HIDDEN unsigned int jsimd_set_h2v1_upsample(j_decompress_ptr cinfo) {
  if (!BUILD_OK || !cinfo->upsample) return JSIMD_NONE;
  cinfo->upsample->h2v1_upsample_simd = jsimd_h2v1_upsample_neon;
  cd_image_simd_wired |= W_H2V1_UP;
  return JSIMD_NEON;
}
HIDDEN void jsimd_h2v1_upsample(j_decompress_ptr cinfo, jpeg_component_info *compptr, JSAMPARRAY input_data,
                                JSAMPARRAY *output_data_ptr) {
  cinfo->upsample->h2v1_upsample_simd(cinfo->max_v_samp_factor, cinfo->output_width, input_data, output_data_ptr);
}
HIDDEN unsigned int jsimd_set_h2v2_upsample(j_decompress_ptr cinfo) {
  if (!BUILD_OK || !cinfo->upsample) return JSIMD_NONE;
  cinfo->upsample->h2v2_upsample_simd = jsimd_h2v2_upsample_neon;
  cd_image_simd_wired |= W_H2V2_UP;
  return JSIMD_NEON;
}
HIDDEN void jsimd_h2v2_upsample(j_decompress_ptr cinfo, jpeg_component_info *compptr, JSAMPARRAY input_data,
                                JSAMPARRAY *output_data_ptr) {
  cinfo->upsample->h2v2_upsample_simd(cinfo->max_v_samp_factor, cinfo->output_width, input_data, output_data_ptr);
}

HIDDEN unsigned int jsimd_set_h2v1_fancy_upsample(j_decompress_ptr cinfo) {
  if (!BUILD_OK || !cinfo->upsample) return JSIMD_NONE;
  cinfo->upsample->h2v1_upsample_simd = jsimd_h2v1_fancy_upsample_neon;
  cd_image_simd_wired |= W_H2V1_FANCY;
  return JSIMD_NEON;
}
HIDDEN void jsimd_h2v1_fancy_upsample(j_decompress_ptr cinfo, jpeg_component_info *compptr, JSAMPARRAY input_data,
                                      JSAMPARRAY *output_data_ptr) {
  cinfo->upsample->h2v1_upsample_simd(cinfo->max_v_samp_factor, compptr->downsampled_width, input_data, output_data_ptr);
}
HIDDEN unsigned int jsimd_set_h2v2_fancy_upsample(j_decompress_ptr cinfo) {
  if (!BUILD_OK || !cinfo->upsample) return JSIMD_NONE;
  cinfo->upsample->h2v2_upsample_simd = jsimd_h2v2_fancy_upsample_neon;
  cd_image_simd_wired |= W_H2V2_FANCY;
  return JSIMD_NEON;
}
HIDDEN void jsimd_h2v2_fancy_upsample(j_decompress_ptr cinfo, jpeg_component_info *compptr, JSAMPARRAY input_data,
                                      JSAMPARRAY *output_data_ptr) {
  cinfo->upsample->h2v2_upsample_simd(cinfo->max_v_samp_factor, compptr->downsampled_width, input_data, output_data_ptr);
}
HIDDEN unsigned int jsimd_set_h1v2_fancy_upsample(j_decompress_ptr cinfo) {
  if (!BUILD_OK || !cinfo->upsample) return JSIMD_NONE;
  cinfo->upsample->h1v2_upsample_simd = jsimd_h1v2_fancy_upsample_neon;
  cd_image_simd_wired |= W_H1V2_FANCY;
  return JSIMD_NEON;
}
HIDDEN void jsimd_h1v2_fancy_upsample(j_decompress_ptr cinfo, jpeg_component_info *compptr, JSAMPARRAY input_data,
                                      JSAMPARRAY *output_data_ptr) {
  cinfo->upsample->h1v2_upsample_simd(cinfo->max_v_samp_factor, compptr->downsampled_width, input_data, output_data_ptr);
}

HIDDEN unsigned int jsimd_set_h2v1_merged_upsample(j_decompress_ptr cinfo) {
  if (!BUILD_OK || !cinfo->upsample) return JSIMD_NONE;
  SET_SIMD_EXTRGB_MERGED_UPSAMPLER(h2v1, neon);
  cd_image_simd_wired |= W_H2V1_MERGED;
  return JSIMD_NEON;
}
HIDDEN void jsimd_h2v1_merged_upsample(j_decompress_ptr cinfo, JSAMPIMAGE input_buf, JDIMENSION in_row_group_ctr,
                                       JSAMPARRAY output_buf) {
  cinfo->upsample->merged_upsample_simd(cinfo->output_width, input_buf, in_row_group_ctr, output_buf);
}
HIDDEN unsigned int jsimd_set_h2v2_merged_upsample(j_decompress_ptr cinfo) {
  if (!BUILD_OK || !cinfo->upsample) return JSIMD_NONE;
  SET_SIMD_EXTRGB_MERGED_UPSAMPLER(h2v2, neon);
  cd_image_simd_wired |= W_H2V2_MERGED;
  return JSIMD_NEON;
}
HIDDEN void jsimd_h2v2_merged_upsample(j_decompress_ptr cinfo, JSAMPIMAGE input_buf, JDIMENSION in_row_group_ctr,
                                       JSAMPARRAY output_buf) {
  cinfo->upsample->merged_upsample_simd(cinfo->output_width, input_buf, in_row_group_ctr, output_buf);
}

HIDDEN unsigned int jsimd_set_idct_islow(j_decompress_ptr cinfo) {
  if (!IDCT_OK) return JSIMD_NONE;
  cinfo->idct->idct_simd = jsimd_idct_islow_neon;
  cd_image_simd_wired |= W_IDCT_ISLOW;
  return JSIMD_NEON;
}
HIDDEN void jsimd_idct_islow(j_decompress_ptr cinfo, jpeg_component_info *compptr, JCOEFPTR coef_block,
                             JSAMPARRAY output_buf, JDIMENSION output_col) {
  cinfo->idct->idct_simd(compptr->dct_table, coef_block, output_buf, output_col);
}
HIDDEN unsigned int jsimd_set_idct_ifast(j_decompress_ptr cinfo) {
  if (!IDCT_OK) return JSIMD_NONE;
  cinfo->idct->idct_simd = jsimd_idct_ifast_neon;
  cd_image_simd_wired |= W_IDCT_IFAST;
  return JSIMD_NEON;
}
HIDDEN void jsimd_idct_ifast(j_decompress_ptr cinfo, jpeg_component_info *compptr, JCOEFPTR coef_block,
                             JSAMPARRAY output_buf, JDIMENSION output_col) {
  cinfo->idct->idct_simd(compptr->dct_table, coef_block, output_buf, output_col);
}
HIDDEN unsigned int jsimd_set_idct_2x2(j_decompress_ptr cinfo) {
  if (!IDCT_OK) return JSIMD_NONE;
  cinfo->idct->idct_2x2_simd = jsimd_idct_2x2_neon;
  cd_image_simd_wired |= W_IDCT_2X2;
  return JSIMD_NEON;
}
HIDDEN void jsimd_idct_2x2(j_decompress_ptr cinfo, jpeg_component_info *compptr, JCOEFPTR coef_block,
                           JSAMPARRAY output_buf, JDIMENSION output_col) {
  cinfo->idct->idct_2x2_simd(compptr->dct_table, coef_block, output_buf, output_col);
}
HIDDEN unsigned int jsimd_set_idct_4x4(j_decompress_ptr cinfo) {
  if (!IDCT_OK) return JSIMD_NONE;
  cinfo->idct->idct_4x4_simd = jsimd_idct_4x4_neon;
  cd_image_simd_wired |= W_IDCT_4X4;
  return JSIMD_NEON;
}
HIDDEN void jsimd_idct_4x4(j_decompress_ptr cinfo, jpeg_component_info *compptr, JCOEFPTR coef_block,
                           JSAMPARRAY output_buf, JDIMENSION output_col) {
  cinfo->idct->idct_4x4_simd(compptr->dct_table, coef_block, output_buf, output_col);
}
