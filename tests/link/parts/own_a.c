/* A test module's own file (tests/link/app.mk, PARTS): compiled with the app's flags (-Wall -Wextra -Werror),
 * so -DPARTS_WARN, which adds an unused variable, stops the build here. */
#include <stdint.h>
#ifdef PARTS_WARN
static int unused_own;
#endif
__attribute__((export_name("parts_own_a"))) uint32_t parts_own_a(void) { return 0xa; }
