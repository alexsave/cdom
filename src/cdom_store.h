/* cdom_store: a key/value store that outlives the page (IndexedDB), bytes in and bytes out. Link
 * src/cdom_store.c to use it. Keys are the app's: put a version, and whatever the bytes depend on,
 * in the key, and a changed app reads a miss rather than old bytes. Nothing is parsed: the value
 * comes back as the bytes put. */
/* The _cb forms are part of the I/O contract (cdom.h, top): they change only additively. */
#ifndef CDOM_STORE_H
#define CDOM_STORE_H
#include <stdint.h>

/* Keep len bytes at p (read during the call) under key; the answer is cd_on_store_put(token, ok),
 * ok 1 when they are kept and 0 when the put failed (a quota, a closed database): an app that keeps
 * something it must not lose (a saved game) checks it; one that keeps a cache may ignore it. */
void cd_store_put(uint32_t token, const char *key, uint32_t klen, const void *p, uint32_t len);
/* Ask for the bytes under key; the answer is cd_on_store(token, p, len), p valid until it returns,
 * or len -1 for a miss, an error, or no answer within deadline_ms (indexedDB.open can hang). */
void cd_store_get(uint32_t token, const char *key, uint32_t klen, uint32_t deadline_ms);
#define CD_STORE_DEADLINE_MS 1500
/* Forget whatever is under key; the answer is cd_on_store_del(token, ok), as a put's: 1 when the
 * key holds nothing now (deleting a missing key is kept too), 0 when the delete failed. */
void cd_store_del(uint32_t token, const char *key, uint32_t klen);
/* The token forms trap on a token in cdom's range (cdom.h, CD_TOKEN_CDOM). */

/* The same three, answered to a callback with ctx, once, instead of the hooks (which do not hear them):
 * a get's fn(ctx, p, len) with what cd_on_store would get (len -1 for a miss, an error or the deadline;
 * p valid until fn returns), a put's or a delete's fn(ctx, ok) with what cd_on_store_put or
 * cd_on_store_del would. cdom picks the token, from its own range, so it never meets an app's; at most
 * 65,535 of these, cd_timer_cb timers, cd_http_cb requests and cd_ws_open_cb sockets are pending at
 * once (traps past that). */
typedef void (*cd_store_fn)(void *ctx, const uint8_t *p, int32_t len);
typedef void (*cd_store_done_fn)(void *ctx, uint32_t ok);
void cd_store_put_cb(cd_store_done_fn fn, void *ctx, const char *key, uint32_t klen, const void *p, uint32_t len);
void cd_store_get_cb(cd_store_fn fn, void *ctx, const char *key, uint32_t klen, uint32_t deadline_ms);
void cd_store_del_cb(cd_store_done_fn fn, void *ctx, const char *key, uint32_t klen);

/* The app's hooks (empty weak defaults). They are the app's own: a library linked into an app never
 * defines them, since the app may, and uses the _cb forms, whose answers never reach them. */
void cd_on_store(uint32_t token, const uint8_t *p, int32_t len);
void cd_on_store_put(uint32_t token, uint32_t ok);
void cd_on_store_del(uint32_t token, uint32_t ok);
#endif
