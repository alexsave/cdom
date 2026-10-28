/* The hydrate entry's blob decoding (src/cdom.c): the shim copies the page's base64 text into
 * cd_state_buf and cd_start_state decodes it in place before cd_state_load sees the bytes.
 * Natively: every padding form decodes to the right bytes, and text that is not base64 is
 * refused before the app's loader runs. Built and run by tests/state.sh. */
#include "../src/cdom.h"
#include <stdio.h>
#include <string.h>

static uint8_t got[64];
static uint32_t ngot, loads;
int cd_state_load(const uint8_t *src, uint32_t len) {
  loads++;
  ngot = len < sizeof got ? len : sizeof got;
  memcpy(got, src, ngot);
  return 0;
}
void cd_main(void) {}
void cd_on_event(const cd_event_rec *e) { (void)e; }
uint8_t *cd_state_buf(uint32_t len);
int cd_start_state(uint32_t len);

static int fails;
static int start_n(const char *b64, uint32_t n) { /* the buffer holds all of b64; C is told n */
  memcpy(cd_state_buf((uint32_t)strlen(b64)), b64, strlen(b64));
  return cd_start_state(n);
}
static int start(const char *b64) { return start_n(b64, (uint32_t)strlen(b64)); }
static void good(const char *b64, const char *want, uint32_t wn) {
  uint32_t before = loads;
  int r = start(b64);
  int ok = r == 0 && loads == before + 1 && ngot == wn && !memcmp(got, want, wn);
  printf("%s \"%s\" decodes to %u bytes\n", ok ? "ok  " : "FAIL", b64, wn);
  fails += !ok;
}
static void refused(const char *b64) {
  uint32_t before = loads;
  int ok = start(b64) != 0 && loads == before;
  printf("%s \"%s\" is refused before the loader runs\n", ok ? "ok  " : "FAIL", b64);
  fails += !ok;
}

int main(void) {
  good("", "", 0);
  good("QQ==", "A", 1);
  good("QUI=", "AB", 2);
  good("QUJD", "ABC", 3);
  good("AP8A/w==", "\x00\xff\x00\xff", 4);
  good("+/+/", "\xfb\xff\xbf", 3);
  good("YWJjZGVmZ2hpams=", "abcdefghijk", 11);
  refused("QUJ");      /* not a multiple of 4 */
  uint32_t before = loads; /* and nothing past the length is read, even when it would decode */
  int r = start_n("QUJD", 3);
  printf("%s \"QUJ\" followed by D is refused\n", r && loads == before ? "ok  " : "FAIL");
  fails += !(r && loads == before);
  refused("QU!D");     /* outside the alphabet */
  refused("Q===");     /* padding where a byte must be */
  refused("QQ=A");     /* padding before data */
  refused("QQ==QUJD"); /* padding not at the end */
  refused("QU D");     /* no whitespace */
  printf(fails ? "state: %d FAILED\n" : "state: all ok\n", fails);
  return fails != 0;
}
