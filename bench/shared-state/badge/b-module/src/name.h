#pragma once
#include "cdom.h"
#define NAME_CAP 256
const char *name_get(uint32_t *len);
void name_set(const char *p, uint32_t len);   /* stores it, then asks every watcher to render */
void name_watch(cd_render_fn fn, void *ctx);  /* call while the view's region builds; unwatched when it goes */
