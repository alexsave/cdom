/* The part's second file. Not marked for export: CDOM_PARTS_LDFLAGS exports it (-Wl,--export), so the
 * module's link flags reach the link. */
#include <stdint.h>
uint32_t parts_lib_b(void) { return 0x1b; }
