/* Unit cases for what the foolish port needed from cdom (PERF_LOG.md, "Port fixes"). Each export
 * builds something; tests/unit.html checks the DOM and C's counters and reports by name. */
#include "../src/cdom.h"
#include "../src/cdom_net.h"
#include "../src/cdom_store.h"
#include "../src/cdom_audio.h"
#include "../src/cdom_random.h"
#include "../src/cdom_url.h"
#include "../src/cdom_locale.h"
#include "../src/cdom_share.h"
#include "../src/cdom_fonts.h"
#include "../src/cdom_ui.h"

#define EXPORT(n) __attribute__((export_name(n)))

/* When the module started (the store's early open is checked against it). */
static double started_at;
void cd_main(void) {
  started_at = cd_now();
  /* No cd_at: the app starts at the end of the mount, so this lands there. */
  cd_open("i"); cd_attr("id", "default-at"); cd_close();
}
EXPORT("t_started") double t_started(void) { return started_at; }

/* Every case builds under its own section: <section id=NAME>. */
static cd_id section(const char *name) {
  cd_id id = cd_id_new();
  cd_at(CD_MOUNT, CD_APPEND);
  cd_open("section");
  cd_attr("id", name);
  cd_name_open(id);
  cd_close();
  return id;
}

/* 1. A template cloned inside an open element is that element's child. */
EXPORT("t_clone_inside") void t_clone_inside(void) {
  cd_id s = section("clone");
  cd_at(CD_DETACHED, CD_APPEND);
  cd_open("b"); cd_text(" "); cd_slot(0); cd_close();
  cd_tpl_save(3);
  cd_at(s, CD_APPEND);
  cd_open("p");
  for (int i = 0; i < 2; i++) { cd_tpl_text(3, 0, i ? cd_s("two") : cd_s("one")); cd_clone(3); cd_close(); }
  cd_close();
  cd_clone(3); cd_close(); /* a root clone, as before */
}

/* 1b. A template slot set from a runtime string: the text behind it changes between calls, so it is never
 * skipped as unchanged, and a literal after it is written again. Each step is its own call. */
EXPORT("t_tpl_tmp") void t_tpl_tmp(uint32_t step) {
  static cd_id s;
  if (step == 0) {
    s = section("tpltmp");
    cd_at(CD_DETACHED, CD_APPEND);
    cd_open("i"); cd_text(" "); cd_slot(0); cd_close();
    cd_tpl_save(5);
  }
  static const char *const T[] = {"alpha", "beta", 0, 0, "gamma", 0};
  static char buf[8]; /* mutable, so cd_tmpz makes a runtime string of it (read-only text would be a literal-like id) */
  if (T[step]) { uint32_t n = 0; while ((buf[n] = T[step][n])) n++; }
  cd_tpl_text(5, 0, T[step] ? cd_tmpz(buf) : cd_s("lit"));
  cd_at(s, CD_APPEND);
  cd_clone(5);
  cd_close();
}

/* 2. <svg> and what is inside it are SVG; the namespace ends where the svg closes, clones included. */
EXPORT("t_svg") void t_svg(void) {
  cd_id s = section("svg");
  cd_at(CD_DETACHED, CD_APPEND);
  cd_open("g"); cd_close(); /* outside an svg: a template of an HTML <g> */
  cd_tpl_save(4);
  cd_at(s, CD_APPEND);
  cd_open("div");
  cd_open("svg"); cd_attr("viewBox", "0 0 4 4");
  cd_open("g"); cd_open("path"); cd_attr("d", "M0 0 L4 4"); cd_close(); cd_close();
  cd_clone(4); cd_close();
  cd_open("circle"); cd_close();
  cd_close();
  cd_open("span"); cd_close();
  cd_close();
}

/* Leaves a walk pending and calls no cd_flush: the entry's export must send it. Every hook starts
 * with enter(), which counts an entry that found something still pending from the one before. */
static void hold(cd_id id) { cd_sel(id); cd_up(); }
static uint32_t stale;
static void enter(void) { stale += cd_pending() != 0; }
EXPORT("t_stale") uint32_t t_stale(void) { enter(); return stale; }

/* 3. Timers and frames come back through the app's hooks, and the export flushes after them:
 * the hook holds a walk, and the page sees it cross. */
static cd_id timer_txt, raf_txt;
EXPORT("t_time") void t_time(void) {
  cd_id s = section("time");
  cd_at(s, CD_APPEND);
  cd_open("i"); cd_text("waiting"); cd_name_text(timer_txt = cd_id_new()); cd_close();
  cd_open("i"); cd_text("waiting"); cd_name_text(raf_txt = cd_id_new()); cd_close();
  cd_timer_start(5, 41);
  cd_raf_request(42);
  cd_flush();
}
static uint32_t app_timer_heard[10], cdom_token_heard; /* tokens 50 to 59 (section 27); any in cdom's range */
void cd_on_timer(uint32_t token) {
  enter();
  if (token >= CD_TOKEN_CDOM) cdom_token_heard++;
  if (token >= 50 && token < 60) { app_timer_heard[token - 50]++; return; }
  cd_set_text_node_num(timer_txt, token);
  hold(timer_txt);
}
static double raf_t;
EXPORT("t_raf_t") double t_raf_t(void) { return raf_t; }
EXPORT("t_now") double t_now(void) { return cd_now(); }
void cd_on_raf(uint32_t token, double t) {
  enter();
  raf_t = t;
  cd_set_text_node_num(raf_txt, t > 0 ? token : 0);
  hold(raf_txt);
}

/* 5. Window events through the event path; the viewport read from C. */
static cd_id vw_txt;
static int32_t resized_w = -1, resized_h = -1, hidden = -1;
EXPORT("t_window") void t_window(void) {
  cd_id s = section("window");
  cd_at(s, CD_APPEND);
  cd_open("i"); cd_text_num(0); cd_name_text(vw_txt = cd_id_new()); cd_close();
  cd_listen_window(CD_EV_RESIZE);
  cd_listen_window(CD_EV_VISIBILITY);
  cd_flush();
}
EXPORT("t_viewport") int32_t t_viewport(int32_t which) {
  cd_viewport_rec v;
  cd_viewport(&v);
  return which == 0 ? v.w : which == 1 ? v.h : which == 2 ? v.hidden : which == 6 ? v.dpr100 : which == 3 ? resized_w : which == 4 ? resized_h : hidden;
}
/* 5b. A node's box, read from C: where the styles put it, and after a transform set in the same
 * entry (no flush between), the transformed box. */
static double rect_at[8];
EXPORT("t_rect") void t_rect(void) {
  cd_id s = section("rect"), id = cd_id_new();
  cd_at(s, CD_APPEND);
  cd_open("div");
  cd_attr("style", "position: fixed; left: 10px; top: 20px; width: 30px; height: 40px;");
  cd_name_open(id);
  cd_close();
  cd_rect(id, rect_at);
  cd_set_style(id, "transform", "scale(2)");
  cd_rect(id, rect_at + 4);
}
EXPORT("t_rect_at") double t_rect_at(int32_t k) { return rect_at[k]; }
/* 5c. A drag's and a key's events: a press delegated to a listening box (mouse and touch) with
 * its point, the document's moves and keys while C listens on it, the default action stopped when
 * C asks, and the node under a point (cd_hit) through an overlay that takes no pointer events. */
static cd_id input_box, input_other, input_layer;
static int32_t ev_rec[9], ev_count, prevent_next;
EXPORT("t_input") void t_input(void) {
  cd_id s = section("input");
  cd_at(s, CD_APPEND);
  cd_open("div");
  cd_attr("style", "position: fixed; left: 100px; top: 100px; width: 100px; height: 50px;");
  cd_listen(input_box = cd_id_new(), CD_EV_MOUSEDOWN);
  cd_listen(input_box, CD_EV_TOUCHSTART);
  cd_open("span"); cd_text("inside"); cd_close();
  cd_close();
  cd_open("div"); /* over the box, and seen through */
  cd_attr("style", "position: fixed; left: 90px; top: 90px; width: 200px; height: 80px; pointer-events: none;");
  cd_close();
  cd_open("div");
  cd_attr("style", "position: fixed; left: 300px; top: 100px; width: 40px; height: 40px;");
  cd_listen(input_other = cd_id_new(), CD_EV_CLICK);
  cd_close();
  cd_open("div"); /* over input_other's left half, taking pointer events; it listened, and listens no more */
  cd_attr("style", "position: fixed; left: 300px; top: 100px; width: 20px; height: 40px;");
  cd_listen(input_layer = cd_id_new(), CD_EV_CLICK);
  cd_close();
  cd_unlisten(input_layer, CD_EV_CLICK);
  cd_open("input"); cd_close();
  cd_id freed = cd_id_new(); /* listened, then its id freed with the node left in place: no longer a target */
  cd_open("div");
  cd_attr("style", "position: fixed; left: 400px; top: 100px; width: 40px; height: 40px;");
  cd_listen(freed, CD_EV_CLICK);
  cd_close();
  cd_id_free(freed);
}
EXPORT("t_input_id") int32_t t_input_id(int32_t which) { return (int32_t)(which == 2 ? input_layer : which ? input_other : input_box); }
EXPORT("t_listen_win") void t_listen_win(int32_t type, int32_t on) {
  if (on) cd_listen_window((uint32_t)type);
  else cd_unlisten_window((uint32_t)type);
}
EXPORT("t_prevent") void t_prevent(int32_t on) { prevent_next = on; }
/* The last event's type, id, x, y, mods, key, ch, how many came (k = 7), and its time (k = 8). */
EXPORT("t_ev") int32_t t_ev(int32_t k) { return k == 7 ? ev_count : ev_rec[k]; }
EXPORT("t_hit") int32_t t_hit(int32_t x, int32_t y) { return (int32_t)cd_hit(x, y); }

static int32_t loaded_id = -1, loaded_key = -1;
static void deferred_event(const cd_event_rec *e);
void cd_on_event(const cd_event_rec *e) {
  enter();
  if (e->type == CD_EV_RESIZE && e->id == CD_WINDOW) { /* heard on the window, named so */
    resized_w = e->x; resized_h = e->y;
    cd_set_text_node_num(vw_txt, (uint32_t)e->x); /* no cd_flush: the event export does it */
    hold(vw_txt);
  } else if (e->type == CD_EV_VISIBILITY) hidden = e->key;
  else {
    const int32_t rec[9] = {e->type, e->id, e->x, e->y, e->mods, e->key, e->ch, 0, e->ms};
    for (int k = 0; k < 9; k++) ev_rec[k] = rec[k];
    ev_count++;
    if (prevent_next) cd_prevent_default();
    deferred_event(e);
  }
}

