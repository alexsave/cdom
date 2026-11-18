#include "cdom_ui.h"
#include "live.h"

typedef struct { cd_render_fn fn; void *ctx; } live_view;
static live_view *live;
static uint32_t nlive, live_cap, *free_slots, nfree, free_cap;

static void run_all(void *) {
  for (uint32_t i = 0; i < nlive; i++)
    if (live[i].fn) live[i].fn(live[i].ctx);
}
uint32_t view_add(cd_render_fn fn, void *ctx) {
  uint32_t slot;
  if (nfree) {
    slot = free_slots[--nfree];
  } else {
    if (nlive == live_cap) {
      live_cap = live_cap ? live_cap * 2 : 64;
      live = cd_realloc(live, live_cap * sizeof *live);
    }
    slot = nlive++;
  }
  live[slot] = (live_view){fn, ctx};
  return slot;
}
void view_remove(uint32_t slot) {
  live[slot].fn = nullptr;
  if (nfree == free_cap) {
    free_cap = free_cap ? free_cap * 2 : 64;
    free_slots = cd_realloc(free_slots, free_cap * sizeof *free_slots);
  }
  free_slots[nfree++] = slot;
}
static void drop(void *slot) { view_remove(CD_CTX_INDEX(slot)); }
void view_add_here(cd_render_fn fn, void *ctx) { cd_on_free(nullptr, drop, CD_CTX_OF(view_add(fn, ctx))); }
void views_changed(void) { cd_dirty(run_all, nullptr); }
