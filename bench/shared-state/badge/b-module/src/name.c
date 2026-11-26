#include <string.h>
#include "cdom_ui.h"
#include "name.h"

#define WATCH_CAP 16
static char name[NAME_CAP];
static uint32_t name_len;
static struct { cd_render_fn fn; void *ctx; } watch[WATCH_CAP];

const char *name_get(uint32_t *len) { *len = name_len; return name; }

void name_set(const char *p, uint32_t len) {
    memcpy(name, p, len);
    name_len = len;
    for (uint32_t i = 0; i < WATCH_CAP; i++) if (watch[i].fn) cd_dirty(watch[i].fn, watch[i].ctx);
}

static void unwatch(void *slot) {
    uint32_t i = CD_CTX_INDEX(slot);
    cd_undirty(watch[i].fn, watch[i].ctx); /* a render already asked for must not run on a view that went */
    watch[i].fn = nullptr;
}

void name_watch(cd_render_fn fn, void *ctx) {
    uint32_t i = 0;
    while (watch[i].fn) i++;
    watch[i].fn = fn;
    watch[i].ctx = ctx;
    cd_on_free(nullptr, unwatch, CD_CTX_OF(i));
}
