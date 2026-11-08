/* What the string.h macros cannot cover: a library that takes memcpy's, memmove's and memset's
 * address. They link to mini.c's weak real functions. */
#include <string.h>
typedef void *(*copy_fn)(void *, const void *, size_t);
static char buf[16];
__attribute__((export_name("libc_addr"))) int libc_addr(void) {
  static copy_fn volatile const copy[2] = {memcpy, memmove};
  void *(*volatile fill)(void *, int, size_t) = &memset;
  fill(buf, 'x', sizeof buf);
  copy[0](buf, "abcdef", 6);
  copy[1](buf + 1, buf, 5); /* overlapping: "aabcde" */
  return memcmp(buf, "aabcdexxxxxxxxxx", 16) == 0 && (memcpy)(buf, "z", 1) == buf && buf[0] == 'z';
}
