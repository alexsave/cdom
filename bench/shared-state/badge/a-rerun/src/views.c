#include "cdom_ui.h"
#include "views.h"

#define VIEW_CAP 64
static view_fn live[VIEW_CAP];

static void run_all(void *) {
  for (uint32_t i = 0; i < VIEW_CAP; i++) if (live[i]) live[i]();
}
static void view_drop(void *slot) { live[CD_CTX_INDEX(slot)] = nullptr; }

void view_add(view_fn fn) {
  uint32_t i = 0;
  while (live[i]) i++;
  live[i] = fn;
  cd_on_free(nullptr, view_drop, CD_CTX_OF(i)); /* nullptr: the region building now */
}
void views_changed(void) { cd_dirty(run_all, nullptr); }
