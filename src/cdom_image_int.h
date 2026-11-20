/* cdom_image's own parts, shared by its files (src/cdom_image*.c); not for apps. */
#ifndef CDOM_IMAGE_INT_H
#define CDOM_IMAGE_INT_H
#include <stddef.h>
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
  uint64_t *acc;            /* dw x 3: the output row being summed (with CDOM_IMAGE_SIMD, dw x 3 bytes more after them: the quotients) */
  uint32_t *hrow;           /* dw x 3: the current source row, reduced across */
  uint64_t ypos;            /* position in the vertical sum, in units of 1/dh of a source row */
  uint32_t oy;              /* the next output row to finish */
  uint32_t row;             /* source rows taken so far */
  uint32_t step;            /* bytes a source pixel: 4 (RGBA, alpha ignored; init's), or 3 (RGB, set after init) */
} cd_image_scaler;

/* 1 on success, 0 when the heap has no room (nothing is held then). Sizes are 1 to 65535 a side. */
int cd_image_scaler_init(cd_image_scaler *s, uint32_t sw, uint32_t sh, uint32_t dw, uint32_t dh, uint32_t orientation);
/* One source row of sw pixels, step bytes each, R, G and B first (RGBA, alpha ignored, or RGB). Rows past sh are
 * ignored. */
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
/* PNG rows as the sink hands them over: row is a row's filter byte, its n bytes after it, and the rows after it one pitch
 * apart (avail of them whole, avail at least 1); prev is the first's row above (n bytes). The SIMD body unfilters a run of
 * 2 to 4 Paeth rows (at most avail, 3 and 4 bytes a pixel) as a wavefront (docs/image-png-speed.md, "Paeth as a
 * wavefront"), any other first row alone; the scalar body always one row. Answers the rows done, 0 for an unknown filter. */
uint32_t cd_image_unfilter_run_scalar(uint8_t *row, uint32_t pitch, const uint8_t *prev, uint32_t n, uint32_t bpp, uint32_t avail);
uint32_t cd_image_unfilter_run_simd(uint8_t *row, uint32_t pitch, const uint8_t *prev, uint32_t n, uint32_t bpp, uint32_t avail);
/* A row of w 8-bit RGB pixels as RGBA with alpha 255. */
void cd_image_rgb_to_rgba_scalar(const uint8_t *r, uint8_t *out, uint32_t w);
void cd_image_rgb_to_rgba_simd(const uint8_t *r, uint8_t *out, uint32_t w);
/* The scaler's two row loops. across: h gets all dw * 3 sums of the source row px (sw RGBA pixels, alpha ignored) reduced to
 * dw pixels by area (it need not be zeroed, and is not read). down: a[i] += wgt * h[i] for i below n. */
void cd_image_scaler_across_scalar(uint32_t *h, const uint8_t *px, uint32_t sw, uint32_t dw);
void cd_image_scaler_across_simd(uint32_t *h, const uint8_t *px, uint32_t sw, uint32_t dw);
/* across on sw RGB pixels (3 bytes each), scalar in both builds: the PNG path's RGB read (cd_image_scaler.step 3). */
void cd_image_scaler_across_rgb(uint32_t *h, const uint8_t *px, uint32_t sw, uint32_t dw);
void cd_image_scaler_down_scalar(uint64_t *a, const uint32_t *h, uint64_t wgt, uint32_t n);
void cd_image_scaler_down_simd(uint64_t *a, const uint32_t *h, uint64_t wgt, uint32_t n);
/* The scaler's divide: out[i] = min(255, (2 * a[i] + t) / (2 * t)) for i below n, where t (at most 2^32) is the weights'
 * sum, sw * sh, and each a[i] is at most 255 * t. The SIMD body divides in doubles, which is exact here (see the loop). */
void cd_image_scaler_div_scalar(uint8_t *out, const uint64_t *a, uint32_t n, uint64_t t);
void cd_image_scaler_div_simd(uint8_t *out, const uint64_t *a, uint32_t n, uint64_t t);
#if CDOM_IMAGE_SIMD
#define cd_image_unfilter cd_image_unfilter_simd
#define cd_image_unfilter_run cd_image_unfilter_run_simd
#define cd_image_rgb_to_rgba cd_image_rgb_to_rgba_simd
#define cd_image_scaler_across cd_image_scaler_across_simd
#define cd_image_scaler_down cd_image_scaler_down_simd
#define cd_image_scaler_div cd_image_scaler_div_simd
#else
#define cd_image_unfilter cd_image_unfilter_scalar
#define cd_image_unfilter_run cd_image_unfilter_run_scalar
#define cd_image_rgb_to_rgba cd_image_rgb_to_rgba_scalar
#define cd_image_scaler_across cd_image_scaler_across_scalar
#define cd_image_scaler_down cd_image_scaler_down_scalar
#define cd_image_scaler_div cd_image_scaler_div_scalar
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
/* cdom's inflate (cdom_image_inflate.c): one zlib stream, from pieces of input, into buf (cap bytes), which a sink
 * empties from the front. more(ctx, &p, &n) answers the next piece (1), or 0 at the end of the input. out(ctx, buf,
 * n, last) gets the bytes made and not yet dropped, buf[0 .. n), each time the buffer is nearly full and once at the
 * end (last 1, after the adler32 matched); it answers how many bytes to drop from the front, at most n less
 * CD_INFLATE_WINDOW (the window the stream may still copy from) or any number up to n when last, or -1 to stop.
 * The adler32 is taken over the bytes before the sink sees them, so the sink may change, in place, the bytes in
 * front of n - CD_INFLATE_WINDOW, which no later match reads (the PNG path unfilters its rows there). cap is at
 * least CD_INFLATE_CAP_MIN (less is CORRUPT), and must leave room for the longest match beside what the sink keeps.
 * The answer: OK, CORRUPT (a stream zlib 1.3.1 refuses, one cut short, or an adler32 that differs) or STOPPED. */
#define CD_INFLATE_WINDOW 32768u
#define CD_INFLATE_CAP_MIN (CD_INFLATE_WINDOW + 1314u)   /* the window, the longest match and its overshoot, 1 KB more */
#define CD_INFLATE_LT 2340u   /* the literal/length table: 2^11 first-level entries and the second levels (zlib's enough) */
#define CD_INFLATE_DT 592u    /* the distance table: 2^9 and the second levels */
enum { CD_INFLATE_OK = 0, CD_INFLATE_CORRUPT = 1, CD_INFLATE_STOPPED = 2 };
typedef struct { uint32_t lt[CD_INFLATE_LT], dt[CD_INFLATE_DT]; } cd_inflate;
typedef int (*cd_inflate_in)(void *ctx, const uint8_t **p, uint32_t *n);
typedef int64_t (*cd_inflate_out)(void *ctx, uint8_t *buf, uint32_t n, int last);
uint32_t cd_inflate_zlib(cd_inflate *z, uint8_t *buf, uint32_t cap, cd_inflate_in more, cd_inflate_out out, void *ctx);
uint32_t cd_image_adler32(uint32_t adler, const uint8_t *p, size_t n);
#endif
