/* cdom_image's own parts, shared by its files (src/cdom_image*.c); not for apps. */
#ifndef CDOM_IMAGE_INT_H
#define CDOM_IMAGE_INT_H
#include <stdint.h>
#include "cdom_image.h"

/* The heap: cdom's cd_alloc_try and cd_free in a cdom build (cdom.h); a native build (tests, fuzz) links its own
 * pair with the same contract (tests/image/host_alloc.c). */
void *cd_alloc_try(uint32_t n);
void cd_free(void *p);

/* The streaming box scaler: source rows go in one at a time, and the scaled image, with the orientation applied,
 * comes out in one RGBA block. An exact area average in integers (cdom_image.c). */
typedef struct {
  uint32_t sw, sh, dw, dh;  /* source and scaled sizes, as stored */
  uint32_t o;               /* orientation 1 to 8 */
  uint32_t ow, oh;          /* the output's size, after orientation */
  uint8_t *out;             /* ow x oh RGBA */
  uint64_t *acc;            /* dw x 3: the output row being summed */
  uint32_t *hrow;           /* dw x 3: the current source row, reduced across */
  uint64_t ypos;            /* position in the vertical sum, in units of 1/dh of a source row */
  uint32_t oy;              /* the next output row to finish */
  uint32_t row;             /* source rows taken so far */
} cd_image_scaler;

/* 1 on success, 0 when the heap has no room (nothing is held then). Sizes are 1 to 65535 a side. */
int cd_image_scaler_init(cd_image_scaler *s, uint32_t sw, uint32_t sh, uint32_t dw, uint32_t dh, uint32_t orientation);
/* One source row of sw RGBA pixels (alpha ignored). Rows past sh are ignored. */
void cd_image_scaler_row(cd_image_scaler *s, const uint8_t *rgba);
/* The image, which the caller owns, once all sh rows went in; 0 otherwise. Either way the scaler holds nothing
 * after it. */
uint8_t *cd_image_scaler_take(cd_image_scaler *s);
/* Frees what the scaler holds; safe after a take, after a failed init, and twice. */
void cd_image_scaler_free(cd_image_scaler *s);

/* The size the image (sw x sh as stored, orientation o) is scaled to, as stored, for a box of bw x bh shown. */
void cd_image_target(uint32_t sw, uint32_t sh, uint32_t o, uint32_t bw, uint32_t bh, uint32_t *dw, uint32_t *dh);

/* The EXIF orientation (1 to 8) of the JPEG at p, or 1 when it has none. */
uint32_t cd_image_orientation(const uint8_t *p, uint32_t len);

/* The formats, behind the checks cdom_image.c makes (the magic, the box). The probes fill info and apply
 * the limits; the decoders answer with *rgba, *w and *h set only on OK. */
uint32_t cd_image_jpeg_probe(const uint8_t *p, uint32_t len, cd_image_info *info);
uint32_t cd_image_png_probe(const uint8_t *p, uint32_t len, cd_image_info *info);
uint32_t cd_image_jpeg_resize(const uint8_t *p, uint32_t len, const cd_image_info *info, uint32_t dw, uint32_t dh,
                              uint8_t **rgba, uint32_t *w, uint32_t *h);
uint32_t cd_image_png_resize(const uint8_t *p, uint32_t len, const cd_image_info *info, uint32_t dw, uint32_t dh,
                             uint8_t **rgba, uint32_t *w, uint32_t *h);

/* The hot loops (cdom_image_loops.c): each has a scalar body, always built, and a wasm SIMD body, built when
 * CDOM_IMAGE_SIMD is 1 (cdom.mk: make variable CDOM_IMAGE_SIMD=1, -msimd128; 0, the default, is scalar). The name
 * without a suffix is the one the module calls; the suffixed ones are what the tests compare (the SIMD body is bit-exact with
 * the scalar one on every input). */
#ifndef CDOM_IMAGE_SIMD
#define CDOM_IMAGE_SIMD 0
#endif
/* PNG row filters: undo filter f (0 to 4) on cur (n bytes, bpp bytes a pixel) against prev (the row above, all zero
 * for the first); 0 for an unknown filter. */
int cd_image_unfilter_scalar(uint32_t f, uint8_t *cur, const uint8_t *prev, uint32_t n, uint32_t bpp);
int cd_image_unfilter_simd(uint32_t f, uint8_t *cur, const uint8_t *prev, uint32_t n, uint32_t bpp);
/* A row of w 8-bit RGB pixels as RGBA with alpha 255. */
void cd_image_rgb_to_rgba_scalar(const uint8_t *r, uint8_t *out, uint32_t w);
void cd_image_rgb_to_rgba_simd(const uint8_t *r, uint8_t *out, uint32_t w);
/* The scaler's two row loops. across: h (dw x 3 sums, zeroed by the caller; with CDOM_IMAGE_SIMD one more 32-bit word
 * of room, which the SIMD body reads and writes back unchanged) gets the source row px (sw RGBA pixels) reduced to dw
 * pixels by area. down: a[i] += wgt * h[i] for i below n. */
void cd_image_scaler_across_scalar(uint32_t *h, const uint8_t *px, uint32_t sw, uint32_t dw);
void cd_image_scaler_across_simd(uint32_t *h, const uint8_t *px, uint32_t sw, uint32_t dw);
void cd_image_scaler_down_scalar(uint64_t *a, const uint32_t *h, uint64_t wgt, uint32_t n);
void cd_image_scaler_down_simd(uint64_t *a, const uint32_t *h, uint64_t wgt, uint32_t n);
#if CDOM_IMAGE_SIMD
#define cd_image_unfilter cd_image_unfilter_simd
#define cd_image_rgb_to_rgba cd_image_rgb_to_rgba_simd
#define cd_image_scaler_across cd_image_scaler_across_simd
#define cd_image_scaler_down cd_image_scaler_down_simd
#else
#define cd_image_unfilter cd_image_unfilter_scalar
#define cd_image_rgb_to_rgba cd_image_rgb_to_rgba_scalar
#define cd_image_scaler_across cd_image_scaler_across_scalar
#define cd_image_scaler_down cd_image_scaler_down_scalar
#endif

/* libjpeg's way out of a fatal error (cdom_image_sjlj.c). cd_image_guard runs body(ctx) and answers 0 when it
 * returned, 1 when it left through cd_image_throw(*slot); while body runs, *slot names this guard. Guards nest (a
 * decode inside a decode's body is allowed): a throw lands at the innermost guard running, which is the one the
 * module names, since it throws only to the guard of the decode running. Nothing re-enters wasm while a decode
 * runs (the decode is one synchronous call), so no other decode can start under it. Three
 * builds: the C library's setjmp natively; in wasm, CDOM_IMAGE_SJLJ=js (a try and catch in the shim) or
 * CDOM_IMAGE_SJLJ=eh (wasm exception handling). */
typedef void (*cd_image_body)(void *ctx);
int cd_image_guard(cd_image_body body, void *ctx, void **slot);
_Noreturn void cd_image_throw(void *guard);
#endif
