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

#endif
