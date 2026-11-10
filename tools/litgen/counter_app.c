#include "cdom.h"

static uint32_t count;
static cd_id count_text;

static void on_click(const cd_event_rec *e, void *ctx) {
  (void)e; (void)ctx;
  cd_set_text_node_num(count_text, ++count);
}

void cd_main(void) {
  cd_at(CD_MOUNT, 0);
  cd_open("p");
  cd_text("Count: ");
  cd_text_num(0); cd_name_text(count_text = cd_id_new());
  cd_close();
  cd_id button = cd_open_id("button");
  cd_text("+1");
  cd_close();
  cd_on(button, CD_EV_CLICK, on_click, 0);
}
