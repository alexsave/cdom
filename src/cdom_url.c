/* cdom_url: the address imports and the export that reports back and forward (cdom_url.h).
 * Separate from cdom.c so an app without routing does not carry its export, and the shim listens
 * for popstate only when it finds it. */
#include "cdom.h"
#include "cdom_url.h"

#define IMPORT(n) __attribute__((import_module("cdom"), import_name(n)))
#define EXPORT(n) __attribute__((export_name(n)))

IMPORT("url_read") uint32_t j_url_read(uint32_t part, char *dst, uint32_t cap);
IMPORT("url_go") uint32_t j_url_go(const char *path, uint32_t len, uint32_t replace);
IMPORT("title") void j_title(const char *text, uint32_t len);

uint32_t cd_url_read(uint32_t part, char *dst, uint32_t cap) { return j_url_read(part, dst, cap); }
uint32_t cd_url_push(const char *path, uint32_t len) { return j_url_go(path, len, 0); }
uint32_t cd_url_replace(const char *path, uint32_t len) { return j_url_go(path, len, 1); }
void cd_set_title(const char *text, uint32_t len) { j_title(text, len); }

__attribute__((weak)) void cd_on_url(const uint8_t *path, uint32_t len) { (void)path; (void)len; }
/* The shim copies the new path into cdom's room for incoming bytes (cd_bytes_buf), then calls this. */
EXPORT("cd_url_changed") void cd_url_changed(const uint8_t *path, uint32_t len) {
  cd_on_url(path, len);
  cd_flush();
}
