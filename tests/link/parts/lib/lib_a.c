/* The test module's part "lib", as third-party code is: compiled apart with the part's flags (-w, its own -I
 * and a -D), after the app's. It includes a header only the part's -I finds, and warns (under -Wall) in
 * ways -w silences: an unused variable, always, and another with -DPARTS_WARN. */
#include <stdint.h>
#include "parts_private.h"
#ifdef PARTS_WARN
static int unused_lib;
#endif
static int unused_always;
#ifdef PARTS_SIMD_LIT
/* With -DPARTS_SIMD_LIT (app.mk's SIMD_LIB_CFLAGS, the part's flags in the APP_SIMD twin only): a string literal only
 * the twin has, which cdom.mk must refuse (one table serves both modules). */
#include "cdom.h"
__attribute__((export_name("parts_simd_lit"))) void parts_simd_lit(void) { cd_text("only in the SIMD twin"); }
#endif
__attribute__((export_name("parts_lib_a"))) uint32_t parts_lib_a(void) { return PARTS_PRIVATE * PARTS_LIB; }
