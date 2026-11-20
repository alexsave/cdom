/* cdom_file: a picked file's bytes into C. Link src/cdom_file.c to use it (APP_MODULES += file).
 * cd_file_read takes a file input (or any node with a files list) and the index of the file in its list,
 * and answers from the event loop, never inside the call, even when the answer is known at once.
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
#define CD_FILE_OK 0          /* p: the file's bytes; len: their count */
#define CD_FILE_NO_FILE 1     /* nothing at that index (none picked, or the node has no files list); p 0, len 0 */
#define CD_FILE_TOO_BIG 2     /* the file is over max_bytes, decided from its size, and never read; p 0, len its size (at most 4,294,967,295) */
#define CD_FILE_UNREADABLE 3  /* the browser could not read it (gone from the disk, changed, no permission); p 0, len 0 */
#define CD_FILE_UNSUPPORTED 4 /* this browser has no FileReader (IE 9); p 0, len 0 */
#define CD_FILE_NO_ROOM 5     /* the bytes did not fit in C memory (cd_alloc_try answered 0); p 0, len their count */

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

/* ---- a reader in order: the file a piece at a time, never whole (docs/chunked-file-read-design.md) ---- */

/* A reader on file index of input's files list: *reader for cd_file_next_cb, *size the file's size. Answers OK,
 * NO_FILE, UNSUPPORTED (no FileReader, the IE 9 tier) or TOO_BIG (4 GiB or more) at once, in the call: it reads
 * nothing, only looks at the list, as cd_get_num(input, "files.0.size") does (*reader and *size are 0 on the others).
 * The shim keeps the File itself, so picking another file in the input does not change what an open reader reads. */
uint32_t cd_file_open(cd_id input, uint32_t index, uint32_t *reader, uint32_t *size);
/* The next bytes, at most max, written at dst; then fn(ctx, status, n) from the event loop:
 *   OK, n 1 to max: n bytes at dst (fewer than max only for the last piece);
 *   OK, n 0: the end, all size bytes were given;
 *   UNREADABLE: the browser could not read on, or the file ended before its size (it changed on disk); nothing more.
 * dst stays the caller's until the answer or cd_file_close. One next at a time per reader: a second traps. Each
 * answer is a task of its own (input and paint run between pieces). It counts against the 65,535 callbacks pending
 * at once. The shim reads with Blob.stream (one read ahead of C), or, where a browser has none, with FileReader on
 * slices of the size asked. */
typedef void (*cd_file_piece_fn)(void *ctx, uint32_t status, uint32_t n);
void cd_file_next_cb(uint32_t reader, uint8_t *dst, uint32_t max, cd_file_piece_fn fn, void *ctx);
/* Ends the reader, any time. After it returns nothing is written at dst and fn is not called: the shim drops the
 * File and cancels its stream, and the pending callback's slot is freed. */
void cd_file_close(uint32_t reader);

#endif
