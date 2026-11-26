/* The operations the page times, the same in every entry: each is an export, ends in cd_flush(), and
 * changes state on every call so that the work is done every time. Every random draw is cd_rand (the page
 * fixes cd_seed with cdom.start's options.seed), and every entry draws the same number, so all entries
 * build the same DOM. */
#include "app.h"

#define EXPORT(n) __attribute__((export_name(n)))
#define ROW_STRIDE 7919u /* odd and prime: op_row visits every row of 10,000 and of 100,000 */

static uint32_t base;       /* the ids are base + 1 .. base + n; the replaces alternate base 0 and n */
static uint32_t cap;        /* the per-row structs reserved, for keys 1 .. cap */
static uint32_t sel_at, row_at, field_at;

static uint32_t new_price(void) { return 100 + cd_rand() % 1000000; }
static void reserve(uint32_t n) {
    if (2 * n <= cap) return;
    if (list.n) __builtin_trap(); /* a live row's view points into the structs */
    cap = 2 * n;
    rows_reserve(cap);
}
/* rows[] and keys[] become n rows with ids base + 1 .. base + n and new prices. */
static void fill(uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        rows[i] = (row_t){.id = base + i + 1, .price_cents = new_price(), .rev = 0};
        keys[i] = rows[i].id;
    }
    nrows = n;
}
static void flip(void) { base = base ? 0 : nrows; }

EXPORT("setup") void setup(uint32_t n) { reserve(n); base = 0; fill(n); rows_changed(); cd_flush(); }
EXPORT("row_struct_size") uint32_t row_struct_size_(void) { return row_struct_size(); }
EXPORT("nrows") uint32_t nrows_(void) { return nrows; }

/* S1 */
EXPORT("op_currency") void op_currency(void) { set_currency((currency + 1) % 3); cd_flush(); }
/* S2: the next row's id */
EXPORT("op_select") void op_select(void) {
    sel_at = (sel_at + 1) % nrows;
    set_selected(rows[sel_at].id);
    cd_flush();
}
/* S3: one row's price, the row advancing by a fixed odd stride */
EXPORT("op_row") void op_row(void) {
    row_at = (row_at + ROW_STRIDE) % nrows;
    set_price(row_at, (rows[row_at].price_cents + 1 + cd_rand() % 997) % 1000000);
    cd_flush();
}
/* S4, S11: one field, k cycling */
EXPORT("op_field") void op_field(void) {
    uint32_t k = field_at++ % NF;
    set_field(k, f[k] + 1);
    cd_flush();
}
/* S5 */
EXPORT("op_fields") void op_fields(void) {
    for (uint32_t k = 0; k < NF; k++) set_field(k, f[k] + 1);
    cd_flush();
}
/* S6: the currency setter m times in one entry; m % 3 != 0 makes the last value differ from the first */
EXPORT("op_bulk") void op_bulk(uint32_t m) {
    for (uint32_t i = 0; i < m; i++) set_currency((currency + 1) % 3);
    cd_flush();
}
/* S7: from 0 rows to n, and back */
EXPORT("op_build") void op_build(uint32_t n) { reserve(n); base = 0; fill(n); rows_changed(); cd_flush(); }
EXPORT("op_clear") void op_clear(void) { nrows = 0; rows_changed(); cd_flush(); }
/* S8: one keystroke in the URL field */
EXPORT("op_draft") void op_draft(void) { draft_append((char)('a' + draft_len % 26)); cd_flush(); }
/* S9 (a1): to no rows, then to n rows of the other id range, in one entry */
EXPORT("op_replace_reset") void op_replace_reset(void) {
    uint32_t n = nrows;
    flip();
    nrows = 0;
    rows_changed();
    cd_render_now(); /* an entry whose list is a view (L) updates it now, so this is a clear and then a build */
    fill(n);
    rows_changed();
    cd_flush();
}
/* S9 (a2): one update straight to the other id range */
EXPORT("op_replace_disjoint") void op_replace_disjoint(void) { flip(); fill(nrows); rows_changed(); cd_flush(); }
/* S9 (b): the same ids, every price new, through the row setter */
EXPORT("op_replace_same") void op_replace_same(void) {
    for (uint32_t i = 0; i < nrows; i++) set_price(i, new_price());
    cd_flush();
}
/* S9 (d): the table's region taken away (its cleanup forgets the list) and built again on the new rows */
EXPORT("op_replace_region") void op_replace_region(void) {
    cd_region_hide(&table_region);
    flip();
    fill(nrows);
    table_show();
    cd_flush();
}
/* S10: the textarea's whole value into big, and the setter */
EXPORT("op_big") uint32_t op_big(void) {
    uint32_t len;
    if (!cd_input_whole(textarea, big, BIG_CAP, &len)) __builtin_trap();
    set_big(len);
    cd_flush();
    return len;
}
/* S12: n distinct asks of cdom's render queue, each an empty function, then the flush */
static void empty_fn(void *) {}
EXPORT("op_dirty") void op_dirty(uint32_t n) {
    for (uint32_t i = 0; i < n; i++) cd_dirty(empty_fn, CD_CTX_OF(i));
    cd_flush();
}
/* S13 (ii): a stand-in for the cd_list_row pass (docs/list-design.md): per row one call that compares the
 * row's (key, rev) with what a cursor into the drawn rows holds, and advances the cursor. */
typedef struct { uint64_t key, rev; cd_id node, ids; } drawn_row;
_Static_assert(sizeof(drawn_row) == 24, "the stand-in's row is 24 bytes");
static drawn_row *drawn;
static uint32_t ndrawn;
static const drawn_row *cursor;
__attribute__((noinline)) int row_same(uint64_t key, uint64_t rev) {
    const drawn_row *d = cursor++;
    return d->key == key && d->rev == rev;
}
EXPORT("op_row_same") uint32_t op_row_same(void) {
    if (ndrawn != nrows) {
        drawn = cd_realloc(drawn, (nrows ? nrows : 1) * sizeof *drawn);
        for (uint32_t i = 0; i < nrows; i++) drawn[i] = (drawn_row){.key = rows[i].id, .rev = rows[i].rev};
        ndrawn = nrows;
    }
    cursor = drawn;
    uint32_t same = 0;
    for (uint32_t i = 0; i < nrows; i++) same += row_same(rows[i].id, rows[i].rev);
    cd_flush();
    return same;
}
