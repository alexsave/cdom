/* The arms' correctness test: one op sequence on cdom.h that touches every bridged operation, run
 * in steps by same.html; run.mjs same compares each arm's DOM (the mount's serialized HTML after
 * every step) and C's reads (rects, hits, events) with arm a's, and fails on any difference.
 * Covered: open (HTML and SVG), close, text, text_num, attr, class, name, name_open, listen,
 * value and value_num through walks, templates with slots and clones (inside an open element too),
 * set_text, set_text_num (past 2^31), set_attr, set_class, style (interned and one-off), style_num
 * in every unit kind used, the fused style builder where the arm has it (else the same writes one
 * by one), remove, free, move (a keyed list reversed, swapped, cut and refilled: runs of moves
 * before one node), clear (a region hidden), detach and attach, rect after writes in the same
 * entry, hit, and the event record. */
#include "cdom.h"

#define EXPORT(n) __attribute__((export_name(n)))
#define L(s) CD_LIT(s)

enum { T_ROW = 0, T_CELL = 1 };
static cd_id box, label, count, svg_c, panel, list_parent, moved, held, region_parent;
static cd_list list;
static cd_region region;
static uint32_t keys[64], nkeys;
static double rects[16 * 4];
static uint32_t nrects;
static int32_t hits[8];
static uint32_t nhits;
static uint32_t ev_sum, ev_n;

static cd_id row_create(void *ctx, uint32_t i, uint32_t *data) {
  (void)ctx;
  cd_id tr = cd_id_new();
  cd_tpl_text_num(T_ROW, 0, keys[i]);
  cd_tpl_text(T_ROW, 1, (keys[i] & 1) ? L("odd") : L("even"));
  cd_clone(T_ROW);
  cd_listen(tr, CD_EV_CLICK);
  cd_close();
  *data = keys[i];
  return tr;
}
static void row_patch(void *ctx, uint32_t i, cd_item *it) {
  (void)ctx;
  if (it->data != keys[i] * 3) { /* every survivor's label rewritten through a walk, once */
    it->data = keys[i] * 3;
    cd_sel(it->node);
    cd_down(); cd_next(); cd_down(); cd_down();
    cd_value(L("patched"));
  }
}
static void row_destroy(void *ctx, cd_item *it) { (void)ctx; (void)it; }
static const cd_list_fns ROW_FNS = {0, row_create, row_patch, row_destroy};

/* transform and filter cross as one-off strings, each made right before the op that takes it. */
static void style4(cd_id id, double x, double y, const char *transform, const char *filter) {
#ifdef CDOM_FUSED
  cd_style_begin(id);
  cd_style_num(L("left"), x, CD_UNIT_PX);
  cd_style_num(L("top"), y, CD_UNIT_PX);
  cd_style_str(L("transform"), cd_tmpz(transform));
  cd_style_str(L("filter"), cd_tmpz(filter));
  cd_style_end();
#else
  cd_set_style_num(id, L("left"), x, CD_UNIT_PX);
  cd_set_style_num(id, L("top"), y, CD_UNIT_PX);
  cd_set_style(id, L("transform"), cd_tmpz(transform));
  cd_set_style(id, L("filter"), cd_tmpz(filter));
#endif
}

static void list_to(const uint32_t *k, uint32_t n) {
  for (uint32_t i = 0; i < n; i++) keys[i] = k[i];
  nkeys = n;
  cd_list_update(&list, keys, nkeys, &ROW_FNS);
}

void cd_main(void) {
  box = cd_id_new(); label = cd_id_new(); count = cd_id_new(); svg_c = cd_id_new(); panel = cd_id_new();
  list_parent = cd_id_new(); moved = cd_id_new(); held = cd_id_new(); region_parent = cd_id_new();
  /* templates first, off the document */
  cd_at(CD_DETACHED, 0);
  cd_open(L("div")); cd_class(L("row"));
  cd_open(L("span")); cd_text(L(" ")); cd_slot(0); cd_close();
  cd_open(L("b")); cd_open(L("i")); cd_text(L(" ")); cd_slot(1); cd_close(); cd_close();
  cd_close();
  cd_tpl_save(T_ROW);
  cd_open(L("em")); cd_text(L(" ")); cd_slot(0); cd_close();
  cd_tpl_save(T_CELL);

  cd_at(0, 0);
  cd_open(L("div")); cd_name_open(box);
  cd_attr(L("style"), L("position: absolute; left: 0px; top: 0px; width: 40px; height: 20px;"));
  cd_attr(L("data-kind"), L("box"));
  cd_class(L("box first"));
  cd_text(L("label: ")); cd_name(label);
  cd_text_num(7); cd_name(count);
  cd_close();
  cd_open(L("svg")); cd_attr(L("width"), L("10"));
  cd_open(L("circle")); cd_name_open(svg_c); cd_attr(L("r"), L("4")); cd_close();
  cd_close();
  cd_open(L("section")); cd_name_open(panel);
  cd_tpl_text(T_CELL, 0, L("in an open element"));
  cd_clone(T_CELL); cd_close();
  cd_open(L("p")); cd_name_open(moved); cd_text(L("moved")); cd_close();
  cd_open(L("p")); cd_name_open(held); cd_text(L("held")); cd_close();
  cd_close();
  cd_open(L("ul")); cd_name_open(region_parent); cd_close();
  cd_open(L("div")); cd_name_open(list_parent); cd_close();
  region.parent = region_parent;
  list.parent = list_parent;
  list.owns_parent = 1;
  static const uint32_t k0[] = {1, 2, 3, 4, 5, 6, 7, 8};
  list_to(k0, 8);
}

