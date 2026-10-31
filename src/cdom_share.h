/* cdom_share: text onto the system clipboard, and a title and a link out through the system's
 * share sheet (navigator.share). Link src/cdom_share.c to use it. Both answer by token, always
 * from the event loop, even when the answer is known at once (a browser without the API).
 *
 * Both need a user gesture: call them while handling a click, a tap or a key (from cd_on_event,
 * which runs inside the browser's own event handler), not from a timer, a frame, a network answer
 * or cd_start. Outside a gesture the browser refuses (CD_SHARE_FAILED), unless the page holds the
 * clipboard-write permission (a test grants it); Chrome lets a few seconds after a gesture count,
 * Safari only the handler itself. */
#ifndef CDOM_SHARE_H
#define CDOM_SHARE_H
#include <stdint.h>

/* The answers. FAILED: the browser refused or the write or share went wrong (no gesture, the
 * permission denied, the page not focused, nothing to share, a share already open). DONE: the text
 * is on the clipboard, or the user picked where to share. UNAVAILABLE: this browser has no such
 * API (no clipboard outside a secure context: https or localhost; no share sheet in most desktop
 * browsers), so the app shows its own way (the link in a field to copy). CANCELLED: the user
 * closed the share sheet without sharing (an AbortError); not a failure to report. */
#define CD_SHARE_FAILED 0
#define CD_SHARE_DONE 1
#define CD_SHARE_UNAVAILABLE 2
#define CD_SHARE_CANCELLED 3

/* Put len bytes of UTF-8 text (read during the call) on the clipboard; the answer is
 * cd_on_clipboard(token, status): DONE, FAILED or UNAVAILABLE. */
void cd_clipboard_write(uint32_t token, const char *text, uint32_t len);

/* Open the share sheet with a title and a url (UTF-8, read during the call; a length of 0 leaves
 * that one out, so a title or a url may go alone); the answer is cd_on_share(token, status):
 * DONE, CANCELLED, FAILED or UNAVAILABLE. A relative url resolves against the page's address. */
void cd_share(uint32_t token, const char *title, uint32_t tlen, const char *url, uint32_t ulen);

/* The app's hooks (empty weak defaults in cdom_share.c). */
void cd_on_clipboard(uint32_t token, uint32_t status);
void cd_on_share(uint32_t token, uint32_t status);
#endif
