/* The stub runtime for tests/lits: cd_tmp and cd_tmpz (records in a ring, nothing copied, as cdom.c keeps
 * them), the stamp word cdlits patches, and two exports the Node run reads. */
#include "lits.h"

#ifndef STAMP_INIT
#define STAMP_INIT 0x7374696c
#endif
#ifndef NO_STAMP
volatile uint32_t cd_lits_stamp = STAMP_INIT;
#endif

static struct { uint32_t p, len; } recs[64];
static uint32_t nrec;
cd_str cd_tmp(const char *s, uint32_t len) {
  recs[nrec % 64].p = (uint32_t)(uintptr_t)s;
  recs[nrec % 64].len = len;
  return (cd_str){0x80000000u | nrec++};
}
/* Written out, since clang turns the loop in cd_tmpz into a call to strlen, and there is no libc here. */
unsigned long strlen(const char *s) {
  unsigned long n = 0;
  while (s[n]) n++;
  return n;
}
cd_str cd_tmpz(const char *s) {
  if (!s) __builtin_trap();
  return cd_tmp(s, (uint32_t)strlen(s));
}
EX(stub_recs) void *stub_recs(void) { return recs; }
#ifndef NO_STAMP
EX(stub_stamp) uint32_t stub_stamp(void) { return cd_lits_stamp; }
#endif
