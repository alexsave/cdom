/* Signals: one setter patches exactly the bindings that read its field, and a dev build reports
 * what was written past the setters. Built twice by tests/cdtc/run.sh with -DCDOM_DEV=1 (the
 * generated code then counts its binding compares in page_dev_compares and the list items it looked
 * at in page_dev_visits):
 *   -DROWS: the templated rows app (bench/cdom-tpl) on 1000 rows: DOM calls and compares per write;
 *   else:   fixtures/signals.cdt: two dirty words on the page, derived values in a chain and from a
 *           list's length, a list-wide binding whose state bit is in the second word.
 * Every step also checks that a render redoing everything (mark_all) then makes no call. */
#include "cdom_html.h"
#include <stdio.h>
#include <string.h>

static int fails;
static uint64_t ops0, warns0;
static uint32_t cmp0, vis0;

#ifdef ROWS
#include "rows.h"
#define ST S
#define MARK_ALL Rows_mark_all
#else
#include "signals.h"
#define ST G
#define MARK_ALL Sig_mark_all
void cd_on_event(const cd_event_rec *e) { (void)e; }
void cd_main(void) {
  page_init();
  page_create(&G);
  page_render(&G);
}
#endif

static void start(void) {
  cd_flush();
  ops0 = cd_html_ops();
  cmp0 = page_dev_compares;
  vis0 = page_dev_visits;
  warns0 = cd_html_dev_count();
}

/* since start(): exactly `ops` DOM calls, `cmps` binding compares and `vis` list items looked at;
 * then a full render finds nothing left to do */
static void expectv(const char *what, uint64_t ops, uint32_t cmps, uint32_t vis) {
  cd_flush();
  uint64_t o = cd_html_ops() - ops0;
  uint32_t c = page_dev_compares - cmp0, v = page_dev_visits - vis0;
  int good = o == ops && c == cmps && v == vis && cd_html_dev_count() == warns0;
  printf("%s %s: %llu calls, %u compares, %u items visited (want %llu, %u, %u)%s%s\n", good ? "ok  " : "FAIL", what,
         (unsigned long long)o, c, v, (unsigned long long)ops, cmps, vis, cd_html_dev_count() != warns0 ? "; dev warned: " : "",
         cd_html_dev_count() != warns0 ? cd_html_dev_last() : "");
  fails += !good;
  uint64_t before = cd_html_ops();
  MARK_ALL(&ST);
  page_render(&ST);
  cd_flush();
  if (cd_html_ops() != before) {
    fails++;
    printf("FAIL %s: a full render after it made %llu calls\n", what, (unsigned long long)(cd_html_ops() - before));
  }
}

/* a dev build names the field a render found written without its setter */
static void expect_warn(const char *what, const char *msg) {
  cd_flush();
  int good = cd_html_dev_count() == warns0 + 1 && !strcmp(cd_html_dev_last(), msg);
  printf("%s %s: %llu warning(s), last \"%s\"\n", good ? "ok  " : "FAIL", what, (unsigned long long)(cd_html_dev_count() - warns0),
         cd_html_dev_last());
  fails += !good;
  MARK_ALL(&ST); /* the app's own repair: redo everything once */
  page_render(&ST);
}

#ifdef ROWS
extern uint32_t cd_seed;
static int node_has(uint32_t row_id, const char *text) { /* the row's <tr> holds text */
  char b[512];
  for (uint32_t i = 0; i < 1u << 16; i++) {
    uint32_t n = cd_html_node(i, b, sizeof b - 1);
    if (!n || n >= sizeof b) continue;
    b[n] = 0;
    char id[32];
    snprintf(id, sizeof id, "<td class=\"col-md-1\">%u</td>", row_id);
    if (!strncmp(b, "<tr", 3) && strstr(b, id)) return strstr(b, text) != 0;
  }
  return 0;
}
#endif

