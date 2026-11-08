/* cdom_url: the address imports and the export that reports back and forward (cdom_url.h).
 * Separate from cdom.c so an app without routing does not carry its export, and the shim listens
 * for popstate only when it finds it. */
#include "cdom.h"
#include "cdom_url.h"

#define IMPORT(n) __attribute__((import_module("cdom"), import_name(n)))
#define EXPORT(n) __attribute__((export_name(n)))

IMPORT("url_read") uint32_t j_url_read(uint32_t part, char *dst, uint32_t cap);
IMPORT("url_go") uint32_t j_url_go(const char *path, uint32_t len, uint32_t replace);
IMPORT("url_step") void j_url_step(int32_t delta);
IMPORT("title") void j_title(const char *text, uint32_t len);

uint32_t cd_url_read(uint32_t part, char *dst, uint32_t cap) { return j_url_read(part, dst, cap); }
uint32_t cd_url_push(const char *path, uint32_t len) { return j_url_go(path, len, 0); }
uint32_t cd_url_replace(const char *path, uint32_t len) { return j_url_go(path, len, 1); }
void cd_url_step(int32_t delta) { j_url_step(delta); }
void cd_set_title(const char *text, uint32_t len) { j_title(text, len); }

uint32_t cd_url_encode(char *dst, const char *src, uint32_t len) {
  static const char HEX[] = "0123456789ABCDEF";
  uint32_t n = 0;
  for (uint32_t i = 0; i < len; i++) {
    uint8_t c = (uint8_t)src[i];
    int keep = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
               c == '.' || c == '!' || c == '~' || c == '*' || c == '\'' || c == '(' || c == ')';
    if (keep) {
      dst[n++] = (char)c;
    } else {
      dst[n++] = '%';
      dst[n++] = HEX[c >> 4];
      dst[n++] = HEX[c & 15];
    }
  }
  return n;
}
static int hexval(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}
uint32_t cd_url_decode(char *dst, const char *src, uint32_t len) {
  uint32_t n = 0;
  for (uint32_t i = 0; i < len; i++) {
    int hi, lo;
    if (src[i] == '%' && i + 2 < len && (hi = hexval(src[i + 1])) >= 0 && (lo = hexval(src[i + 2])) >= 0) {
      dst[n++] = (char)(hi << 4 | lo);
      i += 2;
    } else {
      dst[n++] = src[i];
    }
  }
  return n;
}

__attribute__((weak)) void cd_on_url(const uint8_t *path, uint32_t len) { (void)path; (void)len; }
/* The shim copies the new path into cdom's room for incoming bytes (cd_bytes_buf), then calls this. */
EXPORT("cd_url_changed") void cd_url_changed(const uint8_t *path, uint32_t len) {
  cd_on_url(path, len);
  cd_flush();
}
