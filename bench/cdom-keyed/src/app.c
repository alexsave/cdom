/* js-framework-benchmark rows app on cdom. The model is plain C; render()
 * diffs it against what is on screen and emits only the changes. */
#include "../../../src/cdom.h"

#ifndef CDOM_TEMPLATES
#define CDOM_TEMPLATES 1
#endif

static const char *ADJ[] = {"pretty", "large", "big", "small", "tall", "short", "long", "handsome",
  "plain", "quaint", "clean", "elegant", "easy", "angry", "crazy", "helpful", "mushy", "odd",
  "unsightly", "adorable", "important", "inexpensive", "cheap", "expensive", "fancy"};
static const char *COL[] = {"red", "yellow", "blue", "green", "pink", "brown", "purple", "brown",
  "white", "black", "orange"};
static const char *NOUN[] = {"table", "chair", "house", "bbq", "desk", "car", "pony", "cookie",
  "sandwich", "burger", "pizza", "mouse", "keyboard"};

extern uint32_t cd_seed;
static uint32_t rng;
static uint32_t rnd(uint32_t max) { /* xorshift32, like Math.round(Math.random()*1000) % max */
  rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
  return (rng % 1000) % max;
}

/* ---- model ---- */
typedef struct { uint32_t id; cd_str label; } Row;
static Row *rows;
static uint32_t nrows, rows_cap, next_id = 1, selected;
static uint32_t *keys;

static void reserve_rows(uint32_t n) {
  if (n <= rows_cap) return;
  while (rows_cap < n) rows_cap = rows_cap ? rows_cap * 2 : 1024;
  rows = cd_realloc(rows, rows_cap * sizeof(Row));
  keys = cd_realloc(keys, rows_cap * 4);
}

static uint32_t cpy(char *d, const char *s) {
  uint32_t n = 0;
  while (s[n]) { d[n] = s[n]; n++; }
  return n;
}

static void build(uint32_t count) {
  reserve_rows(nrows + count);
  char buf[64];
  for (uint32_t i = 0; i < count; i++) {
    uint32_t n = cpy(buf, ADJ[rnd(25)]);
    buf[n++] = ' ';
    n += cpy(buf + n, COL[rnd(11)]);
    buf[n++] = ' ';
    n += cpy(buf + n, NOUN[rnd(13)]);
    rows[nrows++] = (Row){next_id++, cd_intern(buf, n)};
  }
}

/* ---- view ---- */
typedef struct { cd_str label; uint8_t sel; } RowView;
static RowView *views;
static uint32_t nviews, views_cap, *free_views, nfree_views;
static cd_list list;
static cd_str S_TR, S_TD, S_A, S_SPAN, S_DANGER, S_EMPTY, S_SPACE, S_COL1, S_COL4, S_COL6, S_REMOVE,
  S_ARIA, S_TRUE;
enum { T_ROW = 1 };
enum { K_RUN = 1, K_RUNLOTS, K_ADD, K_UPDATE, K_CLEAR, K_SWAP, K_ROW = 0x80000000u };

static uint32_t view_new(void) {
  if (nfree_views) return free_views[--nfree_views];
  if (nviews == views_cap) {
    views_cap = views_cap ? views_cap * 2 : 1024;
    views = cd_realloc(views, views_cap * sizeof(RowView));
    free_views = cd_realloc(free_views, views_cap * 4);
  }
  return nviews++;
}

static cd_id row_create(void *ctx, uint32_t i, uint32_t *data) {
  (void)ctx;
  Row *r = &rows[i];
  uint32_t vi = view_new();
  RowView *v = &views[vi];
  cd_id tr = cd_id_new();
  v->label = r->label;
  v->sel = r->id == selected;
  cd_tag_set(tr, K_ROW | r->id); /* a click reports which cell: 1 label, 2 remove */
#if CDOM_TEMPLATES
  cd_tpl_text_num(T_ROW, 0, r->id);                       /* td.col-md-1 > id */
  cd_tpl_text(T_ROW, 1, r->label);                        /* td.col-md-4 > a > label */
  cd_clone(T_ROW);
  cd_listen(tr, CD_EV_CLICK);
  if (v->sel) cd_class(S_DANGER);
  cd_close();
#else
  cd_open(S_TR); cd_listen(tr, CD_EV_CLICK);
  if (v->sel) cd_class(S_DANGER);
  cd_open(S_TD); cd_class(S_COL1); cd_text_num(r->id); cd_close();
  cd_open(S_TD); cd_class(S_COL4);
  cd_open(S_A); cd_text(r->label); cd_close();
  cd_close();
  cd_open(S_TD); cd_class(S_COL1);
  cd_open(S_A);
  cd_open(S_SPAN); cd_class(S_REMOVE); cd_attr(S_ARIA, S_TRUE); cd_close();
  cd_close();
  cd_close();
  cd_open(S_TD); cd_class(S_COL6); cd_close();
  cd_close();
#endif
  *data = vi;
  return tr;
}

