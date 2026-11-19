/* A second file named lib_a.c, in another folder: a part that names both is refused (their objects would be one
 * file, and one of them would be silently left out). */
#include <stdint.h>
__attribute__((export_name("parts_dup_a"))) uint32_t parts_dup_a(void) { return 0xda; }
