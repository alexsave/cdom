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
/* The orientation in an Exif APP1's TIFF header at t, room bytes to the segment's end (at least 8), or 1. */
uint32_t cd_image_exif_orientation(const uint8_t *t, uint64_t room);

/* The probes (cd_image_probe, for bytes in memory), behind the magic: they fill info and apply the limits. */
uint32_t cd_image_jpeg_probe(const uint8_t *p, uint32_t len, cd_image_info *info);
uint32_t cd_image_png_probe(const uint8_t *p, uint32_t len, cd_image_info *info);

/* ---- the fed decoder (cdom_image.h, cd_image_begin) ----
 * cdom_image.c owns the block, the room and the first bytes (which say the format); the formats take the bytes from
 * there: cd_image_jpeg_put and cd_image_png_put get the next n bytes of the file at p, where they lie (in the room, or,
 * for cd_image_resize, the caller's own bytes, all of the rest of the file), and answer MORE or a final status;
 * their _end frees what they hold. */

/* A JPEG keeps what libjpeg has not used of a piece (a suspended marker segment, at most 65,535 bytes after FF and its
 * code, or an MCU) in front of the next: the room is a piece and this much more. */
#define CD_IMAGE_JPEG_KEEP (68u << 10)

/* The JPEG header walker: every byte goes through it before libjpeg sees it, and it applies cd_image_jpeg_probe's
 * rules (a walk to the first SOF) and cd_image_orientation's (a walk to the scan for the first Exif APP1) as the bytes
 * come, keeping only the bytes it reads (a segment's length, the SOF's first 8, the Exif segment) and counting past the
 * rest. Its verdict is the probe's; positions are the file's. */
typedef struct {
  uint32_t live;           /* 1 once the format is known to be JPEG and the walks started */
  uint32_t ps;             /* the probe's walk: its state */
  uint64_t pat;            /* the next byte it reads */
  uint32_t marker, seg, pn;
  uint8_t pg[8];           /* a length, or a SOF's first 8 bytes, gathered */
  uint32_t verdict;        /* MORE until the walk ends, then the probe's status */
  uint32_t os;             /* the orientation's walk */
  uint64_t oat;
  uint32_t oseg, on;
  uint8_t og[10];          /* a segment's first 4 bytes, then (an APP1) the 6 after */
  uint8_t *exif;           /* an Exif segment gathered across pieces (cd_alloc_try), or 0 */
  uint32_t exif_n;
  uint32_t orientation;    /* 0 until the walk ends, then 1 to 8 */
  uint32_t w, h, progressive;
} cd_image_walk;

struct cd_image_dec {
  uint32_t size;           /* the file's bytes */
  uint32_t taken;          /* those put so far */
  uint32_t box_w, box_h;
  uint32_t status;         /* MORE until the final status */
  uint32_t format;         /* 0 until the first bytes say, then CD_IMAGE_JPEG or CD_IMAGE_PNG */
  uint8_t *buf;            /* the room's block (cd_image_begin), or 0 (cd_image_resize: the bytes are the caller's) */
  uint32_t cap;
  uint32_t used;           /* bytes at buf's front, waiting: the first bytes before the format is known, a JPEG's keep */
  uint32_t step;           /* 1: the next put is a step of work that takes no bytes */
  uint8_t *rgba;           /* after OK, until taken */
  uint32_t w, h;
  cd_image_info info;
  cd_image_walk walk;      /* a JPEG's header walker */
  void *fmt;               /* the format's state (cdom_image_jpeg.c, cdom_image_png.c), made when it is needed */
  uint32_t warnings;       /* libjpeg's warnings so far (the tests compare them) */
};
/* cd_image_resize, and libjpeg's warnings through *warnings (when not 0): what the tests compare a fed decode with. */
uint32_t cd_image_decode(const uint8_t *p, uint32_t len, uint32_t max_w, uint32_t max_h, uint8_t **rgba, uint32_t *w,
                         uint32_t *h, uint32_t *warnings);
