/* E: the cd_state prototype, each row a view. The shared states and the small views are state_views.c;
 * here each row has a cd_state of its own (in the row's struct) and three intrusive watchers: of the
 * currency, of the selection, and of its own state, the three one view (cd_watch_at with the first as
 * view). The row's view compares what it drew, as B's does. */
#include "state_views.c"

typedef struct {
  cd_id tr, txt;
  uint32_t idx;
  cd_state st;                                  /* the row's own field; lives as long as the struct */
  cd_watcher w[3];
  uint32_t drawn_cur, drawn_price, drawn_sel;
} row_view_t;
static row_view_t *rv; /* by key - 1 */

void set_price(uint32_t i, uint32_t cents) {
  rows[i].price_cents = cents;
  rows[i].rev++;
  cd_changed(&rv[rows[i].id - 1].st);
}
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
  cd_watch_at(&r->w[0], &currency_state, row_view, r, nullptr);
  cd_watch_at(&r->w[1], &selected_state, row_view, r, &r->w[0]);
  cd_watch_at(&r->w[2], &r->st, row_view, r, &r->w[0]);
  return r->tr;
}
static void row_patch(void *, uint32_t, cd_item *) {}
static void row_destroy(void *, cd_item *it) {
  row_view_t *r = &rv[it->key - 1];
  cd_unwatch(&r->w[2]);
  cd_unwatch(&r->w[1]);
  cd_unwatch(&r->w[0]);
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
