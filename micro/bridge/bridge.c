/* The bridge micro: the same C on cdom.h for every arm (each arm is a generated cdom.c and shim,
 * gen_arms.py), N operations of one kind inside one export, timed by the page (bridge.html).
 * Only to explain the page numbers (RESULTS.md); the page workloads judge the arms. */
#include "cdom.h"

#define EXPORT(n) __attribute__((export_name(n)))

enum {
  K_STYLE_NUM,  /* cd_set_style_num(node, "left", v, px) */
  K_STYLE_TMP,  /* cd_set_style(node, "transform", a one-off string) */
  K_SET_TEXT,   /* cd_set_text(text node, an interned label) */
  K_ATTR,       /* cd_set_attr(node, "data-x", an interned label) */
  K_MOUNT,      /* open div, attr, class, text, close, appended (cleared every 256) */
  K_CLONE,      /* a template's two slots set, then cloned and appended (cleared every 256) */
  K_GHOST,      /* one ghost's frame: left, top (numbers), transform, filter (one-off strings) */
  K_FRAME,      /* one card-table frame: 2 ghosts, a coin's transform, the drag shadow's left and top */
  K_NOW,        /* cd_now() */
  K_KINDS
};

static cd_id node, text_node, coin, shadow, ghost[2], root;
static cd_str labels[8];
static uint32_t events;
static int prevent;
#define T_ROW 0

void cd_main(void) {
  static const char *const L[8] = {"pretty", "large", "big", "small", "tall", "short", "long", "huge"};
  for (int i = 0; i < 8; i++) labels[i] = cd_internz(L[i]);
  node = cd_id_new(); text_node = cd_id_new(); coin = cd_id_new(); shadow = cd_id_new(); root = cd_id_new();
  ghost[0] = cd_id_new(); ghost[1] = cd_id_new();
  cd_at(0, 0);
  cd_open(CD_LIT("div")); cd_listen(node, CD_EV_CLICK); /* names the open div: it is the last node made */
  cd_attr(CD_LIT("style"), CD_LIT("position: absolute; left: 0px; top: 0px;"));
  cd_text(labels[0]); cd_name(text_node);
  cd_close();
  cd_open(CD_LIT("div")); cd_name_open(coin); cd_close();
  cd_open(CD_LIT("div")); cd_name_open(shadow); cd_attr(CD_LIT("style"), CD_LIT("position: absolute;")); cd_close();
  for (int g = 0; g < 2; g++) { cd_open(CD_LIT("div")); cd_name_open(ghost[g]); cd_attr(CD_LIT("style"), CD_LIT("position: absolute;")); cd_close(); }
  cd_open(CD_LIT("div")); cd_name_open(root); cd_close();
  /* the row template: <tr><td>slot 0</td><td><a>slot 1</a></td></tr> */
  cd_at(CD_DETACHED, 0);
  cd_open(CD_LIT("tr"));
  cd_open(CD_LIT("td")); cd_text(CD_LIT(" ")); cd_slot(0); cd_close();
  cd_open(CD_LIT("td")); cd_open(CD_LIT("a")); cd_text(CD_LIT(" ")); cd_slot(1); cd_close(); cd_close();
  cd_close();
  cd_tpl_save(T_ROW);
}

void cd_on_event(const cd_event_rec *e) {
  events += (uint32_t)e->type + 1;
  if (prevent) cd_prevent_default();
}

/* "translate(Npx, 0px)" and friends: a one-off string, as the port's sb_tmp makes them. */
static cd_str tmp_num(const char *pre, double v, uint32_t unit, const char *post) {
  char b[96];
  uint32_t n = 0;
  while (*pre) b[n++] = *pre++;
  n += cd_fmt_len(b + n, v, unit);
  while (*post) b[n++] = *post++;
  return cd_tmp(b, n);
}

