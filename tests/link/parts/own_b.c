/* The test module's second own file: CDOM_PARTS_SRC names two files, and both link. */
#include <stdint.h>
__attribute__((export_name("parts_own_b"))) uint32_t parts_own_b(void) { return 0xb; }
