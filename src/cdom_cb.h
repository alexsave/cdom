/* cdom_cb: cdom's own callback tokens, internal (not part of the app's API). One slot table, owned by
 * cdom.c, for every answer cdom routes to a callback instead of an app hook: cd_timer_cb's timers
 * (cdom.c), cd_http_cb's requests and cd_ws_open_cb's sockets (cdom_net.c), and the store's _cb forms
 * (cdom_store.c). A slot holds fn, ctx and a generation; its token is
 * CD_TOKEN_CDOM | generation << 16 | slot, so a token is never an app's (those stay below
 * CD_TOKEN_CDOM), never CD_FRAMES_TOKEN (slot 0xffff is never handed out), and an old token stops
 * naming its slot once the slot is freed (the generation moves on; it wraps after 32,768 reuses). */
#ifndef CDOM_CB_H
#define CDOM_CB_H
#include <stdint.h>

typedef void (*cd_cb_fn)(void);
/* A free slot (the table grows; traps past 65,535 live at once) given fn and ctx; returns its token. */
uint32_t cd_cb_new(cd_cb_fn fn, void *ctx);
/* While token still names its slot: fn and ctx into *fn and *ctx, the slot freed, 1. Else 0, nothing
 * touched (a token already answered or dropped, or not cdom's). */
int cd_cb_take(uint32_t token, cd_cb_fn *fn, void **ctx);
/* While token still names its slot: fn and ctx into *fn and *ctx, the slot kept (for a socket, which
 * hears many events), 1. Else 0. */
int cd_cb_get(uint32_t token, cd_cb_fn *fn, void **ctx);
/* Frees token's slot while token still names it; does nothing otherwise. */
void cd_cb_drop(uint32_t token);
#endif