static void row_patch(void *ctx, uint32_t i, cd_item *it) {
  (void)ctx;
  Row *r = &rows[i];
  RowView *v = &views[it->data];
  if (v->label != r->label) {
    v->label = r->label;
    cd_sel(it->node); /* tr > td[1] > a > text */
    cd_down(); cd_next(); cd_down(); cd_down();
    cd_value(r->label);
  }
  uint8_t sel = r->id == selected;
  if (v->sel != sel) {
    v->sel = sel;
    cd_set_class(it->node, sel ? S_DANGER : S_EMPTY);
  }
}

static void row_destroy(void *ctx, cd_item *it) {
  (void)ctx;
  free_views[nfree_views++] = it->data;
}

static const cd_list_fns ROW_FNS = {0, row_create, row_patch, row_destroy};

static void render(void) {
  for (uint32_t i = 0; i < nrows; i++) keys[i] = rows[i].id;
  cd_list_update(&list, keys, nrows, &ROW_FNS);
}

/* ---- actions ---- */
static void act(uint32_t k, uint32_t cell) {
  switch (k) {
  case K_RUN: nrows = 0; build(1000); selected = 0; break;
  case K_RUNLOTS: nrows = 0; build(10000); selected = 0; break;
  case K_ADD: build(1000); break;
  case K_UPDATE:
    for (uint32_t i = 0; i < nrows; i += 10) {
      uint32_t len;
      const char *b = cd_bytes(rows[i].label, &len);
      char buf[256];
      if (len + 4 > sizeof buf) continue;
      for (uint32_t j = 0; j < len; j++) buf[j] = b[j];
      buf[len] = ' '; buf[len + 1] = '!'; buf[len + 2] = '!'; buf[len + 3] = '!';
      rows[i].label = cd_intern(buf, len + 4);
    }
    break;
  case K_CLEAR: nrows = 0; selected = 0; break;
  case K_SWAP:
    if (nrows > 998) { Row t = rows[1]; rows[1] = rows[998]; rows[998] = t; }
    break;
  default:
    if (k & K_ROW) {
      uint32_t id = k & ~K_ROW;
      if (cell == 2) {
        for (uint32_t i = 0; i < nrows; i++)
          if (rows[i].id == id) {
            for (uint32_t j = i + 1; j < nrows; j++) rows[j - 1] = rows[j];
            nrows--;
            break;
          }
      } else if (cell == 1) {
        selected = id;
      }
    }
  }
  render();
}

void cd_on_event(const cd_event_rec *e) {
  if (e->type == CD_EV_CLICK) act(cd_tag((cd_id)e->id), e->depth ? (uint32_t)e->path & 255 : 0xff);
}

/* ---- state: the model as one fixed little-endian blob. The server build saves
 * it into the page; the browser build of this same C loads it and hydrates.
 *   RowsHead, 16 bytes: rng, next_id, selected, nrows (u32 each)
 *   then per row: id (u32), label length (u16), label bytes ---- */
typedef struct { uint32_t rng, next_id, selected, nrows; } RowsHead;
_Static_assert(sizeof(RowsHead) == 16, "RowsHead is four u32");
static int loaded; /* the model came from a blob (or the server's fill): keep its rng */

