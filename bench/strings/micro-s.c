/* strings step 10 micro: cd_text through the strings build's cdom.c, as an app writes it (plain C).
 * micro-s.html runs it on the real shim, with text() made a sink for "resolved only". */
#include "cdom.h"

#define EXPORT(n) __attribute__((export_name(n)))

static char buf[512];
static char area[1024 * 64];
EXPORT("buf") char *buf_ptr(void) { return buf; }
EXPORT("area") char *area_ptr(void) { return area; }

/* Eight literal call sites per iteration (each a slot in the page's table). */
EXPORT("lit8") void lit8(uint32_t n) {
  for (uint32_t i = 0; i < n; i++) {
    cd_text("Hello");
    cd_text("div");
    cd_text("col-md-1");
    cd_text("glyphicon glyphicon-remove");
    cd_text("aria-hidden");
    cd_text("Create 1,000 rows");
    cd_text("danger");
    cd_text("x");
  }
}
/* Option B: plain C a macro cannot see; a pointer into read-only data, read once, then cached by address. */
EXPORT("ternary8") void ternary8(uint32_t n) {
  for (uint32_t i = 0; i < n; i++) {
    uint32_t o = i & 1;
    cd_text(o ? "Hide details" : "Show details");
    cd_text(o ? "on" : "off");
    cd_text(o ? "col-md-1" : "col-md-4");
    cd_text(o ? "danger" : "");
    cd_text(o ? "Open" : "Closed");
    cd_text(o ? "aria-hidden" : "aria-label");
    cd_text(o ? "x" : "y");
    cd_text(o ? "Create 1,000 rows" : "Create 10,000 rows");
  }
}
__attribute__((noinline)) static void show(const char *label) { cd_text(label); }
EXPORT("helper8") void helper8(uint32_t n) {
  for (uint32_t i = 0; i < n; i++) {
    show("Hello"); show("div"); show("col-md-1"); show("glyphicon glyphicon-remove");
    show("aria-hidden"); show("Create 1,000 rows"); show("danger"); show("x");
  }
}
static const char *const TABLE[8] = {"Hello", "div", "col-md-1", "glyphicon glyphicon-remove", "aria-hidden",
  "Create 1,000 rows", "danger", "x"};
EXPORT("table8") void table8(uint32_t n) {
  for (uint32_t i = 0; i < n; i++)
    for (uint32_t k = 0; k < 8; k++) cd_text(TABLE[k]);
}
/* A runtime string of len bytes in buf, read on use. */
EXPORT("tmp") void tmp(uint32_t n, uint32_t len) {
  for (uint32_t i = 0; i < n; i++) cd_text(cd_tmp(buf, len));
}
/* Inside a range: 1,024 strings of len bytes laid out in area, announced once per 1,024, each shown once. */
EXPORT("ahead") void ahead(uint32_t n, uint32_t len) {
  for (uint32_t i = 0; i < n;) {
    uint32_t c = n - i < 1024 ? n - i : 1024;
    cd_tmp_ahead(area, c * len);
    for (uint32_t k = 0; k < c; k++) cd_text(cd_tmp(area + k * len, len));
    i += c;
  }
}
EXPORT("dom_reset") void dom_reset(void) {
  cd_close();
  cd_flush();
  cd_open("div");
  cd_flush();
}
void cd_main(void) {
  cd_at(CD_DETACHED, 0);
  cd_open("div");
}
