/* Events in lists inside item templates reach the right record: fixtures/nested.cdt is built
 * natively with cdom.c and the C sink; every listening node on every level (column headings,
 * cards, tags) gets a click, and the record its handler receives must be the one that node
 * shows (names are unique, and the node's HTML from the sink must hold the record's name).
 * Again after the lists are reordered and a column removed, so finds take the key search, not
 * the cached index; each time the page's HTML must also show every level in the state's order
 * (a patched item updates its own lists). Built and run by tests/cdtc/run.sh. */
#include "nested.h"
#include "cdom_html.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static cd_str got; /* the name of the record the last handler received */
void tag_click(Board *S, Tag *g, const cd_event_rec *e) { (void)S; (void)e; got = g->name; }
void card_click(Board *S, Card *k, const cd_event_rec *e) { (void)S; (void)e; got = k->text; }
void col_click(Board *S, Col *c, const cd_event_rec *e) { (void)S; (void)e; got = c->title; }
void cd_on_event(const cd_event_rec *e) { page_dispatch(e); }

static cd_str name(const char *k, uint32_t id) {
  char b[32];
  snprintf(b, sizeof b, "%s%u", k, id);
  return cd_internz(b);
}

void cd_main(void) {
  uint32_t id = 1;
  for (uint32_t i = 0; i < 3; i++) {
    Col *c = Board_cols_push(&B);
    c->id = id++;
    c->title = name("col", c->id);
    for (uint32_t j = 0; j < 3 + i; j++) {
      Card *k = Col_cards_push(c);
      k->id = id++;
      k->text = name("card", k->id);
      k->done = j & 1;
      for (uint32_t t = 0; t < j; t++) {
        Tag *g = Card_tags_push(k);
        g->id = id++;
        g->name = name("tag", g->id);
      }
    }
  }
  page_init();
  page_create(&B);
  page_render(&B); /* page_create builds the page; the lists fill on the first render */
}

static int fails;
static uint32_t clicks(const char *when) {
  uint32_t n = 0, bad = 0;
  char html[4096], want[64];
  for (cd_id id = 1; id < 4096; id++) {
    uint32_t s = cd_tag(id) >> 24;
    if (!s) continue;
    got = 0;
    cd_event_rec e = {CD_EV_CLICK, (int32_t)id, 0, 0, 0, 0, 0, 0, 0};
    cd_on_event(&e);
    uint32_t len = cd_html_node(id, html, sizeof html - 1);
    html[len < sizeof html - 1 ? len : sizeof html - 1] = 0;
    uint32_t wl = 0;
    const char *w = got ? cd_bytes(got, &wl) : "";
    snprintf(want, sizeof want, ">%.*s", (int)wl, w);
    n++;
    if (!got || !strstr(html, want)) {
      if (bad++ < 3) printf("FAIL %s: node %u (slot %u) got %.*s: %.80s\n", when, id, s, (int)wl, w, html);
    }
  }
  fails += bad != 0;
  printf("%s %s: %u listening nodes, each handler got the record its node shows\n", bad ? "FAIL" : "ok  ", when, n);
  return n;
}

/* where ">name" starts in h at or after from, the name not followed by a digit; -1 if nowhere */
static long at_name(const char *h, long from, cd_str nm) {
  uint32_t l;
  const char *w = cd_bytes(nm, &l);
  for (const char *p = h + from; (p = strchr(p, '>')); p++)
    if (!strncmp(p + 1, w, l) && (p[1 + l] < '0' || p[1 + l] > '9')) return p - h;
  return -1;
}
static void order(const char *when) {
  uint32_t n = cd_html_write(0, 0);
  char *h = malloc(n + 1);
  cd_html_write(h, n);
  h[n] = 0;
  long pos = 0;
  int good = 1;
  for (uint32_t i = 0; i < B.cols_n && good; i++) {
    Col *c = &B.cols[i];
    good = (pos = at_name(h, pos, c->title)) >= 0;
    for (uint32_t j = 0; j < c->cards_n && good; j++) {
      Card *k = &c->cards[j];
      good = (pos = at_name(h, pos, k->text)) >= 0;
      for (uint32_t t = 0; t < k->tags_n && good; t++) good = (pos = at_name(h, pos, k->tags[t].name)) >= 0;
    }
  }
  fails += !good;
  printf("%s %s: the page shows columns, cards and tags in the state's order\n", good ? "ok  " : "FAIL", when);
  free(h);
  uint64_t before = cd_html_ops(); /* a render that redoes everything finds nothing the marks missed */
  Board_mark_all(&B);
  page_render(&B);
  cd_flush();
  uint64_t more = cd_html_ops() - before;
  fails += more != 0;
  printf("%s %s: a full render after it makes %llu calls\n", more ? "FAIL" : "ok  ", when, (unsigned long long)more);
}

int main(void) {
  cd_html_reset();
  cd_main();
  cd_flush();
  uint32_t n = clicks("first render");
  order("first render");
  if (n != 3 + 12 + (0 + 1 + 2) + (0 + 1 + 2 + 3) + (0 + 1 + 2 + 3 + 4)) { printf("FAIL listener count %u\n", n); fails++; }
  for (uint32_t i = 0; i < B.cols_n; i++) { /* reverse every list on every level */
    Col *c = &B.cols[i];
    for (uint32_t j = 0; j < c->cards_n / 2; j++) Col_cards_swap(c, j, c->cards_n - 1 - j);
    for (uint32_t j = 0; j < c->cards_n; j++) {
      Card *k = &c->cards[j];
      for (uint32_t t = 0; t < k->tags_n / 2; t++) Card_tags_swap(k, t, k->tags_n - 1 - t);
    }
  }
  Board_cols_swap(&B, 0, 2);
  page_render(&B);
  cd_flush();
  clicks("every list reversed");
  order("every list reversed");
  Board_cols_remove(&B, 1);
  page_render(&B);
  cd_flush();
  clicks("a column removed");
  order("a column removed");
  Tag *deep = 0; /* a tag three levels down, renamed alone: the mark climbs to its column */
  for (uint32_t i = 0; i < B.cols_n && !deep; i++)
    for (uint32_t j = 0; j < B.cols[i].cards_n && !deep; j++)
      if (B.cols[i].cards[j].tags_n) deep = &B.cols[i].cards[j].tags[B.cols[i].cards[j].tags_n - 1];
  Tag_set_name(deep, cd_internz("renamed"));
  page_render(&B);
  cd_flush();
  order("a tag renamed");
  return fails != 0;
}
