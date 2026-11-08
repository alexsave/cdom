/* cdom_locale: what the user asked of every page: the languages they read, in their order, and
 * whether they want a dark page and less motion. Link src/cdom_locale.c to use it. C chooses what
 * to do with them (a translation, a theme, an animation skipped); the shim only reads them. */
#ifndef CDOM_LOCALE_H
#define CDOM_LOCALE_H
#include <stdint.h>

/* The i-th preferred language (0 first), a BCP 47 tag ("fr-CA", "en"), written to dst (at most cap
 * bytes); returns its length, which may be more than cap (then only cap bytes were written), or 0
 * past the last. The first N: ask i from 0 up to N - 1, stopping at the first 0. A browser without
 * navigator.languages answers navigator.language as the only one; one without either answers 0
 * at once, and the app keeps its own default. */
uint32_t cd_language(uint32_t i, char *dst, uint32_t cap);
#define CD_LANGUAGE_MAX 35 /* room for a tag of every subtag RFC 5646 asks a reader to hold */

/* The display preferences, as bits: CD_PREFS_DARK when prefers-color-scheme is dark,
 * CD_PREFS_REDUCED_MOTION when prefers-reduced-motion is reduce. 0 is a light page with motion, the
 * default the media queries give a user who said nothing, and what a browser without matchMedia
 * reads as. */
enum { CD_PREFS_DARK = 1, CD_PREFS_REDUCED_MOTION = 2 };
uint32_t cd_prefs(void);

/* The app's hook (an empty weak default in cdom_locale.c): either preference changed (the system
 * switched to dark at dusk), and prefs is what cd_prefs() reads from then on. The shim listens
 * from start for every app that links cdom_locale.c. */
void cd_on_prefs(uint32_t prefs);
#endif
