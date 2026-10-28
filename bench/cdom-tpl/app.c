/* js-framework-benchmark rows app from a template: rows.cdt declares the state, the row
 * and the page; cdtc compiles it to out/rows.{h,c}. This file holds only the actions
 * and the entry, with the same label generator and rng as bench/cdom-keyed/src/app.c. */
#include "rows.h"

static const char *ADJ[] = {"pretty", "large", "big", "small", "tall", "short", "long", "handsome",
  "plain", "quaint", "clean", "elegant", "easy", "angry", "crazy", "helpful", "mushy", "odd",
  "unsightly", "adorable", "important", "inexpensive", "cheap", "expensive", "fancy"};
static const char *COL[] = {"red", "yellow", "blue", "green", "pink", "brown", "purple", "brown",
  "white", "black", "orange"};
static const char *NOUN[] = {"table", "chair", "house", "bbq", "desk", "car", "pony", "cookie",
  "sandwich", "burger", "pizza", "mouse", "keyboard"};

extern uint32_t cd_seed;
static uint32_t rnd(Rows *s, uint32_t max) { /* xorshift32, like Math.round(Math.random()*1000) % max */
  s->rng ^= s->rng << 13; s->rng ^= s->rng >> 17; s->rng ^= s->rng << 5;
  return (s->rng % 1000) % max;
}

static uint32_t cpy(char *d, const char *s) {
  uint32_t n = 0;
  while (s[n]) { d[n] = s[n]; n++; }
  return n;
}

static void build(Rows *s, uint32_t count) {
  Rows_rows_reserve(s, s->rows_n + count);
  char buf[64];
  for (uint32_t i = 0; i < count; i++) {
    uint32_t n = cpy(buf, ADJ[rnd(s, 25)]);
    buf[n++] = ' ';
    n += cpy(buf + n, COL[rnd(s, 11)]);
    buf[n++] = ' ';
    n += cpy(buf + n, NOUN[rnd(s, 13)]);
    Row *r = Rows_rows_push(s);
    r->id = s->next_id++;
    r->label = cd_intern(buf, n);
  }
}

/* Writes go through the setters rows.h declares, which mark what reads them; the rng and next_id
 * are read by no binding, so they are written directly. */
void run(Rows *s, const cd_event_rec *e) { (void)e; Rows_rows_clear(s); build(s, 1000); Rows_set_selected(s, 0); }
void runlots(Rows *s, const cd_event_rec *e) { (void)e; Rows_rows_clear(s); build(s, 10000); Rows_set_selected(s, 0); }
void add(Rows *s, const cd_event_rec *e) { (void)e; build(s, 1000); }
void clear(Rows *s, const cd_event_rec *e) { (void)e; Rows_rows_clear(s); Rows_set_selected(s, 0); }

void update(Rows *s, const cd_event_rec *e) {
  (void)e;
  for (uint32_t i = 0; i < s->rows_n; i += 10) {
    uint32_t len;
    const char *b = cd_bytes(s->rows[i].label, &len);
    char buf[256];
    if (len + 4 > sizeof buf) continue;
    for (uint32_t j = 0; j < len; j++) buf[j] = b[j];
    buf[len] = ' '; buf[len + 1] = '!'; buf[len + 2] = '!'; buf[len + 3] = '!';
    Row_set_label(&s->rows[i], cd_intern(buf, len + 4));
  }
}

void swaprows(Rows *s, const cd_event_rec *e) { (void)e; if (s->rows_n > 998) Rows_rows_swap(s, 1, 998); }

/* A click on a row reports which cell through the path: 1 the label, 2 the remove icon. */
void row_click(Rows *s, Row *r, const cd_event_rec *e) {
  uint32_t cell = e->depth ? (uint32_t)e->path & 255 : 0xff;
  if (cell == 2) Rows_rows_remove(s, (uint32_t)(r - s->rows));
  else if (cell == 1) Rows_set_selected(s, r->id);
}

void cd_on_event(const cd_event_rec *e) {
  page_dispatch(e);
  page_render(&S);
}

void cd_main(void) {
  if (!S.rng) S.rng = cd_seed | 1; /* a loaded state keeps its rng */
  page_init();
  page_create(&S);
  page_render(&S); /* a loaded state shows its rows: the server writes them, hydration walks them */
}

#ifndef __wasm__
/* The server's model for a request: what "Create n rows" would make from cd_seed. */
void app_ssr_fill(uint32_t n) {
  S.rng = cd_seed | 1;
  S.rows_n = 0;
  S.selected = 0;
  build(&S, n);
}
#endif