int main(void) {
  cd_html_reset();
#ifdef ROWS
  cd_seed = 42;
  cd_main();
  cd_event_rec e = {CD_EV_CLICK, 0, 0, 0, 0, 0, 0, 0, 0};
  run(&S, &e);
  page_render(&S);
  /* a key change recreates the row; the select right after must not find marks left on it */
  start(); Row_set_label(&S.rows[0], cd_internz("new key")); Row_set_id(&S.rows[0], 77777); page_render(&S);
  Rows_set_selected(&S, S.rows[4].id); page_render(&S);
  expectv("a key change (remove; at, 2 slots, clone, listen, close: the 999 others visited by the keyed update), "
          "then a select (one set_class; the class binding compared on every row and nothing else)", 7 + 1, 1000, 999 + 1000);
  start(); Rows_set_selected(&S, S.rows[6].id); page_render(&S);
  expectv("selecting another: exactly 2 set_class calls, 1000 compares of that one binding", 2, 1000, 1000);
  printf("%s the new row is danger and the old one is not\n",
         node_has(S.rows[6].id, "class=\"danger\"") && !node_has(S.rows[4].id, "class=\"danger\"") ? "ok  " : "FAIL");
  fails += !(node_has(S.rows[6].id, "class=\"danger\"") && !node_has(S.rows[4].id, "class=\"danger\""));
  start(); Row_set_label(&S.rows[3], cd_internz("relabelled")); page_render(&S);
  expectv("one label: one call, one compare, one row visited", 1, 1, 1);
  start(); Row_set_label(&S.rows[3], cd_internz("relabelled")); page_render(&S);
  expectv("the same label again: the setter marks nothing", 0, 0, 0);
  start(); Rows_set_rng(&S, 7); page_render(&S);
  expectv("a field no binding reads: nothing", 0, 0, 0);
  start(); page_render(&S);
  expectv("a render with nothing marked: nothing", 0, 0, 0);
  start(); Rows_rows_swap(&S, 1, 998); page_render(&S);
  expectv("swap: two moves, no compare (the keyed update visits every row)", 2, 0, 1000);
  start(); update(&S, &e); page_render(&S);
  expectv("update every 10th (100 queued rows): 100 calls, 100 compares, 100 rows visited", 100, 100, 100);
  start(); Row_set_label(&S.rows[3], cd_internz("once")); page_render(&S); Row_set_label(&S.rows[3], cd_internz("twice"));
  page_render(&S);
  expectv("one row in two renders: it is queued again in the second", 2, 2, 2);
  start();
  for (uint32_t i = 0; i < 1000; i += 5) Row_set_label(&S.rows[i], cd_internz(i & 1 ? "odd" : "even"));
  page_render(&S);
  expectv("200 labels, past the queue: every row visited, only the 200 compared", 200, 200, 1000);
  start(); Rows_rows_remove(&S, 10); Row_set_label(&S.rows[20], cd_internz("after a remove")); page_render(&S);
  expectv("a remove and a label in one render: the remove and the label, no other compare (999 rows left, all visited by the keyed update)", 2, 1, 999);
  /* the dev build's reports */
  start(); S.selected = S.rows[2].id; page_render(&S);
  expect_warn("a direct write to Rows.selected", "state written without a setter: Rows.selected");
  start(); S.rows[8].label = cd_internz("direct"); page_render(&S);
  expect_warn("a direct write to a row's label", "state written without a setter: Row.label");
  start(); { Row t = S.rows[0]; S.rows[0] = S.rows[1]; S.rows[1] = t; } page_render(&S);
  expect_warn("a reorder without a mark", "list changed without a setter: Rows.rows");
  start(); S.rows[9].label = cd_internz("marked"); Row_mark_label(&S.rows[9]); page_render(&S);
  expectv("a direct write and its mark: one call, no warning", 1, 1, 1);
#else
  for (uint32_t i = 0; i < 3; i++) { It *r = Sig_items_push(&G); r->id = i + 1; r->v = i; }
  G.note = cd_internz("note");
  cd_main();
  start(); Sig_set_a5(&G, 5); page_render(&G);
  expectv("a field in the first word: one call, one compare", 1, 1, 0);
  start(); Sig_set_a38(&G, 6); page_render(&G);
  expectv("a field in the second word: one compare; its text is 40 steps into the <p>: two walks, then the value", 3, 1, 0);
  start(); Sig_set_a0(&G, 1); page_render(&G);
  expectv("an input of a derived chain: a0, total and twice", 3, 3, 0);
  start(); Sig_set_a0(&G, 0); Sig_set_a39(&G, 1); page_render(&G);
  expectv("two inputs whose derived sum is unchanged: a0 and a39 (two walks and a value) only", 4, 2, 0);
  start(); Sig_set_x(&G, 2); page_render(&G);
  expectv("a list-wide binding (state bit in the second word): x compares every item's class (two change) and the derived str; "
          "the 3 items visited", 3, 4, 3);
  start(); It *r = Sig_items_push(&G); r->id = 9; r->v = 2; page_render(&G);
  expectv("a push: the item (2 slots, clone, class, close) and the derived count; the keyed update visits the 3 others", 6, 1, 3);
  start(); Sig_set_note(&G, cd_internz("other")); page_render(&G);
  expectv("a str input of a derived str", 1, 1, 0);
  start(); It_set_v(&G.items[1], 2); page_render(&G);
  expectv("an item field: its text and its class, one item visited", 2, 2, 1);
  start(); G.a7 = 3; page_render(&G);
  expect_warn("a direct write in the first word", "state written without a setter: Sig.a7");
#endif
  printf(fails ? "%d FAILED\n" : "all ok\n", fails);
  return fails != 0;
}