static void ghost_frame(cd_id g, uint32_t i) {
  double p = (double)(i & 63) / 64.0;
#ifdef CDOM_FUSED
  cd_style_begin(g);
  cd_style_num(CD_LIT("left"), 100 + 300 * p, CD_UNIT_PX);
  cd_style_num(CD_LIT("top"), 200 - 100 * p, CD_UNIT_PX);
  cd_style_str(CD_LIT("transform"), tmp_num("translate(-50%, -50%) rotate(", 360 * p, CD_UNIT_DEG, ") scale(1.2)"));
  cd_style_str(CD_LIT("filter"), tmp_num("drop-shadow(0 ", 6 * p, CD_UNIT_PX, " 8px rgba(0,0,0,0.4))"));
  cd_style_end();
#else
  cd_set_style_num(g, CD_LIT("left"), 100 + 300 * p, CD_UNIT_PX);
  cd_set_style_num(g, CD_LIT("top"), 200 - 100 * p, CD_UNIT_PX);
  cd_set_style(g, CD_LIT("transform"), tmp_num("translate(-50%, -50%) rotate(", 360 * p, CD_UNIT_DEG, ") scale(1.2)"));
  cd_set_style(g, CD_LIT("filter"), tmp_num("drop-shadow(0 ", 6 * p, CD_UNIT_PX, " 8px rgba(0,0,0,0.4))"));
#endif
}

static void drag_move(uint32_t i) {
#ifdef CDOM_FUSED
  cd_style_begin(shadow);
  cd_style_num(CD_LIT("left"), (double)(i & 511), CD_UNIT_PX);
  cd_style_num(CD_LIT("top"), (double)(300 - (i & 127)), CD_UNIT_PX);
  cd_style_end();
#else
  cd_set_style_num(shadow, CD_LIT("left"), (double)(i & 511), CD_UNIT_PX);
  cd_set_style_num(shadow, CD_LIT("top"), (double)(300 - (i & 127)), CD_UNIT_PX);
#endif
}

static double sink;
EXPORT("bench_run") void bench_run(uint32_t kind, uint32_t n) {
  for (uint32_t i = 0; i < n; i++) {
    switch (kind) {
    case K_STYLE_NUM: cd_set_style_num(node, CD_LIT("left"), (double)(i & 255), CD_UNIT_PX); break;
    case K_STYLE_TMP: cd_set_style(node, CD_LIT("transform"), tmp_num("translate(", (double)(i & 255), CD_UNIT_PX, ", 0px)")); break;
    case K_SET_TEXT: cd_set_text(text_node, labels[i & 7]); break;
    case K_ATTR: cd_set_attr(node, CD_LIT("data-x"), labels[i & 7]); break;
    case K_MOUNT:
      if (!(i & 255)) cd_clear(root);
      cd_at(root, 0);
      cd_open(CD_LIT("div")); cd_attr(CD_LIT("data-k"), labels[i & 7]); cd_class(labels[(i + 1) & 7]); cd_text(labels[(i + 2) & 7]); cd_close();
      break;
    case K_CLONE:
      if (!(i & 255)) cd_clear(root);
      cd_at(root, 0);
      cd_tpl_text_num(T_ROW, 0, i);
      cd_tpl_text(T_ROW, 1, labels[i & 7]);
      cd_clone(T_ROW); cd_close();
      break;
    case K_GHOST: ghost_frame(ghost[0], i); break;
    case K_FRAME:
      ghost_frame(ghost[0], i);
      ghost_frame(ghost[1], i + 17);
      cd_set_style(coin, CD_LIT("transform"), tmp_num("rotateY(", (double)(i & 127), CD_UNIT_DEG, ")"));
      drag_move(i);
      break;
    case K_NOW: sink += cd_now(); break;
    }
  }
  cd_flush();
}
EXPORT("bench_kinds") uint32_t bench_kinds(void) { return K_KINDS; }
EXPORT("bench_events") uint32_t bench_events(void) { return events; }
EXPORT("bench_prevent") void bench_prevent(int on) { prevent = on; }
EXPORT("bench_sink") double bench_sink(void) { return sink; }
