/* Micro: n rows of div > span > text built with cd_open/cd_close (no templates), then cleared.
 * Built twice by run.sh: with the namespace kept in C (open_svg import) and with the port's
 * JS tag compare per open (OPEN_JS: every open goes to one import that checks the tag). */
#include "cdom.h"
void cd_on_event(const cd_event_rec *e) { (void)e; }
static cd_id box;
void cd_main(void) {
  cd_at(CD_MOUNT, 0);
  cd_open(cd_internz("div")); cd_name_open(box = cd_id_new()); cd_close();
}
__attribute__((export_name("rows"))) void rows(uint32_t n) {
  cd_str D = CD_LIT("div"), S = CD_LIT("span"), T = CD_LIT("x");
  cd_at(box, 0);
  for (uint32_t i = 0; i < n; i++) { cd_open(D); cd_open(S); cd_text(T); cd_close(); cd_close(); }
  cd_clear(box);
  cd_flush();
}