static void put16(uint8_t *d, uint32_t v) { d[0] = (uint8_t)v; d[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *d, uint32_t v) { put16(d, v); put16(d + 2, v >> 16); }
static uint32_t get16(const uint8_t *s) { return s[0] | (uint32_t)s[1] << 8; }
static uint32_t get32(const uint8_t *s) { return get16(s) | get16(s + 2) << 16; }

uint32_t cd_state_save(uint8_t *dst, uint32_t cap) {
  uint32_t need = sizeof(RowsHead), len;
  for (uint32_t i = 0; i < nrows; i++) { cd_bytes(rows[i].label, &len); need += 6 + len; }
  if (need > cap) return need;
  RowsHead h = {rng, next_id, selected, nrows};
  put32(dst, h.rng); put32(dst + 4, h.next_id); put32(dst + 8, h.selected); put32(dst + 12, h.nrows);
  uint8_t *d = dst + sizeof(RowsHead);
  for (uint32_t i = 0; i < nrows; i++) {
    const char *b = cd_bytes(rows[i].label, &len);
    put32(d, rows[i].id); put16(d + 4, len);
    for (uint32_t j = 0; j < len; j++) d[6 + j] = (uint8_t)b[j];
    d += 6 + len;
  }
  return need;
}

int cd_state_load(const uint8_t *src, uint32_t len) {
  if (len < sizeof(RowsHead)) return -1;
  RowsHead h = {get32(src), get32(src + 4), get32(src + 8), get32(src + 12)};
  if (!h.rng || h.nrows > (len - sizeof(RowsHead)) / 6) return -1;
  reserve_rows(h.nrows);
  uint32_t o = sizeof(RowsHead);
  for (uint32_t i = 0; i < h.nrows; i++) {
    if (len - o < 6) return -1;
    uint32_t n = get16(src + o + 4);
    if (len - o - 6 < n) return -1;
    rows[i] = (Row){get32(src + o), cd_intern((const char *)src + o + 6, n)};
    o += 6 + n;
  }
  if (o != len) return -1;
  rng = h.rng; next_id = h.next_id; selected = h.selected; nrows = h.nrows;
  loaded = 1;
  return 0;
}

#ifndef __wasm__
/* The server's model for a request: what "Create n rows" would make from cd_seed. */
void app_ssr_fill(uint32_t n) {
  rng = cd_seed | 1;
  loaded = 1;
  nrows = 0;
  selected = 0;
  build(n);
}
#endif

/* ---- the page ---- */
static cd_str z(const char *s) { return cd_internz(s); }

static void button(const char *id, const char *label, uint32_t k) {
  cd_open(z("div")); cd_class(z("col-sm-6 smallpad"));
  cd_open(z("button"));
  cd_attr(z("type"), z("button"));
  cd_class(z("btn btn-primary btn-block"));
  cd_attr(z("id"), z(id));
  cd_id b = cd_id_new();
  cd_listen(b, CD_EV_CLICK);
  cd_tag_set(b, k);
  cd_text(z(label));
  cd_close();
  cd_close();
}

void cd_main(void) {
  if (!loaded) rng = cd_seed | 1;
  S_TR = z("tr"); S_TD = z("td"); S_A = z("a"); S_SPAN = z("span");
  S_DANGER = z("danger"); S_EMPTY = z(""); S_SPACE = z(" ");
  S_COL1 = z("col-md-1"); S_COL4 = z("col-md-4"); S_COL6 = z("col-md-6");
  S_REMOVE = z("glyphicon glyphicon-remove"); S_ARIA = z("aria-hidden"); S_TRUE = z("true");

#if CDOM_TEMPLATES
  cd_at(CD_DETACHED, 0);
  cd_open(S_TR);
  cd_open(S_TD); cd_class(S_COL1); cd_text(S_SPACE); cd_slot(0); cd_close();
  cd_open(S_TD); cd_class(S_COL4); cd_open(S_A); cd_text(S_SPACE); cd_slot(1); cd_close(); cd_close();
  cd_open(S_TD); cd_class(S_COL1);
  cd_open(S_A); cd_open(S_SPAN); cd_class(S_REMOVE); cd_attr(S_ARIA, S_TRUE); cd_close(); cd_close();
  cd_close();
  cd_open(S_TD); cd_class(S_COL6); cd_close();
  cd_close();
  cd_tpl_save(T_ROW);
#endif

  cd_at(CD_MOUNT, 0);
  cd_open(z("div")); cd_class(z("container"));
  cd_open(z("div")); cd_class(z("jumbotron"));
  cd_open(z("div")); cd_class(z("row"));
  cd_open(z("div")); cd_class(z("col-md-6"));
  cd_open(z("h1")); cd_text(z("cdom-keyed")); cd_close();
  cd_close();
  cd_open(z("div")); cd_class(z("col-md-6"));
  cd_open(z("div")); cd_class(z("row"));
  button("run", "Create 1,000 rows", K_RUN);
  button("runlots", "Create 10,000 rows", K_RUNLOTS);
  button("add", "Append 1,000 rows", K_ADD);
  button("update", "Update every 10th row", K_UPDATE);
  button("clear", "Clear", K_CLEAR);
  button("swaprows", "Swap Rows", K_SWAP);
  cd_close(); cd_close(); cd_close(); cd_close();
  cd_open(z("table")); cd_class(z("table table-hover table-striped test-data"));
  cd_open(z("tbody"));
  list.parent = cd_id_new();
  list.owns_parent = 1;
  cd_name(list.parent);
  cd_close();
  cd_close();
  cd_open(S_SPAN); cd_class(z("preloadicon glyphicon glyphicon-remove")); cd_attr(S_ARIA, S_TRUE);
  cd_close();
  cd_close();
  if (nrows) render(); /* a loaded model shows its rows: the server writes them, hydration walks them */
}
