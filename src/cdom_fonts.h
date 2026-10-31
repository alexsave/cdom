/* cdom_fonts: when the page's web fonts are ready, for an app that measures text or lays out by
 * it (a label sized to fit, a canvas drawn in the page's font). Link src/cdom_fonts.c to use it.
 * The shim only reads the browser's FontFaceSet (document.fonts); C decides what to wait for and
 * what to do after. */
#ifndef CDOM_FONTS_H
#define CDOM_FONTS_H
#include <stdint.h>

/* The answers. TIMEOUT: deadline_ms passed and a font was still loading (the page shows a fallback
 * font for now; cd_on_fonts_changed says when it is done). READY: no font is loading, every one
 * the page asked for has loaded or failed (a failed one keeps the fallback). UNAVAILABLE: the
 * browser has no document.fonts, so there is nothing to wait for and nothing will be heard. */
#define CD_FONTS_TIMEOUT 0
#define CD_FONTS_READY 1
#define CD_FONTS_UNAVAILABLE 2

/* Wait for the fonts (document.fonts.ready): the answer is cd_on_fonts(token, status), once,
 * always from the event loop, READY at once (but not during the call) when nothing is loading.
 * deadline_ms 0: no deadline. A font the page only starts to load after the call (a style set
 * later) is not waited for: ask again. */
void cd_fonts_wait(uint32_t token, uint32_t deadline_ms);

/* The app's hooks (empty weak defaults in cdom_fonts.c). cd_on_fonts_changed: the fonts that were
 * loading are done (document.fonts' loadingdone), so text laid out in a fallback may have changed
 * size: measure again. The shim listens from start for every app that links cdom_fonts.c. */
void cd_on_fonts(uint32_t token, uint32_t status);
void cd_on_fonts_changed(void);
#endif