void cd_on_event(const cd_event_rec *e) {
  ev_sum += (uint32_t)(e->type * 7 + e->id * 31 + e->path * 3 + e->depth + e->button);
  ev_n++;
  if (e->id == (int32_t)box) cd_prevent_default();
}

EXPORT("test_step") void test_step(uint32_t k) {
  char b[64];
  switch (k) {
  case 0: /* writes of every kind */
    cd_set_text(label, L("text: "));
    cd_set_text_num(count, 3000000000u);
    cd_set_attr(box, L("data-kind"), L("written"));
    cd_set_class(box, L("box second"));
    cd_set_style(box, L("border-color"), L("red"));
    { /* a one-off string built with cd_fmt, as the port builds its transforms */
      uint32_t n = 0;
      for (const char *c = "rotate("; *c; c++) b[n++] = *c;
      n += cd_fmt(b + n, -1250, 2, CD_UNIT_DEG);
      b[n++] = ')';
      cd_set_style(box, L("transform"), cd_tmp(b, n));
    }
    cd_set_style_num(box, L("width"), 42.5, CD_UNIT_PX);
    cd_set_style_num(box, L("opacity"), 0.25, CD_UNIT_NONE);
    cd_set_style_num(box, L("margin-left"), 12.5, CD_UNIT_PCT);
    cd_set_style_num(svg_c, L("opacity"), 0.5, CD_UNIT_NONE);
    style4(held, 10.25, -3, "translate(-50%, -50%) rotate(90deg) scale(1.2)", "drop-shadow(0 2px 3px rgba(0,0,0,0.4))");
    break;
  case 1: { /* the list: reversed (moves), then two swapped, then cut and refilled */
    static const uint32_t k1[] = {8, 7, 6, 5, 4, 3, 2, 1};
    list_to(k1, 8);
    static const uint32_t k2[] = {8, 2, 6, 5, 4, 3, 7, 1};
    list_to(k2, 8);
    static const uint32_t k3[] = {9, 2, 10, 5, 11, 1, 12, 13, 14};
    list_to(k3, 9);
    break;
  }
  case 2: /* a region shown, hidden (clear and frees), shown again; a move, a detach and attach, a remove */
    for (int round = 0; round < 3; round++) {
      if (cd_region_show(&region, round != 1)) {
        cd_at(region_parent, 0);
        for (int i = 0; i < 3; i++) {
          cd_id li = cd_id_new();
          cd_open(L("li")); cd_name_open(li); cd_text_num((uint32_t)(round * 10 + i)); cd_close();
        }
        cd_region_end(&region);
      }
    }
    cd_move(held, moved);
    cd_detach(moved);
    cd_set_text(label, L("detached: "));
    cd_attach(moved);
    cd_remove(svg_c);
    break;
  case 3: /* reads after writes in the same entry */
    nrects = 0;
    cd_set_style_num(box, L("width"), 60, CD_UNIT_PX);
    cd_rect(box, &rects[4 * nrects++]);
    cd_set_style(box, L("transform"), L("scale(2)"));
    cd_rect(box, &rects[4 * nrects++]);
    style4(held, 0, 0, "none", "none");
    cd_rect(held, &rects[4 * nrects++]);
    static const uint32_t k4[] = {14, 13, 12, 1, 5};
    list_to(k4, 5);
    cd_rect(list.items[0].node, &rects[4 * nrects++]);
    nhits = 0;
    hits[nhits++] = (int32_t)cd_hit(5, 5);
    hits[nhits++] = (int32_t)cd_hit(900, 700);
    break;
  }
}
EXPORT("test_steps") uint32_t test_steps(void) { return 4; }
EXPORT("test_rects") double *test_rects(void) { return rects; }
EXPORT("test_nrects") uint32_t test_nrects(void) { return nrects; }
EXPORT("test_hit") int32_t test_hit(uint32_t i) { return i < nhits ? hits[i] : -1; }
EXPORT("test_events") uint32_t test_events(void) { return ev_sum * 1000 + ev_n; }
EXPORT("test_live") uint32_t test_live(void) { return cd_ids_live(); }
