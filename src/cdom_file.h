/* cdom_file: a picked file into C. Link src/cdom_file.c to use it (APP_MODULES += file).
 * cd_file_read hands C a file's bytes as they are; cd_file_fit hands it a picture made from an image
 * file by the browser (decoded, turned upright, scaled to fit a box and encoded), so no full-size pixels
 * ever cross into C memory. Both take a file input (or any node with a files list: a drop's
 * dataTransfer is a path away) and the index of the file in its list, and both answer from the event
 * loop, never inside the call, even when the answer is known at once.
 * What is known before reading is a path away (cdom.h, host access): cd_get_num(input, "files.length"),
 * cd_get_str(input, "files.0.name", ...), cd_get_str(input, "files.0.type", ...) and
 * cd_get_num(input, "files.0.size"), the size in bytes.
 * An answer may come after the app moved on (another file picked, the view gone): a callback's ctx from
 * cd_req_begin, checked with cd_req_current (cdom.h, request slots), drops the stale ones. */
#ifndef CDOM_FILE_H
#define CDOM_FILE_H
#include <stdint.h>
#include "cdom.h"

/* The answers. p and len are what each status says; p is valid until the hook or callback returns,
 * and cdom frees it then (copy what is kept). */
#define CD_FILE_OK 0          /* p: the bytes (cd_file_read) or the encoded picture (cd_file_fit); len: their count */
#define CD_FILE_NO_FILE 1     /* nothing at that index (none picked, or the node has no files list); p 0, len 0 */
#define CD_FILE_TOO_BIG 2     /* cd_file_read: the file is over max_bytes, decided from its size, and never read; p 0, len its size */
#define CD_FILE_UNREADABLE 3  /* the browser could not read it (gone from the disk, changed, no permission); p 0, len 0 */
#define CD_FILE_UNSUPPORTED 4 /* this browser cannot (no FileReader: IE 9; no createImageBitmap: IE, Safari before 15); p 0, len 0 */
#define CD_FILE_NO_ROOM 5     /* the bytes did not fit in C memory (cd_alloc_try answered 0); p 0, len their count */
#define CD_FILE_NOT_IMAGE 6   /* cd_file_fit: the browser could not decode the file as an image; p 0, len 0 */

/* The bytes of file index of input's files, at most max_bytes of them: a file over max_bytes is
 * answered TOO_BIG from its size alone, before any of it is read. One copy crosses: the browser reads the
 * file, then the bytes go into a block of cdom's heap (cd_alloc_try, so a file too big for the memory is
 * NO_ROOM, not a trap), which is freed when the answer returns. The answer is cd_on_file(token, status, p,
 * len); the token stays below CD_TOKEN_CDOM (traps on one in cdom's range). */
void cd_file_read(cd_id input, uint32_t index, uint32_t max_bytes, uint32_t token);
void cd_on_file(uint32_t token, uint32_t status, const uint8_t *p, uint32_t len); /* the app's hook (empty weak default) */
/* The same, answered to fn(ctx, status, p, len) once, as cd_http_cb's answers are (the shape of
 * cd_http_fn); cd_on_file does not hear it. It counts against the 65,535 callbacks pending at once. */
typedef void (*cd_file_fn)(void *ctx, uint32_t status, const uint8_t *p, uint32_t len);
void cd_file_read_cb(cd_id input, uint32_t index, uint32_t max_bytes, cd_file_fn fn, void *ctx);

/* The picture's encodings, for cd_file_fit's type and its answer's. */
#define CD_IMAGE_JPEG 0
#define CD_IMAGE_WEBP 1
#define CD_IMAGE_PNG 2
/* The image file index of input's files as a picture of at most max_w x max_h pixels: the browser
 * decodes it (createImageBitmap), turns it upright by its EXIF orientation (a phone's portrait photo is
 * stored sideways), scales it to fit the box with its aspect ratio kept (never up: one that fits already
 * keeps its size) at high smoothing, and encodes it as type at quality (0 to 100, as cd_encode_image's;
 * PNG ignores it). The size: when the upright image is w x h and either is over the box, the side that
 * meets the box first becomes the box's (max_w when w * max_h >= h * max_w, else max_h) and the other is
 * its share of that, rounded to the nearest pixel and at least 1. A browser that cannot encode type
 * (Safari has no WebP encoder) gives JPEG at the same quality instead, and the answer says which came
 * out. Only the encoded picture crosses into C. The answer is cd_on_file_fit(token, status, p, len, w, h,
 * type): OK with the picture's bytes, its width, height and type; else NO_FILE, UNREADABLE, NOT_IMAGE,
 * NO_ROOM (len the picture's size) or UNSUPPORTED, with w, h and type 0. Traps on max_w or max_h 0, a
 * type not above, a quality over 100, or a token in cdom's range. */
void cd_file_fit(cd_id input, uint32_t index, uint32_t max_w, uint32_t max_h, uint32_t type, uint32_t quality,
                 uint32_t token);
void cd_on_file_fit(uint32_t token, uint32_t status, const uint8_t *p, uint32_t len, uint32_t w, uint32_t h,
                    uint32_t type); /* the app's hook (empty weak default) */
/* The same, answered to fn(ctx, status, p, len, w, h, type) once; cd_on_file_fit does not hear it. */
typedef void (*cd_fit_fn)(void *ctx, uint32_t status, const uint8_t *p, uint32_t len, uint32_t w, uint32_t h,
                          uint32_t type);
void cd_file_fit_cb(cd_id input, uint32_t index, uint32_t max_w, uint32_t max_h, uint32_t type, uint32_t quality,
                    cd_fit_fn fn, void *ctx);
#endif
