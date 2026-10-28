/* Unit cases for what the foolish port needed from cdom (PERF_LOG.md, "Port fixes"). Each export
 * builds something; tests/unit.html checks the DOM and C's counters and reports by name. */
#include "../src/cdom.h"
#include "../src/cdom_net.h"

#define EXPORT(n) __attribute__((export_name(n)))
#define L(s) CD_LIT(s)

void cd_main(void) {}

/* Every case builds under its own section: <section id=NAME>. */
static cd_id section(const char *name) {
  cd_id id = cd_id_new();
  cd_at(CD_MOUNT, 0);
  cd_open(L("section"));
  cd_attr(L("id"), cd_internz(name));
  cd_name_open(id);
  cd_close();
  return id;
}

/* 1. A template cloned inside an open element is that element's child. */
EXPORT("t_clone_inside") void t_clone_inside(void) {
  cd_id s = section("clone");
  cd_at(CD_DETACHED, 0);
  cd_open(L("b")); cd_text(L(" ")); cd_slot(0); cd_close();
  cd_tpl_save(3);
  cd_at(s, 0);
  cd_open(L("p"));
  for (int i = 0; i < 2; i++) { cd_tpl_text(3, 0, i ? L("two") : L("one")); cd_clone(3); cd_close(); }
  cd_close();
  cd_clone(3); cd_close(); /* a root clone, as before */
}

/* 2. <svg> and what is inside it are SVG; the namespace ends where the svg closes, clones included. */
EXPORT("t_svg") void t_svg(void) {
  cd_id s = section("svg");
  cd_at(CD_DETACHED, 0);
  cd_open(L("g")); cd_close(); /* outside an svg: a template of an HTML <g> */
  cd_tpl_save(4);
  cd_at(s, 0);
  cd_open(L("div"));
  cd_open(L("svg")); cd_attr(L("viewBox"), L("0 0 4 4"));
  cd_open(L("g")); cd_open(L("path")); cd_attr(L("d"), L("M0 0 L4 4")); cd_close(); cd_close();
  cd_clone(4); cd_close();
  cd_open(L("circle")); cd_close();
  cd_close();
  cd_open(L("span")); cd_close();
  cd_close();
}

/* Leaves a walk pending and calls no cd_flush: the entry's export must send it. Every hook starts
 * with enter(), which counts an entry that found something still pending from the one before. */
static void hold(cd_id id) { cd_sel(id); cd_up(); }
static uint32_t stale;
static void enter(void) { stale += cd_pending() != 0; }
EXPORT("t_stale") uint32_t t_stale(void) { enter(); return stale; }

/* 3. Timers and frames come back through the app's hooks, and the export flushes after them:
 * the hook interns a string the DOM never uses and holds a walk, and the page sees both cross. */
static cd_id timer_txt, raf_txt;
EXPORT("t_time") void t_time(void) {
  cd_id s = section("time");
  cd_at(s, 0);
  cd_open(L("i")); cd_text(L("waiting")); cd_name(timer_txt = cd_id_new()); cd_close();
  cd_open(L("i")); cd_text(L("waiting")); cd_name(raf_txt = cd_id_new()); cd_close();
  cd_timer_start(5, 41);
  cd_raf_request(42);
  cd_flush();
}
void cd_on_timer(uint32_t token) {
  enter();
  cd_set_text_num(timer_txt, token);
  cd_intern("timer-flushed", 13);
  hold(timer_txt);
}
void cd_on_raf(uint32_t token, double t) {
  enter();
  cd_set_text_num(raf_txt, t > 0 ? token : 0);
  hold(raf_txt);
}

/* 5. Window events through the event path; the viewport read from C. */
static cd_id vw_txt;
static int32_t resized_w = -1, resized_h = -1, hidden = -1;
EXPORT("t_window") void t_window(void) {
  cd_id s = section("window");
  cd_at(s, 0);
  cd_open(L("i")); cd_text_num(0); cd_name(vw_txt = cd_id_new()); cd_close();
  cd_listen_window(CD_EV_RESIZE);
  cd_listen_window(CD_EV_VISIBILITY);
  cd_flush();
}
EXPORT("t_viewport") int32_t t_viewport(int32_t which) {
  cd_viewport_rec v;
  cd_viewport(&v);
  return which == 0 ? v.w : which == 1 ? v.h : which == 2 ? v.hidden : which == 3 ? resized_w : which == 4 ? resized_h : hidden;
}
void cd_on_event(const cd_event_rec *e) {
  enter();
  if (e->type == CD_EV_RESIZE && e->id == CD_MOUNT) {
    resized_w = e->x; resized_h = e->y;
    cd_set_text_num(vw_txt, (uint32_t)e->x); /* no cd_flush: the event export does it */
    hold(vw_txt);
  } else if (e->type == CD_EV_VISIBILITY) hidden = e->key;
}

