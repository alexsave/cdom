/* L: the cd_state prototype, the list one view. No per-row view, watcher or state: one rows_state for the
 * collection (any row setter, and any add or remove, changes it), and the table's region watches the
 * currency, the selection and rows_state with one view, rows_view, which runs cd_list_update over the
 * same keys. Its patch compares the row's revision, ((row's rev + the currency's version) << 1) | selected,
 * with the item's data, and writes the text when the upper bits differ and the class when the low bit
 * does. The shared states and the small views are state_views.c. */
#include "state_views.c"

#define EXPORT(n) __attribute__((export_name(n)))

static cd_state rows_state;
typedef struct { cd_id tr, txt; } row_view_t;
static row_view_t *rv; /* by key - 1 */

void set_price(uint32_t i, uint32_t cents) { rows[i].price_cents = cents; rows[i].rev++; cd_changed(&rows_state); }
void rows_changed(void) { cd_changed(&rows_state); }

static uint32_t row_rev(uint32_t i) { return ((rows[i].rev + currency_state.ver) << 1) | (rows[i].id == selected); }
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
const cd_list_fns row_fns = {nullptr, row_create, row_patch, row_destroy};

static void rows_view(void *) { cd_list_update(&list, keys, nrows, &row_fns); }
void table_built(void) {
    cd_watch(&currency_state, rows_view, nullptr);
    cd_watch(&selected_state, rows_view, nullptr);
    cd_watch(&rows_state, rows_view, nullptr);
    rows_view(nullptr);
}
void rows_reserve(uint32_t cap) {
    cd_free(rv);
    rv = cd_alloc(cap * sizeof *rv);
    __builtin_memset(rv, 0, cap * sizeof *rv);
}
uint32_t row_struct_size(void) { return sizeof(row_view_t); }

/* S13 (i): rows_view over rows of which none changed */
EXPORT("op_nochange") void op_nochange(void) { cd_changed(&rows_state); cd_flush(); }
