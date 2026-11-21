/* What foolish's uttt_code.c needs of libc beyond cdom's (src/libc/string.h): strncmp, defined in uttt_replay.c.
 * Every file of the module is built with -include compat.h (demo/uttt/Makefile), so foolish's files compile as they
 * are. */
#include <stddef.h>
int strncmp(const char *a, const char *b, size_t n);
