/* cdom_file: the file imports and the exports that answer them (cdom_file.h). Separate from cdom.c so
 * an app that reads no files does not carry its import and exports. */
#include "cdom.h"
#include "cdom_file.h"
#include "cdom_cb.h"

#define IMPORT(n) __attribute__((import_module("cdom"), import_name(n)))
#define EXPORT(n) __attribute__((export_name(n)))

IMPORT("file_read") void j_file_read(uint32_t token, cd_id input, uint32_t index, uint32_t max_bytes);
/* file_open writes the reader and the size (on OK); file_next answers 1 for a reader closed or with a piece pending (the
 * call traps); file_close answers the pending piece's token, or 0. */
IMPORT("file_open") uint32_t j_file_open(cd_id input, uint32_t index, uint32_t *reader, uint32_t *size);
IMPORT("file_next") uint32_t j_file_next(uint32_t reader, uint8_t *dst, uint32_t max, uint32_t token);
IMPORT("file_close") uint32_t j_file_close(uint32_t reader);

/* The token forms trap on a token in cdom's range: its answer would go to a callback slot. */
void cd_file_read(cd_id input, uint32_t index, uint32_t max_bytes, uint32_t token) {
    if (token >= CD_TOKEN_CDOM) __builtin_trap();
    j_file_read(token, input, index, max_bytes);
}
void cd_file_read_cb(cd_id input, uint32_t index, uint32_t max_bytes, cd_file_fn fn, void *ctx) {
    j_file_read(cd_cb_new((cd_cb_fn)fn, ctx), input, index, max_bytes);
}
__attribute__((weak)) void cd_on_file(uint32_t, uint32_t, const uint8_t *, uint32_t) { }

/* Room for an answer's bytes: a block of their size, or 0 when the heap has none (the shim then answers
 * NO_ROOM). The export that answers frees it. */
EXPORT("cd_file_room") void *cd_file_room(uint32_t n) { return cd_alloc_try(n ? n : 1); }
/* An app's token goes to its hook, cdom's to the callback that asked; then p (a block of cd_file_room's,
 * or 0) is freed, so nothing holds the bytes past the answer. */
EXPORT("cd_file_got") void cd_file_got(uint32_t token, uint32_t status, const uint8_t *p, uint32_t len) {
    cd_cb_fn fn;
    void *ctx;
    if (token < CD_TOKEN_CDOM) cd_on_file(token, status, p, len);
    else if (cd_cb_take(token, &fn, &ctx)) ((cd_file_fn)fn)(ctx, status, p, len);
    cd_flush();
    cd_free((void *)p);
}

uint32_t cd_file_open(cd_id input, uint32_t index, uint32_t *reader, uint32_t *size) {
    *reader = 0; *size = 0;
    return j_file_open(input, index, reader, size);
}
void cd_file_next_cb(uint32_t reader, uint8_t *dst, uint32_t max, cd_file_piece_fn fn, void *ctx) {
    uint32_t token = cd_cb_new((cd_cb_fn)fn, ctx);
    if (j_file_next(reader, dst, max, token)) __builtin_trap();   /* a second next while one is pending, or a closed reader */
}
void cd_file_close(uint32_t reader) {
    uint32_t token = j_file_close(reader);
    if (token) cd_cb_drop(token);
}
/* A piece's answer: to the callback that asked (gone when the reader was closed), then the flush. */
EXPORT("cd_file_piece") void cd_file_piece(uint32_t token, uint32_t status, uint32_t n) {
    cd_cb_fn fn;
    void *ctx;
    if (cd_cb_take(token, &fn, &ctx)) ((cd_file_piece_fn)fn)(ctx, status, n);
    cd_flush();
}
