/* A library's file as it is written for any C target: <string.h> (cdom's, src/libc) and the five it
 * offers. run.sh builds it under cdom's flags and freestanding ones (-ffreestanding), with mini.c and no
 * other libc; copy and fill must be instructions here, never calls. The string is behind a volatile
 * so the sizes are known only at run time, as a library's are (the compiler keeps the operations). */
#include <string.h>
static char buf[32];
static const char *volatile text = "cdom string.h";
__attribute__((export_name("libc_header"))) int libc_header(void) {
  const char *s = text;
  size_t n = strlen(s);
  memset(buf, '#', n + 19);
  memcpy(buf, s, n + 1);
  memmove(buf + 2, buf, n + 1); /* overlapping: "cdcdom string.h" */
  int ok = n == 13;
  ok &= memcmp(buf, "cdcdom string.h", 16) == 0;
  ok &= memcmp(buf, "cdcdom strinG.h", 16) > 0;
  ok &= buf[16] == '#' && buf[31] == '#';
  return ok;
}