/* libjpeg's warnings so far, of a fed decoder (the tests compare them). */
uint32_t cd_image_dec_warnings(const cd_image_dec *d);
uint32_t cd_image_jpeg_put(cd_image_dec *d, const uint8_t *p, uint32_t n);
void cd_image_jpeg_end(cd_image_dec *d);
uint32_t cd_image_png_put(cd_image_dec *d, const uint8_t *p, uint32_t n);
void cd_image_png_end(cd_image_dec *d);

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
#define cd_image_rgb_to_rgba cd_image_rgb_to_rgba_simd
#define cd_image_scaler_across cd_image_scaler_across_simd
#define cd_image_scaler_down cd_image_scaler_down_simd
#define cd_image_scaler_div cd_image_scaler_div_simd
#else
#define cd_image_unfilter cd_image_unfilter_scalar
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
/* cdom's inflate (cdom_image_inflate.c): one zlib stream, pushed in pieces of any size, into buf (cap bytes), which a
 * sink empties from the front. cd_inflate_begin readies z; then cd_inflate_put(z, p, n, last) gives it the next n
 * bytes of the stream (any n, 0 too), last 1 when no more will come. It decodes while at least CD_INFLATE_LOOK
 * bytes are ahead of it (or last is set), so no block header and no symbol is ever cut; the tail it cannot use yet,
 * under CD_INFLATE_LOOK bytes, it keeps in z (its carry) and puts in front of the next bytes. p is read during the
 * call only. It answers CD_INFLATE_MORE until the stream ends, then its final answer, which every later put answers
 * again (bytes after the stream are not read): OK, CORRUPT (a stream zlib 1.3.1 refuses, one cut short, or an
 * adler32 that differs) or STOPPED.
 * out(ctx, buf, n, last) gets the bytes made and not yet dropped, buf[0 .. n), each time the buffer is nearly full and
 * once at the end (last 1, after the adler32 matched); it answers how many bytes to drop from the front, at most n
 * less CD_INFLATE_WINDOW (the window the stream may still copy from) or any number up to n when last, or -1 to stop.
 * The adler32 is taken over the bytes before the sink sees them, so the sink may change, in place, the bytes in
 * front of n - CD_INFLATE_WINDOW, which no later match reads (the PNG path unfilters its rows there). cap is at
 * least CD_INFLATE_CAP_MIN (less is CORRUPT), and must leave room for the longest match beside what the sink keeps.
 * Every state between two symbols lives in z (no globals, nothing on the stack between puts). */
#define CD_INFLATE_WINDOW 32768u
#define CD_INFLATE_CAP_MIN (CD_INFLATE_WINDOW + 1314u)   /* the window, the longest match and its overshoot, 1 KB more */
#define CD_INFLATE_LT 2340u   /* the literal/length table: 2^11 first-level entries and the second levels (zlib's enough) */
#define CD_INFLATE_DT 592u    /* the distance table: 2^9 and the second levels */
#define CD_INFLATE_LOOK 1024u /* the look-ahead: the longest dynamic block header (under 600 bytes) and symbol (48 bits) */
#define CD_INFLATE_CARRY (2 * CD_INFLATE_LOOK)   /* the carry: a tail under LOOK and the next bytes, so each use of it takes LOOK or more */
enum { CD_INFLATE_OK = 0, CD_INFLATE_CORRUPT = 1, CD_INFLATE_STOPPED = 2, CD_INFLATE_MORE = 3 };
typedef int64_t (*cd_inflate_out)(void *ctx, uint8_t *buf, uint32_t n, int last);
typedef struct {
  uint32_t lt[CD_INFLATE_LT], dt[CD_INFLATE_DT];
  /* the input: the bit buffer (the next bits from bit 0), its bits, zero bytes put in past the end (last only) */
  uint64_t bb;
  uint32_t bl, pad;
  const uint8_t *in, *end; /* the bytes being read */
  int last;                /* no more bytes after end */
  /* the output */
  uint8_t *buf;            /* buf[0 .. pos) made and not dropped */
  uint32_t cap, pos;
  uint32_t adl;            /* buf[0 .. adl) is in the adler32 */
  uint32_t adler;
  uint64_t total;          /* bytes made since the start */
  cd_inflate_out out;
  void *ctx;
  /* where the stream is between puts */
  uint32_t stage;          /* the zlib header, a block's header, a Huffman block, a stored block, the adler32, done */
  uint32_t final;          /* the block being read is the last */
  uint32_t stored;         /* a stored block's bytes left */
  uint32_t status;         /* the final answer, once done */
  uint32_t carry_n;
  uint8_t carry[CD_INFLATE_CARRY];
} cd_inflate;
void cd_inflate_begin(cd_inflate *z, uint8_t *buf, uint32_t cap, cd_inflate_out out, void *ctx);
uint32_t cd_inflate_put(cd_inflate *z, const uint8_t *p, uint32_t n, int last);
uint32_t cd_image_adler32(uint32_t adler, const uint8_t *p, size_t n);
#endif
