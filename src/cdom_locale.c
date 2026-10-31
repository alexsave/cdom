/* cdom_locale: the language and preference imports and the export that reports a change
 * (cdom_locale.h). Separate from cdom.c so an app without them does not carry the export, and the
 * shim listens to the media queries only when it finds it. */
#include "cdom.h"
#include "cdom_locale.h"

#define IMPORT(n) __attribute__((import_module("cdom"), import_name(n)))
#define EXPORT(n) __attribute__((export_name(n)))

IMPORT("language") uint32_t j_language(uint32_t i, char *dst, uint32_t cap);
IMPORT("prefs") uint32_t j_prefs(void);

uint32_t cd_language(uint32_t i, char *dst, uint32_t cap) { return j_language(i, dst, cap); }
uint32_t cd_prefs(void) { return j_prefs(); }

__attribute__((weak)) void cd_on_prefs(uint32_t prefs) { (void)prefs; }
EXPORT("cd_prefs_changed") void cd_prefs_changed(uint32_t prefs) {
  cd_on_prefs(prefs);
  cd_flush();
}