/* 7. A keyed list followed by a fixed sibling: tail inserts land before it. */
static cd_list tail_list;
static uint32_t tail_keys[8];
static cd_id tail_create(void *ctx, uint32_t i, uint32_t *data) {
  (void)ctx; *data = 0;
  cd_id id = cd_id_new();
  cd_open(L("li")); cd_name_open(id); cd_text_num(tail_keys[i]); cd_close();
  return id;
}
static void nop_patch(void *ctx, uint32_t i, cd_item *it) { (void)ctx; (void)i; (void)it; }
static void nop_destroy(void *ctx, cd_item *it) { (void)ctx; (void)it; }
static const cd_list_fns TAIL = {0, tail_create, nop_patch, nop_destroy};
EXPORT("t_tail") void t_tail(uint32_t step) {
  static const uint32_t K[][4] = {{1, 2}, {1, 2, 3}, {3, 1, 2, 4}, {4}, {0}};
  static const uint32_t N[] = {2, 3, 4, 1, 0};
  if (step == 0) {
    cd_id s = section("tail");
    cd_at(s, 0);
    cd_open(L("ul")); cd_name_open(tail_list.parent = cd_id_new());
    cd_open(L("li")); cd_name_open(tail_list.end = cd_id_new()); cd_text(L("end")); cd_close();
    cd_close();
  }
  for (uint32_t i = 0; i < N[step]; i++) tail_keys[i] = K[step][i];
  cd_list_update(&tail_list, tail_keys, N[step], &TAIL);
  cd_flush();
}

/* 8, 9. Regions and lists, filled and taken away thousands of times: ids and nodes stay flat.
 * Each item names a sub node (freed by destroy); each region names three nodes and holds a
 * list of its own, dropped with cd_list_forget when the region goes. */
static cd_list leak_list, inner[2];
static uint32_t leak_keys[64];
static cd_id sub_of[128];
static cd_id leak_create(void *ctx, uint32_t i, uint32_t *data) {
  (void)ctx;
  cd_id id = cd_id_new();
  cd_open(L("li")); cd_name_open(id);
  cd_open(L("b")); cd_name_open(sub_of[leak_keys[i]] = cd_id_new()); cd_text_num(leak_keys[i]); cd_close();
  cd_close();
  *data = leak_keys[i];
  return id;
}
static void leak_destroy(void *ctx, cd_item *it) { (void)ctx; cd_id_free(sub_of[it->data]); }
static const cd_list_fns LEAK = {0, leak_create, nop_patch, leak_destroy};
static cd_region opt[2]; /* opt[0] parent mode, opt[1] root mode */
static cd_id root_box;

EXPORT("t_leak") void t_leak(uint32_t rounds) {
  static uint32_t rng = 7;
  if (!root_box) {
    cd_id s = section("leak");
    cd_at(s, 0);
    cd_open(L("ul")); cd_name_open(leak_list.parent = cd_id_new()); leak_list.owns_parent = 1; cd_close();
    cd_open(L("div")); cd_name_open(opt[0].parent = cd_id_new()); cd_close();
    cd_open(L("div")); cd_name_open(root_box = cd_id_new()); cd_close();
  }
  for (uint32_t r = 0; r < rounds; r++) {
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    uint32_t n = rng % 40;
    for (uint32_t i = 0; i < n; i++) leak_keys[i] = (rng >> 8) % 2 ? i : 63 - i;
    cd_list_update(&leak_list, leak_keys, n, &LEAK);
    for (int k = 0; k < 2; k++) {
      cd_region *o = &opt[k];
      int want = (rng >> (12 + k)) & 1, rebuild = (rng >> (20 + k)) % 3 == 0;
      if (o->shown && (!want || rebuild)) { cd_list_forget(&inner[k], &LEAK); cd_region_hide(o); }
      if (cd_region_show(o, want)) {
        cd_at(k ? root_box : o->parent, 0);
        cd_open(L("div"));
        if (k) cd_name_open(o->root);
        cd_open(L("i")); cd_name_open(cd_id_new()); cd_text(L("x")); cd_name(cd_id_new()); cd_close();
        cd_open(L("ol")); cd_name_open(inner[k].parent = cd_id_new()); cd_close();
        cd_close();
        if (!k) { cd_open(L("hr")); cd_name_open(cd_id_new()); cd_close(); } /* parent mode: a second root */
        /* the inner list, filled while the region records: its ids stay the list's own */
        for (uint32_t i = 0; i < 3; i++) leak_keys[i] = 100 + 10 * (uint32_t)k + i;
        cd_list_update(&inner[k], leak_keys, 3, &LEAK);
        cd_region_end(o);
      }
    }
  }
  /* end empty, so every call compares like with like */
  cd_list_update(&leak_list, leak_keys, 0, &LEAK);
  for (int k = 0; k < 2; k++)
    if (opt[k].shown) { cd_list_forget(&inner[k], &LEAK); cd_region_hide(&opt[k]); }
}
EXPORT("t_ids_live") uint32_t t_ids_live(void) { return cd_ids_live(); }
EXPORT("t_ids_high") uint32_t t_ids_high(void) { return cd_ids_high(); }
EXPORT("t_free_twice") void t_free_twice(void) { cd_id a = cd_id_new(); cd_id_free(a); cd_id_free(a); }

