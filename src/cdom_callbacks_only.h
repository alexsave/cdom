/* cdom_callbacks_only: an opt-in profile. Included after the cdom headers an app or a library uses, it
 * makes every call that routes an answer or an event through an app-wide hook or a token a compile
 * error (#pragma GCC poison), so the code uses cd_on and the _cb forms alone: each answer reaches the
 * callback that asked, with its ctx, and two modules (an app and a library inside it) never fight over
 * one hook. It costs no bytes: it is only a list of names. What has no _cb form yet (cd_raf_request,
 * the audio, share, fonts and image answers) is left alone. */
#ifndef CDOM_CALLBACKS_ONLY_H
#define CDOM_CALLBACKS_ONLY_H
/* events: cd_on(id, ev, fn, ctx), and cd_on(id, ev, 0, 0) to stop */
#pragma GCC poison cd_listen cd_unlisten cd_listen_window cd_unlisten_window cd_tag_set cd_tag cd_on_event
/* timers: cd_timer_cb, cd_timer_set (cdom.h) */
#pragma GCC poison cd_timer_start cd_on_timer
/* the network: cd_http_cb, cd_ws_open_cb (cdom_net.h) */
#pragma GCC poison cd_http cd_on_http cd_ws_open cd_on_ws_open cd_on_ws_msg cd_on_ws_close
/* the store: cd_store_get_cb, cd_store_put_cb, cd_store_del_cb (cdom_store.h) */
#pragma GCC poison cd_store_get cd_store_put cd_store_del cd_on_store cd_on_store_put cd_on_store_del
/* picked files: cd_file_read_cb (cdom_file.h) */
#pragma GCC poison cd_file_read cd_on_file
#endif
