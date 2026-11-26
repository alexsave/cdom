/* A: re-run every live view. One list of live views (live.c); every setter of any state calls
 * views_changed(), so every view runs after any change, formats its value every time, and writes it
 * through a cd_bound, which sends it only when it differs from what it sent last. Each row is a view. */
#include "app.h"
#include "live.h"

void set_currency(uint32_t c) { currency = c; views_changed(); }
void set_selected(uint32_t id) { selected = id; views_changed(); }
void set_price(uint32_t i, uint32_t cents) { rows[i].price_cents = cents; rows[i].rev++; views_changed(); }
void set_field(uint32_t k, uint32_t v) { f[k] = v; views_changed(); }
void draft_append(char c) {
    if (draft_len == DRAFT_WRAP) draft_len = 0;
    draft[draft_len++] = c;
    views_changed();
}
void set_big(uint32_t len) { big_len = len; views_changed(); }

/* ---- the rows: each a live view, added in create and removed in destroy ---- */
typedef struct { cd_id tr, txt; uint32_t idx, slot; cd_bound text, cls; } row_view_t;
static row_view_t *rv; /* by key - 1 */

static void row_view(void *ctx) {
    row_view_t *r = ctx;
    const row_t *m = &rows[r->idx];
    char room[32];
    cd_bound_set_bytes(&r->text, room, price_text(room, sizeof room, m));
    cd_bound_set(&r->cls, sel_cls[m->id == selected]);
}
static cd_id row_create(void *, uint32_t i, uint32_t *) {
    row_view_t *r = &rv[rows[i].id - 1];
    r->idx = i;
    r->tr = row_build(i, &r->txt, 0);
    cd_bind_text(&r->text, r->txt);
    cd_bind_class(&r->cls, r->tr);
    cd_bound_shows(&r->cls, sel_cls[rows[i].id == selected]); /* row_build set it */
    r->slot = view_add(row_view, r);
    row_view(r);
    return r->tr;
}
static void row_patch(void *, uint32_t, cd_item *) {}
static void row_destroy(void *, cd_item *it) {
    row_view_t *r = &rv[it->key - 1];
    view_remove(r->slot);
    cd_unbind(&r->text);
    cd_unbind(&r->cls);
    cd_id_free(r->txt);
}
const cd_list_fns row_fns = {nullptr, row_create, row_patch, row_destroy};
void rows_reserve(uint32_t cap) {
    cd_free(rv);
    rv = cd_alloc(cap * sizeof *rv);
    __builtin_memset(rv, 0, cap * sizeof *rv);
}
void rows_changed(void) { cd_list_update(&list, keys, nrows, &row_fns); }
void table_built(void) { rows_changed(); }
uint32_t row_struct_size(void) { return sizeof(row_view_t); }

/* ---- the 1,000 leaves, the draft's length and the mirror: views of the region that builds them ---- */
typedef struct { cd_bound b; uint32_t k; } leaf_view_t;
static leaf_view_t leaves[LEAVES];
static void leaf_view(void *ctx) {
    leaf_view_t *l = ctx;
    char room[24];
    cd_bound_set_bytes(&l->b, room, leaf_text(room, sizeof room, l->k));
}
void leaf_built(uint32_t leaf, uint32_t k, cd_id txt) {
    leaf_view_t *l = &leaves[leaf];
    l->k = k;
    cd_bind_text(&l->b, txt); /* unbound by cdom when the region goes */
    view_add_here(leaf_view, l);
    leaf_view(l);
}
static cd_bound draft_b, mirror_b;
static void draft_view(void *) {
    char room[16];
    cd_bound_set_bytes(&draft_b, room, draft_text(room, sizeof room));
}
void draft_built(cd_id txt) { cd_bind_text(&draft_b, txt); view_add_here(draft_view, nullptr); draft_view(nullptr); }
static void mirror_view(void *) { cd_bound_set_bytes(&mirror_b, big, big_len); }
void mirror_built(cd_id txt) { cd_bind_text(&mirror_b, txt); view_add_here(mirror_view, nullptr); mirror_view(nullptr); }
