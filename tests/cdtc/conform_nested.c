/* Shim-vs-sink conformance of lists inside item templates: tests/cdtc/fixtures/nested.cdt
 * (columns hold cards, cards hold tags, a card has an {#if} before its tag list), compiled by
 * cdtc, runs the same steps in Chrome and natively; tests/conform.mjs compares the DOMs after
 * every step and resumes over the native first frame (three columns with cards and tags, an empty
 * card list and an empty tag list among them). Steps add, remove, reorder and edit records on
 * every level at random and click random listeners on every level.
 * Run: tests/cdtc/conform.sh nested */
#include "nested.h"
#ifdef __wasm__
#define EXPORT(n) __attribute__((export_name(n)))
#else
#define EXPORT(n)
#endif

#define STEPS 300

static uint32_t rng = 0x2545f491u;
static uint32_t rnd(uint32_t m) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng % m; }
static const char *WORDS[] = {"a", "b&c", "<d>", "", "\xc3\xa9t\xc3\xa9", "f g", "\xf0\x9f\x98\x80", "long word here"};
static cd_str word(void) { return cd_internz(WORDS[rnd(sizeof WORDS / sizeof *WORDS)]); }
static uint32_t next_id = 1;

void tag_click(Board *S, Tag *g, const cd_event_rec *e) { (void)S; (void)e; g->name = cd_internz("clicked"); }
void card_click(Board *S, Card *k, const cd_event_rec *e) { (void)e; S->sel = k->id; k->done = !k->done; }
void col_click(Board *S, Col *c, const cd_event_rec *e) { (void)S; (void)e; c->title = cd_internz("picked"); }

void cd_on_event(const cd_event_rec *e) {
  page_dispatch(e);
  page_render(&B);
}

static void fill_tags(Card *k, uint32_t n) {
  for (uint32_t i = 0; i < n; i++) {
    Tag *g = Card_tags_push(k);
    g->id = next_id++;
    g->name = word();
  }
}
static void fill_cards(Col *c, uint32_t n) {
  for (uint32_t i = 0; i < n; i++) {
    Card *k = Col_cards_push(c);
    k->id = next_id++;
    k->text = word();
    k->done = rnd(2);
    fill_tags(k, rnd(4));
  }
}

void cd_main(void) {
  if (!B.cols_n) { /* a fresh start; a loaded state keeps what it has */
    for (uint32_t i = 0; i < 3; i++) {
      Col *c = Board_cols_push(&B);
      c->id = next_id++;
      c->title = cd_internz(WORDS[i]);
      fill_cards(c, i == 1 ? 0 : 3);
    }
    B.cols[0].cards[0].tags_n = 0; /* an empty tag list (its records stay allocated) */
    B.cols[0].cards[1].done = 1;
    B.sel = B.cols[2].cards[1].id;
  } else { /* continue ids after the blob's, as the fresh run does */
    for (uint32_t i = 0; i < B.cols_n; i++) {
      Col *c = &B.cols[i];
      if (c->id >= next_id) next_id = c->id + 1;
      for (uint32_t j = 0; j < c->cards_n; j++) {
        Card *d = &c->cards[j];
        if (d->id >= next_id) next_id = d->id + 1;
        for (uint32_t t = 0; t < d->tags_n; t++)
          if (d->tags[t].id >= next_id) next_id = d->tags[t].id + 1;
      }
    }
  }
  rng = 0x9e3779b9u; /* the steps draw the same numbers after a fresh start and after a load */
  page_init();
  page_create(&B);
  page_render(&B);
}

static void click(uint32_t s) {
  cd_id ids[512];
  uint32_t n = 0;
  for (cd_id id = 1; id < 8192 && n < 512; id++)
    if (cd_tag(id) >> 24 == s) ids[n++] = id;
  if (!n) return;
  cd_event_rec e = {CD_EV_CLICK, (int32_t)ids[rnd(n)], 0, 0, 0, 0, 0, 0, 0};
  cd_on_event(&e);
}

