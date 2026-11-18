/* B: the owner keeps the watchers, as badge/b-module/src/name.c is written: each field's owner keeps a
 * table of (fn, ctx) slots, grown to fit, a watcher takes the first free slot (a linear search), the
 * setter asks cdom for every used slot (cd_dirty), and a watcher that goes is cd_undirty'd and cleared.
 * Tables: the currency, the selection, each f[k], the draft, the big text; each row's own field has one
 * slot inside the row. A view writes directly; a row's view compares what it drew. */
#include <string.h>
#include "app.h"

typedef struct { cd_render_fn fn; void *ctx; } watcher;
typedef struct { watcher *w; uint32_t cap; } table;
static uint32_t table_add(table *t, cd_render_fn fn, void *ctx) {
  uint32_t i = 0;
  while (i < t->cap && t->w[i].fn) i++;
  if (i == t->cap) {
    uint32_t n = t->cap ? t->cap * 2 : 16;
    t->w = cd_realloc(t->w, n * sizeof *t->w);
    memset(t->w + t->cap, 0, (n - t->cap) * sizeof *t->w);
    t->cap = n;
  }
  t->w[i] = (watcher){fn, ctx};
  return i;
}
static void table_remove(table *t, uint32_t i) {
  cd_undirty(t->w[i].fn, t->w[i].ctx); /* a render already asked for must not run on a view that went */
  t->w[i].fn = nullptr;
}
static void table_changed(const table *t) {
  for (uint32_t i = 0; i < t->cap; i++)
    if (t->w[i].fn) cd_dirty(t->w[i].fn, t->w[i].ctx);
}
static table currency_t, selected_t, f_t[NF], draft_t, big_t;

/* ---- the rows ---- */
typedef struct {
  cd_id tr, txt;
  uint32_t idx, cur_slot, sel_slot;
  watcher own;                                  /* the row's own field's table: one slot */
  uint32_t drawn_cur, drawn_price, drawn_sel;
} row_view_t;
static row_view_t *rv; /* by key - 1 */

void set_currency(uint32_t c) { currency = c; table_changed(&currency_t); }
void set_selected(uint32_t id) { selected = id; table_changed(&selected_t); }
void set_price(uint32_t i, uint32_t cents) {
  rows[i].price_cents = cents;
  rows[i].rev++;
  row_view_t *r = &rv[rows[i].id - 1];
  if (r->own.fn) cd_dirty(r->own.fn, r->own.ctx);
}
void set_field(uint32_t k, uint32_t v) { f[k] = v; table_changed(&f_t[k]); }
void draft_append(char c) {
  if (draft_len == DRAFT_WRAP) draft_len = 0;
  draft[draft_len++] = c;
  table_changed(&draft_t);
}
void set_big(uint32_t len) { big_len = len; table_changed(&big_t); }

static void row_view(void *ctx) {
  row_view_t *r = ctx;
  const row_t *m = &rows[r->idx];
  if (currency != r->drawn_cur || m->price_cents != r->drawn_price) {
    row_write_text(r->txt, r->idx);
    r->drawn_cur = currency;
    r->drawn_price = m->price_cents;
  }
  uint32_t sel = m->id == selected;
  if (sel != r->drawn_sel) {
    cd_set_class(r->tr, sel_cls[sel]);
    r->drawn_sel = sel;
  }
}
static cd_id row_create(void *, uint32_t i, uint32_t *) {
  row_view_t *r = &rv[rows[i].id - 1];
  r->idx = i;
  r->tr = row_build(i, &r->txt, 1);
  r->drawn_cur = currency;
  r->drawn_price = rows[i].price_cents;
  r->drawn_sel = rows[i].id == selected;
  r->cur_slot = table_add(&currency_t, row_view, r);
  r->sel_slot = table_add(&selected_t, row_view, r);
  r->own = (watcher){row_view, r};
  return r->tr;
}
static void row_patch(void *, uint32_t, cd_item *) {}
static void row_destroy(void *, cd_item *it) {
  row_view_t *r = &rv[it->key - 1];
  table_remove(&currency_t, r->cur_slot);
  table_remove(&selected_t, r->sel_slot);
  cd_undirty(r->own.fn, r->own.ctx);
  r->own.fn = nullptr;
  cd_id_free(r->txt);
}
const cd_list_fns row_fns = {nullptr, row_create, row_patch, row_destroy};
void rows_reserve(uint32_t cap) {
  cd_free(rv);
  rv = cd_alloc(cap * sizeof *rv);
  memset(rv, 0, cap * sizeof *rv);
}
void rows_changed(void) { cd_list_update(&list, keys, nrows, &row_fns); }
void table_built(void) { rows_changed(); }
uint32_t row_struct_size(void) { return sizeof(row_view_t); }

/* ---- the leaves, the draft's length and the mirror: each takes a slot while its region builds, and
 * gives it back when the region goes ---- */
typedef struct { table *t; uint32_t slot; } held;
static void give_back(void *ctx) { held *h = ctx; table_remove(h->t, h->slot); }
static void watch_here(held *h, table *t, cd_render_fn fn, void *ctx) {
  h->t = t;
  h->slot = table_add(t, fn, ctx);
  cd_on_free(nullptr, give_back, h);
}
typedef struct { cd_id txt; uint32_t k; held h; } leaf_view_t;
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
  watch_here(&l->h, &f_t[k], leaf_view, l);
  leaf_view(l);
}
static cd_id draft_txt, mirror_txt;
static held draft_h, mirror_h;
static void draft_view(void *) {
  char room[16];
  cd_set_text_node(draft_txt, cd_tmp(room, draft_text(room, sizeof room)));
}
void draft_built(cd_id txt) { draft_txt = txt; watch_here(&draft_h, &draft_t, draft_view, nullptr); draft_view(nullptr); }
static void mirror_view(void *) { cd_set_text_node(mirror_txt, cd_tmp(big, big_len)); }
void mirror_built(cd_id txt) { mirror_txt = txt; watch_here(&mirror_h, &big_t, mirror_view, nullptr); mirror_view(nullptr); }
