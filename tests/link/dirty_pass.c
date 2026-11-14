/* The render pass costs an app only when it asks for a render: built as is, this app never calls
 * cd_dirty, and with -DASKS it does. run.sh checks the second is larger by at least the pass, so a
 * change that links the pass into every app (the volatile pointer in cdom.c's renders section
 * dropped) fails here, not silently. */
#include "../../src/cdom.h"

static cd_id txt;
static uint32_t n;
static void view(void *ctx) { (void)ctx; cd_set_text_node_num(txt, n); }
static void click(const cd_event_rec *e, void *ctx) {
  (void)e; (void)ctx; n++;
#ifdef ASKS
  cd_dirty(view, 0);
#else
  view(0);
#endif
}
void cd_main(void) {
  cd_at(CD_MOUNT, 0);
  cd_id b = cd_open_id(CD_LIT("button"));
  cd_text_num(0); cd_name_text(txt = cd_id_new());
  cd_close();
  cd_on(b, CD_EV_CLICK, click, 0);
}
