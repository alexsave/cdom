/* cdom_fonts: the fonts import and the exports that answer it (cdom_fonts.h). Separate from cdom.c
 * so an app without it does not carry the exports, and the shim listens to document.fonts only
 * when it finds them. */
#include "cdom.h"
#include "cdom_fonts.h"

#define IMPORT(n) __attribute__((import_module("cdom"), import_name(n)))
#define EXPORT(n) __attribute__((export_name(n)))

IMPORT("fonts_wait") void j_fonts_wait(uint32_t token, uint32_t deadline_ms);

void cd_fonts_wait(uint32_t token, uint32_t deadline_ms) { j_fonts_wait(token, deadline_ms); }

__attribute__((weak)) void cd_on_fonts(uint32_t token, uint32_t status) { (void)token; (void)status; }
EXPORT("cd_fonts_done") void cd_fonts_done(uint32_t token, uint32_t status) {
  cd_on_fonts(token, status);
  cd_flush();
}
__attribute__((weak)) void cd_on_fonts_changed(void) {}
EXPORT("cd_fonts_changed") void cd_fonts_changed(void) {
  cd_on_fonts_changed();
  cd_flush();
}