/* 10. cd_name_open names the open element after children were built in it. */
EXPORT("t_name_open") void t_name_open(void) {
  cd_id s = section("nameopen"), el = cd_id_new();
  cd_at(s, 0);
  cd_open(L("div"));
  cd_open(L("b")); cd_text(L("child")); cd_close();
  cd_name_open(el);
  cd_close();
  cd_set_class(el, L("named"));
  cd_flush();
}

/* 11. One style property at a time, by string (transient or interned) and by number; transient
 * strings leave the table as it was. */
static cd_id styled;
EXPORT("t_style") uint32_t t_style(uint32_t n) {
  cd_id s = section("style");
  cd_at(s, 0);
  cd_open(L("div")); cd_name_open(styled = cd_id_new()); cd_close();
  uint32_t before = cd_str_count();
  char buf[32];
  for (uint32_t i = 0; i < n; i++) {
    uint32_t k = cd_fmt(buf, (int32_t)i, 0, CD_UNIT_PX);
    cd_set_style(styled, L("left"), cd_tmp(buf, k));
    cd_set_style_num(styled, L("top"), i + 0.5, CD_UNIT_PX);
  }
  cd_set_style(styled, L("color"), L("red"));
  cd_set_style_num(styled, L("width"), 12.25, CD_UNIT_EM);
  cd_set_style_num(styled, L("opacity"), 0.5, CD_UNIT_NONE);
  cd_set_attr(styled, L("data-t"), cd_tmpz("one-off \xc3\xa9"));
  cd_flush();
  return cd_str_count() - before; /* the 7 literals and 3 units above (10), none of the n values */
}
EXPORT("t_tmp_stale") void t_tmp_stale(void) {
  cd_str t = cd_tmpz("stale");
  cd_set_class(styled, L("x")); /* another op crosses first */
  cd_set_class(styled, t);       /* traps */
}

/* 12. A literal per call site. */
static cd_str lit_a(void) { return CD_LIT("alpha"); }
static cd_str lit_b(void) { return CD_LIT("beta"); }
EXPORT("t_lit") uint32_t t_lit(void) {
  for (int i = 0; i < 3; i++)
    if (lit_a() != cd_internz("alpha") || lit_b() != cd_internz("beta") || lit_a() == lit_b()) return 0;
  return 1;
}

/* 13. The formatter: the page compares with String(v / 10 ** d) + unit. */
static char fmt_buf[64];
EXPORT("t_fmt") uint32_t t_fmt(int32_t v, uint32_t d, uint32_t unit) { return cd_fmt(fmt_buf, v, d, unit); }
EXPORT("t_fmt_len") uint32_t t_fmt_len(double v, uint32_t unit) { return cd_fmt_len(fmt_buf, v, unit); }
EXPORT("t_fmt_buf") char *t_fmt_buf(void) { return fmt_buf; }

/* 4. The network against the page server's echo: a binary WebSocket message out and back, then
 * an HTTP POST answered reversed. No cd_flush: the network exports flush. */
static const uint8_t MSG[] = {0, 1, 2, 3, 0x7f, 0x80, 0xfe, 0xff};
static cd_id net_txt;
static int ws_ok;
static void say(const char *s) { cd_set_text(net_txt, cd_internz(s)); }
EXPORT("t_net") void t_net(uint32_t port) {
  cd_id s = section("net");
  cd_at(s, 0);
  cd_open(L("i")); cd_text(L("waiting")); cd_name(net_txt = cd_id_new()); cd_close();
  char url[40];
  uint32_t n = 0;
  for (const char *c = "ws://127.0.0.1:"; *c; c++) url[n++] = *c;
  n += cd_fmt(url + n, (int32_t)port, 0, CD_UNIT_NONE);
  for (const char *c = "/ws"; *c; c++) url[n++] = *c;
  cd_ws_open(url, n);
  cd_flush();
}
void cd_on_ws_open(uint32_t id) { enter(); cd_ws_send(id, MSG, sizeof MSG); hold(net_txt); }
void cd_on_ws_msg(uint32_t id, const uint8_t *p, uint32_t len) {
  enter();
  ws_ok = len == sizeof MSG;
  for (uint32_t i = 0; ws_ok && i < len; i++) ws_ok = p[i] == MSG[i];
  say(ws_ok ? "ws ok" : "ws wrong bytes");
  cd_ws_close(id);
  cd_http(7, "POST", 4, "/echo", 5, MSG, sizeof MSG);
  hold(net_txt);
}
void cd_on_http(uint32_t token, uint32_t status, const uint8_t *p, uint32_t len) {
  enter();
  int ok = token == 7 && status == 200 && len == sizeof MSG;
  for (uint32_t i = 0; ok && i < len; i++) ok = p[i] == MSG[len - 1 - i];
  say(!ws_ok ? "ws failed" : ok ? "ws ok, http ok" : "http wrong");
  hold(net_txt);
}
