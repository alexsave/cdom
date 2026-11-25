/* cdom_image: a JPEG or PNG in memory, decoded and resized in C. Link it with APP_MODULES += image.
 *
 * cd_image_resize decodes the image and scales it down to the largest size that fits inside a
 * max_w x max_h box, keeping its aspect, never enlarging it, with its EXIF orientation applied, and
 * answers an RGBA block of cdom's heap. The flow an app uses, from a file input (cdom_file.h):
 *
 *   static void got(void *ctx, uint32_t status, const uint8_t *p, uint32_t len) {
 *     uint8_t *rgba; uint32_t w, h;
 *     if (status != CD_FILE_OK) return;
 *     if (cd_image_resize(p, len, 1600, 1600, &rgba, &w, &h) != CD_IMAGE_OK) return;  (the app says why)
 *     cd_encode_image(rgba, w, h, 4, "image/jpeg", 85, ENCODED);  (the file arrives in cd_on_encoded)
 *     cd_free(rgba);
 *   }
 *   cd_file_read_cb(input, 0, 30000000, got, 0);   (APP_MODULES += file image)
 *
 * The file's bytes are read where cdom put them (no copy). A JPEG is decoded one row at a time at the
 * smallest of libjpeg's scales (n/8) that is still at least the output size, and a PNG is inflated and
 * unfiltered one row at a time; both feed one streaming scaler (an exact area average), so what is
 * held is the file, a few MB of working state and the output, whatever the image's size.
 * A PNG's alpha (and a colour key, tRNS) is composited on white: every output pixel has alpha 255.
 *
 * Every allocation goes through cd_alloc_try: a heap with no room is CD_IMAGE_NO_ROOM, never a trap,
 * and nothing is left allocated on any status but OK. No global state: the calls are reentrant.
 * No user text here: the app says what each status means to its user.
 *
 * Third-party code inside (third_party/, README.md "The image module"): libjpeg-turbo (the IJG licence
 * and the BSD licence) and miniz's inflate (MIT). An app that ships this module must say, in its
 * documentation, "this software is based in part on the work of the Independent JPEG Group", and
 * reproduce libjpeg-turbo's BSD notice and miniz's MIT notice. */
#ifndef CDOM_IMAGE_H
#define CDOM_IMAGE_H
#include <stdint.h>

/* The statuses. */
#define CD_IMAGE_OK 0
#define CD_IMAGE_NOT_IMAGE 1   /* neither a JPEG nor a PNG by its first bytes */
#define CD_IMAGE_TOO_BIG 2     /* over a limit below: a side, the pixels, or the decoder's memory */
#define CD_IMAGE_UNSUPPORTED 3 /* a JPEG that is arithmetic coded, lossless, hierarchical, 12-bit or CMYK;
                                * a PNG that is interlaced or 16-bit */
#define CD_IMAGE_CORRUPT 4     /* damaged or cut short: a header that cannot be read, a stream that ends early,
                                * a checksum (PNG's zlib adler32) that does not match */
#define CD_IMAGE_NO_ROOM 5     /* the heap had no room (cd_alloc_try answered 0) */
#define CD_IMAGE_BAD_BOX 6     /* max_w or max_h is 0 or over CD_IMAGE_BOX_MAX: the call's own mistake */
#define CD_IMAGE_UNREADABLE 7  /* cd_image_file_cb only: the file could not be read to its end (gone, changed, cut
                                * short), or there was none to read (cd_file_open's NO_FILE, TOO_BIG, UNSUPPORTED) */
#define CD_IMAGE_MORE 8        /* cd_image_put: not done; ask cd_image_room again */

/* The limits. */
#define CD_IMAGE_SIDE_MAX 65500u         /* the most pixels on either side of the image, as stored */
#define CD_IMAGE_PIXELS_MAX 64000000u    /* the most pixels (width times height): a bound on decode time */
#define CD_IMAGE_BOX_MAX 8192u           /* the largest max_w and max_h: an output of at most 256 MB */
#define CD_IMAGE_MEM_MAX (96u << 20)     /* the most bytes the JPEG decoder may hold at once (beside the file, its
                                          * rows and the output): a progressive or non-interleaved JPEG holds every
                                          * coefficient, 3 bytes a pixel at 4:2:0 and 6 at 4:4:4, so this takes
                                          * 16 MP at any subsampling and 33 MP at 4:2:0; a baseline JPEG holds a
                                          * few MB at any size */
#define CD_IMAGE_SCANS_MAX 500u          /* the most scans a JPEG may have: a bound on decode time */

#define CD_IMAGE_JPEG 1
#define CD_IMAGE_PNG 2

