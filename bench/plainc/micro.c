/* strings-plain-c micro: literals written the plain C way (a ternary, a helper that takes const char *),
 * through the real cdom.c, against a slot lookup and a mutable buffer. micro.html resolves the string only. */
#include "cdom.h"
#define EXPORT(n) __attribute__((export_name(n)))
extern const char __rodata_start[] __attribute__((weak)), __rodata_end[] __attribute__((weak));
EXPORT("ro0") uint32_t ro0(void) { return (uint32_t)(uintptr_t)__rodata_start; }
EXPORT("ro1") uint32_t ro1(void) { return (uint32_t)(uintptr_t)__rodata_end; }
static char buf[64] = "twelve bytes";
EXPORT("buf") char *buf_ptr(void) { return buf; }

/* Eight direct literals: today's slot lookup, the floor. 5 to 15 bytes each. */
EXPORT("lit8") void lit8(uint32_t n) {
  for (uint32_t i = 0; i < n; i++) {
    cd_text("Hide details"); cd_text("Show details"); cd_text("true"); cd_text("false");
    cd_text("tab active"); cd_text("Overview"); cd_text("item done"); cd_text("aria-selected");
  }
}
/* The same strings chosen by a ternary, written plainly: 8 calls per iteration. */
EXPORT("tern8") void tern8(uint32_t n) {
  for (uint32_t i = 0; i < n; i++) {
    uint32_t a = i & 1, b = i & 2, c = i & 4, d = i % 3;
    cd_text(a ? "Hide details" : "Show details"); cd_text(b ? "true" : "false");
    cd_text(c ? "tab active" : "tab bar"); cd_text(d ? "item done" : "item");
    cd_text(a ? "Pause" : "Play now"); cd_text(b ? "Stop" : "Start");
    cd_text(c ? "aria-selected" : "aria-pressed"); cd_text(d ? "Overview" : "Usage");
  }
}
/* The same with today's ceremony, cd_s on each arm: slots. */
EXPORT("terns8") void terns8(uint32_t n) {
  for (uint32_t i = 0; i < n; i++) {
    uint32_t a = i & 1, b = i & 2, c = i & 4, d = i % 3;
    cd_text(a ? cd_s("Hide details") : cd_s("Show details")); cd_text(b ? cd_s("true") : cd_s("false"));
    cd_text(c ? cd_s("tab active") : cd_s("tab bar")); cd_text(d ? cd_s("item done") : cd_s("item"));
    cd_text(a ? cd_s("Pause") : cd_s("Play now")); cd_text(b ? cd_s("Stop") : cd_s("Start"));
    cd_text(c ? cd_s("aria-selected") : cd_s("aria-pressed")); cd_text(d ? cd_s("Overview") : cd_s("Usage"));
  }
}
/* A helper that takes const char *, as an author writes it; never inlined, so no build can see the literal. */
__attribute__((noinline)) static void label(const char *s) { cd_text(s); }
EXPORT("help8") void help8(uint32_t n) {
  for (uint32_t i = 0; i < n; i++) {
    label("Overview"); label("Usage"); label("Notes"); label("All");
    label("Active"); label("Done"); label("Hide details"); label("tab active");
  }
}
/* A mutable buffer passed plainly: must stay a read on use, and must not get slower. len bytes, ASCII. */
EXPORT("buf1") void buf1(uint32_t n, uint32_t len) {
  buf[len] = 0;
  for (uint32_t i = 0; i < n; i++) cd_text(buf);
}
/* The same buffer rewritten before every call (a counter in its first bytes): a cache never hits. */
EXPORT("bufnew") void bufnew(uint32_t n, uint32_t len) {
  buf[len] = 0;
  for (uint32_t i = 0; i < n; i++) { buf[0] = (char)('a' + (i & 15)); buf[1] = (char)('a' + ((i >> 4) & 15)); cd_text(buf); }
}
void cd_main(void) {}
