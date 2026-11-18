/* js-framework-benchmark rows app on cdom. The model is plain C; render()
 * diffs it against what is on screen and emits only the changes. */
#include "../../../src/cdom.h"

/* A literal goes to the page's string table; a runtime string is read when it is used. */

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

static uint32_t rng;
static uint32_t rnd(uint32_t max) { /* xorshift32, like Math.round(Math.random()*1000) % max */
  rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
  return (rng % 1000) % max;
}

/* ---- model ---- */
typedef struct { uint32_t id, off, len, rev; } Row;
/* Labels live in one byte arena the app owns. A row's rev changes when its label does, so the
 * patch compares revs, not text. Every use is a cd_tmp over the arena bytes. */
static char *arena;
static uint32_t arena_len, arena_cap, live_bytes, next_rev = 1;
static uint32_t arena_put(const char *s, uint32_t n) {
  if (arena_len + n > arena_cap) {
    while (arena_len + n > arena_cap) arena_cap = arena_cap ? arena_cap * 2 : 65536;
    arena = cd_realloc(arena, arena_cap);
  }
  for (uint32_t i = 0; i < n; i++) arena[arena_len + i] = s[i];
  arena_len += n;
  live_bytes += n;
  return arena_len - n;
}
#define LABEL(r) cd_tmp(arena + (r)->off, (r)->len)
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
  uint32_t first = arena_len;
  for (uint32_t i = 0; i < count; i++) {
    uint32_t n = cpy(buf, ADJ[rnd(25)]);
    buf[n++] = ' ';
    n += cpy(buf + n, COL[rnd(11)]);
    buf[n++] = ' ';
    n += cpy(buf + n, NOUN[rnd(13)]);
    rows[nrows++] = (Row){next_id++, arena_put(buf, n), n, next_rev++};
  }
  /* The new labels sit together and render() shows them in this call: one decode for all of them. */
  cd_tmp_ahead(arena + first, arena_len - first);
}

/* ---- view ---- */
typedef struct { uint32_t rev; uint8_t sel; } RowView;
static RowView *views;
static uint32_t nviews, views_cap, *free_views, nfree_views;
static cd_list list;
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
  v->rev = r->rev;
  v->sel = r->id == selected;
  cd_tag_set(tr, K_ROW | r->id); /* a click reports which cell: 1 label, 2 remove */
#if CDOM_TEMPLATES
  cd_tpl_text_num(T_ROW, 0, r->id);                       /* td.col-md-1 > id */
  cd_tpl_text(T_ROW, 1, LABEL(r));                       /* td.col-md-4 > a > label */
  cd_clone(T_ROW);
  cd_listen(tr, CD_EV_CLICK);
  if (v->sel) cd_class("danger");
  cd_close();
#else
  cd_open("tr"); cd_listen(tr, CD_EV_CLICK);
  if (v->sel) cd_class("danger");
  cd_open("td"); cd_class("col-md-1"); cd_text_num(r->id); cd_close();
  cd_open("td"); cd_class("col-md-4");
  cd_open("a"); cd_text(LABEL(r)); cd_close();
  cd_close();
  cd_open("td"); cd_class("col-md-1");
  cd_open("a");
  cd_open("span"); cd_class("glyphicon glyphicon-remove"); cd_attr("aria-hidden", "true"); cd_close();
  cd_close();
  cd_close();
  cd_open("td"); cd_class("col-md-6"); cd_close();
  cd_close();
#endif
  *data = vi;
  return tr;
}

