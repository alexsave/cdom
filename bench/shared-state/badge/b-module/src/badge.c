#include "cdom_ui.h"
#include "parts.h"
#include "name.h"

static cd_region badge_region;
static cd_id hi;

/* Runs only when the name changed, so it writes every time it runs. */
static void badge_view(void *) {
  uint32_t n;
  const char *p = name_get(&n);
  char room[4 + NAME_CAP];
  cd_buf b = cd_buf_in(room, sizeof room);
  cd_buf_putz(&b, "Hi, ");
  cd_buf_put(&b, p, n);
  cd_set_text_node(hi, cd_tmp(b.p, b.len));
}

void badge_show(void) {
  if (!cd_region_show(&badge_region, 1)) return;
  cd_open("span"); cd_name_open(badge_region.root);
  cd_text(""); cd_name_text(hi = cd_id_new());
  cd_close();
  name_watch(badge_view, nullptr);
  badge_view(nullptr);
  cd_region_end(&badge_region);
}
