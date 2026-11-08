/* cdom_store: the store's imports and the exports that answer them (cdom_store.h). Separate from
 * cdom.c so an app without a store does not carry its exports. */
#include "cdom.h"
#include "cdom_store.h"
#include "cdom_cb.h"

#define IMPORT(n) __attribute__((import_module("cdom"), import_name(n)))
#define EXPORT(n) __attribute__((export_name(n)))

IMPORT("store_put") void j_store_put(uint32_t token, const char *key, uint32_t klen, const void *p, uint32_t len);
IMPORT("store_get") void j_store_get(uint32_t token, const char *key, uint32_t klen, uint32_t ms);
IMPORT("store_del") void j_store_del(uint32_t token, const char *key, uint32_t klen);

/* The token forms trap on a token in cdom's range: its answer would go to a callback slot. */
void cd_store_put(uint32_t token, const char *key, uint32_t klen, const void *p, uint32_t len) {
  if (token >= CD_TOKEN_CDOM) __builtin_trap();
  j_store_put(token, key, klen, p, len);
}
void cd_store_get(uint32_t token, const char *key, uint32_t klen, uint32_t deadline_ms) {
  if (token >= CD_TOKEN_CDOM) __builtin_trap();
  j_store_get(token, key, klen, deadline_ms);
}
void cd_store_del(uint32_t token, const char *key, uint32_t klen) {
  if (token >= CD_TOKEN_CDOM) __builtin_trap();
  j_store_del(token, key, klen);
}
/* The _cb forms: the token is a callback slot's (cdom_cb.h), taken when the answer comes. */
void cd_store_put_cb(cd_store_done_fn fn, void *ctx, const char *key, uint32_t klen, const void *p, uint32_t len) {
  j_store_put(cd_cb_new((cd_cb_fn)fn, ctx), key, klen, p, len);
}
void cd_store_get_cb(cd_store_fn fn, void *ctx, const char *key, uint32_t klen, uint32_t deadline_ms) {
  j_store_get(cd_cb_new((cd_cb_fn)fn, ctx), key, klen, deadline_ms);
}
void cd_store_del_cb(cd_store_done_fn fn, void *ctx, const char *key, uint32_t klen) {
  j_store_del(cd_cb_new((cd_cb_fn)fn, ctx), key, klen);
}

/* An app's token goes to its hook, cdom's to the callback that asked. */
__attribute__((weak)) void cd_on_store(uint32_t token, const uint8_t *p, int32_t len) { (void)token; (void)p; (void)len; }
EXPORT("cd_store_got") void cd_store_got(uint32_t token, const uint8_t *p, int32_t len) {
  cd_cb_fn fn;
  void *ctx;
  if (token < CD_TOKEN_CDOM) cd_on_store(token, p, len);
  else if (cd_cb_take(token, &fn, &ctx)) ((cd_store_fn)fn)(ctx, p, len);
  cd_flush();
}
__attribute__((weak)) void cd_on_store_put(uint32_t token, uint32_t ok) { (void)token; (void)ok; }
EXPORT("cd_store_put_done") void cd_store_put_done(uint32_t token, uint32_t ok) {
  cd_cb_fn fn;
  void *ctx;
  if (token < CD_TOKEN_CDOM) cd_on_store_put(token, ok);
  else if (cd_cb_take(token, &fn, &ctx)) ((cd_store_done_fn)fn)(ctx, ok);
  cd_flush();
}
__attribute__((weak)) void cd_on_store_del(uint32_t token, uint32_t ok) { (void)token; (void)ok; }
EXPORT("cd_store_del_done") void cd_store_del_done(uint32_t token, uint32_t ok) {
  cd_cb_fn fn;
  void *ctx;
  if (token < CD_TOKEN_CDOM) cd_on_store_del(token, ok);
  else if (cd_cb_take(token, &fn, &ctx)) ((cd_store_done_fn)fn)(ctx, ok);
  cd_flush();
}