static void row_patch(void *ctx, uint32_t i, cd_item *it) {
  (void)ctx;
  Row *r = &rows[i];
  RowView *v = &views[it->data];
  if (v->rev != r->rev) {
    v->rev = r->rev;
    cd_sel(it->node); /* tr > td[1] > a > text */
    cd_down(); cd_next(); cd_down(); cd_down();
    cd_value(LABEL(r));
  }
  uint8_t sel = r->id == selected;
  if (v->sel != sel) {
    v->sel = sel;
    cd_set_class(it->node, sel ? cd_s("danger") : cd_s(""));
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
/* Dead bytes are labels that were replaced or whose row was removed. Slide the live ones down
 * once the dead outweigh them. A row keeps its rev, so the view does not see a change. */
static void arena_compact(void) {
  uint32_t to = 0;
  /* Rows can be in any order in the arena, so copy through a second buffer. */
  char *fresh = cd_alloc(live_bytes ? live_bytes : 1);
  for (uint32_t i = 0; i < nrows; i++) {
    for (uint32_t j = 0; j < rows[i].len; j++) fresh[to + j] = arena[rows[i].off + j];
    rows[i].off = to;
    to += rows[i].len;
  }
  for (uint32_t j = 0; j < to; j++) arena[j] = fresh[j];
  cd_free(fresh);
  arena_len = to;
}

static void act(uint32_t k, uint32_t cell) {
  switch (k) {
  case K_RUN: nrows = 0; arena_len = live_bytes = 0; build(1000); selected = 0; break;
  case K_RUNLOTS: nrows = 0; arena_len = live_bytes = 0; build(10000); selected = 0; break;
  case K_ADD: build(1000); break;
  case K_UPDATE: {
    uint32_t first = arena_len;
    for (uint32_t i = 0; i < nrows; i += 10) {
      uint32_t len = rows[i].len;
      char buf[256];
      if (len + 4 > sizeof buf) continue;
      for (uint32_t j = 0; j < len; j++) buf[j] = arena[rows[i].off + j];
      buf[len] = ' '; buf[len + 1] = '!'; buf[len + 2] = '!'; buf[len + 3] = '!';
      live_bytes -= len; /* the old bytes are dead now */
      rows[i].off = arena_put(buf, len + 4); rows[i].len = len + 4; rows[i].rev = next_rev++;
    }
    /* A compaction rewrites the arena, so the appended labels are announced only when it did not run. */
    if (arena_len - live_bytes > live_bytes) arena_compact();
    else cd_tmp_ahead(arena + first, arena_len - first);
    break;
  }
  case K_CLEAR: nrows = 0; arena_len = live_bytes = 0; selected = 0; break;
  case K_SWAP:
    if (nrows > 998) { Row t = rows[1]; rows[1] = rows[998]; rows[998] = t; }
    break;
  default:
    if (k & K_ROW) {
      uint32_t id = k & ~K_ROW;
      if (cell == 2) {
        for (uint32_t i = 0; i < nrows; i++)
          if (rows[i].id == id) {
            live_bytes -= rows[i].len;
            for (uint32_t j = i + 1; j < nrows; j++) rows[j - 1] = rows[j];
            nrows--;
            if (arena_len - live_bytes > live_bytes) arena_compact();
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

/* ---- the page ---- */
static void button(const char *id, const char *label, uint32_t k) {
  cd_open("div"); cd_class("col-sm-6 smallpad");
  cd_open("button");
  cd_attr("type", "button");
  cd_class("btn btn-primary btn-block");
  cd_attr("id", id);
  cd_id b = cd_id_new();
  cd_listen(b, CD_EV_CLICK);
  cd_tag_set(b, k);
  cd_text(label);
  cd_close();
  cd_close();
}

void cd_main(void) {
  rng = cd_seed | 1;
#if CDOM_TEMPLATES
  cd_at(CD_DETACHED, CD_APPEND);
  cd_open("tr");
  cd_open("td"); cd_class("col-md-1"); cd_text(" "); cd_slot(0); cd_close();
  cd_open("td"); cd_class("col-md-4"); cd_open("a"); cd_text(" "); cd_slot(1); cd_close(); cd_close();
  cd_open("td"); cd_class("col-md-1");
  cd_open("a"); cd_open("span"); cd_class("glyphicon glyphicon-remove"); cd_attr("aria-hidden", "true"); cd_close(); cd_close();
  cd_close();
  cd_open("td"); cd_class("col-md-6"); cd_close();
  cd_close();
  cd_tpl_save(T_ROW);
#endif

  cd_at(CD_MOUNT, CD_APPEND);
  cd_open("div"); cd_class("container");
  cd_open("div"); cd_class("jumbotron");
  cd_open("div"); cd_class("row");
  cd_open("div"); cd_class("col-md-6");
  cd_open("h1"); cd_text("cdom-keyed"); cd_close();
  cd_close();
  cd_open("div"); cd_class("col-md-6");
  cd_open("div"); cd_class("row");
  button("run", "Create 1,000 rows", K_RUN);
  button("runlots", "Create 10,000 rows", K_RUNLOTS);
  button("add", "Append 1,000 rows", K_ADD);
  button("update", "Update every 10th row", K_UPDATE);
  button("clear", "Clear", K_CLEAR);
  button("swaprows", "Swap Rows", K_SWAP);
  cd_close(); cd_close(); cd_close(); cd_close();
  cd_open("table"); cd_class("table table-hover table-striped test-data");
  cd_open("tbody");
  list.parent = cd_id_new();
  list.owns_parent = 1;
  cd_name_el(list.parent);
  cd_close();
  cd_close();
  cd_open("span"); cd_class("preloadicon glyphicon glyphicon-remove"); cd_attr("aria-hidden", "true");
  cd_close();
  cd_close();
}
