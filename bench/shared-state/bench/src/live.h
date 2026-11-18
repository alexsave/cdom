/* A's and C's one list of live views: any setter asks for all of them (views_changed), and each runs.
 * Adding and removing are O(1): a removed slot goes on a free list and is taken again first. */
#pragma once
#include "cdom.h"
uint32_t view_add(cd_render_fn fn, void *ctx); /* returns the slot, for view_remove */
void view_remove(uint32_t slot);
void view_add_here(cd_render_fn fn, void *ctx); /* while a region builds: removed when it goes (cd_on_free) */
void views_changed(void);                       /* one cd_dirty(run_all, nullptr) */