/* 7. A keyed list followed by a fixed sibling: tail inserts land before it. */
static cd_list tail_list;
static uint32_t tail_keys[8];
static cd_id tail_create(void *, uint32_t i, uint32_t *data) {
  *data = 0;
  cd_id id = cd_id_new();
  cd_open("li"); cd_name_open(id); cd_text_num(tail_keys[i]); cd_close();
  return id;
}
static void nop_patch(void *, uint32_t, cd_item *) { }
static void nop_destroy(void *, cd_item *) { }
static const cd_list_fns TAIL = {0, tail_create, nop_patch, nop_destroy};
EXPORT("t_tail") void t_tail(uint32_t step) {
  static const uint32_t K[][4] = {{1, 2}, {1, 2, 3}, {3, 1, 2, 4}, {4}, {0}};
  static const uint32_t N[] = {2, 3, 4, 1, 0};
  if (step == 0) {
    cd_id s = section("tail");
    cd_at(s, CD_APPEND);
    cd_open("ul"); cd_name_open(tail_list.parent = cd_id_new());
    cd_open("li"); cd_name_open(tail_list.end = cd_id_new()); cd_text("end"); cd_close();
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
static cd_id leak_create(void *, uint32_t i, uint32_t *data) {
  cd_id id = cd_id_new();
  cd_open("li"); cd_name_open(id);
  cd_open("b"); cd_name_open(sub_of[leak_keys[i]] = cd_id_new()); cd_text_num(leak_keys[i]); cd_close();
  cd_close();
  *data = leak_keys[i];
  return id;
}
static void leak_destroy(void *, cd_item *it) { cd_id_free(sub_of[it->data]); }
static const cd_list_fns LEAK = {0, leak_create, nop_patch, leak_destroy};
static cd_region opt[2]; /* opt[0] parent mode, opt[1] root mode */
static cd_id root_box;

EXPORT("t_leak") void t_leak(uint32_t rounds) {
  static uint32_t rng;
  if (!root_box) {
    rng = cd_seed | 1; /* unit.html fixes cd_seed, so the rounds are the same every run */
    cd_id s = section("leak");
    cd_at(s, CD_APPEND);
    cd_open("ul"); cd_name_open(leak_list.parent = cd_id_new()); leak_list.owns_parent = 1; cd_close();
    cd_open("div"); cd_name_open(opt[0].parent = cd_id_new()); cd_close();
    cd_open("div"); cd_name_open(root_box = cd_id_new()); cd_close();
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
        cd_at(k ? root_box : o->parent, CD_APPEND);
        cd_open("div");
        if (k) cd_name_open(o->root);
        cd_open("i"); cd_name_open(cd_id_new()); cd_text("x"); cd_name_text(cd_id_new()); cd_close();
        cd_open("ol"); cd_name_open(inner[k].parent = cd_id_new()); cd_close();
        cd_close();
        if (!k) { cd_open("hr"); cd_name_open(cd_id_new()); cd_close(); } /* parent mode: a second root */
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
  cd_at(s, CD_APPEND);
  cd_open("div");
  cd_open("b"); cd_text("child"); cd_close();
  cd_name_open(el);
  cd_close();
  cd_set_class(el, "named");
  cd_flush();
}

/* 10b. cd_name_el names the element opened last though a text came after it (in its parent), and
 * cd_name_text the text node made last (by cd_text, then by cd_text_num) though an element was opened
 * and closed after it: the two cases where the last node made is the other kind. */
static cd_id name_kinds_el;
EXPORT("t_name_kinds") void t_name_kinds(void) {
  cd_id s = section("namekinds"), el = cd_id_new(), txt = cd_id_new(), num = cd_id_new();
  cd_at(s, CD_APPEND);
  cd_open("div");
  cd_open("b"); cd_close();
  cd_text("after the b");
  cd_name_el(el);
  cd_text("text");
  cd_open("i"); cd_close();
  cd_name_text(txt);
  cd_text_num(7);
  cd_open("u"); cd_close();
  cd_name_text(num);
  cd_close();
  cd_set_class(el, "named");
  cd_set_text_node(txt, "renamed");
  cd_set_text_node_num(num, 8);
  cd_flush();
  name_kinds_el = el;
}
/* 10c. The same calls given the element (which: 0 cd_set_text_node, 1 cd_set_text_node_num) or an id that
 * names nothing (2): each throws, and the page is as it was. */
EXPORT("t_set_text_el") void t_set_text_el(uint32_t which) {
  if (which == 0) cd_set_text_node(name_kinds_el, "lost");
  else if (which == 1) cd_set_text_node_num(name_kinds_el, 9);
  else {
    cd_id unbound = cd_id_new(); /* handed back first, so the throw leaks no id */
    cd_id_free(unbound);
    cd_set_text_node(unbound, "lost");
  }
}

/* 11. One style property at a time, by string (a literal or a runtime string) and by number. */
static cd_id styled;
EXPORT("t_style") void t_style(uint32_t n) {
  cd_id s = section("style");
  cd_at(s, CD_APPEND);
  cd_open("div"); cd_name_open(styled = cd_id_new()); cd_close();
  char buf[32];
  for (uint32_t i = 0; i < n; i++) {
    uint32_t k = cd_fmt(buf, (int32_t)i, 0, CD_UNIT_PX);
    cd_set_style(styled, "left", cd_tmp(buf, k));
    cd_set_style_num(styled, "top", i + 0.5, CD_UNIT_PX);
  }
  cd_set_style(styled, "color", "red");
  cd_set_style_num(styled, "width", 12.25, CD_UNIT_EM);
  cd_set_style_num(styled, "opacity", 0.5, CD_UNIT_NONE);
  cd_set_attr(styled, "data-t", cd_tmpz("one-off \xc3\xa9"));
  cd_flush();
}
/* Each unit through cd_set_style_num, on a custom property so the browser keeps the text as given.
 * The same value goes through cd_fmt into an attribute: the page compares the two with a list of its
 * own, so the shim's UNITS, C's UNIT_STR and the CD_UNIT_* order cannot drift apart. */
static cd_id unit_el;
EXPORT("t_units") uint32_t t_units(void) {
  cd_id s = section("units");
  cd_at(s, CD_APPEND);
  cd_open("div"); cd_name_open(unit_el = cd_id_new()); cd_close();
  char prop[16], attr[16], val[16];
  for (uint32_t u = 0; u < CD_UNITS; u++) {
    uint32_t n = cd_fmt(prop + 3, (int32_t)u, 0, CD_UNIT_NONE);
    prop[0] = '-'; prop[1] = '-'; prop[2] = 'u';
    cd_set_style_num(unit_el, cd_tmp(prop, 3 + n), 5, u);
    __builtin_memcpy(attr, "data-u", 6);
    cd_fmt(attr + 6, (int32_t)u, 0, CD_UNIT_NONE);
    uint32_t k = cd_fmt(val, 5, 0, u);
    cd_set_attr(unit_el, cd_tmp(attr, 6 + n), cd_tmp(val, k));
  }
  cd_flush();
  return CD_UNITS;
}
/* The shim's views go stale (length 0) when memory grows, and each import that reads or writes C memory
 * must make them again first. cd_alloc of 8 MiB or more grows memory (the page checks that it did); which picks
 * the import: 0 a runtime string, 1 the viewport record, 2 a property set, 3 a method call. */
static cd_id grown_el;
EXPORT("t_grow") int32_t t_grow(uint32_t which) {
  if (!grown_el) {
    cd_at(section("grow"), CD_APPEND);
    cd_open("div"); cd_name_open(grown_el = cd_id_new()); cd_close();
    cd_flush();
  }
  /* The values of the set and the call are literals, so no string read makes the views fresh first and the
   * set or the call is what the test sees. */
  cd_val a[2] = {cd_v_str("data-called"), cd_v_str("yes")};
  if (which == 2) cd_set_num(CD_BODY, "tabIndex", 1); /* on the body: the page reads grown_el */
  if (which == 3) cd_call_args(CD_BODY, "setAttribute", 2, a);
  cd_flush();
  uint8_t *big = cd_alloc((8u << 20) * (which + 1)); /* kept, so each call grows memory again */
  big[0] = 1;
  int32_t r = 0;
  if (which == 0) {
    cd_set_attr(grown_el, "data-grown", cd_tmpz("read after the memory grew \xc3\xa9"));
  } else if (which == 1) {
    cd_viewport_rec v;
    cd_viewport(&v);
    r = v.w;
  } else if (which == 2) {
    cd_set_num(grown_el, "tabIndex", 7);
  } else {
    cd_call_args(grown_el, "setAttribute", 2, a);
  }
  cd_flush();
  return r;
}

/* 11b. The runtime read: a string of len bytes in C memory, shown through cd_set_text_node as a cd_tmp, so
 * the shim reads the bytes. ASCII letters; kind 1 to 3 put a two-byte letter (e acute) first, in the middle
 * and last, kind 4 a three-byte character (euro) and kind 5 an astral one (a grin) in the middle. The page
 * compares the text with its own decode of the bytes (t_read_buf). 0 when len is too short for the kind. */
static char read_buf[512];
static cd_id read_txt;
EXPORT("t_read_buf") char *t_read_buf(void) { return read_buf; }
EXPORT("t_read") uint32_t t_read(uint32_t len, uint32_t kind) {
  static const char *const CH[] = {"", "\xc3\xa9", "\xc3\xa9", "\xc3\xa9", "\xe2\x82\xac", "\xf0\x9f\x98\x80"};
  uint32_t w = kind == 0 ? 0 : kind <= 3 ? 2 : kind == 4 ? 3 : 4;
  if (len > sizeof read_buf || len < w) return 0;
  if (!read_txt) {
    cd_id s = section("read");
    cd_at(s, CD_APPEND);
    cd_text("");
    cd_name_text(read_txt = cd_id_new());
  }
  for (uint32_t i = 0; i < len; i++) read_buf[i] = (char)('a' + i % 26);
  uint32_t at = kind == 1 ? 0 : kind == 3 ? len - w : (len - w) / 2;
  for (uint32_t i = 0; i < w; i++) read_buf[at + i] = CH[kind][i];
  cd_set_text_node(read_txt, cd_tmp(read_buf, len));
  return 1;
}

/* 11c. Every string position, each given four ways by k: a literal, a cd_tmp, a cd_tmpz and a ternary of
 * cd_s. V(k, a, b, c, d) is the string a, b, c or d by k (all four in the page's expectations), so the
 * four ways show four different texts and a position that read the wrong kind shows the wrong one. */
static volatile int sel = 1;
#define V(k, a, b, c, d) ((k) == 0 ? cd_s(a) : (k) == 1 ? cd_tmp(b, sizeof(b) - 1) : (k) == 2 ? cd_tmpz(c) : (sel ? cd_s(d) : cd_s("unused")))
static char pos_out[4][32];
static uint32_t pos_len[4];
EXPORT("t_pos_out") char *t_pos_out(uint32_t k) { return pos_out[k & 3]; }
EXPORT("t_pos_len") uint32_t t_pos_len(uint32_t k) { return pos_len[k & 3]; }
EXPORT("t_pos") void t_pos(uint32_t k) {
  static cd_id sec;
  if (!sec) sec = section("pos");
  cd_id e = cd_id_new(), t = cd_id_new();
  cd_at(sec, CD_APPEND);
  cd_open(V(k, "mark", "kbd", "samp", "var"));                                  /* a tag */
  cd_attr(V(k, "data-a", "data-b", "data-c", "data-d"), V(k, "va", "vb", "vc", "vd")); /* an attribute's name and value */
  cd_name_open(e);
  cd_open("span"); cd_class(V(k, "ca", "cb", "cc", "cd")); cd_close();       /* a class */
  cd_text(V(k, "ta", "tb", "tc", "td"));                                        /* a text */
  cd_name_text(t);
  cd_close();
  cd_set_text_node(t, V(k, "t2a", "t2b", "t2c", "t2d"));
  cd_set_attr(e, V(k, "data-e", "data-f", "data-g", "data-h"), V(k, "vea", "veb", "vec", "ved"));
  cd_set_class(e, V(k, "c2a", "c2b", "c2c", "c2d"));
  cd_set_style(e, V(k, "width", "height", "left", "top"), V(k, "1px", "2px", "3px", "4px")); /* a style's property and value */
  cd_set_str(e, V(k, "title", "lang", "dataset.p", "style.color"), V(k, "set-a", "set-b", "set-c", "red")); /* a path and a string value */
  cd_val a[2] = {cd_v_str(V(k, "data-m", "data-m", "data-m", "data-m")), cd_v_str(V(k, "ma", "mb", "mc", "md"))}; /* cd_v_str in arguments */
  cd_call_args(e, V(k, "setAttribute", "setAttribute", "setAttribute", "setAttribute"), 2, a); /* a method */
  cd_val o[4] = {cd_v_id(e), cd_v_obj(1), cd_v_str(V(k, "id", "id", "id", "id")), cd_v_str(V(k, "oa", "ob", "oc", "od"))};
  cd_call_args(CD_WINDOW, cd_s("Object.assign"), 2, o);                          /* cd_v_str in an object, key and value */
  pos_len[k & 3] = cd_get_str(e, V(k, "title", "lang", "dataset.p", "style.color"), pos_out[k & 3], 32); /* a path read */
}

/* 11d0. A pointer into read-only data is its own id (cd_tmpz): the shim keeps its string by address, so a
 * constant string the macro cannot see is read from memory once. Mutable bytes, on the stack, in .data and on
 * the heap, are read on every use, so a reused address shows its new text. t_ro(k, i) appends one text node
 * to <section id=ro>: k picks the way the string is made, i picks which of two. */
static __attribute__((noinline)) void ro_put(const char *s) { cd_text(s); }
static const char *const RO_TABLE[] = {"table-a", "table-b"};
static const char RO_ARRAY[] = "static-const-array";
/* Two strings 256 bytes apart: the same slot of the shim's 256-entry direct-mapped cache. */
static const struct { char a[256]; char b[256]; } RO_COL = {"col-a", "col-b"};
static char ro_static[16];
static char *ro_heap;
static cd_id ro_text_node;
static void ro_copy(char *d, uint32_t i) {
  const char *s = i ? "buf-two" : "buf-one";
  while ((*d++ = *s++)) {}
}
EXPORT("t_ro") void t_ro(uint32_t k, uint32_t i) {
  static cd_id sec;
  if (!sec) sec = section("ro");
  cd_at(sec, CD_APPEND);
  switch (k) {
  case 0: cd_text(i ? "tern-yes" : "tern-no"); break;
  case 1: ro_put(i ? "help-yes" : "help-no"); break;
  case 2: cd_text(RO_TABLE[i & 1]); break;
  case 3: cd_text(RO_ARRAY); break;
  case 4: ro_copy(ro_static, i); cd_text(ro_static); break;
  case 5: { char b[16]; ro_copy(b, i); cd_text(b); break; }
  case 6:
    if (!ro_heap) ro_heap = (char *)((uintptr_t)__builtin_wasm_memory_grow(0, 1) * 65536); /* a fresh page past everything */
    ro_copy(ro_heap, i); cd_text(ro_heap); break;
  case 8: break; /* makes the section: its name goes through a helper, so it is read once here and not in a count */
  default: cd_text(i ? RO_COL.b : RO_COL.a); break;
  }
}
/* The shim reads an id above the slot range as a pointer too: a layout with read-only data after the
 * page's table must work, so the test hands it the address of bytes that lie above the table. */
EXPORT("t_ro_above") char *t_ro_above(void) { ro_copy(ro_static, 0); return ro_static; }
EXPORT("t_ro_node") cd_id t_ro_node(void) {
  static cd_id sec;
  if (!sec) sec = section("ro-above");
  cd_at(sec, CD_APPEND);
  ro_text_node = cd_id_new();
  cd_text("placeholder");
  cd_name_text(ro_text_node);
  return ro_text_node;
}

/* 11d. The life of a cd_tmp: good for the next call that takes a string. Used after another such call it
 * throws out of the call that uses it. 32 are alive at once (here as the arguments of one call); the 33rd traps. */
EXPORT("t_tmp_stale") void t_tmp_stale(void) {
  static char stale_buf[] = "stale"; /* mutable: a pointer into read-only data is its own id and never dies */
  cd_str t = cd_tmpz(stale_buf);
  cd_set_class(styled, "x"); /* another call that takes a string: t is dead */
  cd_set_class(styled, t);       /* throws out of this call */
}
EXPORT("t_tmp_cap") double t_tmp_cap(uint32_t n) {
  static char digits[40][3];
  cd_val a[40];
  for (uint32_t i = 0; i < n; i++) {
    digits[i][0] = (char)('0' + (i + 1) / 10);
    digits[i][1] = (char)('0' + (i + 1) % 10);
    a[i] = cd_v_str(cd_tmp(digits[i], 2));
  }
  cd_call_args(CD_WINDOW, "Math.max", n, a); /* the numbers as strings: the largest is n */
  return cd_get_num(CD_RESULT, CD_NONE);
}
/* 11e. A cd_tmp made before a throw is dead when the next call into C starts: the throw leaves the
 * window the cd_tmp were made in, and the shim ends it when the outermost call ends, whether it returned or
 * threw. The first export throws with `kept` alive; the second, a later call, uses it. */
static cd_str kept_tmp;
EXPORT("t_tmp_throw") void t_tmp_throw(void) {
  static char dead_buf[] = "dead", kept_buf[] = "kept"; /* mutable, so they are runtime strings */
  cd_str dead = cd_tmpz(dead_buf);
  cd_set_class(styled, "x"); /* dead is stale now */
  kept_tmp = cd_tmpz(kept_buf);   /* alive: in the window when the next line throws */
  cd_set_class(styled, dead);   /* throws out of the call, with kept alive */
}
EXPORT("t_tmp_after_throw") void t_tmp_after_throw(void) { cd_set_class(styled, kept_tmp); }

/* 11f. The batched read. 1024 strings sit in one buffer, each its number and a suffix: kind 0 is ASCII, 1
 * mixes 0, 2, 3 and 4 byte characters (and ends in an empty string), 2 is all astral. t_ahead_show shows
 * every one as a cd_tmp text node in a section of its own (its number is returned) after the announcement the mode
 * says; the page compares with the same strings read on use and with the text it expects. */
static char ah[1024 * 8];
static uint32_t ah_at[1025], ah_sections;
EXPORT("t_ahead_fill") uint32_t t_ahead_fill(uint32_t kind) {
  static const char *const SUFFIX[3][4] = {
    {"", "a", "bc", "def"},
    {"", "\xc3\xa9", "\xe6\x97\xa5", "\xf0\x9f\x98\x80"},
    {"\xf0\x9f\x98\x80", "\xf0\x9f\x98\x80", "\xf0\x9f\x98\x80", "\xf0\x9f\x98\x80"},
  };
  uint32_t n = 0;
  for (uint32_t i = 0; i < 1024; i++) {
    ah_at[i] = n;
    if (kind == 1 && i == 1023) break; /* the last string is empty, at the range's last byte */
    char d[4]; uint32_t k = 0;
    for (uint32_t v = i;; v /= 10) { d[k++] = (char)('0' + v % 10); if (v < 10) break; }
    while (k) ah[n++] = d[--k];
    for (const char *c = SUFFIX[kind][i & 3]; *c; c++) ah[n++] = *c;
  }
  ah_at[1024] = n;
  return n;
}
EXPORT("t_ahead_poke") void t_ahead_poke(void) { ah[ah_at[5]] = 'Z'; } /* string 5's first byte, a digit */
EXPORT("t_ahead_end") void t_ahead_end(void) { cd_tmp_ahead(0, 0); }
/* Modes: 0 no announcement; 1 all of it; 2 from one byte into string 100 to one byte into string 900, so
 * string 100 starts before the range, 900 ends past it, and the rest of the strings lie outside or inside;
 * 3 all, then ended by (0, 0), then string 5 changed; 4 all, string 5 changed, then another range
 * announced over string 700 only; 5 all, then string 5 changed; 6 all, and nothing shown. */
EXPORT("t_ahead_show") uint32_t t_ahead_show(uint32_t mode) {
  uint32_t end = ah_at[1024];
  if (mode == 1 || mode == 3 || mode == 4 || mode == 5 || mode == 6) cd_tmp_ahead(ah, end);
  if (mode == 2) cd_tmp_ahead(ah + ah_at[100] + 1, ah_at[900] + 1 - (ah_at[100] + 1));
  if (mode == 3) { cd_tmp_ahead(0, 0); t_ahead_poke(); }
  if (mode == 4) { t_ahead_poke(); cd_tmp_ahead(ah + ah_at[700], ah_at[701] - ah_at[700]); }
  if (mode == 5) t_ahead_poke();
  if (mode == 6) return 0;
  char name[8] = "ah";
  uint32_t id = ++ah_sections;
  name[2] = (char)('0' + id / 100 % 10); name[3] = (char)('0' + id / 10 % 10); name[4] = (char)('0' + id % 10);
  cd_at(section(name), CD_APPEND);
  for (uint32_t i = 0; i < 1024; i++) cd_text(cd_tmp(ah + ah_at[i], ah_at[i + 1] - ah_at[i])); /* no open element: a throw leaves none */
  return id;
}
/* A string argument is evaluated once, whichever kind it is. */
static uint32_t once_calls;
static const char *once_next(void) { once_calls++; return "once"; }
EXPORT("t_once") uint32_t t_once(void) {
  static const char *const names[] = {"n0", "n1", "n2"};
  uint32_t i = 0;
  once_calls = 0;
  cd_at(section("once"), CD_APPEND);
  cd_open("p");
  cd_text(once_next());
  cd_text(names[i++]);
  cd_text(cd_tmp(names[i++], 2));
  cd_close();
  return once_calls * 100 + i; /* 102: one call, two increments */
}
/* CD_NONE is the object itself; a literal 0 compiles (a null pointer) and traps. */
EXPORT("t_none") double t_none(void) {
  cd_val a = cd_v_num(-7);
  cd_call_args(CD_WINDOW, "Math.abs", 1, &a);
  return cd_get_num(CD_RESULT, CD_NONE);
}
static char none_buf[32];
EXPORT("t_none_buf") char *t_none_buf(void) { return none_buf; }
EXPORT("t_none_str") uint32_t t_none_str(void) { return cd_get_str(CD_BODY, CD_NONE, none_buf, sizeof none_buf); }
EXPORT("t_zero_path") double t_zero_path(void) { return cd_get_num(CD_RESULT, 0); }

/* Pixels as a CSS image: encoded to a 3 x 2 PNG, whose bytes come back and go on a div's background. */
static cd_id imaged;
static uint8_t pix[3 * 2 * 4];
EXPORT("t_image") void t_image(void) {
  cd_id s = section("image");
  cd_at(s, CD_APPEND);
  cd_open("div"); cd_name_open(imaged = cd_id_new()); cd_close();
  for (uint32_t i = 0; i < 6; i++) { pix[i * 4] = (uint8_t)(40 * i); pix[i * 4 + 1] = (uint8_t)(255 - 30 * i); pix[i * 4 + 2] = (uint8_t)(7 * i); pix[i * 4 + 3] = 255; }
  cd_encode_image(pix, 3, 2, 4, "image/png", 77);
  __builtin_memset(pix, 0, sizeof pix); /* the shim copied them during the call */
}
/* The same pixels as RGB, at (1, 1) in a 5 x 4 PNG: its bytes kept for the page to decode. */
static uint8_t rgb_png[512];
static uint32_t rgb_len;
EXPORT("t_image_rgb") void t_image_rgb(void) {
  uint8_t rgb[3 * 2 * 3];
  for (uint32_t i = 0; i < 6; i++) { rgb[i * 3] = (uint8_t)(40 * i); rgb[i * 3 + 1] = (uint8_t)(255 - 30 * i); rgb[i * 3 + 2] = (uint8_t)(7 * i); }
  cd_encode_image_in(rgb, 3, 2, 3, 5, 4, 1, 1, "image/png", 78);
}
EXPORT("t_rgb_png") uint8_t *t_rgb_png(void) { return rgb_png; }
EXPORT("t_rgb_len") uint32_t t_rgb_len(void) { return rgb_len; }
void cd_on_encoded(uint32_t token, const uint8_t *p, uint32_t len) {
  enter();
  if (token == 78) { rgb_len = len < sizeof rgb_png ? len : 0; __builtin_memcpy(rgb_png, p, rgb_len); return; }
  loaded_key = (int32_t)token;
  loaded_id = (int32_t)len;
  /* a PNG starts 0x89 'P' 'N' 'G' */
  if (len > 4 && p[0] == 0x89 && p[1] == 'P') cd_set_image_bytes(imaged, "background-image", p, len, "image/png", 79);
  hold(imaged); /* no cd_flush: the export does it */
}
EXPORT("t_loaded") int32_t t_loaded(int32_t which) { return which ? loaded_key : loaded_id > 8; }
/* Bytes that are no image: nothing is set on the second div, and the answer is 0. The answer for
 * a picture marks its node (--landed) in the hook, which must paint with the picture. */
static cd_id unimaged;
static int32_t image_answer[2] = {-1, -1};
EXPORT("t_image_bad") void t_image_bad(void) {
  static const uint8_t junk[8] = {0x89, 'P', 'N', 'G', 1, 2, 3, 4};
  cd_id s = section("imagebad");
  cd_at(s, CD_APPEND);
  cd_open("div"); cd_name_open(unimaged = cd_id_new()); cd_close();
  cd_set_image_bytes(unimaged, "background-image", junk, sizeof junk, "image/png", 80);
}
void cd_on_image(uint32_t token, uint32_t ok) {
  enter();
  if (token == 79 || token == 80) image_answer[token - 79] = (int32_t)ok;
  if (token == 79) cd_set_style(imaged, "--landed", "1");
  hold(imaged); /* no cd_flush: the export does it */
}
EXPORT("t_image_answer") int32_t t_image_answer(int32_t k) { return image_answer[k]; }

/* The store: a put, then gets that hit, miss, and miss by the deadline. */
static uint32_t store_cdom_heard; /* answers the store's hooks got for a token in cdom's range */
static int32_t store_len[8] = {-2, -2, -2, -2, -2, -2, -2, -2};
static uint32_t store_sum[8];
EXPORT("t_store_put") void t_store_put(void) {
  static const uint8_t v[5] = {9, 1, 250, 3, 7};
  cd_store_put(1, "unit-k1", 7, v, 5);
}
/* A put whose request fails (the page makes put throw for this key): answered 0. */
static int32_t put_ok[4] = {-1, -1, -1, -1};
EXPORT("t_store_put_fail") void t_store_put_fail(void) {
  static const uint8_t v[1] = {1};
  cd_store_put(2, "unit-fail", 9, v, 1);
}
void cd_on_store_put(uint32_t token, uint32_t ok) {
  enter();
  if (token >= CD_TOKEN_CDOM) { store_cdom_heard++; return; }
  put_ok[token & 3] = (int32_t)ok;
}
/* A put whose key lies in a buffer C writes over as soon as the call returns. */
static char tmp_key[8];
EXPORT("t_store_put_tmp") void t_store_put_tmp(void) {
  static const uint8_t v[2] = {4, 2};
  __builtin_memcpy(tmp_key, "unit-tmp", 8);
  cd_store_put(3, tmp_key, 8, v, 2);
  __builtin_memset(tmp_key, 'X', 8);
}
/* Deletes: of the key put above (token 1), and one the page makes fail (token 2). */
static int32_t del_ok[4] = {-1, -1, -1, -1};
EXPORT("t_store_del") void t_store_del(uint32_t token) {
  char key[10];
  __builtin_memcpy(key, token == 2 ? "unit-dfail" : "unit-tmp", token == 2 ? 10 : 8);
  cd_store_del(token, key, token == 2 ? 10 : 8);
}
void cd_on_store_del(uint32_t token, uint32_t ok) {
  enter();
  if (token >= CD_TOKEN_CDOM) { store_cdom_heard++; return; }
  del_ok[token & 3] = (int32_t)ok;
}
EXPORT("t_del_ok") int32_t t_del_ok(uint32_t token) { return del_ok[token & 3]; }
EXPORT("t_put_ok") int32_t t_put_ok(uint32_t token) { return put_ok[token & 3]; }
EXPORT("t_store_get") void t_store_get(uint32_t token, int32_t present, uint32_t ms) {
  if (present == 3) cd_store_get(token, "unit-tmp", 8, ms);
  else if (present == 2) cd_store_get(token, "unit-err", 8, ms); /* the page makes get throw for it */
  else if (present) cd_store_get(token, "unit-k1", 7, ms);
  else cd_store_get(token, "unit-none", 9, ms);
}
void cd_on_store(uint32_t token, const uint8_t *p, int32_t len) {
  enter();
  if (token >= CD_TOKEN_CDOM) { store_cdom_heard++; return; }
  store_len[token & 7] = len;
  uint32_t sum = 0;
  for (int32_t i = 0; i < len; i++) sum = sum * 31 + p[i];
  store_sum[token & 7] = sum;
  hold(imaged); /* no cd_flush: the export does it */
}
EXPORT("t_store") int32_t t_store(uint32_t token, int32_t sum) { return sum ? (int32_t)store_sum[token & 7] : store_len[token & 7]; }

/* 13. The formatter: the page compares with String(v / 10 ** d) + unit. */
static char fmt_buf[64];
EXPORT("t_fmt") uint32_t t_fmt(int32_t v, uint32_t d, uint32_t unit) { return cd_fmt(fmt_buf, v, d, unit); }
EXPORT("t_fmt_len") uint32_t t_fmt_len(double v, uint32_t unit) { return cd_fmt_len(fmt_buf, v, unit); }
EXPORT("t_fmt_buf") char *t_fmt_buf(void) { return fmt_buf; }

/* 4. The network against the page server's echo: a binary WebSocket message out and back, then
 * an HTTP POST answered reversed. No cd_flush: the network exports flush. The message is sent
 * from a buffer C overwrites as soon as the send returns: the send must have read it by then. */
static const uint8_t MSG[] = {0, 1, 2, 3, 0x7f, 0x80, 0xfe, 0xff};
static uint8_t out_msg[sizeof MSG];
static cd_id net_txt;
static int ws_ok;
static uint32_t net_port;
static void say(const char *s) { cd_set_text_node(net_txt, s); }
/* What the sockets are doing: the echo, the open-and-close cycles, the large send. */
enum { WS_ECHO, WS_CYCLE, WS_BIG, WS_PAIR };
static int ws_mode;
static uint32_t ws_hook_heard; /* every hook call, of any socket: a refused cd_ws_open's must add none */
static uint32_t ws_url(char *url) {
  uint32_t n = 0;
  for (const char *c = "ws://127.0.0.1:"; *c; c++) url[n++] = *c;
  n += cd_fmt(url + n, (int32_t)net_port, 0, CD_UNIT_NONE);
  for (const char *c = "/ws"; *c; c++) url[n++] = *c;
  return n;
}
static uint32_t ws_open_here(void) {
  char url[40];
  return cd_ws_open(url, ws_url(url));
}
EXPORT("t_net") void t_net(uint32_t port) {
  cd_id s = section("net");
  cd_at(s, CD_APPEND);
  cd_open("i"); cd_text("waiting"); cd_name_text(net_txt = cd_id_new()); cd_close();
  net_port = port;
  ws_mode = WS_ECHO;
  ws_open_here();
  cd_flush();
}

/* Sockets opened and closed one after another, n times: the ids they get. */
static uint32_t cycles_left, cycle_lo = ~0u, cycle_hi, cycles_done;
static int echo_closed;
EXPORT("t_echo_closed") int32_t t_echo_closed(void) { return echo_closed; }
EXPORT("t_ws_cycles") void t_ws_cycles(uint32_t n) {
  ws_mode = WS_CYCLE;
  cycles_left = n;
  ws_open_here();
}
EXPORT("t_ws_cycle") uint32_t t_ws_cycle(uint32_t which) { return which == 0 ? cycles_done : which == 1 ? cycle_lo : cycle_hi; }

/* One large message: bufferedAmount right after the send, and while it drains. */
static uint32_t big_id = ~0u, big_after_send;
#define BIG (4u << 20)
EXPORT("t_ws_big") void t_ws_big(void) { ws_mode = WS_BIG; ws_open_here(); }
EXPORT("t_ws_big_after_send") uint32_t t_ws_big_after_send(void) { return big_after_send; }
EXPORT("t_ws_buffered") int32_t t_ws_buffered(void) { return big_id == ~0u ? -1 : (int32_t)cd_ws_buffered(big_id); }
EXPORT("t_ws_big_close") void t_ws_big_close(void) { cd_ws_close(big_id); }

static void wp_hook(uint32_t ev, uint32_t id, const uint8_t *p, uint32_t len);
void cd_on_ws_open(uint32_t id) {
  enter();
  ws_hook_heard++;
  if (ws_mode == WS_PAIR) { wp_hook(CD_WS_OPEN, id, 0, 0); return; }
  if (ws_mode == WS_CYCLE) {
    if (id < cycle_lo) cycle_lo = id;
    if (id > cycle_hi) cycle_hi = id;
    cd_ws_close(id);
    return;
  }
  if (ws_mode == WS_BIG) {
    uint8_t *big = cd_alloc(BIG);
    __builtin_memset(big, 0x5a, BIG);
    big_id = id;
    cd_ws_send(id, big, BIG);
    big_after_send = cd_ws_buffered(id);
    cd_free(big);
    return;
  }
  __builtin_memcpy(out_msg, MSG, sizeof MSG);
  cd_ws_send(id, out_msg, sizeof out_msg);
  __builtin_memset(out_msg, 0xee, sizeof out_msg);
  hold(net_txt);
}
void cd_on_ws_close(uint32_t id, uint32_t code) {
  enter();
  ws_hook_heard++;
  if (ws_mode == WS_PAIR) { wp_hook(CD_WS_CLOSE, id, 0, code); return; }
  if (ws_mode == WS_ECHO) echo_closed = 1;
  if (ws_mode != WS_CYCLE) return;
  cycles_done++;
  if (--cycles_left) ws_open_here();
}
void cd_on_ws_msg(uint32_t id, const uint8_t *p, uint32_t len) {
  enter();
  ws_hook_heard++;
  if (ws_mode == WS_PAIR) { wp_hook(CD_WS_MSG, id, p, len); return; }
  ws_ok = len == sizeof MSG;
  for (uint32_t i = 0; ws_ok && i < len; i++) ws_ok = p[i] == MSG[i];
  say(ws_ok ? "ws ok" : "ws wrong bytes");
  cd_ws_close(id);
  cd_http(7, "POST", 4, "/echo", 5, 0, 0, MSG, sizeof MSG, 0);
  hold(net_txt);
}

/* Requests whose answers the page reads by token: the status, and the body's first bytes. */
static int32_t http_status[16];
static uint8_t http_body[16][64];
static uint32_t http_len[16];
static double http_at[16];
EXPORT("t_http_status") int32_t t_http_status(uint32_t token) { return http_status[token & 15]; }
EXPORT("t_http_body") uint8_t *t_http_body(uint32_t token) { return http_body[token & 15]; }
EXPORT("t_http_len") uint32_t t_http_len(uint32_t token) { return http_len[token & 15]; }
EXPORT("t_http_at") double t_http_at(uint32_t token) { return http_at[token & 15]; }
static void ask(uint32_t token, const char *method, const char *url, const char *headers, uint32_t deadline_ms) {
  http_status[token & 15] = -1;
  uint32_t ml = 0, ul = 0, hl = 0;
  while (method[ml]) ml++;
  while (url[ul]) ul++;
  while (headers[hl]) hl++;
  static const uint8_t body[3] = {1, 2, 3};
  cd_http(token, method, ml, url, ul, headers, hl, method[0] == 'P' ? body : 0, method[0] == 'P' ? 3 : 0, deadline_ms);
}
/* Request headers from one block of lines, the status passed through (201), a deadline that
 * does not pass; a deadline that passes first (TIMEOUT); a port nobody listens on (FAILED). */
EXPORT("t_http_cases") void t_http_cases(void) {
  ask(8, "POST", "/headers", "Authorization: Bearer abc.def\nContent-Type: application/octet-stream\n", 5000);
  ask(9, "GET", "/slow", "", 50);
  ask(10, "GET", "http://127.0.0.1:1/nobody", "Authorization: Bearer x", 5000);
  ask(11, "GET", "/headers", "X-Last: no newline", 0);
}

static uint32_t http_cdom_heard; /* answers cd_on_http got for a token in cdom's range */
void cd_on_http(uint32_t token, uint32_t status, const uint8_t *p, uint32_t len) {
  enter();
  if (token >= CD_TOKEN_CDOM) { http_cdom_heard++; return; }
  if (token != 7) {
    http_status[token & 15] = (int32_t)status;
    http_len[token & 15] = len;
    http_at[token & 15] = cd_now();
    for (uint32_t i = 0; i < len && i < 64; i++) http_body[token & 15][i] = p[i];
    return;
  }
  int ok = status == 200 && len == sizeof MSG;
  for (uint32_t i = 0; ok && i < len; i++) ok = p[i] == MSG[len - 1 - i];
  say(!ws_ok ? "ws failed" : ok ? "ws ok, http ok" : "http wrong");
  hold(net_txt);
}
EXPORT("t_epoch") double t_epoch(void) { return cd_epoch_ms(); }

/* 14. Sound: clips made in C as WAV bytes, loaded, played, scheduled, stopped, made quieter and
 * muted; ids handed out again. The page reads what Web Audio was asked to do. */
static cd_id audio_txt;
EXPORT("t_audio_section") void t_audio_section(void) {
  cd_id s = section("audio");
  cd_at(s, CD_APPEND);
  cd_open("i"); cd_text("audio"); cd_name_text(audio_txt = cd_id_new()); cd_close();
}
/* A WAV file of ms milliseconds (ms <= 1000): 8 kHz, 16-bit mono, +0.25 for the first half and
 * -0.25 for the second. ms 0: eight bytes that are no sound. */
static uint8_t wav_buf[44 + 2 * 8 * 1000];
static void put32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static void put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static uint32_t wav(uint32_t ms) {
  if (!ms) {
    __builtin_memcpy(wav_buf, "RIFFjunk", 8);
    return 8;
  }
  uint32_t n = 8 * ms, data = 2 * n;
  __builtin_memcpy(wav_buf, "RIFF", 4); put32(wav_buf + 4, 36 + data);
  __builtin_memcpy(wav_buf + 8, "WAVEfmt ", 8); put32(wav_buf + 16, 16);
  put16(wav_buf + 20, 1); put16(wav_buf + 22, 1);          /* PCM, one channel */
  put32(wav_buf + 24, 8000); put32(wav_buf + 28, 16000);   /* samples and bytes per second */
  put16(wav_buf + 32, 2); put16(wav_buf + 34, 16);          /* bytes per frame, bits per sample */
  __builtin_memcpy(wav_buf + 36, "data", 4); put32(wav_buf + 40, data);
  for (uint32_t i = 0; i < n; i++) put16(wav_buf + 44 + 2 * i, i < n / 2 ? 8192 : (uint32_t)-8192);
  return 44 + data;
}
static int32_t audio_clip[16] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
EXPORT("t_audio_load") void t_audio_load(uint32_t token, uint32_t ms) {
  uint32_t len = wav(ms);
  cd_audio_load(token, wav_buf, len);
  __builtin_memset(wav_buf, 0, len); /* the shim copied the bytes during the call */
}
void cd_on_audio_loaded(uint32_t token, uint32_t clip) {
  enter();
  audio_clip[token & 15] = (int32_t)clip;
  hold(audio_txt); /* no cd_flush: the export does it */
}
EXPORT("t_audio_loaded") int32_t t_audio_loaded(uint32_t token) { return audio_clip[token & 15]; }
EXPORT("t_audio_free") void t_audio_free(uint32_t clip) { cd_audio_free(clip); }
EXPORT("t_audio_state") uint32_t t_audio_state(void) { return cd_audio_state(); }
/* When each voice started being asked for and when it ended (0: not yet), by voice id. */
static double audio_played_at[64], audio_ended_at[64];
EXPORT("t_audio_play") uint32_t t_audio_play(uint32_t clip, double gain, double rate, double delay_ms) {
  double now = cd_now();
  uint32_t voice = cd_audio_play(clip, gain, rate, delay_ms ? now + delay_ms : 0);
  audio_played_at[voice & 63] = now;
  audio_ended_at[voice & 63] = 0;
  return voice;
}
EXPORT("t_audio_took") double t_audio_took(uint32_t voice) {
  return audio_ended_at[voice & 63] ? audio_ended_at[voice & 63] - audio_played_at[voice & 63] : 0;
}
EXPORT("t_audio_stop") void t_audio_stop(uint32_t voice) { cd_audio_stop(voice); }
EXPORT("t_audio_gain") void t_audio_gain(uint32_t voice, double gain) { cd_audio_set_gain(voice, gain); }
EXPORT("t_audio_master") void t_audio_master(double gain) { cd_audio_master(gain); }
EXPORT("t_audio_mute") void t_audio_mute(int32_t on) { cd_audio_mute(on); }
EXPORT("t_audio_play_unloaded") void t_audio_play_unloaded(uint32_t clip) { cd_audio_play(clip, 1, 1, 0); }
/* Voices played one after another, each from the last one's ended hook: the ids they get. */
static uint32_t chain_clip, chain_left, chain_done, chain_lo = ~0u, chain_hi;
static void chain_play(void) {
  uint32_t voice = cd_audio_play(chain_clip, 1, 1, 0);
  if (voice < chain_lo) chain_lo = voice;
  if (voice > chain_hi) chain_hi = voice;
}
EXPORT("t_audio_chain") void t_audio_chain(uint32_t clip, uint32_t n) {
  chain_clip = clip;
  chain_left = n;
  chain_play();
}
EXPORT("t_audio_chained") uint32_t t_audio_chained(uint32_t which) { return which == 0 ? chain_done : which == 1 ? chain_lo : chain_hi; }
void cd_on_audio_ended(uint32_t voice) {
  enter();
  audio_ended_at[voice & 63] = cd_now();
  if (chain_left) {
    chain_done++;
    if (--chain_left) chain_play();
  }
  hold(audio_txt); /* no cd_flush: the export does it */
}
/* Every state the hook heard, in order. */
static uint32_t audio_states[16], audio_nstates;
void cd_on_audio_state(uint32_t state) {
  enter();
  audio_states[audio_nstates++ & 15] = state;
}
EXPORT("t_audio_states") uint32_t t_audio_states(void) {
  uint32_t all = 0; /* the last eight, one decimal digit each, oldest first */
  for (uint32_t i = audio_nstates > 8 ? audio_nstates - 8 : 0; i < audio_nstates; i++) all = all * 10 + audio_states[i & 15] + 1;
  return all;
}

/* 15. Random bytes: a buffer between two guard bytes, filled from C in one call that the module
 * splits into chunks; the page counts what getRandomValues was asked for. */
#define RANDOM_MAX 200000
static uint8_t random_buf[1 + RANDOM_MAX + 1], random_last[16];
static uint32_t random_len;
EXPORT("t_random") uint32_t t_random(uint32_t len) {
  random_len = len;
  __builtin_memset(random_buf, 0, sizeof random_buf);
  random_buf[0] = 0xa5;
  random_buf[1 + len] = 0xa5;
  return cd_random_bytes(random_buf + 1, len);
}
/* The zero bytes in chunk k of the last fill (about 1 in 256 when it was written). */
EXPORT("t_random_zeros") uint32_t t_random_zeros(uint32_t k) {
  uint32_t zeros = 0;
  for (uint32_t i = k * CD_RANDOM_CHUNK; i < (k + 1) * CD_RANDOM_CHUNK && i < random_len; i++) zeros += !random_buf[1 + i];
  return zeros;
}
EXPORT("t_random_guards") uint32_t t_random_guards(void) { return random_buf[0] == 0xa5 && random_buf[1 + random_len] == 0xa5; }
/* 1 when the first 16 bytes of this fill differ from the last one's. */
EXPORT("t_random_new") uint32_t t_random_new(void) {
  uint32_t differs = 0;
  for (int i = 0; i < 16; i++) {
    differs |= random_buf[1 + i] != random_last[i];
    random_last[i] = random_buf[1 + i];
  }
  return differs;
}

/* 16. The address: the page writes a path into url_in and has C push, replace or title with it;
 * reads land in url_out, which is filled with '*' first so the page sees what was written. */
static char url_in[256], url_out[256];
static uint8_t url_heard[256];
static uint32_t url_heard_len, url_heard_count;
static cd_id url_txt;
EXPORT("t_url_section") void t_url_section(void) {
  cd_id s = section("url");
  cd_at(s, CD_APPEND);
  cd_open("i"); cd_text("url"); cd_name_text(url_txt = cd_id_new()); cd_close();
}
EXPORT("t_url_in") char *t_url_in(void) { return url_in; }
EXPORT("t_url_out") char *t_url_out(void) { return url_out; }
EXPORT("t_url_read") uint32_t t_url_read(uint32_t part, uint32_t cap) {
  __builtin_memset(url_out, '*', sizeof url_out);
  return cd_url_read(part, url_out, cap);
}
EXPORT("t_url_push") uint32_t t_url_push(uint32_t len) { return cd_url_push(url_in, len); }
EXPORT("t_url_replace") uint32_t t_url_replace(uint32_t len) { return cd_url_replace(url_in, len); }
EXPORT("t_title") void t_title(uint32_t len) { cd_set_title(url_in, len); }
EXPORT("t_url_step") void t_url_step(int32_t delta) { cd_url_step(delta); }
/* Percent-encoding: url_in's len bytes encoded (or decoded) into url_out. */
EXPORT("t_url_encode") uint32_t t_url_encode(uint32_t len) { return cd_url_encode(url_out, url_in, len); }
EXPORT("t_url_decode") uint32_t t_url_decode(uint32_t len) { return cd_url_decode(url_out, url_in, len); }
EXPORT("t_seed") uint32_t t_seed(void) { return cd_seed; }
EXPORT("t_rand") uint32_t t_rand(void) { return cd_rand(); }
void cd_on_url(const uint8_t *path, uint32_t len) {
  enter();
  url_heard_count++;
  url_heard_len = len;
  for (uint32_t i = 0; i < len && i < sizeof url_heard; i++) url_heard[i] = path[i];
  hold(url_txt); /* no cd_flush: the export does it */
}
EXPORT("t_url_heard") uint8_t *t_url_heard(void) { return url_heard; }
EXPORT("t_url_heard_len") uint32_t t_url_heard_len(void) { return url_heard_len; }
EXPORT("t_url_heard_count") uint32_t t_url_heard_count(void) { return url_heard_count; }

/* 17. Languages and display preferences: tags read into lang_out (filled with '*' first), and
 * every preference change the hook heard. */
static char lang_out[64];
static cd_id prefs_txt;
EXPORT("t_locale_section") void t_locale_section(void) {
  cd_id s = section("locale");
  cd_at(s, CD_APPEND);
  cd_open("i"); cd_text("prefs"); cd_name_text(prefs_txt = cd_id_new()); cd_close();
}
EXPORT("t_lang_out") char *t_lang_out(void) { return lang_out; }
EXPORT("t_language") uint32_t t_language(uint32_t i, uint32_t cap) {
  __builtin_memset(lang_out, '*', sizeof lang_out);
  return cd_language(i, lang_out, cap);
}
EXPORT("t_prefs") uint32_t t_prefs(void) { return cd_prefs(); }
static uint32_t prefs_heard[16], prefs_nheard;
void cd_on_prefs(uint32_t prefs) {
  enter();
  prefs_heard[prefs_nheard++ & 15] = prefs;
  hold(prefs_txt); /* no cd_flush: the export does it */
}
EXPORT("t_prefs_heard") uint32_t t_prefs_heard(void) {
  uint32_t all = 0; /* the last eight, one decimal digit each (prefs + 1), oldest first */
  for (uint32_t i = prefs_nheard > 8 ? prefs_nheard - 8 : 0; i < prefs_nheard; i++) all = all * 10 + prefs_heard[i & 15] + 1;
  return all;
}

/* 18. The clipboard and the share sheet: the page writes the text, the title and the url into
 * share_text and share_url, and reads every answer back by token (-1 until it came) with how many
 * came. */
static char share_text[256], share_url[256];
static int32_t clipboard_status[16], share_status[16];
static uint32_t clipboard_answers[16], share_answers[16];
static cd_id share_txt;
EXPORT("t_share_section") void t_share_section(void) {
  cd_id s = section("share");
  cd_at(s, CD_APPEND);
  cd_open("i"); cd_text("share"); cd_name_text(share_txt = cd_id_new()); cd_close();
  for (int i = 0; i < 16; i++) clipboard_status[i] = share_status[i] = -1;
}
EXPORT("t_share_text") char *t_share_text(void) { return share_text; }
EXPORT("t_share_url") char *t_share_url(void) { return share_url; }
EXPORT("t_clipboard") void t_clipboard(uint32_t token, uint32_t len) { cd_clipboard_write(token, share_text, len); }
EXPORT("t_share") void t_share(uint32_t token, uint32_t tlen, uint32_t ulen) { cd_share(token, share_text, tlen, share_url, ulen); }
void cd_on_clipboard(uint32_t token, uint32_t status) {
  enter();
  clipboard_status[token & 15] = (int32_t)status;
  clipboard_answers[token & 15]++;
  hold(share_txt); /* no cd_flush: the export does it */
}
void cd_on_share(uint32_t token, uint32_t status) {
  enter();
  share_status[token & 15] = (int32_t)status;
  share_answers[token & 15]++;
  hold(share_txt);
}
EXPORT("t_clipboard_status") int32_t t_clipboard_status(uint32_t token) { return clipboard_status[token & 15]; }
EXPORT("t_clipboard_answers") uint32_t t_clipboard_answers(uint32_t token) { return clipboard_answers[token & 15]; }
EXPORT("t_share_status") int32_t t_share_status(uint32_t token) { return share_status[token & 15]; }
EXPORT("t_share_answers") uint32_t t_share_answers(uint32_t token) { return share_answers[token & 15]; }

/* 19. The page's fonts: every answer by token (-1 until it came) with how many came, and how often
 * a load was heard done, and when last (on cd_now's clock). */
static int32_t fonts_status[16];
static uint32_t fonts_answers[16], fonts_changed;
static double fonts_changed_at;
static cd_id fonts_txt;
EXPORT("t_fonts_section") void t_fonts_section(void) {
  cd_id s = section("fonts");
  cd_at(s, CD_APPEND);
  cd_open("i"); cd_text("fonts"); cd_name_text(fonts_txt = cd_id_new()); cd_close();
  for (int i = 0; i < 16; i++) fonts_status[i] = -1;
}
EXPORT("t_fonts_wait") void t_fonts_wait(uint32_t token, uint32_t deadline_ms) { cd_fonts_wait(token, deadline_ms); }
void cd_on_fonts(uint32_t token, uint32_t status) {
  enter();
  fonts_status[token & 15] = (int32_t)status;
  fonts_answers[token & 15]++;
  hold(fonts_txt); /* no cd_flush: the export does it */
}
void cd_on_fonts_changed(void) {
  enter();
  fonts_changed++;
  fonts_changed_at = cd_now();
  hold(fonts_txt);
}
EXPORT("t_fonts_status") int32_t t_fonts_status(uint32_t token) { return fonts_status[token & 15]; }
EXPORT("t_fonts_answers") uint32_t t_fonts_answers(uint32_t token) { return fonts_answers[token & 15]; }
EXPORT("t_fonts_changed") uint32_t t_fonts_changed(void) { return fonts_changed; }
EXPORT("t_fonts_changed_at") double t_fonts_changed_at(void) { return fonts_changed_at; }

/* 22. Host access: properties and methods along a dotted path. A text input, a checkbox and a
 * button; the page types into the input first, then C writes, calls and reads. */
static cd_id host_input, host_box, host_button, host_div;
static char host_out[64];
EXPORT("t_host_section") void t_host_section(void) {
  cd_id s = section("host");
  cd_at(s, CD_APPEND);
  cd_open("input"); cd_attr("type", "text"); cd_name_el(host_input = cd_id_new()); cd_close();
  cd_open("input"); cd_attr("type", "checkbox"); cd_name_el(host_box = cd_id_new()); cd_close();
  cd_open("button"); cd_text("b"); cd_name_open(host_button = cd_id_new()); cd_close();
  cd_open("div"); cd_name_el(host_div = cd_id_new()); cd_close();
}
EXPORT("t_host_set") void t_host_set(void) {
  cd_set_str(host_input, "value", "한국어 값");
  cd_set_bool(host_box, "checked", 1);
  cd_set_bool(host_button, "disabled", 1);
  cd_set_num(host_input, "maxLength", 12);
  cd_set_str(host_div, "style.--accent", "rgb(1, 2, 3)");
  cd_set_str(host_div, "dataset.mark", "set");
}
EXPORT("t_host_enable") void t_host_enable(void) { cd_set_bool(host_button, "disabled", 0); }
EXPORT("t_host_focus") void t_host_focus(void) { cd_call(host_input, "focus"); }
EXPORT("t_host_blur") void t_host_blur(void) { cd_call(host_input, "blur"); }
EXPORT("t_host_checked") double t_host_checked(void) { return cd_get_num(host_box, "checked"); }
EXPORT("t_host_length") double t_host_length(void) { return cd_get_num(host_input, "value.length"); }
EXPORT("t_host_missing") double t_host_missing(void) { return cd_get_num(host_input, "no.such.thing"); }
EXPORT("t_host_value") uint32_t t_host_value(uint32_t cap) { return cd_get_str(host_input, "value", host_out, cap); }
EXPORT("t_host_text") uint32_t t_host_text(uint32_t cap) { /* 1 << 16 when it fitted, and the length */
  uint32_t n, fits = (uint32_t)cd_input_whole(host_input, host_out, cap, &n);
  return fits << 16 | n;
}
EXPORT("t_host_accent") uint32_t t_host_accent(void) { return cd_get_str(host_div, "style.--accent", host_out, sizeof host_out); }
EXPORT("t_host_out") char *t_host_out(void) { return host_out; }
EXPORT("t_host_bad_set") void t_host_bad_set(void) { cd_set_str(host_div, "no.such.thing", "x"); }

/* 23. An event the browser dispatches while C runs waits for a microtask, and must not then reach a node
 * that is gone. Each input listens for keydown and focusout; the page focuses one and presses a key on it.
 * The reuse input's handler removes it and makes a new input in the same handler, which takes the freed
 * id (cd_id_new hands freed ids out first) and listens for focusout too: the focusout the removal fires
 * was the old node's, so the new one must not hear it. The gone input is removed and its id left free.
 * The alive input's handler moves focus to another input: its focusout is still its own, heard once. */
static cd_id def_sec, def_reuse, def_new, def_gone, def_alive, def_other;
static int32_t def_heard[4]; /* focusouts on: the new input, the gone id, the alive input, the other input */
EXPORT("t_deferred_section") void t_deferred_section(void) {
  def_sec = section("deferred");
  cd_at(def_sec, CD_APPEND);
  cd_open("input"); cd_attr("class", "reuse");
  cd_listen(def_reuse = cd_id_new(), CD_EV_KEYDOWN); cd_listen(def_reuse, CD_EV_FOCUSOUT); cd_close();
  cd_open("input"); cd_attr("class", "gone");
  cd_listen(def_gone = cd_id_new(), CD_EV_KEYDOWN); cd_listen(def_gone, CD_EV_FOCUSOUT); cd_close();
  cd_open("input"); cd_attr("class", "alive");
  cd_listen(def_alive = cd_id_new(), CD_EV_KEYDOWN); cd_listen(def_alive, CD_EV_FOCUSOUT); cd_close();
  cd_open("input"); cd_attr("class", "other");
  cd_listen(def_other = cd_id_new(), CD_EV_FOCUSOUT); cd_close();
  def_new = CD_DETACHED;
}
/* 0: did the new input take the reuse input's id; 1-4: def_heard. */
EXPORT("t_deferred") int32_t t_deferred(uint32_t k) { return k ? def_heard[k - 1] : def_new == def_reuse; }
static void deferred_event(const cd_event_rec *e) {
  const cd_id id = (cd_id)e->id;
  if (e->type == CD_EV_KEYDOWN && id == def_reuse) {
    cd_remove(def_reuse);
    def_new = cd_id_new();
    cd_at(def_sec, CD_APPEND);
    cd_open("input"); cd_attr("class", "new"); cd_listen(def_new, CD_EV_FOCUSOUT); cd_close();
  } else if (e->type == CD_EV_KEYDOWN && id == def_gone) {
    cd_remove(def_gone);
  } else if (e->type == CD_EV_KEYDOWN && id == def_alive) {
    cd_call(def_other, "focus");
  } else if (e->type == CD_EV_FOCUSOUT) {
    def_heard[0] += id == def_new;
    def_heard[1] += id == def_gone;
    def_heard[2] += id == def_alive;
    def_heard[3] += id == def_other;
  }
}
/* 24. A <dialog>: shown with cd_call(showModal), its cancel and close heard on the dialog itself
 * (they do not bubble: the mount hears them in the capture phase). */
static cd_id dialog_node;
EXPORT("t_dialog_section") cd_id t_dialog_section(void) {
  cd_id s = section("dialog");
  cd_at(s, CD_APPEND);
  cd_open("dialog");
  dialog_node = cd_id_new();
  cd_listen(dialog_node, CD_EV_CANCEL);
  cd_listen(dialog_node, CD_EV_CLOSE);
  cd_open("button"); cd_text("ok"); cd_close();
  cd_close();
  return dialog_node;
}
EXPORT("t_dialog_show") void t_dialog_show(void) { cd_call(dialog_node, "showModal"); }
EXPORT("t_dialog_close") void t_dialog_close(void) { cd_call(dialog_node, "close"); }
EXPORT("t_ev_close") int32_t t_ev_close(void) { return CD_EV_CLOSE; }

/* 25. cd_open_id: an element opened and named in one call, the id naming it after children were built
 * inside; inside a region, the id is the region's and goes with it. */
static cd_id open_outer, open_inner;
static cd_region open_region;
static uint32_t open_live[3];
EXPORT("t_open_id") void t_open_id(void) {
  cd_id s = section("openid");
  cd_at(s, CD_APPEND);
  open_outer = cd_open_id("ul");
  open_inner = cd_open_id("li"); cd_text("a"); cd_close();
  cd_open("li"); cd_text("b"); cd_close();
  cd_close();
  cd_set_class(open_outer, "outer");
  cd_set_class(open_inner, "inner");
  open_live[0] = cd_ids_live();
  if (cd_region_show(&open_region, 1)) {
    cd_at(s, CD_APPEND);
    cd_open("p"); cd_name_open(open_region.root);
    cd_id in = cd_open_id("b"); cd_text("c"); cd_close();
    cd_close();
    cd_region_end(&open_region);
    cd_set_class(in, "in-region");
  }
  open_live[1] = cd_ids_live();
}
EXPORT("t_open_hide") void t_open_hide(void) {
  cd_region_hide(&open_region);
  open_live[2] = cd_ids_live();
}
EXPORT("t_open_live") uint32_t t_open_live(uint32_t k) { return open_live[k]; }

/* 26. cd_on: a handler per (id, type), called with its ctx in place of cd_on_event; other events (no
 * handler for the id, or for that type) still reach cd_on_event. a and b get handlers once all three
 * buttons are built (the last node is c's text), c only listens; the document gets one too. */
typedef struct { uint32_t calls; int32_t id, type, prevent; } on_ctx;
static on_ctx on_a, on_b, on_r, on_doc; /* k = 0 to 3 */
static on_ctx *const ON_CTX[] = {&on_a, &on_b, &on_r, &on_doc};
static cd_id on_btn[3];
static void on_hit(const cd_event_rec *e, void *ctx) {
  enter();
  on_ctx *c = ctx;
  c->calls++;
  c->id = e->id;
  c->type = e->type;
  if (c->prevent) cd_prevent_default();
  hold(on_btn[2]); /* no cd_flush: the event export does it */
}
static void on_hit_r(const cd_event_rec *e, void *ctx) { on_hit(e, ctx); } /* a second fn, for the replace */
EXPORT("t_on_section") void t_on_section(void) {
  cd_id s = section("on");
  cd_at(s, CD_APPEND);
  for (int k = 0; k < 3; k++) { on_btn[k] = cd_open_id("button"); cd_text_num((uint32_t)k); cd_close(); }
  on_b.prevent = 1;
  cd_on(on_btn[0], CD_EV_CLICK, on_hit, &on_a);
  cd_on(on_btn[1], CD_EV_CLICK, on_hit, &on_b);
  cd_sel(on_btn[0]); cd_listen(on_btn[0], CD_EV_DBLCLICK); /* a's dblclick has no handler */
  cd_sel(on_btn[2]); cd_listen(on_btn[2], CD_EV_CLICK);
  cd_on(CD_DOCUMENT, CD_EV_FULLSCREENCHANGE, on_hit, &on_doc);
}
/* a's click replaced (another fn and ctx), b's and the document's removed. */
EXPORT("t_on_change") void t_on_change(void) {
  cd_on(on_btn[0], CD_EV_CLICK, on_hit_r, &on_r);
  cd_on(on_btn[1], CD_EV_CLICK, 0, 0);
  cd_on(CD_DOCUMENT, CD_EV_FULLSCREENCHANGE, 0, 0);
  cd_on(on_btn[1], CD_EV_CLICK, 0, 0); /* removing again does nothing */
}
EXPORT("t_on_btn") cd_id t_on_btn(uint32_t k) { return on_btn[k]; }
EXPORT("t_on_calls") uint32_t t_on_calls(uint32_t k) { return ON_CTX[k]->calls; }
EXPORT("t_on_last") int32_t t_on_last(uint32_t k, uint32_t type) { return type ? ON_CTX[k]->type : ON_CTX[k]->id; }

/* 300 spans, each with a click and an input handler; then every other id freed (its node left in place)
 * and handed out again to a new span that only listens: the old handlers stay with the odd ids, and the
 * new spans' clicks reach cd_on_event. */
#define ON_MANY 300
static uint32_t on_many_calls[ON_MANY];
static cd_id on_many_id[ON_MANY], on_reused[ON_MANY / 2];
static cd_id on_many_box;
static void on_count(const cd_event_rec *, void *ctx) { enter(); ++*(uint32_t *)ctx; }
EXPORT("t_on_many") void t_on_many(void) {
  cd_id s = section("onmany");
  cd_at(s, CD_APPEND);
  on_many_box = cd_open_id("div");
  for (int i = 0; i < ON_MANY; i++) {
    on_many_id[i] = cd_open_id("span");
    cd_on(on_many_id[i], CD_EV_INPUT, on_count, &on_many_calls[i]);
    cd_on(on_many_id[i], CD_EV_CLICK, on_count, &on_many_calls[i]);
    cd_close();
  }
  cd_close();
}
EXPORT("t_on_many_free") uint32_t t_on_many_free(void) {
  for (int i = 0; i < ON_MANY; i += 2) cd_id_free(on_many_id[i]);
  uint32_t same = 0;
  cd_at(on_many_box, CD_APPEND);
  for (int i = 0; i < ON_MANY / 2; i++) {
    cd_open("b");
    cd_listen(on_reused[i] = cd_id_new(), CD_EV_CLICK);
    cd_close();
    for (int k = 0; k < ON_MANY; k += 2) same += on_reused[i] == on_many_id[k];
  }
  return same; /* how many of the new ids are freed ones */
}
EXPORT("t_on_many_calls") uint32_t t_on_many_calls(uint32_t i) { return on_many_calls[i]; }

/* 27. Timers stopped, and timers with a callback. App tokens: 50 started and stopped; 51 started twice
 * (fires twice); 52 started twice and stopped (neither fires). Callbacks (tm[k], each counting into its
 * own ctx): 0 fires; 1 stopped before firing; 2 stopped, then 3 takes its slot under another token, and
 * stopping 2 again leaves 3 alone; 4 re-arms itself until it has fired 5 times. Later (t_timer_late),
 * once 0 has fired: 0 stopped (a no-op), 5 started in 0's freed slot, 0 stopped again, 5 still fires;
 * and tokens never started, or already stopped, stopped too. */
typedef struct { uint32_t calls; void *self; } tm_ctx;
static tm_ctx tm[6];
static uint32_t tm_tok[6];
static void tm_fire(void *ctx) {
  enter();
  tm_ctx *c = ctx;
  c->calls++;
  c->self = ctx;
  if (c == &tm[4] && c->calls < 5) tm_tok[4] = cd_timer_cb(5, tm_fire, ctx);
  hold(timer_txt); /* no cd_flush: the timer export does it */
}
EXPORT("t_timers") void t_timers(void) {
  cd_timer_start(10, 50); cd_timer_stop(50);
  cd_timer_start(10, 51); cd_timer_start(20, 51);
  cd_timer_start(10, 52); cd_timer_start(20, 52); cd_timer_stop(52);
  tm_tok[0] = cd_timer_cb(10, tm_fire, &tm[0]);
  tm_tok[1] = cd_timer_cb(10, tm_fire, &tm[1]); cd_timer_stop(tm_tok[1]);
  tm_tok[2] = cd_timer_cb(10, tm_fire, &tm[2]); cd_timer_stop(tm_tok[2]);
  tm_tok[3] = cd_timer_cb(30, tm_fire, &tm[3]); cd_timer_stop(tm_tok[2]);
  tm_tok[4] = cd_timer_cb(5, tm_fire, &tm[4]);
}
EXPORT("t_timer_late") void t_timer_late(void) {
  static tm_ctx extra; /* timers that took the other free slots first (they fire, into extra) */
  cd_timer_stop(tm_tok[0]);
  for (int i = 0; i < 8; i++) {
    uint32_t t = cd_timer_cb(10, tm_fire, &extra);
    if ((t & 0xffffu) != (tm_tok[0] & 0xffffu)) continue;
    cd_timer_stop(t); /* frees the slot again, for tm[5] */
    break;
  }
  tm_tok[5] = cd_timer_cb(10, tm_fire, &tm[5]);
  cd_timer_stop(tm_tok[0]);
  cd_timer_stop(50); cd_timer_stop(52); cd_timer_stop(57); cd_timer_stop(tm_tok[1]);
}
EXPORT("t_timer_bad") void t_timer_bad(void) { cd_timer_start(10, CD_TOKEN_CDOM); }
/* k < 6: callback k's calls (k + 10: did it get its own ctx; k + 20: its token); 30 + i: app token 50 + i heard;
 * 40: cdom tokens heard by cd_on_timer. */
EXPORT("t_timer_seen") uint32_t t_timer_seen(uint32_t k) {
  if (k < 6) return tm[k].calls;
  if (k >= 10 && k < 16) return tm[k - 10].self == &tm[k - 10];
  if (k >= 20 && k < 26) return tm_tok[k - 20];
  if (k >= 30 && k < 40) return app_timer_heard[k - 30];
  return cdom_token_heard;
}

/* 28. cd_http_cb: a POST to /echo answered reversed, a GET past its deadline, a port nobody listens on,
 * each to its own ctx; cd_on_http hears none of them. */
typedef struct { int32_t status; uint32_t calls, len; uint8_t body[8]; } hc_ctx;
static hc_ctx hc[3];
static void hc_answer(void *ctx, uint32_t status, const uint8_t *p, uint32_t len) {
  enter();
  hc_ctx *c = ctx;
  c->status = (int32_t)status;
  c->calls++;
  c->len = len;
  for (uint32_t i = 0; i < len && i < 8; i++) c->body[i] = p[i];
  hold(net_txt); /* no cd_flush: the export does it */
}
EXPORT("t_http_cb") void t_http_cb(void) {
  static const uint8_t body[4] = {1, 2, 3, 4};
  for (int k = 0; k < 3; k++) hc[k].status = -1;
  cd_http_cb(hc_answer, &hc[0], "POST", 4, "/echo", 5, "", 0, body, 4, 5000);
  cd_http_cb(hc_answer, &hc[1], "GET", 3, "/slow", 5, "", 0, 0, 0, 50);
  cd_http_cb(hc_answer, &hc[2], "GET", 3, "http://127.0.0.1:1/x", 20, "", 0, 0, 0, 5000);
}
EXPORT("t_http_cb_bad") void t_http_cb_bad(void) { cd_http(CD_TOKEN_CDOM | 7, "GET", 3, "/x", 2, "", 0, 0, 0, 0); }
/* k < 3: hc[k]'s status, calls, len and first 4 body bytes as one string the page splits; 3: cd_on_http's
 * answers for cdom tokens. */
EXPORT("t_http_cb_seen") int32_t t_http_cb_seen(uint32_t k, uint32_t f) {
  if (k == 3) return (int32_t)http_cdom_heard;
  const hc_ctx *c = &hc[k];
  return f == 0 ? c->status : f == 1 ? (int32_t)c->calls : f == 2 ? (int32_t)c->len : c->body[f - 3];
}

/* A free callback slot's index: a timer callback started and stopped at once takes the free list's top
 * and gives it back, so two probes around work that frees every slot it took name the same slot. */
static void slot_probe_fire(void *) { }
static uint32_t slot_probe(void) {
  uint32_t t = cd_timer_cb(1000, slot_probe_fire, 0);
  cd_timer_stop(t);
  return t & 0xffffu;
}

/* 29. The store's _cb forms, one after another from each callback: a put, a get of it, a delete, a get
 * that misses; and (t_store_cb_fail, while the page makes put throw for "unit-fail") a put that fails.
 * Each answer to its own ctx, once; the store's hooks hear none of them. */
typedef struct { int32_t v; uint32_t calls, sum; void *self; } sc_ctx;
static sc_ctx sc[5];
static uint32_t sc_probe[2];
static void sc_got(void *ctx, const uint8_t *p, int32_t len);
static void sc_done(void *ctx, uint32_t ok) {
  enter();
  sc_ctx *c = ctx;
  c->v = (int32_t)ok;
  c->calls++;
  c->self = ctx;
  if (c == &sc[0]) cd_store_get_cb(sc_got, &sc[1], "unit-cb", 7, 1500);
  else if (c == &sc[2]) cd_store_get_cb(sc_got, &sc[3], "unit-cb", 7, 1500);
  hold(imaged); /* no cd_flush: the export does it */
}
static void sc_got(void *ctx, const uint8_t *p, int32_t len) {
  enter();
  sc_ctx *c = ctx;
  c->v = len;
  c->calls++;
  c->self = ctx;
  for (int32_t i = 0; i < len; i++) c->sum = c->sum * 31 + p[i];
  if (c == &sc[1]) cd_store_del_cb(sc_done, &sc[2], "unit-cb", 7);
  else sc_probe[1] = slot_probe(); /* the last answer: every slot of the chain freed */
  hold(imaged);
}
EXPORT("t_store_cb") void t_store_cb(void) {
  static const uint8_t v[3] = {5, 6, 250};
  for (int k = 0; k < 5; k++) sc[k].v = -2;
  sc_probe[0] = slot_probe();
  cd_store_put_cb(sc_done, &sc[0], "unit-cb", 7, v, 3);
}
EXPORT("t_store_cb_fail") void t_store_cb_fail(void) {
  static const uint8_t v[1] = {1};
  cd_store_put_cb(sc_done, &sc[4], "unit-fail", 9, v, 1);
}
/* 29b. A write is answered when its transaction commits. k 0: a put of "unit-abort", 1: a delete of
 * "unit-keep", each of which the page aborts after its request succeeded (answered 0); 2: a put of
 * "unit-keep", 3: its delete (answered 1, and the page checks no answer came before the transaction's
 * complete event); 4: a put of "unit-dup", whose request the page makes fail after the call (answered 0). */
static int32_t commit_ok[5] = {-2, -2, -2, -2, -2};
static void commit_done(void *ctx, uint32_t ok) {
  enter();
  *(int32_t *)ctx = (int32_t)ok;
}
EXPORT("t_store_commit") void t_store_commit(uint32_t k) {
  static const uint8_t v[2] = {8, 1};
  const char *key = k == 0 ? "unit-abort" : k == 4 ? "unit-dup" : "unit-keep";
  uint32_t klen = k == 0 ? 10 : k == 4 ? 8 : 9;
  if (k == 0 || k == 2 || k == 4) cd_store_put_cb(commit_done, &commit_ok[k], key, klen, v, 2);
  else cd_store_del_cb(commit_done, &commit_ok[k], key, klen);
}
EXPORT("t_store_commit_ok") int32_t t_store_commit_ok(uint32_t k) { return k < 5 ? commit_ok[k] : -3; }
/* The token forms refuse a token of cdom's: 0 a get, 1 a put, 2 a delete. */
EXPORT("t_store_cb_bad") void t_store_cb_bad(uint32_t which) {
  if (which == 0) cd_store_get(CD_TOKEN_CDOM | 7, "unit-x", 6, 100);
  else if (which == 1) cd_store_put(CD_TOKEN_CDOM | 7, "unit-x", 6, "", 0);
  else cd_store_del(CD_TOKEN_CDOM | 7, "unit-x", 6);
}
/* k < 5: sc[k]'s value, calls, sum and whether it got its own ctx; 5: the store hooks' answers for cdom
 * tokens; 6, 7: the slot probes before and after the chain. */
EXPORT("t_store_cb_seen") int32_t t_store_cb_seen(uint32_t k, uint32_t f) {
  if (k == 5) return (int32_t)store_cdom_heard;
  if (k == 6 || k == 7) return (int32_t)sc_probe[k - 6];
  const sc_ctx *c = &sc[k];
  return f == 0 ? c->v : f == 1 ? (int32_t)c->calls : f == 2 ? (int32_t)c->sum : c->self == c;
}

/* 30. cd_ws_open_cb next to cd_ws_open, against the page server's echo. The hooks' sockets send WA, the
 * callback's send WB, each closes once its echo is back, so a message that reached the wrong sink is
 * the wrong bytes there. At once: socket G1 (the hooks) and C1 (a callback, ctx wc[0]). G1's close
 * opens C2 (ctx wc[1]) and C1's close opens G2, each from the close answer, when the id just closed is
 * the one the shim hands out next: so C2 takes G1's id and G2 takes C1's, and a socket's kind must not
 * follow its id. Then (t_ws_cb_cycles) n callback sockets opened and closed one after another, each
 * from the last one's close: their ids are one id, and a slot probe before and after names one slot
 * (every socket's slot freed at its close). */
static const uint8_t WA[3] = {0xa1, 0xa2, 0xa3}, WB[4] = {0xb1, 0xb2, 0xb3, 0xb4};
typedef struct { uint32_t open, msg, close, ok, code, id, own; } wp_sink;
static wp_sink wp_g, wp_c[2]; /* the hooks' (both sockets), and each callback socket's */
static uint32_t wp_g1, wp_g2 = ~0u, wp_c1, wp_c2 = ~0u;
static int bytes_are(const uint8_t *p, uint32_t len, const uint8_t *want, uint32_t n) {
  if (len != n) return 0;
  for (uint32_t i = 0; i < n; i++) if (p[i] != want[i]) return 0;
  return 1;
}
static void wp_sink_ev(wp_sink *s, uint32_t ev, uint32_t id, const uint8_t *p, uint32_t len, const uint8_t *mine, uint32_t n) {
  if (ev == CD_WS_OPEN) {
    s->open++;
    __builtin_memcpy(out_msg, mine, n);
    cd_ws_send(id, out_msg, n);
    __builtin_memset(out_msg, 0xee, n);
  } else if (ev == CD_WS_MSG) {
    s->msg++;
    s->ok += bytes_are(p, len, mine, n);
    cd_ws_close(id);
  } else {
    s->close++;
    s->code = len;
  }
}
static void wp_cb(void *ctx, uint32_t id, uint32_t ev, const uint8_t *p, uint32_t len);
static void wp_hook(uint32_t ev, uint32_t id, const uint8_t *p, uint32_t len) {
  wp_sink_ev(&wp_g, ev, id, p, len, WA, sizeof WA);
  if (ev == CD_WS_CLOSE && id == wp_g1 && wp_c2 == ~0u) {
    char url[40];
    wp_c2 = cd_ws_open_cb(wp_cb, &wp_c[1], url, ws_url(url));
  }
  hold(net_txt); /* no cd_flush: the network exports flush */
}
static void wp_cb(void *ctx, uint32_t id, uint32_t ev, const uint8_t *p, uint32_t len) {
  enter();
  wp_sink *s = ctx;
  s->own += s == &wp_c[0] || s == &wp_c[1];
  s->id = id;
  wp_sink_ev(s, ev, id, p, len, WB, sizeof WB);
  if (ev == CD_WS_CLOSE && s == &wp_c[0]) wp_g2 = ws_open_here();
  hold(net_txt);
}
EXPORT("t_ws_pair") void t_ws_pair(void) {
  char url[40];
  ws_mode = WS_PAIR;
  wp_g1 = ws_open_here();
  wp_c1 = cd_ws_open_cb(wp_cb, &wp_c[0], url, ws_url(url));
}
/* k 0 the hooks, 1 and 2 the callback sockets: open, msg, close, ok (bytes right), close code, the id the
 * callback heard, calls with its own ctx; k 3: the ids G1, C1, C2, G2. */
EXPORT("t_ws_pair_seen") uint32_t t_ws_pair_seen(uint32_t k, uint32_t f) {
  if (k == 3) return f == 0 ? wp_g1 : f == 1 ? wp_c1 : f == 2 ? wp_c2 : wp_g2;
  const wp_sink *s = k == 0 ? &wp_g : &wp_c[k - 1];
  const uint32_t v[7] = {s->open, s->msg, s->close, s->ok, s->code, s->id, s->own};
  return v[f];
}

static uint32_t wcy_left, wcy_done, wcy_lo = ~0u, wcy_hi, wcy_probe[2], wcy_ctx_ok;
static void wcy_cb(void *ctx, uint32_t id, uint32_t ev, const uint8_t *, uint32_t) {
  enter();
  wcy_ctx_ok += ctx == &wcy_left;
  if (ev == CD_WS_OPEN) {
    if (id < wcy_lo) wcy_lo = id;
    if (id > wcy_hi) wcy_hi = id;
    cd_ws_close(id);
  } else if (ev == CD_WS_CLOSE) {
    wcy_done++;
    char url[40];
    if (--wcy_left) cd_ws_open_cb(wcy_cb, &wcy_left, url, ws_url(url));
    else wcy_probe[1] = slot_probe();
  }
}
EXPORT("t_ws_cb_cycles") void t_ws_cb_cycles(uint32_t n) {
  char url[40];
  wcy_left = n;
  wcy_probe[0] = slot_probe();
  cd_ws_open_cb(wcy_cb, &wcy_left, url, ws_url(url));
}
/* 0 cycles done, 1 and 2 the lowest and highest id, 3 and 4 the probes, 5 calls with the right ctx. */
EXPORT("t_ws_cb_cycle") uint32_t t_ws_cb_cycle(uint32_t f) {
  const uint32_t v[6] = {wcy_done, wcy_lo, wcy_hi, wcy_probe[0], wcy_probe[1], wcy_ctx_ok};
  return v[f];
}

/* 31. The shim's view of C's structs is C's: every offset it uses comes from cd_layout. A box listens
 * for pointerdown, wheel and keydown with cd_on, and the page fires each at a grandchild (the p is
 * the box's child 1, the b the p's child 2: path 0x201, depth 2) with a known value in every field;
 * the handler keeps the record, and C hands back each field by its name. The other way, C passes
 * every kind of cd_val (an object's pairs among them) to a page function, and reads the viewport. */
static cd_event_rec layout_rec;
static cd_id layout_box;
static void layout_heard(const cd_event_rec *e, void *) {
  __builtin_memcpy(&layout_rec, e, sizeof layout_rec);
}
EXPORT("t_layout") cd_id t_layout(void) {
  cd_id s = section("layout");
  cd_at(s, CD_APPEND);
  cd_open("div"); cd_name_open(layout_box = cd_id_new());
  cd_open("i"); cd_close();
  cd_open("p");
  cd_open("b"); cd_close(); cd_open("b"); cd_close();
  cd_open("b"); cd_text("target"); cd_close();
  cd_close();
  cd_close();
  cd_on(layout_box, CD_EV_POINTERDOWN, layout_heard, 0);
  cd_on(layout_box, CD_EV_WHEEL, layout_heard, 0);
  cd_on(layout_box, CD_EV_KEYDOWN, layout_heard, 0);
  return layout_box;
}
/* The last record's fields by name, in cd_event_rec's order; dx and dy times 100. */
EXPORT("t_layout_field") int32_t t_layout_field(int32_t k) {
  const cd_event_rec *e = &layout_rec;
  const int32_t v[15] = {e->type, e->id, e->x, e->y, e->button, e->key, e->mods, e->path, e->depth,
                         e->ch, e->ms, e->pointer, e->buttons, (int32_t)(e->dx * 100), (int32_t)(e->dy * 100)};
  return v[k];
}
EXPORT("t_layout_vals") void t_layout_vals(void) {
  const cd_val v[] = {cd_v_num(2.5), cd_v_str("two"), cd_v_bool(1), cd_v_id(layout_box), cd_v_null(),
                      cd_v_obj(2), cd_v_str("a"), cd_v_num(-1.25), cd_v_str("b"), cd_v_bool(0),
                      cd_v_num(1e300)};
  cd_call_args(CD_WINDOW, "layoutVals", 7, v);
}
EXPORT("t_layout_viewport") int32_t t_layout_viewport(int32_t k) {
  cd_viewport_rec v;
  cd_viewport(&v);
  const int32_t f[4] = {v.w, v.h, v.hidden, v.dpr100};
  return f[k];
}

/* 32. Sockets the browser refuses to make. cd_ws_open_cb on a URL its WebSocket constructor throws on: a
 * scheme it opens no socket on (a real SyntaxError), and a host the page makes it refuse with a
 * SecurityError (as Firefox does for ws: from an https: page). Each socket's callback hears REFUSED, then
 * CLOSE with CD_WS_REFUSED_CODE, never OPEN, and nothing while cd_ws_open_cb runs; until then its id takes
 * a send, a close and a buffered query, and is no other socket's; at the close its id is handed out
 * again, and its slot freed (probes around a third socket, alone, name one slot). The hooks' cd_ws_open on such a URL throws, and the hooks hear nothing. Then
 * cd_page_origin, whole and cut short. */
typedef struct { uint32_t seq, n, code, id, inside, own, buffered; } rf_sink;
static rf_sink rf[3];
static uint32_t rf_ids[3], rf_probe[2], rf_opening, rf_left;
static void rf_cb(void *ctx, uint32_t id, uint32_t ev, const uint8_t *, uint32_t len) {
  enter();
  rf_sink *s = ctx;
  s->own += s == &rf[0] || s == &rf[1] || s == &rf[2];
  s->inside += rf_opening;
  s->seq = s->seq * 10 + ev + 1; /* the events in order, one digit each: REFUSED then CLOSE is 43 */
  s->n++;
  s->id = id;
  if (ev == CD_WS_CLOSE) {
    s->code = len;
    if (!--rf_left && s == &rf[2]) rf_probe[1] = slot_probe();
  }
}
static uint32_t rf_open(uint32_t k, const char *url) {
  uint32_t n = 0;
  while (url[n]) n++;
  rf_opening = 1;
  uint32_t id = cd_ws_open_cb(rf_cb, &rf[k], url, n);
  cd_ws_send(id, MSG, sizeof MSG);
  rf[k].buffered = cd_ws_buffered(id) + 1;
  cd_ws_close(id);
  rf_opening = 0;
  return id;
}
EXPORT("t_ws_refused") void t_ws_refused(void) {
  rf_left = 2;
  rf_ids[0] = rf_open(0, "ftp://127.0.0.1/ws");
  rf_ids[1] = rf_open(1, "ws://blocked.example/ws");
}
/* Once both have closed: a third, alone, which takes one of their ids, with slot probes around it. */
EXPORT("t_ws_refused_again") void t_ws_refused_again(void) {
  rf_probe[0] = slot_probe();
  rf_left = 1;
  rf_ids[2] = rf_open(2, "ftp://127.0.0.1/again");
}
/* k 0 to 2 the sockets: events in order, how many, the close code, the id heard, calls inside the open,
 * calls with its own ctx, buffered + 1 during the open, the id cd_ws_open_cb returned; k 3: the probes. */
EXPORT("t_ws_refused_seen") uint32_t t_ws_refused_seen(uint32_t k, uint32_t f) {
  if (k == 3) return rf_probe[f];
  const rf_sink *s = &rf[k];
  const uint32_t v[8] = {s->seq, s->n, s->code, s->id, s->inside, s->own, s->buffered, rf_ids[k]};
  return v[f];
}
EXPORT("t_ws_hook_refused") uint32_t t_ws_hook_refused(void) {
  static const char url[] = "ftp://127.0.0.1/hook";
  return cd_ws_open(url, sizeof url - 1);
}
EXPORT("t_ws_hook_heard") uint32_t t_ws_hook_heard(void) { return ws_hook_heard; }
static char origin_buf[128];
EXPORT("t_page_origin") uint32_t t_page_origin(uint32_t cap) {
  __builtin_memset(origin_buf, '*', sizeof origin_buf);
  return cd_page_origin(origin_buf, cap);
}
EXPORT("t_origin_buf") char *t_origin_buf(void) { return origin_buf; }

/* 20. Nested regions and cd_on_free (cdom.h, regions). A parent-mode region A holds a root-mode
 * child B, built inside A's build, and B a parent-mode grandchild C. Each cleanup logs its name and
 * how many ids were live when it ran, so the order (children first, newest first) and the time
 * (before any id of its region goes) are both visible. */
static cd_region nest_a, nest_b, nest_c;
static cd_id nest_box, nest_c_box;
static uint32_t nest_log[8], nest_live[8], nest_n;
enum { NEST_A1 = 1, NEST_A2, NEST_B1, NEST_C1 };
static void nest_cleanup(void *ctx) {
  if (nest_n < 8) { nest_log[nest_n] = (uint32_t)(uintptr_t)ctx; nest_live[nest_n++] = cd_ids_live(); }
}
static void nest_show_b(void) { /* B inside the region that builds now (A), or in nest_b.up when none builds */
  if (!cd_region_show(&nest_b, 1)) return;
  cd_at(nest_box, CD_APPEND);
  cd_open("p"); cd_name_open(nest_b.root); cd_attr("class", "b");
  cd_open("span"); cd_name_open(nest_c_box = cd_id_new()); cd_close();
  cd_close();
  cd_on_free(0, nest_cleanup, (void *)NEST_B1);
  nest_c.parent = nest_c_box;
  if (cd_region_show(&nest_c, 1)) {
    cd_at(nest_c_box, CD_APPEND);
    cd_open("i"); cd_name_open(cd_id_new()); cd_text("c"); cd_close();
    cd_on_free(&nest_c, nest_cleanup, (void *)NEST_C1);
    cd_region_end(&nest_c);
  }
  cd_region_end(&nest_b);
}
EXPORT("t_nest_section") void t_nest_section(void) {
  cd_id s = section("nest");
  cd_at(s, CD_APPEND); cd_open("div"); cd_name_open(nest_a.parent = cd_id_new()); cd_close();
}
EXPORT("t_nest_build") void t_nest_build(void) {
  nest_n = 0;
  if (!cd_region_show(&nest_a, 1)) return;
  cd_at(nest_a.parent, CD_APPEND);
  cd_open("div"); cd_name_open(nest_box = cd_id_new()); cd_attr("class", "a"); cd_close();
  cd_on_free(0, nest_cleanup, (void *)NEST_A1);
  nest_show_b();
  cd_on_free(0, nest_cleanup, (void *)NEST_A2);
  cd_region_end(&nest_a);
}
EXPORT("t_nest_hide") void t_nest_hide(uint32_t k) { cd_region_hide(k == 0 ? &nest_a : k == 1 ? &nest_b : &nest_c); }
EXPORT("t_nest_show_b") void t_nest_show_b(void) { nest_show_b(); }
EXPORT("t_nest_log") uint32_t t_nest_log(uint32_t i, uint32_t live) { return live ? nest_live[i] : i < nest_n ? nest_log[i] : 0; }
EXPORT("t_nest_shown") uint32_t t_nest_shown(void) { return nest_a.shown | nest_b.shown << 1 | nest_c.shown << 2; }
EXPORT("t_nest_kids") uint32_t t_nest_kids(void) { /* A's children, a cycle capped at 9 */
  uint32_t n = 0;
  for (cd_region *k = nest_a.kids; k && n < 9; k = k->next) n++;
  return n;
}

/* 20b. Handing a region to another module (Alex's case): module A builds its region with an empty
 * box and hands the region and the box to module B's view, which fills the box later, from a timer,
 * in a region of its own whose up is A's. B's C state (a slot it holds) goes when A's region goes,
 * through cd_on_free, as does B's timer when A goes before it fired. */
typedef struct { cd_region r; uint32_t timer, holding; } b_view;  /* module B's state */
static b_view b_views[2];
static uint32_t b_released, b_stopped;
static void b_release(void *ctx) { b_view *v = ctx; v->holding = 0; b_released++; }
static void b_stop(void *ctx) { b_view *v = ctx; if (v->timer) { cd_timer_stop(v->timer); v->timer = 0; b_stopped++; } }
static void b_fill(void *ctx) { /* the timer: no region builds now */
  b_view *v = ctx;
  v->timer = 0;
  if (!cd_region_show(&v->r, 1)) return;
  cd_at(v->r.parent, CD_APPEND);
  cd_open("span"); cd_name_open(cd_id_new()); cd_text("loaded"); cd_close();
  v->holding = 1;
  cd_on_free(0, b_release, v);
  cd_region_end(&v->r);
}
static void b_mount(b_view *v, cd_region *up, cd_id box, uint32_t ms) { /* module B's entry */
  v->r.up = up;
  v->r.parent = box;
  v->timer = cd_timer_cb(ms, b_fill, v);
  cd_on_free(up, b_stop, v);
}
static cd_region hand_a[2];
static cd_id hand_sec;
EXPORT("t_handoff_section") void t_handoff_section(void) { hand_sec = section("handoff"); }
EXPORT("t_handoff") void t_handoff(uint32_t k, uint32_t ms) { /* module A */
  cd_id sec = hand_sec;
  cd_region *a = &hand_a[k];
  if (!cd_region_show(a, 1)) return;
  cd_at(sec, CD_APPEND);
  cd_open("div"); cd_name_open(a->root); cd_attr("class", k ? "a1" : "a0");
  cd_id box = cd_open_id("section"); cd_close();
  cd_close();
  cd_region_end(a);
  b_mount(&b_views[k], a, box, ms);
}
EXPORT("t_handoff_hide") void t_handoff_hide(uint32_t k) { cd_region_hide(&hand_a[k]); }
EXPORT("t_handoff_state") uint32_t t_handoff_state(uint32_t k) {
  return b_views[k].holding | b_views[k].r.shown << 1 | (b_views[k].timer != 0) << 2 | b_released << 4 | b_stopped << 8;
}

/* 20c. cd_region_forget: no DOM op, cleanups and ids as a hide. */
static cd_region forget_r;
static uint32_t forget_ran;
static void forget_cleanup(void *) { forget_ran++; }
EXPORT("t_forget_build") void t_forget_build(void) {
  cd_id s = section("forget");
  forget_r.parent = s;
  if (cd_region_show(&forget_r, 1)) {
    cd_at(s, CD_APPEND);
    cd_open("b"); cd_name_open(cd_id_new()); cd_text("kept"); cd_close();
    cd_on_free(0, forget_cleanup, 0);
    cd_region_end(&forget_r);
  }
}
EXPORT("t_forget") uint32_t t_forget(void) { cd_region_forget(&forget_r); return forget_ran | forget_r.shown << 8; }

/* 20d. What traps (unit.html runs these last: a trap leaves cdom's state as it was mid-call). */
static cd_region trap_r[10], trap_other;
static void trap_dom_cleanup(void *ctx) { cd_set_class((cd_id)(uintptr_t)ctx, "late"); }
EXPORT("t_trap_on_free_idle") void t_trap_on_free_idle(void) { cd_on_free(0, forget_cleanup, 0); }
EXPORT("t_trap_up_other") void t_trap_up_other(void) { /* r->up names A, but B builds */
  static cd_region in, other, r;
  cd_id s = section("trapup");
  in.parent = other.parent = r.parent = s;
  if (cd_region_show(&other, 1)) cd_region_end(&other);
  r.up = &other;
  if (cd_region_show(&in, 1)) { cd_region_show(&r, 1); }
}
EXPORT("t_trap_up_hidden") void t_trap_up_hidden(void) {
  static cd_region r;
  r.up = &trap_other; /* never shown */
  r.parent = CD_BODY;
  cd_region_show(&r, 1);
}
EXPORT("t_trap_cleanup_dom") void t_trap_cleanup_dom(void) {
  cd_id s = section("trapdom");
  static cd_region r;
  r.parent = s;
  if (cd_region_show(&r, 1)) { cd_on_free(0, trap_dom_cleanup, (void *)(uintptr_t)s); cd_region_end(&r); }
  cd_region_hide(&r);
}
static uint32_t trap_depth_shown;
EXPORT("t_trap_depth") void t_trap_depth(void) { /* nested builds: the 9th traps */
  for (uint32_t n = 0; n < 10; n++) { trap_r[n].parent = CD_BODY; cd_region_show(&trap_r[n], 1); trap_depth_shown++; }
}
EXPORT("t_trap_depth_shown") uint32_t t_trap_depth_shown(void) { return trap_depth_shown; }

/* 21. Regions inside list rows: each row's create opens a region of its own in the row (no outer
 * region's child), with a cleanup; destroy forgets it. The list lives in an outer region, which
 * forgets the list from a cleanup when it goes. Each cleanup logs its key and the ids live when it
 * ran, so "before any of the row's ids" is visible. */
static cd_list rows_list;
static cd_region rows_outer, rows_r[16];
static cd_id rows_box[16];
static uint32_t rows_log[32], rows_live_at[32], rows_n, rows_before;
static void rows_cleanup(void *ctx) {
  if (rows_n < 32) { rows_log[rows_n] = (uint32_t)(uintptr_t)ctx; rows_live_at[rows_n++] = cd_ids_live(); }
}
static cd_id rows_create(void *ctx, uint32_t i, uint32_t *data) {
  uint32_t k = ((const uint32_t *)ctx)[i];
  cd_id li = cd_open_id("li");
  cd_open("span"); cd_name_open(rows_box[k] = cd_id_new()); cd_close();
  cd_close();
  cd_region *r = &rows_r[k];
  r->parent = rows_box[k];
  if (cd_region_show(r, 1)) {
    cd_at(rows_box[k], CD_APPEND);
    cd_open("b"); cd_name_open(cd_id_new()); cd_text_num(k); cd_close();
    cd_on_free(0, rows_cleanup, (void *)(uintptr_t)k);
    cd_region_end(r);
  }
  *data = k;
  return li;
}
static void rows_destroy(void *, cd_item *it) {
  cd_region_forget(&rows_r[it->data]);
  cd_id_free(rows_box[it->data]);
}
static uint32_t rows_keys[8];
static const cd_list_fns ROWS = {rows_keys, rows_create, nop_patch, rows_destroy};
static void rows_forget_list(void *ctx) { cd_list_forget(ctx, &ROWS); }
static cd_id rows_sec;
EXPORT("t_rows_section") void t_rows_section(void) { rows_sec = section("rows"); rows_outer.parent = rows_sec; }
EXPORT("t_rows") void t_rows(uint32_t step) {
  static const uint32_t K[4][5] = {{4, 1, 2, 3, 4}, {3, 1, 3, 4}, {3, 4, 3, 1}, {2, 5, 6}};
  rows_before = cd_ids_live();
  if (step == 4) { cd_region_hide(&rows_outer); return; }
  if (step == 0 && cd_region_show(&rows_outer, 1)) {
    cd_at(rows_sec, CD_APPEND);
    cd_open("ul"); cd_name_open(rows_list.parent = cd_id_new()); cd_close();
    rows_list.owns_parent = 1;
    cd_on_free(0, rows_forget_list, &rows_list);
    cd_region_end(&rows_outer);
  }
  for (uint32_t i = 0; i < K[step][0]; i++) rows_keys[i] = K[step][i + 1];
  cd_list_update(&rows_list, rows_keys, K[step][0], &ROWS);
}
EXPORT("t_rows_log") uint32_t t_rows_log(uint32_t i, uint32_t live) { return live ? rows_live_at[i] : rows_log[i]; }
EXPORT("t_rows_n") uint32_t t_rows_n(void) { return rows_n; }
EXPORT("t_rows_before") uint32_t t_rows_before(void) { return rows_before; }

/* 22. cd_dirty: renders run once each, in the order asked, before the entry from JS returns (here a
 * click through cd_on, so cd_event's flush runs them); rounds, cd_undirty, cd_render_now. Each render
 * logs one letter. */
static char dlog[32];
static uint32_t dlog_n, dmode, dcount, d_e;
static cd_id dtext, dbtn;
static void dput(char c) { if (dlog_n < 31) dlog[dlog_n++] = c; }
static void dv(void *ctx) { /* A and C draw the count */
  char c = (char)(uintptr_t)ctx;
  dput(c);
  if (c == 'A' || c == 'C') cd_set_text_node_num(dtext, ++dcount);
}
static void d_r2(void *ctx);
static void d_r1(void *) { dput('D'); cd_dirty(d_r2, 0); cd_render_now(); dput('d'); }
static void d_r2(void *) { dput('E'); if (++d_e == 1) cd_dirty(d_r1, 0); }
static void d_x(void *) { dput('X'); cd_undirty(dv, (void *)'Y'); }
static void d_loop(void *) { dput('L'); cd_dirty(d_loop, 0); }
#define DV(c) cd_dirty(dv, (void *)(uintptr_t)(c))
static void dirty_click(const cd_event_rec *, void *) {
  if (dmode == 1) { DV('A'); DV('A'); DV('B'); DV('A'); DV('C'); DV('B'); }
  if (dmode == 2) cd_dirty(d_r1, 0);
  if (dmode == 3) { DV('A'); DV('B'); cd_undirty(dv, (void *)'B'); cd_dirty(d_x, 0); DV('Y'); }
  if (dmode == 4) { DV('A'); cd_render_now(); dput('H'); }
}
EXPORT("t_dirty_section") void t_dirty_section(void) {
  cd_id s = section("dirty");
  cd_at(s, CD_APPEND);
  dbtn = cd_open_id("button"); cd_close();
  cd_open("b"); cd_text_num(0); cd_name_text(dtext = cd_id_new()); cd_close();
  cd_on(dbtn, CD_EV_CLICK, dirty_click, 0);
}
EXPORT("t_dirty_mode") void t_dirty_mode(uint32_t m) { dmode = m; dlog_n = 0; d_e = 0; __builtin_memset(dlog, 0, sizeof dlog); }
EXPORT("t_dirty_log") const char *t_dirty_log(void) { return dlog; }
EXPORT("t_dirty_loop") void t_dirty_loop(void) { cd_dirty(d_loop, 0); cd_render_now(); }
EXPORT("t_dirty_n") uint32_t t_dirty_n(void) { return dlog_n; }

/* 23. The reads, the layout reads, cd_set and the calls run the pending renders first. The render
 * widens a box from 50 to 120 px and logs R; each read logs r after it. A region's cleanup that
 * reads does not run them (they would change the DOM inside it), and they run after. */
static cd_id sync_box, sync_input;
static char slog[8];
static uint32_t slog_n;
static void sput(char c) { if (slog_n < 7) slog[slog_n++] = c; }
static void sync_view(void *) { sput('R'); cd_set_style_num(sync_box, "width", 120, CD_UNIT_PX); }
EXPORT("t_sync_section") void t_sync_section(void) {
  cd_id s = section("sync");
  cd_at(s, CD_APPEND);
  sync_box = cd_open_id("div");
  cd_attr("style", "width: 50px; height: 10px; position: fixed; left: 0; top: 0");
  cd_listen(sync_box, CD_EV_CLICK);
  cd_close();
  sync_input = cd_open_id("input"); cd_close();
}
static double sync_w;
EXPORT("t_sync") const char *t_sync(uint32_t k) {
  char buf[16];
  double r[4] = {0};
  cd_viewport_rec v;
  slog_n = 0;
  __builtin_memset(slog, 0, sizeof slog);
  cd_set_style_num(sync_box, "width", 50, CD_UNIT_PX);
  cd_dirty(sync_view, 0);
  switch (k) {
  case 0: sync_w = cd_get_num(sync_box, "offsetWidth"); break;
  case 1: cd_get_str(sync_box, "style.width", buf, sizeof buf); break;
  case 2: cd_get_id(CD_DOCUMENT, "activeElement"); break;
  case 3: { uint32_t n; cd_input_whole(sync_input, buf, sizeof buf, &n); } break;
  case 4: cd_rect(sync_box, r); sync_w = r[2]; break;
  case 5: cd_hit(1, 1); break;
  case 6: cd_viewport(&v); break;
  case 7: cd_set_num(sync_input, "value", 1); break;
  case 8: cd_call(sync_input, "blur"); break;
  }
  sput('r');
  cd_render_now(); /* a case that did not run it leaves it pending: run it so the next starts clean */
  return slog;
}
EXPORT("t_sync_w") double t_sync_w(void) { return sync_w; }
static cd_region sync_r;
static void sync_cleanup(void *) { sput('c'); sync_w = cd_get_num(sync_box, "offsetWidth"); }
EXPORT("t_sync_cleanup") const char *t_sync_cleanup(void) {
  slog_n = 0;
  __builtin_memset(slog, 0, sizeof slog);
  cd_set_style_num(sync_box, "width", 50, CD_UNIT_PX);
  sync_r.parent = sync_input;
  if (cd_region_show(&sync_r, 1)) { cd_on_free(0, sync_cleanup, 0); cd_region_end(&sync_r); }
  cd_dirty(sync_view, 0);
  cd_region_hide(&sync_r);
  sput('h');
  cd_render_now();
  return slog;
}

/* 24. Bound values: a set writes only when the value changed (unit.html counts the mutations each
 * step makes). Bound inside a region, they are unbound when it goes. A literal's id is its slot, one
 * per call site, so a value set again is set from the same call site (a loop, or two steps that share
 * a case). */
static cd_region bound_r;
static cd_bound bt, ba, bc, bs, bb;
EXPORT("t_bound_section") void t_bound_section(void) {
  cd_id s = section("bound");
  bound_r.parent = s;
  if (!cd_region_show(&bound_r, 1)) return;
  cd_at(s, CD_APPEND);
  cd_id t, el, in;
  cd_open("p"); cd_text(""); cd_name_text(t = cd_id_new()); cd_close();
  el = cd_open_id("b"); cd_close();
  in = cd_open_id("input"); cd_close();
  cd_bind_text(&bt, t);
  cd_bind_attr(&ba, el, "data-x");
  cd_bind_class(&bc, el);
  cd_bind_style(&bs, el, "width");
  cd_bind_bool(&bb, in, "disabled");
  cd_region_end(&bound_r);
}
static uint32_t bound_pend; /* cd_pending() at the end of the last step: a walk, or a runtime string made and not read */
static void bset_tmp(cd_bound *b, const char *s) { uint32_t n = 0; while (s[n]) n++; cd_bound_set_bytes(b, s, n); }
EXPORT("t_bound") void t_bound(uint32_t step) {
  switch (step) {
  case 0: case 1: cd_bound_set(&bt, "a"); break; /* 1, then 0 */
  case 2: cd_bound_set(&bt, "b"); break;          /* 1 */
  case 3: bset_tmp(&bt, "x5"); break;                /* 1 */
  case 4: bset_tmp(&bt, "x5"); break;                /* 0, and nothing left to cross */
  case 5: bset_tmp(&bt, "x6"); break;                /* 1 */
  case 6: cd_bound_set_num(&bt, 7); cd_bound_set_num(&bt, 7); break; /* 1 */
  case 7: /* 2 */
    for (int k = 0; k < 2; k++) cd_bound_set(&ba, "on");
    for (int k = 0; k < 2; k++) cd_bound_set_num(&ba, 3);
    break;
  case 8: for (int k = 0; k < 2; k++) cd_bound_set(&bc, "k"); break; /* 1 */
  case 9: cd_bound_set(&bs, "10px"); bset_tmp(&bs, "10px"); bset_tmp(&bs, "10px"); break; /* 1: the browser records no change for the same style, set again by bytes */
  case 10: cd_bound_set_num(&bb, 1); cd_bound_set_num(&bb, 5); cd_bound_set_num(&bb, 0); break; /* 2: 5 is the same true as 1 */
  case 11: cd_region_hide(&bound_r); break;
  }
  bound_pend = cd_pending(); /* read inside the step: the shim ends what a call left when it returns */
}
EXPORT("t_bound_pending") uint32_t t_bound_pending(void) { return bound_pend; }
EXPORT("t_bound_live") uint32_t t_bound_live(void) {
  return cd_bound_live(&bt) | cd_bound_live(&ba) << 1 | cd_bound_live(&bc) << 2 | cd_bound_live(&bs) << 3 | cd_bound_live(&bb) << 4;
}
static cd_bound trap_b;
static char trap_bytes[] = "a"; /* mutable, so it is a runtime string, read on use */
EXPORT("t_trap_bound_unbound") void t_trap_bound_unbound(void) { cd_bound_set_num(&trap_b, 1); } /* the node would be id 0 */
EXPORT("t_trap_bound_tmp") void t_trap_bound_tmp(void) { /* cd_bound_set given a runtime string */
  cd_id s = section("trapbound"), t;
  cd_at(s, CD_APPEND);
  cd_text(""); cd_name_text(t = cd_id_new());
  cd_bind_text(&trap_b, t);
  cd_bound_set(&trap_b, trap_bytes);
}

/* 25. Request slots. The answers here are timers (cd_timer_cb), which carry the ctx as cd_http_cb's
 * would; a region's cleanup drops the slot, so an answer after the region went is not applied. */
static cd_req rq;
static uint32_t rq_heard[4], rq_applied[4];
static uint32_t rq_check(uint32_t, void *ctx) { /* one synchronous check: 1 current, plus the index */
  uint16_t i = 0xffff;
  return cd_req_current(&rq, ctx, &i) ? 0x10000u | i : 0;
}
EXPORT("t_req_sync") uint32_t t_req_sync(void) {
  /* each bit one fact, all must be 1 */
  uint32_t ok = 0, b = 0;
  void *a = cd_req_begin(&rq, 3);
  ok |= (rq_check(0, a) == (0x10000u | 3)) << b++;        /* the latest, its index back */
  ok |= (rq.state == CD_REQ_LOADING) << b++;
  void *c = cd_req_begin(&rq, 4);
  ok |= (rq_check(0, a) == 0) << b++;                      /* an older generation */
  ok |= (rq_check(0, c) == (0x10000u | 4)) << b++;
  cd_req_end(&rq, 1);
  ok |= (rq.state == CD_REQ_OK && rq_check(0, c) == 0) << b++; /* over: its answers are dropped */
  void *d = cd_req_begin(&rq, 5);
  cd_req_end(&rq, 0);
  ok |= (rq.state == CD_REQ_FAILED && rq_check(0, d) == 0) << b++;
  void *e = cd_req_begin(&rq, 6);
  cd_req_drop(&rq);
  ok |= (rq.state == CD_REQ_IDLE && rq_check(0, e) == 0) << b++;
  void *f = cd_req_begin(&rq, 7);
  ok |= (rq_check(0, f) == (0x10000u | 7)) << b++;        /* begins again after a drop */
  cd_req_drop(&rq);
  return ok == (1u << b) - 1 ? 1 : ok;
}
static cd_req rq_run;
static cd_region rq_r;
static void rq_answer(void *ctx) {
  uint16_t i;
  uint32_t v = (uint32_t)(uintptr_t)ctx & 0xffff;
  rq_heard[v]++;
  if (cd_req_current(&rq_run, ctx, &i)) rq_applied[i]++;
}
static void rq_drop(void *ctx) { cd_req_drop(ctx); }
EXPORT("t_req_async") void t_req_async(void) {
  cd_id s = section("req");
  rq_r.parent = s;
  if (cd_region_show(&rq_r, 1)) { cd_on_free(0, rq_drop, &rq_run); cd_region_end(&rq_r); }
  cd_timer_cb(10, rq_answer, cd_req_begin(&rq_run, 0)); /* an older run: dropped by the next begin */
  void *run = cd_req_begin(&rq_run, 1);
  cd_timer_cb(10, rq_answer, run);
  cd_timer_cb(10, rq_answer, (void *)((uintptr_t)run + 1)); /* two lines of one run, both current */
  cd_timer_cb(60, rq_answer, (void *)((uintptr_t)run + 2)); /* after the region went: dropped */
}
EXPORT("t_req_hide") void t_req_hide(void) { cd_region_hide(&rq_r); }
EXPORT("t_req_seen") uint32_t t_req_seen(uint32_t i, uint32_t applied) { return applied ? rq_applied[i] : rq_heard[i]; }
/* 24b. cd_bound_shows: what a node was built with is recorded, so the first set of it writes nothing.
 * The values come from a const table, so "a" is one read-only address wherever it is passed. */
static cd_bound bshow;
static const char *const SHOW[] = {"a", "b"};
EXPORT("t_bound_shows") void t_bound_shows(uint32_t step) {
  if (step == 0) {
    cd_id s = section("boundshows"), t;
    cd_at(s, CD_APPEND);
    cd_text("a"); cd_name_text(t = cd_id_new());
    cd_bind_text(&bshow, t);
    cd_bound_shows(&bshow, SHOW[0]);
  } else cd_bound_set(&bshow, SHOW[step - 1]);
}

/* 26. Timer slots: a set replaces the pending timer (a debounce); a fired slot is empty before its fn
 * runs, which may set it again; a clear, from a region's cleanup too, stops what is pending. */
static cd_timer_slot ts[4];
static uint32_t ts_heard[8], ts_pending_inside;
static void ts_fn(void *ctx) {
  uint32_t k = (uint32_t)(uintptr_t)ctx;
  ts_heard[k]++;
  if (k == 2) { if (!ts_pending_inside) ts_pending_inside = cd_timer_pending(&ts[1]) + 1; /* at the first fire */ if (ts_heard[2] == 1) cd_timer_set(&ts[1], 5, ts_fn, (void *)2); }
}
static cd_region ts_r;
static void ts_clear(void *ctx) { cd_timer_clear(ctx); }
EXPORT("t_tslot") uint32_t t_tslot(void) {
  uint32_t pend = 0;
  cd_timer_set(&ts[0], 20, ts_fn, (void *)0); /* replaced before it fires */
  cd_timer_set(&ts[0], 20, ts_fn, (void *)1);
  pend |= (uint32_t)cd_timer_pending(&ts[0]);
  cd_timer_set(&ts[1], 5, ts_fn, (void *)2);  /* fires, and sets itself once more */
  cd_timer_set(&ts[2], 5, ts_fn, (void *)3);  /* cleared before it fires */
  cd_timer_clear(&ts[2]);
  pend |= (uint32_t)!cd_timer_pending(&ts[2]) << 1;
  cd_timer_clear(&ts[3]);                       /* a slot that never ran */
  cd_id s = section("tslot");
  ts_r.parent = s;
  if (cd_region_show(&ts_r, 1)) { cd_on_free(0, ts_clear, &ts[3]); cd_region_end(&ts_r); }
  cd_timer_set(&ts[3], 5, ts_fn, (void *)4);  /* its region goes first */
  cd_region_hide(&ts_r);
  pend |= (uint32_t)!cd_timer_pending(&ts[3]) << 2;
  return pend;
}
EXPORT("t_tslot_heard") uint32_t t_tslot_heard(uint32_t k) { return k == 7 ? ts_pending_inside : ts_heard[k]; }
EXPORT("t_tslot_after") uint32_t t_tslot_after(void) { /* every slot empty, and clearing one that fired is nothing */
  cd_timer_clear(&ts[0]);
  return (uint32_t)(cd_timer_pending(&ts[0]) | cd_timer_pending(&ts[1]) | cd_timer_pending(&ts[2]) | cd_timer_pending(&ts[3]));
}

/* 27. cdom_ui: elements, a button and the string builder. */
static cd_id ui_label;
static uint32_t ui_clicks, ui_click_ctx;
CD_ON_EVENT(ui_clicked) { ui_clicks++; ui_click_ctx = CD_CTX_INDEX(ctx); }
EXPORT("t_ui_build") void t_ui_build(void) {
  cd_id s = section("ui");
  cd_at(s, CD_APPEND);
  cd_el("div", "box");
  cd_el_text("p", CD_NONE, "plain");
  cd_id named = cd_el_id("span", "named");
  cd_close();
  cd_close();
  cd_set_attr(named, "data-n", "1");
  cd_button("go", "Go", ui_clicked, CD_CTX_OF(7), &ui_label);
  cd_button(CD_NONE, "Plain", ui_clicked, CD_CTX_OF(8), nullptr);
}
EXPORT("t_ui_relabel") void t_ui_relabel(void) { cd_set_text_node(ui_label, "Going"); }
EXPORT("t_ui_clicks") uint32_t t_ui_clicks(uint32_t k) { return k ? ui_click_ctx : ui_clicks; }
static char ui_room[48];
EXPORT("t_ui_buf") uint32_t t_ui_buf(uint32_t cap) { /* b.len, with 1 << 16 when over */
  cd_buf b = cd_buf_in(ui_room, cap);
  cd_buf_putz(&b, "u");
  cd_buf_u64(&b, 18446744073709551615ull);
  cd_buf_put(&b, " i", 2);
  cd_buf_i64(&b, -9223372036854775807ll - 1);
  cd_buf_putz(&b, " ");
  cd_buf_u64(&b, 0);
  cd_buf_putz(&b, "!"); /* after an over, nothing more: the buffer holds whole pieces, the first ones */
  return (uint32_t)b.over << 16 | b.len;
}
EXPORT("t_ui_room") char *t_ui_room(void) { return ui_room; }
/* 28. A region taken away owns no heap memory: an array of regions, each shown with ids and cleanups,
 * hidden and the array freed, round after round, keeps the ids live and the memory where they were.
 * And a stop inside a cleanup works (amgi's chip stops its voice as its region goes). */
static uint32_t rb_cleaned;
static void rb_cleanup(void *) { rb_cleaned++; }
EXPORT("t_regbuf") void t_regbuf(uint32_t rounds) {
  static cd_id sec;
  if (!sec) sec = section("regbuf");
  for (uint32_t k = 0; k < rounds; k++) {
    cd_region *rs = cd_alloc(8 * sizeof(cd_region));
    __builtin_memset(rs, 0, 8 * sizeof(cd_region));
    for (int i = 0; i < 8; i++) {
      if (!cd_region_show(&rs[i], 1)) continue;
      cd_at(sec, CD_APPEND);
      cd_open("i"); cd_name_open(rs[i].root);
      for (int j = 0; j < 10; j++) { cd_open("b"); cd_name_open(cd_id_new()); cd_close(); }
      cd_close();
      for (int j = 0; j < 6; j++) cd_on_free(0, rb_cleanup, 0);
      cd_region_end(&rs[i]);
    }
    for (int i = 0; i < 8; i++) cd_region_hide(&rs[i]);
    cd_free(rs);
  }
}
EXPORT("t_regbuf_cleaned") uint32_t t_regbuf_cleaned(void) { return rb_cleaned; }
static cd_region rb_stop_r;
static uint32_t rb_timer, rb_fired;
static void rb_fire(void *) { rb_fired++; }
static void rb_stop(void *) { cd_timer_stop(rb_timer); }
EXPORT("t_regbuf_stop") void t_regbuf_stop(void) {
  if (!rb_stop_r.parent) rb_stop_r.parent = section("regbufstop");
  if (cd_region_show(&rb_stop_r, 1)) { cd_on_free(0, rb_stop, 0); cd_region_end(&rb_stop_r); }
  rb_timer = cd_timer_cb(10, rb_fire, 0);
  cd_region_hide(&rb_stop_r);
}
EXPORT("t_regbuf_fired") uint32_t t_regbuf_fired(void) { return rb_fired; }
/* 29. Shared state (cd_state, cd_changed, cd_watch). Every view logs one letter (sput's log, slog's
 * twin); each case is an export that ends in cd_flush, as an entry from JS does, and returns the log of
 * that entry. The regions live under <section id=state>; each region is one <i>. */
static char stlog[64];
static uint32_t stlog_n;
static void stput(char c) { if (stlog_n < 63) stlog[stlog_n++] = c; }
static const char *st_end(void) { cd_flush(); stlog[stlog_n] = 0; return stlog; }
static void st_begin(void) { stlog_n = 0; __builtin_memset(stlog, 0, sizeof stlog); }
static cd_id st_sec;
/* Shows r, one <i> under the section or inside its parent region's <i>, and leaves the <i> open: 1 when
 * its build is open. st_show closes the <i> at once. */
static int st_open(cd_region *r) {
  if (!st_sec) st_sec = section("state");
  if (!cd_region_show(r, 1)) return 0;
  cd_at(r->up ? r->up->root : st_sec, CD_APPEND); /* r->up: set before, or the region building now */
  cd_open("i"); cd_name_open(r->root);
  return 1;
}
static int st_show(cd_region *r) {
  if (!st_open(r)) return 0;
  cd_close();
  return 1;
}
static void st_letter(void *ctx) { stput((char)(uintptr_t)ctx); }
#define ST_L(c) ((void *)(uintptr_t)(c))

/* A: one view of two states; one changed 1,000 times and the other once, in one click: it runs once. */
static cd_state sa1, sa2;
static cd_region ra;
static cd_id st_btn;
static void st_click(const cd_event_rec *, void *) {
  for (int i = 0; i < 1000; i++) cd_changed(&sa1);
  cd_changed(&sa2);
}
EXPORT("t_state_a_setup") void t_state_a_setup(void) {
  if (st_show(&ra)) {
    cd_watch(&sa1, st_letter, ST_L('a'));
    cd_watch(&sa2, st_letter, ST_L('a'));
    cd_region_end(&ra);
  }
  cd_at(st_sec, CD_APPEND);
  st_btn = cd_open_id("button"); cd_close();
  cd_on(st_btn, CD_EV_CLICK, st_click, 0);
  st_end();
  st_begin();
}
EXPORT("t_state_log") const char *t_state_log(void) { stlog[stlog_n] = 0; return stlog; }
EXPORT("t_state_a_ver") uint64_t t_state_a_ver(uint32_t k) { return k ? sa2.ver : sa1.ver; }

/* B: a state changed, then its view's region hidden, in one entry: the view does not run, and nothing
 * watches the state after; a later change runs nothing. */
static cd_state sb;
static cd_region rb;
static uint32_t b_seen = 9; /* whether sb was watched when rb's cleanup ran */
static void st_b_cleanup(void *) { b_seen = sb.first != 0; }
EXPORT("t_state_b") const char *t_state_b(uint32_t step) {
  st_begin();
  if (step == 0 && st_show(&rb)) { cd_on_free(0, st_b_cleanup, 0); cd_watch(&sb, st_letter, ST_L('b')); cd_region_end(&rb); }
  if (step == 1) { cd_changed(&sb); cd_region_hide(&rb); }
  if (step == 2) cd_changed(&sb);
  return st_end();
}
EXPORT("t_state_b_watched") uint32_t t_state_b_watched(void) { return (sb.first != 0 || sb.last != 0) + 10 * b_seen; }

/* C: two views of one state at one depth; the first hides the second's region, and shows a region whose
 * new view (n) watches the same state: the second does not run, and neither does n, made in the pass after
 * the change (a view taken away in the pass is freed at its end, so n cannot take its memory and its
 * place in the round). The next change runs c and n. */
static cd_state sc_st;
static cd_region rc1, rc2, rc3;
static void st_c1(void *) {
  stput('c');
  cd_region_hide(&rc2);
  if (st_show(&rc3)) { cd_watch(&sc_st, st_letter, ST_L('n')); cd_region_end(&rc3); }
}
EXPORT("t_state_c") const char *t_state_c(uint32_t step) {
  st_begin();
  if (step == 0) {
    if (st_show(&rc1)) { cd_watch(&sc_st, st_c1, 0); cd_region_end(&rc1); }
    if (st_show(&rc2)) { cd_watch(&sc_st, st_letter, ST_L('d')); cd_region_end(&rc2); }
  }
  if (step == 1 || step == 2) cd_changed(&sc_st);
  return st_end();
}

/* D: view x of sd shows a region whose build watches sd and sd2 (view y), and draws y; a later view z
 * changes sd2: y was made before that change, so it runs in the next round, once. At the next change of
 * sd, x and y both run once. */
static cd_state sd, sd2, sd3;
static cd_region rdx, rdy, rdz;
static uint32_t d_pre; /* x changes sd3 before it shows y, and y watches sd3: y drew after that change */
static void st_dy(void *) { stput('y'); }
static void st_dx(void *) {
  stput('x');
  if (d_pre) cd_changed(&sd3);
  rdy.up = &rdx;
  if (st_show(&rdy)) {
    cd_watch(&sd, st_dy, 0);
    cd_watch(&sd2, st_dy, 0);
    cd_watch(&sd3, st_dy, 0);
    cd_region_end(&rdy);
  }
}
static void st_dz(void *) { stput('z'); cd_changed(&sd2); }
EXPORT("t_state_d") const char *t_state_d(uint32_t step) {
  st_begin();
  if (step == 0) {
    if (st_show(&rdx)) { cd_watch(&sd, st_dx, 0); cd_region_end(&rdx); }
    if (st_show(&rdz)) { cd_watch(&sd, st_dz, 0); cd_region_end(&rdz); }
  }
  if (step == 1) cd_changed(&sd); /* x (makes y), z (changes sd2), then y */
  if (step == 2) { cd_region_hide(&rdz); cd_changed(&sd); } /* x (y is shown: no build), y */
  if (step == 3) { cd_region_hide(&rdy); d_pre = 1; cd_changed(&sd); } /* x (changes sd3, makes y): y is not run */
  return st_end();
}

/* E: a view that changes another state, and asks for a render; a render that changes a state. All in
 * one pass: e (changes se2, asks r1), then r1 (changes se3), then se2's and se3's views (f, g). */
static cd_state se1, se2, se3;
static cd_region re1, re2;
static void st_r1(void *) { stput('1'); cd_changed(&se3); }
static void st_r2(void *) { stput('r'); }
static void st_e(void *) { stput('e'); cd_changed(&se2); cd_dirty(st_r1, 0); }
EXPORT("t_state_e") const char *t_state_e(uint32_t step) {
  st_begin();
  if (step == 0) {
    if (st_show(&re1)) { cd_watch(&se1, st_e, 0); cd_region_end(&re1); }
    if (st_show(&re2)) { cd_watch(&se2, st_letter, ST_L('f')); cd_watch(&se3, st_letter, ST_L('g')); cd_region_end(&re2); }
  }
  if (step == 1) cd_changed(&se1);
  if (step == 2) { cd_dirty(st_r2, 0); cd_changed(&se1); } /* a round's views run before its renders: e r 1 f g */
  return st_end();
}

/* F: the tick. p (depth 1) watches sf1 and changes sf2; q (depth 2) watches sf1 and sf2 and runs after
 * that change: it runs once. P (depth 2) watches sf3 and changes sf4; Q (depth 1) watches sf3 and sf4
 * and ran before that change: it runs again, in the next round. */
static cd_state sf1, sf2, sf3, sf4;
static cd_region rfp, rfq, rfP, rfQ;
static void st_fp(void *) { stput('p'); cd_changed(&sf2); }
static void st_fP(void *) { stput('P'); cd_changed(&sf4); }
EXPORT("t_state_f") const char *t_state_f(uint32_t step) {
  st_begin();
  if (step == 0) {
    if (st_show(&rfp)) {
      cd_watch(&sf1, st_fp, 0);
      if (st_show(&rfq)) { cd_watch(&sf2, st_letter, ST_L('q')); cd_watch(&sf1, st_letter, ST_L('q')); cd_region_end(&rfq); }
      cd_region_end(&rfp);
    }
    if (st_show(&rfQ)) {
      cd_watch(&sf3, st_letter, ST_L('Q'));
      cd_watch(&sf4, st_letter, ST_L('Q'));
      if (st_show(&rfP)) { cd_watch(&sf3, st_fP, 0); cd_region_end(&rfP); }
      cd_region_end(&rfQ);
    }
  }
  if (step == 1) cd_changed(&sf1);
  if (step == 2) cd_changed(&sf3);
  return st_end();
}

/* G: outermost region first. The list (depth 1) watches the selection, and shows the detail (depth 2)
 * while an item is selected; the detail watches the items and shows the selected one. Deleting the
 * selected item clears the selection, in one entry; the items' change comes first, so the detail's view
 * is found first. Run first it would show an item that is gone (X); the list's view, run first, hides
 * the detail, whose view then never runs. */
static cd_state items_st, sel_st;
static uint32_t g_items[4] = {1, 1, 1, 1}, g_sel; /* g_items[k]: item k exists; g_sel: k + 1, or 0 */
static cd_region rg_list, rg_detail;
static void st_g_detail(void *) { stput(g_sel && g_items[g_sel - 1] ? 'D' : 'X'); }
static void st_g_list(void *) {
  stput('L');
  if (!g_sel) cd_region_hide(&rg_detail);
}
EXPORT("t_state_g") const char *t_state_g(uint32_t step) {
  st_begin();
  if (step == 0) {
    g_sel = 3;
    if (st_show(&rg_list)) {
      cd_watch(&sel_st, st_g_list, 0);
      if (st_show(&rg_detail)) { cd_watch(&items_st, st_g_detail, 0); cd_watch(&sel_st, st_g_detail, 0); cd_region_end(&rg_detail); }
      cd_region_end(&rg_list);
    }
  }
  if (step == 1) { g_items[2] = 0; cd_changed(&items_st); g_sel = 0; cd_changed(&sel_st); }
  return st_end();
}
/* G2: four views at depths 2, 1, 2, 1, found in that order (a then d from sg1, b then c from sg2):
 * they run d b a c, outermost first, and at one depth in the order found. */
static cd_state sg1, sg2;
static cd_region rg1, rg1k, rg2, rg2k;
EXPORT("t_state_g2") const char *t_state_g2(uint32_t step) {
  st_begin();
  if (step == 0) {
    if (st_show(&rg1)) {
      if (st_show(&rg1k)) { cd_watch(&sg1, st_letter, ST_L('a')); cd_region_end(&rg1k); }
      cd_watch(&sg1, st_letter, ST_L('d'));
      cd_watch(&sg2, st_letter, ST_L('b'));
      cd_region_end(&rg1);
    }
    if (st_show(&rg2)) {
      if (st_show(&rg2k)) { cd_watch(&sg2, st_letter, ST_L('c')); cd_region_end(&rg2k); }
      cd_region_end(&rg2);
    }
  }
  if (step == 1) { cd_changed(&sg1); cd_changed(&sg2); }
  return st_end();
}

/* H: a region hidden and shown again draws the value at its build, and follows later changes. */
static cd_state sh;
static uint32_t h_value;
static cd_region rh;
static cd_id h_txt;
static void st_h(void *) { stput('h'); cd_set_text_node_num(h_txt, h_value); }
EXPORT("t_state_h") const char *t_state_h(uint32_t op, uint32_t v) {
  st_begin();
  if (op == 0 && st_open(&rh)) {
    cd_attr("id", "st-h");
    cd_text(""); cd_name_text(h_txt = cd_id_new());
    cd_close();
    cd_watch(&sh, st_h, 0);
    st_h(0);
    cd_region_end(&rh);
  }
  if (op == 1) cd_region_hide(&rh);
  if (op == 2) { h_value = v; cd_changed(&sh); }
  return st_end();
}

/* I: one (fn, ctx) watched twice in one region, once on each of two states and once again on the first,
 * is one view; the same (fn, ctx) in a second region is a second view. Both states change: 2 runs. */
static cd_state si1, si2;
static cd_region ri1, ri2;
EXPORT("t_state_i") const char *t_state_i(uint32_t step) {
  st_begin();
  if (step == 0) {
    if (st_show(&ri1)) {
      cd_watch(&si1, st_letter, ST_L('i'));
      cd_watch(&si2, st_letter, ST_L('j')); /* another view between */
      cd_watch(&si2, st_letter, ST_L('i'));
      cd_watch(&si1, st_letter, ST_L('i'));
      cd_region_end(&ri1);
    }
    if (st_show(&ri2)) { cd_watch(&si2, st_letter, ST_L('i')); cd_region_end(&ri2); }
  }
  if (step == 1) { cd_changed(&si1); cd_changed(&si2); }
  return st_end();
}

/* I2: the same view watching the same state n times in one build holds one watch: 10,000 times grow no memory. */
static cd_region ri3;
EXPORT("t_state_i_dup") void t_state_i_dup(uint32_t n) {
  if (st_show(&ri3)) {
    for (uint32_t k = 0; k < n; k++) cd_watch(&si1, st_letter, ST_L('k'));
    cd_region_end(&ri3);
  }
  cd_region_hide(&ri3);
}

/* J: views that keep changing what they watch trap past 16 rounds (the trap page). */
static cd_state sj;
static cd_region rj;
static uint32_t j_runs;
static void st_j(void *) { j_runs++; cd_changed(&sj); }
EXPORT("t_state_loop") void t_state_loop(void) {
  if (st_show(&rj)) { cd_watch(&sj, st_j, 0); cd_region_end(&rj); }
  cd_changed(&sj);
  cd_flush();
}
EXPORT("t_state_loop_n") uint32_t t_state_loop_n(void) { return j_runs; }
/* On a fresh page (the trap page, 'no trap' cases): a state changed that no view ever watched; and a view
 * made by a render (cd_dirty, before any cd_changed in the page) runs at a later change of its state. */
static cd_state su, sv;
static cd_region rv;
static uint32_t v_runs;
EXPORT("t_state_unwatched") uint32_t t_state_unwatched(void) { cd_changed(&su); cd_flush(); return (uint32_t)su.ver; }
static void st_v(void *) { v_runs++; }
static void st_v_render(void *) {
  if (st_show(&rv)) { cd_watch(&sv, st_v, 0); cd_region_end(&rv); }
}
EXPORT("t_state_render_made") uint32_t t_state_render_made(void) {
  cd_dirty(st_v_render, 0);
  cd_flush();
  cd_changed(&sv);
  cd_flush();
  return v_runs;
}
/* cd_watch with no region building, and in a list row's create, trap (the trap page). */
static cd_state sk;
EXPORT("t_state_idle_watch") void t_state_idle_watch(void) { cd_watch(&sk, st_letter, 0); }
/* A cleanup that watches traps, though a region builds (one hidden while another builds). */
static cd_region rk_outer, rk_inner;
static void k_cleanup_watch(void *) { cd_watch(&sk, st_letter, 0); }
EXPORT("t_state_cleanup_watch") void t_state_cleanup_watch(void) {
  if (st_show(&rk_inner)) { cd_on_free(0, k_cleanup_watch, 0); cd_region_end(&rk_inner); }
  if (st_show(&rk_outer)) { cd_region_hide(&rk_inner); cd_region_end(&rk_outer); }
}
static cd_id k_create(void *, uint32_t, uint32_t *) {
  cd_id li = cd_open_id("li"); cd_close();
  cd_watch(&sk, st_letter, 0);
  return li;
}
static void k_patch(void *, uint32_t, cd_item *) {}
static void k_destroy(void *, cd_item *) {}
EXPORT("t_state_row_watch") void t_state_row_watch(void) {
  static const cd_list_fns fns = {0, k_create, k_patch, k_destroy};
  static cd_list kl;
  static const uint32_t key = 1;
  cd_at(CD_MOUNT, CD_APPEND);
  kl.parent = cd_open_id("ul"); cd_close();
  cd_list_update(&kl, &key, 1, &fns);
}

/* L: a keyed list whose rows each open a region in their create, and the region's view watches sl. One
 * entry changes sl and removes row 2: rows 1 and 3 run, row 2's view does not; the removed row's region
 * went with the row (its destroy forgets it). */
static cd_state sl;
static cd_list ll;
static cd_region l_rows[4];
static uint32_t l_keys[4];
static void st_l_row(void *ctx) { stput((char)('0' + CD_CTX_INDEX(ctx))); }
static cd_id l_create(void *, uint32_t i, uint32_t *) {
  uint32_t k = l_keys[i];
  cd_region *r = &l_rows[k];
  cd_id li = cd_open_id("li");
  if (cd_region_show(r, 1)) { /* the row's region: one <b> in the <li>, no outer region's child */
    cd_open("b"); cd_name_open(r->root); cd_close();
    cd_watch(&sl, st_l_row, CD_CTX_OF(k));
    cd_region_end(r);
  }
  cd_close();
  return li;
}
static void l_patch(void *, uint32_t, cd_item *) {}
static void l_destroy(void *, cd_item *it) { cd_region_forget(&l_rows[it->key]); }
static const cd_list_fns l_fns = {0, l_create, l_patch, l_destroy};
EXPORT("t_state_l") const char *t_state_l(uint32_t step) {
  st_begin();
  if (step == 0) {
    if (!st_sec) st_sec = section("state");
    cd_at(st_sec, CD_APPEND);
    ll.parent = cd_open_id("ol"); cd_close();
    l_keys[0] = 1; l_keys[1] = 2; l_keys[2] = 3;
    cd_list_update(&ll, l_keys, 3, &l_fns);
  }
  if (step == 1) {
    cd_changed(&sl);
    l_keys[1] = 3;
    cd_list_update(&ll, l_keys, 2, &l_fns); /* row 2 goes: its destroy forgets its region */
  }
  if (step == 2) cd_changed(&sl);
  return st_end();
}
EXPORT("t_state_l_rows") uint32_t t_state_l_rows(void) { return ll.n * 10 + l_rows[1].shown + l_rows[2].shown + l_rows[3].shown; }

/* W: a region opened in a keyed-list row's create is under what updates the list, with no r->up. The
 * page (depth 1) watches the items, and its view updates the list; each row's create opens a region
 * that watches the shared state and logs its key, or X when its item is gone. In the page's build, the
 * list's first rows (depth 2) and then the note n (depth 2, watching sw_note); from the entry, after
 * that build, m with m.up = the page (depth 2, watching the shared state). Deleting row 2 and changing
 * the shared state in one entry: the page's view removes row 2 first, so its view never runs. Row 4,
 * made by the page's view in a later entry, is depth 2 too. */
static cd_state sw_items, sw_shared, sw_note;
static cd_list wl;
static cd_region rw_page, rw_n, rw_m, rw_free, rw_rows[5];
static uint32_t w_live[5] = {0, 1, 1, 1, 0}, w_keys[4];
static void st_w_row(void *ctx) { uint32_t k = CD_CTX_INDEX(ctx); stput(w_live[k] ? (char)('0' + k) : 'X'); }
static cd_id w_create(void *, uint32_t i, uint32_t *) {
  uint32_t k = w_keys[i];
  cd_region *r = &rw_rows[k];
  cd_id li = cd_open_id("li");
  if (cd_region_show(r, 1)) { /* no r->up: the list's level is no region's */
    cd_open("b"); cd_name_open(r->root); cd_close();
    cd_watch(&sw_shared, st_w_row, CD_CTX_OF(k));
    cd_region_end(r);
  }
  cd_close();
  return li;
}
static void w_patch(void *, uint32_t, cd_item *) {}
static void w_destroy(void *, cd_item *it) { cd_region_forget(&rw_rows[it->key]); }
static const cd_list_fns w_fns = {0, w_create, w_patch, w_destroy};
static void w_update(void) {
  uint32_t n = 0;
  for (uint32_t k = 1; k < 5; k++) if (w_live[k]) w_keys[n++] = k;
  cd_list_update(&wl, w_keys, n, &w_fns);
}
static void st_w_list(void *) { stput('L'); w_update(); }
EXPORT("t_state_w") const char *t_state_w(uint32_t step) {
  st_begin();
  if (step == 0) {
    if (st_open(&rw_page)) {
      cd_watch(&sw_items, st_w_list, 0);
      wl.parent = cd_open_id("ol"); cd_close();
      w_update(); /* the first rows, in the page's build */
      if (st_show(&rw_n)) { cd_watch(&sw_note, st_letter, ST_L('n')); cd_region_end(&rw_n); }
      cd_close();
      cd_region_end(&rw_page);
    }
    rw_m.up = &rw_page; /* shown with no build open: its depth is its r->up's + 1 */
    if (st_show(&rw_m)) { cd_watch(&sw_shared, st_letter, ST_L('m')); cd_region_end(&rw_m); }
  }
  if (step == 1) { w_live[2] = 0; cd_changed(&sw_note); cd_changed(&sw_shared); cd_changed(&sw_items); }
  if (step == 2) { w_live[4] = 1; cd_changed(&sw_items); } /* row 4, made in the page's view */
  if (step == 3) { w_live[4] = 0; cd_changed(&sw_shared); cd_changed(&sw_items); }
  return st_end();
}
/* After the passes: a region shown with no build open, no view running and no r->up is at depth 1. */
EXPORT("t_state_w_free") uint32_t t_state_w_free(void) {
  uint32_t d = 0;
  if (st_show(&rw_free)) { d = rw_free.depth; cd_region_end(&rw_free); }
  cd_region_hide(&rw_free);
  return d * 100 + wl.n * 10 + rw_rows[2].shown + rw_rows[4].shown;
}

/* M: the reads run the pending views first: the view widens a box, and cd_get_num reads it. */
static cd_state sm;
static cd_region rm;
static cd_id m_box;
static uint32_t m_w = 50;
static void st_m(void *) { stput('m'); cd_set_style_num(m_box, "width", m_w, CD_UNIT_PX); }
EXPORT("t_state_m") const char *t_state_m(void) {
  st_begin();
  if (!rm.shown && st_open(&rm)) {
    cd_attr("style", "display: block; width: 50px; height: 1px");
    cd_close();
    m_box = rm.root;
    cd_watch(&sm, st_m, 0);
    cd_region_end(&rm);
  }
  m_w = 120;
  cd_changed(&sm);
  stput((char)('0' + (uint32_t)cd_get_num(m_box, "offsetWidth") / 40)); /* 120 / 40: 3 */
  return st_end();
}

/* N: memory. A region with three views on two states, shown, changed and hidden from inside the pass
 * (one view hides it: its views die in the pass and are freed at its end), then shown and hidden from
 * outside; round after round, the memory and the live ids stay as they were. */
static cd_state sn1, sn2;
static cd_region rn;
static uint32_t n_runs;
static void st_n_hide(void *) { n_runs++; cd_region_hide(&rn); }
static void st_n(void *) { n_runs++; }
static void n_show(void) {
  if (!st_show(&rn)) return;
  cd_watch(&sn1, st_n_hide, 0);
  cd_watch(&sn2, st_n_hide, 0);
  cd_watch(&sn1, st_n, CD_CTX_OF(1));
  cd_watch(&sn2, st_n, CD_CTX_OF(1));
  cd_watch(&sn1, st_n, CD_CTX_OF(2));
  cd_watch(&sn2, st_n, CD_CTX_OF(2));
  cd_region_end(&rn);
}
EXPORT("t_state_n") uint32_t t_state_n(uint32_t rounds) {
  for (uint32_t k = 0; k < rounds; k++) {
    n_show();
    cd_changed(&sn1);
    cd_changed(&sn2);
    cd_flush(); /* st_n_hide runs first and hides rn: the other two never run */
    n_show();
    cd_region_hide(&rn);
    cd_changed(&sn1);
    cd_flush();
  }
  return n_runs;
}
EXPORT("t_state_n_watched") uint32_t t_state_n_watched(void) { return sn1.first != 0 || sn2.first != 0; }

/* R: two states, each with a view of its own; one entry changes r1, r2, then r1 again: both views run,
 * in the order their states first changed; the next entry's change of r2 runs its view. */
static cd_state sr1, sr2;
static cd_region rr;
EXPORT("t_state_r") const char *t_state_r(uint32_t step) {
  st_begin();
  if (step == 0 && st_show(&rr)) { cd_watch(&sr1, st_letter, ST_L('1')); cd_watch(&sr2, st_letter, ST_L('2')); cd_region_end(&rr); }
  if (step == 1) { cd_changed(&sr1); cd_changed(&sr2); cd_changed(&sr1); }
  if (step == 2) cd_changed(&sr2);
  return st_end();
}

/* O: the version is 64 bits. */
static cd_state so = {.ver = 0xffffffffu};
EXPORT("t_state_o") uint64_t t_state_o(void) { cd_changed(&so); cd_flush(); return so.ver; }

/* P: cd_bind_prop and cd_bound_shows_bytes: an input's value bound; what the user typed is recorded
 * with shows_bytes, so setting the same bytes writes nothing. unit.html counts the shim's set calls. */
static cd_bound pv_b;
static cd_region rp;
static cd_id p_input;
EXPORT("t_state_p") void t_state_p(uint32_t step) {
  static char typed[16];
  if (step == 0 && st_open(&rp)) {
    p_input = cd_open_id("input"); cd_close();
    cd_close();
    cd_bind_prop(&pv_b, p_input, "value");
    cd_region_end(&rp);
  }
  if (step == 1) cd_bound_set_bytes(&pv_b, "abc", 3);       /* writes */
  if (step == 2) cd_bound_set_bytes(&pv_b, "abc", 3);       /* nothing */
  if (step == 3) {                                          /* the user typed: C reads it and records it */
    uint32_t n;
    if (cd_input_whole(p_input, typed, sizeof typed, &n)) cd_bound_shows_bytes(&pv_b, typed, n);
  }
  if (step == 4) cd_bound_set_bytes(&pv_b, typed, 5);       /* the bytes it shows: nothing */
  if (step == 5) cd_bound_set_bytes(&pv_b, "reset", 5);     /* other bytes: writes */
  if (step == 6) cd_bound_set_num(&pv_b, 42);               /* a number, as text: writes */
  if (step == 7) cd_region_hide(&rp);
  cd_flush();
}
EXPORT("t_state_p_live") uint32_t t_state_p_live(void) { return (uint32_t)cd_bound_live(&pv_b); }

/* Q: cd_dirty's table. 100 asks of one function with ctx 0..99, each asked three times, in one entry;
 * ctx 50 dropped, and ctx 60 dropped and asked again: 99 runs, each once, in the order first asked (60
 * last). The render of ctx 99 asks for ctx 0..9 again (they ran: they run again) and 100..139 (new, past
 * the table's growth). */
static uint8_t q_order[256];
static uint32_t q_n;
static void st_q(void *ctx) {
  uint32_t i = CD_CTX_INDEX(ctx);
  if (q_n < 256) q_order[q_n++] = (uint8_t)i;
  if (i == 99)
    for (uint32_t k = 0; k < 140; k++) if (k < 10 || k >= 100) cd_dirty(st_q, CD_CTX_OF(k));
}
EXPORT("t_state_q") uint32_t t_state_q(void) {
  q_n = 0;
  for (uint32_t r = 0; r < 3; r++)
    for (uint32_t i = 0; i < 100; i++) cd_dirty(st_q, CD_CTX_OF(i));
  cd_undirty(st_q, CD_CTX_OF(50));
  cd_undirty(st_q, CD_CTX_OF(60));
  cd_dirty(st_q, CD_CTX_OF(60));
  cd_flush();
  return q_n;
}
EXPORT("t_state_q_at") uint32_t t_state_q_at(uint32_t k) { return q_order[k]; }

/* The check build's cases (unit_check.wasm, -DCD_STATE_CHECK; the trap page). A getter calls cd_read. */
static cd_state sx1, sx2;
static uint32_t x_bad;
static cd_region rx1, rx2, rx3, rx4;
static void x_view(void *) { cd_read(&sx1); if (x_bad) cd_read(&sx2); }
static void x_other(void *) { cd_read(&sx2); }
EXPORT("t_check_build") uint32_t t_check_build(void) {
#ifdef CD_STATE_CHECK
  return 1;
#else
  return 0;
#endif
}
/* Views that read what they watch, and a build that reads before it watches, do not trap. */
EXPORT("t_check_view_ok") uint32_t t_check_view_ok(void) {
  if (st_show(&rx1)) {
    cd_read(&sx1); /* before the watch: the build's reads are checked at its end */
    cd_watch(&sx1, x_view, 0);
    cd_watch(&sx2, x_other, 0);
    cd_region_end(&rx1);
  }
  cd_changed(&sx1);
  cd_changed(&sx2);
  cd_read(&sx2); /* no view runs and no region builds: not checked */
  cd_flush();
  return 1;
}
/* A view that reads a state it does not watch traps. */
EXPORT("t_check_view_bad") void t_check_view_bad(void) { t_check_view_ok(); x_bad = 1; cd_changed(&sx1); cd_flush(); }
/* A build reading a state its region does not watch, but the region building under it does, and its
 * r->up does: no trap. A region opened in a list row's create, inside that build, is no region's child
 * (its r->up is 0), and reads what the region building under it watches: no trap. A region shown by a
 * view, whose build reads what the view's region watches, through its r->up: no trap. */
static cd_region rx_row;
static cd_id x_row_create(void *, uint32_t, uint32_t *) {
  cd_id li = cd_open_id("li");
  if (cd_region_show(&rx_row, 1)) {
    cd_open("b"); cd_name_open(rx_row.root); cd_close();
    cd_read(&sx1);
    cd_region_end(&rx_row);
  }
  cd_close();
  return li;
}
static void x_row_patch(void *, uint32_t, cd_item *) {}
static void x_row_destroy(void *, cd_item *) { cd_region_forget(&rx_row); }
static const cd_list_fns x_row_fns = {0, x_row_create, x_row_patch, x_row_destroy};
static void x_shows(void *) {
  rx4.up = &rx1;
  if (st_show(&rx4)) {
    cd_read(&sx1);  /* watched by its r->up */
    cd_watch(&sx2, x_other, 0);
    cd_read(&sx2);  /* watched by itself, not by the view running: a build's read, not the view's */
    cd_region_end(&rx4);
  }
}
EXPORT("t_check_build_ok") uint32_t t_check_build_ok(void) {
  if (st_show(&rx1)) {
    cd_watch(&sx1, x_shows, 0);
    if (st_show(&rx2)) { cd_read(&sx1); cd_region_end(&rx2); }
    static cd_list xl;
    static const uint32_t key = 1;
    cd_at(rx1.root, CD_APPEND);
    xl.parent = cd_open_id("ul"); cd_close();
    cd_list_update(&xl, &key, 1, &x_row_fns);
    cd_region_end(&rx1);
  }
  rx3.up = &rx1;
  if (st_show(&rx3)) { cd_read(&sx1); cd_region_end(&rx3); }
  cd_changed(&sx1);
  cd_flush();
  return rx4.shown;
}
/* A build that reads a state nothing around it watches traps at its end. */
EXPORT("t_check_build_bad") void t_check_build_bad(void) {
  if (st_show(&rx1)) {
    cd_watch(&sx1, x_view, 0);
    if (st_show(&rx2)) { cd_read(&sx2); cd_region_end(&rx2); }
    cd_region_end(&rx1);
  }
}