static Col *any_col(void) { return B.cols_n ? &B.cols[rnd(B.cols_n)] : 0; }
static Card *any_card(void) {
  Col *c = any_col();
  return c && c->cards_n ? &c->cards[rnd(c->cards_n)] : 0;
}

EXPORT("step") void step(uint32_t k) {
  (void)k;
  Col *c;
  Card *d;
  switch (rnd(14)) {
  case 0: if (B.cols_n < 6) { c = Board_cols_push(&B); c->id = next_id++; c->title = word(); fill_cards(c, rnd(3)); } break;
  case 1: if (B.cols_n) Board_cols_remove(&B, rnd(B.cols_n)); break;
  case 2: /* reverse the columns: moves whose subtrees carry their lists */
    for (uint32_t i = 0; i < B.cols_n / 2; i++) { Col t = B.cols[i]; B.cols[i] = B.cols[B.cols_n - 1 - i]; B.cols[B.cols_n - 1 - i] = t; }
    break;
  case 3: if ((c = any_col())) fill_cards(c, 1 + rnd(2)); break;
  case 4: if ((c = any_col()) && c->cards_n) Col_cards_remove(c, rnd(c->cards_n)); break;
  case 5: /* shuffle one column's cards */
    if ((c = any_col()))
      for (uint32_t i = c->cards_n; i > 1; i--) { uint32_t j = rnd(i); Card t = c->cards[i - 1]; c->cards[i - 1] = c->cards[j]; c->cards[j] = t; }
    break;
  case 6: if ((d = any_card())) fill_tags(d, 1 + rnd(2)); break;
  case 7: if ((d = any_card()) && d->tags_n) Card_tags_remove(d, rnd(d->tags_n)); break;
  case 8: if ((d = any_card())) { d->done = !d->done; d->text = word(); } break;
  case 9: if ((d = any_card()) && d->tags_n) d->tags[rnd(d->tags_n)].name = word(); break;
  case 10: if ((c = any_col())) c->title = word(); break;
  case 11: click(1 + rnd(3)); break;
  case 12: if ((d = any_card())) B.sel = d->id; break;
  default: /* move a card to another column */
    if ((c = any_col()) && c->cards_n) {
      Col *to = any_col();
      uint32_t i = rnd(c->cards_n);
      Card moved = c->cards[i];
      for (uint32_t j = i + 1; j < c->cards_n; j++) c->cards[j - 1] = c->cards[j];
      c->cards_n--;
      *Col_cards_push(to) = moved;
    }
    break;
  }
  page_render(&B);
  cd_flush();
}

EXPORT("steps") uint32_t steps(void) { return STEPS; }

#ifndef __wasm__
#include "cdom_html.h"
#include <stdio.h>
#include <stdlib.h>
static void dump(void) { /* <length>\n<html> */
  uint32_t n = cd_html_write(0, 0);
  char *b = malloc(n + 1);
  cd_html_write(b, n);
  printf("%u\n", n);
  fwrite(b, 1, n, stdout);
  free(b);
}
static void blob(void) { /* S<base64>\n: the state after the first frame, for resuming;
                            * then E<types>, <names>: the event types the page listens to at once */
  static const char A[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  uint32_t n = cd_state_save(0, 0);
  uint8_t *s = malloc(n + 3);
  cd_state_save(s, n);
  s[n] = s[n + 1] = 0;
  putchar('S');
  for (uint32_t i = 0; i < n; i += 3) {
    uint32_t v = (uint32_t)s[i] << 16 | (uint32_t)s[i + 1] << 8 | s[i + 2];
    putchar(A[v >> 18]); putchar(A[(v >> 12) & 63]);
    putchar(i + 1 < n ? A[(v >> 6) & 63] : '='); putchar(i + 2 < n ? A[v & 63] : '=');
  }
  putchar('\n');
  free(s);
  char ev[256];
  uint32_t ne = cd_html_resume_events(ev, sizeof ev);
  if (ne <= sizeof ev) printf("E%.*s\n", (int)ne, ev);
}
int main(void) {
  cd_html_reset();
  cd_main();
  cd_flush();
  blob();
  dump();
  for (uint32_t k = 1; k <= steps(); k++) { step(k); dump(); }
  return 0;
}
#endif
