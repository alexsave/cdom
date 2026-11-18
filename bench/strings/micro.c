/* strings-bench micro: cd_text with literals and runtime strings, through the real cdom.c of each
 * design (built as A, today; B, -DCDOM_STRB=1; D, -DCDOM_STRB=1 -DCDOM_LITD=1). The page (micro.html)
 * gives it either its own imports that only resolve the string, or the real shim. */
#include "cdom.h"

#define EXPORT(n) __attribute__((export_name(n)))

static char buf[512];
EXPORT("buf") char *buf_ptr(void) { return buf; }

/* Eight literal call sites per iteration, so the lookup is not one constant. */
EXPORT("lit8") void lit8(uint32_t n) {
  for (uint32_t i = 0; i < n; i++) {
    cd_text(CD_LIT("Hello"));
    cd_text(CD_LIT("div"));
    cd_text(CD_LIT("col-md-1"));
    cd_text(CD_LIT("glyphicon glyphicon-remove"));
    cd_text(CD_LIT("aria-hidden"));
    cd_text(CD_LIT("Create 1,000 rows"));
    cd_text(CD_LIT("danger"));
    cd_text(CD_LIT("x"));
  }
}
/* E: dense ids 1 to 8 handed straight to cd_text (the page's imports read lits[id]). */
EXPORT("dense8") void dense8(uint32_t n) {
  for (uint32_t i = 0; i < n; i++)
    for (uint32_t k = 1; k <= 8; k++) (cd_text)(k);
}
/* A runtime string of len bytes in buf, one cd_text each. */
EXPORT("tmp") void tmp(uint32_t n, uint32_t len) {
  for (uint32_t i = 0; i < n; i++) cd_text(cd_tmp(buf, len));
}
#if !CDOM_STRB
/* Today's interned path: hashed and compared on every call (a hit after the first). */
EXPORT("intern_hit") void intern_hit(uint32_t n, uint32_t len) {
  for (uint32_t i = 0; i < n; i++) cd_text(cd_intern(buf, len));
}
/* Today's interned path for fresh strings: n distinct strings of len bytes (buf with a counter in its
 * first bytes), each interned (hashed, copied, queued) and then shown once, so they decode together. */
EXPORT("intern_new") void intern_new(uint32_t n, uint32_t len, uint32_t salt) {
  static cd_str ids[1024];
  for (uint32_t i = 0; i < n;) {
    uint32_t c = n - i < 1024 ? n - i : 1024;
    for (uint32_t k = 0; k < c; k++) {
      uint32_t v = salt * 1000003u + i + k;
      char save[4];
      for (uint32_t b = 0; b < 4 && b < len; b++) { save[b] = buf[b]; buf[b] = (char)('a' + (v >> (5 * b)) % 26); }
      ids[k] = cd_intern(buf, len);
      for (uint32_t b = 0; b < 4 && b < len; b++) buf[b] = save[b];
    }
    for (uint32_t k = 0; k < c; k++) cd_text(ids[k]);
    i += c;
  }
}
#endif
/* With the real shim: text nodes go into an open, detached div, replaced between samples. */
EXPORT("dom_reset") void dom_reset(void) {
  cd_close();
  cd_flush();
  cd_open(CD_LIT("div"));
  cd_flush();
}
void cd_main(void) {
  cd_at(CD_DETACHED, 0);
  cd_open(CD_LIT("div"));
}
