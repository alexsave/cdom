/* E's and L's states and small views (included by entry_e.c and entry_l.c): each state is a cd_state
 * beside its data (the prototype, ../../lib/cd_state.h; or, built with -DCD_STATE_REAL for the entries EI and
 * LI, cdom's own in src/cdom.h), its setter ends in cd_changed, and a view watches what it draws from
 * while its region builds (cd_watch, gone with the region) and draws once. */
#include "app.h"
#ifndef CD_STATE_REAL
#include "cd_state.h"
#endif

static cd_state currency_state, selected_state, f_state[NF], draft_state, big_state;

void set_currency(uint32_t c) { currency = c; cd_changed(&currency_state); }
void set_selected(uint32_t id) { selected = id; cd_changed(&selected_state); }
void set_field(uint32_t k, uint32_t v) { f[k] = v; cd_changed(&f_state[k]); }
void draft_append(char c) {
    if (draft_len == DRAFT_WRAP) draft_len = 0;
    draft[draft_len++] = c;
    cd_changed(&draft_state);
}
void set_big(uint32_t len) { big_len = len; cd_changed(&big_state); }

typedef struct { cd_id txt; uint32_t k; } leaf_view_t;
static leaf_view_t leaves[LEAVES];
static void leaf_view(void *ctx) {
    leaf_view_t *l = ctx;
    char room[24];
    cd_set_text_node(l->txt, cd_tmp(room, leaf_text(room, sizeof room, l->k)));
}
void leaf_built(uint32_t leaf, uint32_t k, cd_id txt) {
    leaf_view_t *l = &leaves[leaf];
    l->txt = txt;
    l->k = k;
    cd_watch(&f_state[k], leaf_view, l);
    leaf_view(l);
}
static cd_id draft_txt, mirror_txt;
static void draft_view(void *) {
    char room[16];
    cd_set_text_node(draft_txt, cd_tmp(room, draft_text(room, sizeof room)));
}
void draft_built(cd_id txt) { draft_txt = txt; cd_watch(&draft_state, draft_view, nullptr); draft_view(nullptr); }
static void mirror_view(void *) { cd_set_text_node(mirror_txt, cd_tmp(big, big_len)); }
void mirror_built(cd_id txt) { mirror_txt = txt; cd_watch(&big_state, mirror_view, nullptr); mirror_view(nullptr); }
