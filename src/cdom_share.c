/* cdom_share: the clipboard and share imports and the exports that answer them (cdom_share.h).
 * Separate from cdom.c so an app without them does not carry the exports. */
#include "cdom.h"
#include "cdom_share.h"

#define IMPORT(n) __attribute__((import_module("cdom"), import_name(n)))
#define EXPORT(n) __attribute__((export_name(n)))

IMPORT("clipboard_write") void j_clipboard_write(uint32_t token, const char *text, uint32_t len);
IMPORT("share") void j_share(uint32_t token, const char *title, uint32_t tlen, const char *url, uint32_t ulen);

void cd_clipboard_write(uint32_t token, const char *text, uint32_t len) { j_clipboard_write(token, text, len); }
void cd_share(uint32_t token, const char *title, uint32_t tlen, const char *url, uint32_t ulen) {
  j_share(token, title, tlen, url, ulen);
}

__attribute__((weak)) void cd_on_clipboard(uint32_t token, uint32_t status) { (void)token; (void)status; }
EXPORT("cd_clipboard_done") void cd_clipboard_done(uint32_t token, uint32_t status) {
  cd_on_clipboard(token, status);
  cd_flush();
}
__attribute__((weak)) void cd_on_share(uint32_t token, uint32_t status) { (void)token; (void)status; }
EXPORT("cd_share_done") void cd_share_done(uint32_t token, uint32_t status) {
  cd_on_share(token, status);
  cd_flush();
}
