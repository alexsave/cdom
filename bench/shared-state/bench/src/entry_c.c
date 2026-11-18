/* C: versions and one list of live views. A's live list and views_changed(), but every state has a
 * version its setter moves on (the selection's value is its own version), and each view first compares
 * the versions it drew with the versions now, returns when they are equal ("304 Not Modified"), and
 * otherwise writes directly only the part that changed.
 * ENTRY_CL is C with the list as one live view instead of a view per row: the list's view compares one
 * version of the results first, and only then loops over the rows comparing each row's revision. */
#include "app.h"
#include "live.h"

static uint32_t currency_ver, f_ver[NF], draft_ver, big_ver;
#ifdef ENTRY_CL
static uint32_t rows_ver;
#endif

void set_currency(uint32_t c) { currency = c; currency_ver++; views_changed(); }
void set_selected(uint32_t id) { selected = id; views_changed(); }
void set_price(uint32_t i, uint32_t cents) {
  rows[i].price_cents = cents;
  rows[i].rev++;
#ifdef ENTRY_CL
  rows_ver++;
#endif
  views_changed();
}
void set_field(uint32_t k, uint32_t v) { f[k] = v; f_ver[k]++; views_changed(); }
void draft_append(char c) {
  if (draft_len == DRAFT_WRAP) draft_len = 0;
  draft[draft_len++] = c;
  draft_ver++;
  views_changed();
}
void set_big(uint32_t len) { big_len = len; big_ver++; views_changed(); }

#ifndef ENTRY_CL
/* ---- C: the rows, each a live view ---- */
typedef struct { cd_id tr, txt; uint32_t idx, slot, drawn_sum, drawn_sel; } row_view_t;
static row_view_t *rv; /* by key - 1 */

static void row_view(void *ctx) {
  row_view_t *r = ctx;
  const row_t *m = &rows[r->idx];
  uint32_t sum = currency_ver + m->rev, sel = m->id == selected;
  if (sum == r->drawn_sum && sel == r->drawn_sel) return;
  if (sum != r->drawn_sum) { row_write_text(r->txt, r->idx); r->drawn_sum = sum; }
  if (sel != r->drawn_sel) { cd_set_class(r->tr, sel_cls[sel]); r->drawn_sel = sel; }
}
static cd_id row_create(void *, uint32_t i, uint32_t *) {
  row_view_t *r = &rv[rows[i].id - 1];
  r->idx = i;
  r->tr = row_build(i, &r->txt, 1);
  r->drawn_sum = currency_ver + rows[i].rev;
  r->drawn_sel = rows[i].id == selected;
  r->slot = view_add(row_view, r);
  return r->tr;
}
static void row_patch(void *, uint32_t, cd_item *) {}
static void row_destroy(void *, cd_item *it) {
  row_view_t *r = &rv[it->key - 1];
  view_remove(r->slot);
  cd_id_free(r->txt);
}
void rows_changed(void) { cd_list_update(&list, keys, nrows, &row_fns); }
void table_built(void) { rows_changed(); }
#else
/* ---- CL: the list is one live view; a row is an item whose data is its revision ---- */
typedef struct { cd_id tr, txt; } row_view_t;
static row_view_t *rv; /* by key - 1 */
static uint32_t drawn_cur, drawn_sel, drawn_rows;

static uint32_t row_rev(uint32_t i) { return ((rows[i].rev + currency_ver) << 1) | (rows[i].id == selected); }
static cd_id row_create(void *, uint32_t i, uint32_t *data) {
  row_view_t *r = &rv[rows[i].id - 1];
  r->tr = row_build(i, &r->txt, 1);
  *data = row_rev(i);
  return r->tr;
}
static void row_patch(void *, uint32_t i, cd_item *it) {
  uint32_t rev = row_rev(i);
  if (rev == it->data) return;
  row_view_t *r = &rv[it->key - 1];
  if ((rev ^ it->data) >> 1) row_write_text(r->txt, i);
  if ((rev ^ it->data) & 1) cd_set_class(r->tr, sel_cls[rev & 1]);
  it->data = rev;
}
static void row_destroy(void *, cd_item *it) { cd_id_free(rv[it->key - 1].txt); }
static void list_draw(void) {
  drawn_cur = currency_ver;
  drawn_sel = selected;
  drawn_rows = rows_ver;
  cd_list_update(&list, keys, nrows, &row_fns);
}
static void list_view(void *) {
  if (currency_ver == drawn_cur && selected == drawn_sel && rows_ver == drawn_rows) return;
  list_draw();
}
void rows_changed(void) { rows_ver++; views_changed(); }
void table_built(void) { view_add_here(list_view, nullptr); list_draw(); }
#endif
const cd_list_fns row_fns = {nullptr, row_create, row_patch, row_destroy};
void rows_reserve(uint32_t cap) {
  cd_free(rv);
  rv = cd_alloc(cap * sizeof *rv);
  __builtin_memset(rv, 0, cap * sizeof *rv);
}
uint32_t row_struct_size(void) { return sizeof(row_view_t); }

/* ---- the leaves, the draft's length and the mirror ---- */
typedef struct { cd_id txt; uint32_t k, drawn; } leaf_view_t;
static leaf_view_t leaves[LEAVES];
static void leaf_draw(leaf_view_t *l) {
  char room[24];
  l->drawn = f_ver[l->k];
  cd_set_text_node(l->txt, cd_tmp(room, leaf_text(room, sizeof room, l->k)));
}
static void leaf_view(void *ctx) {
  leaf_view_t *l = ctx;
  if (l->drawn != f_ver[l->k]) leaf_draw(l);
}
void leaf_built(uint32_t leaf, uint32_t k, cd_id txt) {
  leaf_view_t *l = &leaves[leaf];
  l->txt = txt;
  l->k = k;
  view_add_here(leaf_view, l);
  leaf_draw(l);
}
static cd_id draft_txt, mirror_txt;
static uint32_t draft_drawn, mirror_drawn;
static void draft_draw(void) {
  char room[16];
  draft_drawn = draft_ver;
  cd_set_text_node(draft_txt, cd_tmp(room, draft_text(room, sizeof room)));
}
static void draft_view(void *) { if (draft_drawn != draft_ver) draft_draw(); }
void draft_built(cd_id txt) { draft_txt = txt; view_add_here(draft_view, nullptr); draft_draw(); }
static void mirror_draw(void) { mirror_drawn = big_ver; cd_set_text_node(mirror_txt, cd_tmp(big, big_len)); }
static void mirror_view(void *) { if (mirror_drawn != big_ver) mirror_draw(); }
void mirror_built(cd_id txt) { mirror_txt = txt; view_add_here(mirror_view, nullptr); mirror_draw(); }
