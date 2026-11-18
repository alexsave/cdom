/* S14: a text input that watches its own field (docs/shared-state-design.md). The input handler reads
 * the whole value and calls the setter; a view watching the value's state (the cd_state prototype) writes
 * it back with cd_set_str(input, "value", ...). One define picks the variant:
 *   CARET_A  the view writes back unguarded (the same bytes the handler read)
 *   CARET_C  the setter upper-cases, the view writes back unguarded (a value that differs)
 *   CARET_G  guarded: the view skips the write when the state's bytes are the bytes the handler last read */
#include <string.h>
#include "cdom_ui.h"
#include "cd_state.h"

#define EXPORT(n) __attribute__((export_name(n)))
#define CAP 256
static char value[CAP], last_read[CAP];
static uint32_t value_len, last_read_len, writes, skips;
static cd_state value_state;
static cd_region region;
static cd_id input;

static void value_set(const char *p, uint32_t n) {
  memcpy(value, p, n);
#ifdef CARET_C
  for (uint32_t i = 0; i < n; i++) if (value[i] >= 'a' && value[i] <= 'z') value[i] -= 32;
#endif
  value_len = n;
  cd_changed(&value_state);
}
static void on_input(const cd_event_rec *, void *) {
  uint32_t n;
  if (!cd_input_whole(input, last_read, CAP, &n)) n = cd_get_str(input, "value", last_read, CAP);
  last_read_len = n;
  value_set(last_read, n);
}
static void input_view(void *) {
  (void)last_read_len; /* read by the guard (CARET_G) only */
#ifdef CARET_G
  if (value_len == last_read_len && !memcmp(value, last_read, value_len)) { skips++; return; }
#endif
  writes++;
  cd_set_str(input, "value", cd_tmp(value, value_len));
}
void cd_main(void) {
  cd_at(CD_MOUNT, CD_APPEND);
  if (!cd_region_show(&region, 1)) return;
  cd_open("label"); cd_name_open(region.root);
  input = cd_open_id("input"); cd_close();
  cd_close();
  cd_on(input, CD_EV_INPUT, on_input, nullptr);
  cd_watch(&value_state, input_view, nullptr);
  input_view(nullptr);
  cd_region_end(&region);
}
EXPORT("writes") uint32_t writes_(void) { return writes; }
EXPORT("skips") uint32_t skips_(void) { return skips; }