typedef struct {
    uint32_t format;         /* CD_IMAGE_JPEG or CD_IMAGE_PNG */
    uint32_t width, height;  /* as stored */
    uint32_t orientation;    /* the EXIF orientation, 1 to 8 (1 for a PNG, and for a JPEG without one) */
    uint32_t shown_w, shown_h; /* the size with the orientation applied (width and height swapped for 5 to 8) */
    uint32_t progressive;    /* 1 for a progressive JPEG */
} cd_image_info;

/* Reads the header only (no allocation) and fills info. Answers OK, or the status cd_image_resize would answer
 * from the header: NOT_IMAGE, TOO_BIG (a side or the pixels), UNSUPPORTED or CORRUPT. info is filled as far as the
 * header was read. */
uint32_t cd_image_probe(const uint8_t *p, uint32_t len, cd_image_info *info);

/* Decodes the image at p and answers it resized to fit inside max_w x max_h (after orientation, so the box is
 * the shown image's): *rgba is a block of *w x *h RGBA pixels that the caller frees with cd_free. Never enlarged:
 * an image that fits already keeps its size. The shorter side is rounded to the nearest pixel, at least 1.
 * On any status but OK, *rgba is 0 and *w and *h are 0. */
uint32_t cd_image_resize(const uint8_t *p, uint32_t len, uint32_t max_w, uint32_t max_h, uint8_t **rgba,
                         uint32_t *w, uint32_t *h);

/* ---- a decoder that is fed: the file's bytes in order, a piece at a time (docs/chunked-file-read-design.md) ----
 *
 * The same decode as cd_image_resize (which is begin, one put of its bytes where they lie, take and end), for a file
 * that is never held whole: what is held is the decoder's working state and one piece. The flow:
 *
 *   cd_image_dec *d;
 *   if (cd_image_begin(size, 1600, 1600, &d) != CD_IMAGE_OK) ...;     (BAD_BOX or NO_ROOM)
 *   for (;;) {
 *     uint32_t max; uint8_t *at = cd_image_room(d, &max);
 *     uint32_t n = (write up to max of the file's next bytes at at; max 0: write none);
 *     uint32_t st = cd_image_put(d, n);
 *     if (st == CD_IMAGE_MORE) continue;          (each piece may be its own event: the decoder keeps its place)
 *     if (st == CD_IMAGE_OK) cd_image_take(d, &rgba, &w, &h);
 *     break;
 *   }
 *   cd_image_end(d);
 *
 * The end of the file is known from size: the decoder never asks past it, and once size bytes went in, it treats any
 * further want as the file ending there, so the statuses are cd_image_resize's (a file cut short whose size says so
 * is CORRUPT). A reader that ends before size is the reader's error, not the image's: the caller answers it
 * (cd_image_file_cb answers UNREADABLE). No globals: every state is in the cd_image_dec block. src/cdom_image_file.c
 * ties this to a picked file (cd_image_file_cb, cdom_image_file.h). */

/* The size of a piece: the most cd_image_room asks for at once. One constant, overridable only by building with
 * -DCD_IMAGE_PIECE=... (the bench sweeps 16, 64 and 256 KiB and 1 MiB). */
#ifndef CD_IMAGE_PIECE
#define CD_IMAGE_PIECE (1u << 20)
#endif

typedef struct cd_image_dec cd_image_dec;   /* opaque, in cdom's heap */

/* A decoder for a file of size bytes, to fit max_w x max_h as cd_image_resize does: OK and *d, or BAD_BOX or
 * NO_ROOM (and *d 0). It holds the room for one piece (the smaller of the file and a piece, with a JPEG's keep). */
uint32_t cd_image_begin(uint32_t size, uint32_t max_w, uint32_t max_h, cd_image_dec **d);
/* Where the next bytes of the file go, and *max, the most it takes now (at most CD_IMAGE_PIECE, never past size).
 * *max 0: it has work that needs no bytes (a progressive JPEG's output pass, cut into steps); call cd_image_put(d, 0). */
uint8_t *cd_image_room(cd_image_dec *d, uint32_t *max);
/* n bytes (0 to *max) were written at the room. Decodes as far as they go and answers MORE, or the final status,
 * which is cd_image_resize's: OK, NOT_IMAGE, TOO_BIG, UNSUPPORTED, CORRUPT or NO_ROOM. After a final status, put
 * answers it again and reads nothing. */
uint32_t cd_image_put(cd_image_dec *d, uint32_t n);
/* After OK: the image, as cd_image_resize answers it (the caller frees *rgba with cd_free); once (then 0, 0, 0). */
void cd_image_take(cd_image_dec *d, uint8_t **rgba, uint32_t *w, uint32_t *h);
/* Frees everything the decoder holds (an image not taken too). Any time, in any state: this is how a decode is
 * cancelled. cd_image_end(0) does nothing. */
void cd_image_end(cd_image_dec *d);
#endif
