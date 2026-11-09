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

#define EXPORT(n) __attribute__((export_name(n)))
#define L(s) CD_LIT(s)

/* When the module started (the store's early open is checked against it). */
static double started_at;
void cd_main(void) { started_at = cd_now(); }
EXPORT("t_started") double t_started(void) { return started_at; }

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
  cd_open(L("i")); cd_text(L("waiting")); cd_name_text(timer_txt = cd_id_new()); cd_close();
  cd_open(L("i")); cd_text(L("waiting")); cd_name_text(raf_txt = cd_id_new()); cd_close();
  cd_timer_start(5, 41);
  cd_raf_request(42);
  cd_flush();
}
static uint32_t app_timer_heard[10], cdom_token_heard; /* tokens 50 to 59 (section 27); any in cdom's range */
void cd_on_timer(uint32_t token) {
  enter();
  if (token >= CD_TOKEN_CDOM) cdom_token_heard++;
  if (token >= 50 && token < 60) { app_timer_heard[token - 50]++; return; }
  cd_set_text_num(timer_txt, token);
  cd_intern("timer-flushed", 13);
  hold(timer_txt);
}
static double raf_t;
EXPORT("t_raf_t") double t_raf_t(void) { return raf_t; }
EXPORT("t_now") double t_now(void) { return cd_now(); }
void cd_on_raf(uint32_t token, double t) {
  enter();
  raf_t = t;
  cd_set_text_num(raf_txt, t > 0 ? token : 0);
  hold(raf_txt);
}

/* 5. Window events through the event path; the viewport read from C. */
static cd_id vw_txt;
static int32_t resized_w = -1, resized_h = -1, hidden = -1;
EXPORT("t_window") void t_window(void) {
  cd_id s = section("window");
  cd_at(s, 0);
  cd_open(L("i")); cd_text_num(0); cd_name_text(vw_txt = cd_id_new()); cd_close();
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
  cd_at(s, 0);
  cd_open(L("div"));
  cd_attr(L("style"), L("position: fixed; left: 10px; top: 20px; width: 30px; height: 40px;"));
  cd_name_open(id);
  cd_close();
  cd_rect(id, rect_at);
  cd_set_style(id, L("transform"), L("scale(2)"));
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
  cd_at(s, 0);
  cd_open(L("div"));
  cd_attr(L("style"), L("position: fixed; left: 100px; top: 100px; width: 100px; height: 50px;"));
  cd_listen(input_box = cd_id_new(), CD_EV_MOUSEDOWN);
  cd_listen(input_box, CD_EV_TOUCHSTART);
  cd_open(L("span")); cd_text(L("inside")); cd_close();
  cd_close();
  cd_open(L("div")); /* over the box, and seen through */
  cd_attr(L("style"), L("position: fixed; left: 90px; top: 90px; width: 200px; height: 80px; pointer-events: none;"));
  cd_close();
  cd_open(L("div"));
  cd_attr(L("style"), L("position: fixed; left: 300px; top: 100px; width: 40px; height: 40px;"));
  cd_listen(input_other = cd_id_new(), CD_EV_CLICK);
  cd_close();
  cd_open(L("div")); /* over input_other's left half, taking pointer events; it listened, and listens no more */
  cd_attr(L("style"), L("position: fixed; left: 300px; top: 100px; width: 20px; height: 40px;"));
  cd_listen(input_layer = cd_id_new(), CD_EV_CLICK);
  cd_close();
  cd_unlisten(input_layer, CD_EV_CLICK);
  cd_open(L("input")); cd_close();
  cd_id freed = cd_id_new(); /* listened, then its id freed with the node left in place: no longer a target */
  cd_open(L("div"));
  cd_attr(L("style"), L("position: fixed; left: 400px; top: 100px; width: 40px; height: 40px;"));
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
    cd_set_text_num(vw_txt, (uint32_t)e->x); /* no cd_flush: the event export does it */
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
        cd_open(L("i")); cd_name_open(cd_id_new()); cd_text(L("x")); cd_name_text(cd_id_new()); cd_close();
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

/* 10b. cd_name_el names the element opened last though a text came after it (in its parent), and
 * cd_name_text the text node made last (by cd_text, then by cd_text_num) though an element was opened
 * and closed after it: the two cases where the last node made is the other kind. */
EXPORT("t_name_kinds") void t_name_kinds(void) {
  cd_id s = section("namekinds"), el = cd_id_new(), txt = cd_id_new(), num = cd_id_new();
  cd_at(s, 0);
  cd_open(L("div"));
  cd_open(L("b")); cd_close();
  cd_text(L("after the b"));
  cd_name_el(el);
  cd_text(L("text"));
  cd_open(L("i")); cd_close();
  cd_name_text(txt);
  cd_text_num(7);
  cd_open(L("u")); cd_close();
  cd_name_text(num);
  cd_close();
  cd_set_class(el, L("named"));
  cd_set_text(txt, L("renamed"));
  cd_set_text_num(num, 8);
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

/* Pixels as a CSS image: encoded to a 3 x 2 PNG, whose bytes come back and go on a div's background. */
static cd_id imaged;
static uint8_t pix[3 * 2 * 4];
EXPORT("t_image") void t_image(void) {
  cd_id s = section("image");
  cd_at(s, 0);
  cd_open(L("div")); cd_name_open(imaged = cd_id_new()); cd_close();
  for (uint32_t i = 0; i < 6; i++) { pix[i * 4] = (uint8_t)(40 * i); pix[i * 4 + 1] = (uint8_t)(255 - 30 * i); pix[i * 4 + 2] = (uint8_t)(7 * i); pix[i * 4 + 3] = 255; }
  cd_encode_image(pix, 3, 2, 4, L("image/png"), 77);
  __builtin_memset(pix, 0, sizeof pix); /* the shim copied them during the call */
}
/* The same pixels as RGB, at (1, 1) in a 5 x 4 PNG: its bytes kept for the page to decode. */
static uint8_t rgb_png[512];
static uint32_t rgb_len;
EXPORT("t_image_rgb") void t_image_rgb(void) {
  uint8_t rgb[3 * 2 * 3];
  for (uint32_t i = 0; i < 6; i++) { rgb[i * 3] = (uint8_t)(40 * i); rgb[i * 3 + 1] = (uint8_t)(255 - 30 * i); rgb[i * 3 + 2] = (uint8_t)(7 * i); }
  cd_encode_image_in(rgb, 3, 2, 3, 5, 4, 1, 1, L("image/png"), 78);
}
EXPORT("t_rgb_png") uint8_t *t_rgb_png(void) { return rgb_png; }
EXPORT("t_rgb_len") uint32_t t_rgb_len(void) { return rgb_len; }
void cd_on_encoded(uint32_t token, const uint8_t *p, uint32_t len) {
  enter();
  if (token == 78) { rgb_len = len < sizeof rgb_png ? len : 0; __builtin_memcpy(rgb_png, p, rgb_len); return; }
  loaded_key = (int32_t)token;
  loaded_id = (int32_t)len;
  /* a PNG starts 0x89 'P' 'N' 'G' */
  if (len > 4 && p[0] == 0x89 && p[1] == 'P') cd_set_image_bytes(imaged, L("background-image"), p, len, L("image/png"), 79);
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
  cd_at(s, 0);
  cd_open(L("div")); cd_name_open(unimaged = cd_id_new()); cd_close();
  cd_set_image_bytes(unimaged, L("background-image"), junk, sizeof junk, L("image/png"), 80);
}
void cd_on_image(uint32_t token, uint32_t ok) {
  enter();
  if (token == 79 || token == 80) image_answer[token - 79] = (int32_t)ok;
  if (token == 79) cd_set_style(imaged, L("--landed"), L("1"));
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
 * an HTTP POST answered reversed. No cd_flush: the network exports flush. The message is sent
 * from a buffer C overwrites as soon as the send returns: the send must have read it by then. */
static const uint8_t MSG[] = {0, 1, 2, 3, 0x7f, 0x80, 0xfe, 0xff};
static uint8_t out_msg[sizeof MSG];
static cd_id net_txt;
static int ws_ok;
static uint32_t net_port;
static void say(const char *s) { cd_set_text(net_txt, cd_internz(s)); }
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
  cd_at(s, 0);
  cd_open(L("i")); cd_text(L("waiting")); cd_name_text(net_txt = cd_id_new()); cd_close();
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
  (void)id; (void)code;
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
  cd_at(s, 0);
  cd_open(L("i")); cd_text(L("audio")); cd_name_text(audio_txt = cd_id_new()); cd_close();
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
  cd_at(s, 0);
  cd_open(L("i")); cd_text(L("url")); cd_name_text(url_txt = cd_id_new()); cd_close();
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
  cd_at(s, 0);
  cd_open(L("i")); cd_text(L("prefs")); cd_name_text(prefs_txt = cd_id_new()); cd_close();
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
  cd_at(s, 0);
  cd_open(L("i")); cd_text(L("share")); cd_name_text(share_txt = cd_id_new()); cd_close();
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
  cd_at(s, 0);
  cd_open(L("i")); cd_text(L("fonts")); cd_name_text(fonts_txt = cd_id_new()); cd_close();
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
  cd_at(s, 0);
  cd_open(L("input")); cd_attr(L("type"), L("text")); cd_name_el(host_input = cd_id_new()); cd_close();
  cd_open(L("input")); cd_attr(L("type"), L("checkbox")); cd_name_el(host_box = cd_id_new()); cd_close();
  cd_open(L("button")); cd_text(L("b")); cd_name_open(host_button = cd_id_new()); cd_close();
  cd_open(L("div")); cd_name_el(host_div = cd_id_new()); cd_close();
}
EXPORT("t_host_set") void t_host_set(void) {
  cd_set_str(host_input, L("value"), L("한국어 값"));
  cd_set_bool(host_box, L("checked"), 1);
  cd_set_bool(host_button, L("disabled"), 1);
  cd_set_num(host_input, L("maxLength"), 12);
  cd_set_str(host_div, L("style.--accent"), L("rgb(1, 2, 3)"));
  cd_set_str(host_div, L("dataset.mark"), L("set"));
}
EXPORT("t_host_enable") void t_host_enable(void) { cd_set_bool(host_button, L("disabled"), 0); }
EXPORT("t_host_focus") void t_host_focus(void) { cd_call(host_input, L("focus")); }
EXPORT("t_host_blur") void t_host_blur(void) { cd_call(host_input, L("blur")); }
EXPORT("t_host_checked") double t_host_checked(void) { return cd_get_num(host_box, L("checked")); }
EXPORT("t_host_length") double t_host_length(void) { return cd_get_num(host_input, L("value.length")); }
EXPORT("t_host_missing") double t_host_missing(void) { return cd_get_num(host_input, L("no.such.thing")); }
EXPORT("t_host_value") uint32_t t_host_value(uint32_t cap) { return cd_get_str(host_input, L("value"), host_out, cap); }
EXPORT("t_host_accent") uint32_t t_host_accent(void) { return cd_get_str(host_div, L("style.--accent"), host_out, sizeof host_out); }
EXPORT("t_host_out") char *t_host_out(void) { return host_out; }
EXPORT("t_host_bad_set") void t_host_bad_set(void) { cd_set_str(host_div, L("no.such.thing"), L("x")); }

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
  cd_at(def_sec, 0);
  cd_open(L("input")); cd_attr(L("class"), L("reuse"));
  cd_listen(def_reuse = cd_id_new(), CD_EV_KEYDOWN); cd_listen(def_reuse, CD_EV_FOCUSOUT); cd_close();
  cd_open(L("input")); cd_attr(L("class"), L("gone"));
  cd_listen(def_gone = cd_id_new(), CD_EV_KEYDOWN); cd_listen(def_gone, CD_EV_FOCUSOUT); cd_close();
  cd_open(L("input")); cd_attr(L("class"), L("alive"));
  cd_listen(def_alive = cd_id_new(), CD_EV_KEYDOWN); cd_listen(def_alive, CD_EV_FOCUSOUT); cd_close();
  cd_open(L("input")); cd_attr(L("class"), L("other"));
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
    cd_at(def_sec, 0);
    cd_open(L("input")); cd_attr(L("class"), L("new")); cd_listen(def_new, CD_EV_FOCUSOUT); cd_close();
  } else if (e->type == CD_EV_KEYDOWN && id == def_gone) {
    cd_remove(def_gone);
  } else if (e->type == CD_EV_KEYDOWN && id == def_alive) {
    cd_call(def_other, L("focus"));
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
  cd_at(s, 0);
  cd_open(L("dialog"));
  dialog_node = cd_id_new();
  cd_listen(dialog_node, CD_EV_CANCEL);
  cd_listen(dialog_node, CD_EV_CLOSE);
  cd_open(L("button")); cd_text(L("ok")); cd_close();
  cd_close();
  return dialog_node;
}
EXPORT("t_dialog_show") void t_dialog_show(void) { cd_call(dialog_node, L("showModal")); }
EXPORT("t_dialog_close") void t_dialog_close(void) { cd_call(dialog_node, L("close")); }
EXPORT("t_ev_close") int32_t t_ev_close(void) { return CD_EV_CLOSE; }

/* 25. cd_open_id: an element opened and named in one call, the id naming it after children were built
 * inside; inside a region, the id is the region's and goes with it. */
static cd_id open_outer, open_inner;
static cd_region open_region;
static uint32_t open_live[3];
EXPORT("t_open_id") void t_open_id(void) {
  cd_id s = section("openid");
  cd_at(s, 0);
  open_outer = cd_open_id(L("ul"));
  open_inner = cd_open_id(L("li")); cd_text(L("a")); cd_close();
  cd_open(L("li")); cd_text(L("b")); cd_close();
  cd_close();
  cd_set_class(open_outer, L("outer"));
  cd_set_class(open_inner, L("inner"));
  open_live[0] = cd_ids_live();
  if (cd_region_show(&open_region, 1)) {
    cd_at(s, 0);
    cd_open(L("p")); cd_name_open(open_region.root);
    cd_id in = cd_open_id(L("b")); cd_text(L("c")); cd_close();
    cd_close();
    cd_region_end(&open_region);
    cd_set_class(in, L("in-region"));
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
  cd_at(s, 0);
  for (int k = 0; k < 3; k++) { on_btn[k] = cd_open_id(L("button")); cd_text_num((uint32_t)k); cd_close(); }
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
static void on_count(const cd_event_rec *e, void *ctx) { (void)e; enter(); ++*(uint32_t *)ctx; }
EXPORT("t_on_many") void t_on_many(void) {
  cd_id s = section("onmany");
  cd_at(s, 0);
  on_many_box = cd_open_id(L("div"));
  for (int i = 0; i < ON_MANY; i++) {
    on_many_id[i] = cd_open_id(L("span"));
    cd_on(on_many_id[i], CD_EV_INPUT, on_count, &on_many_calls[i]);
    cd_on(on_many_id[i], CD_EV_CLICK, on_count, &on_many_calls[i]);
    cd_close();
  }
  cd_close();
}
EXPORT("t_on_many_free") uint32_t t_on_many_free(void) {
  for (int i = 0; i < ON_MANY; i += 2) cd_id_free(on_many_id[i]);
  uint32_t same = 0;
  cd_at(on_many_box, 0);
  for (int i = 0; i < ON_MANY / 2; i++) {
    cd_open(L("b"));
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
static void slot_probe_fire(void *ctx) { (void)ctx; }
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
static void wcy_cb(void *ctx, uint32_t id, uint32_t ev, const uint8_t *p, uint32_t len) {
  enter();
  (void)p; (void)len;
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
static void layout_heard(const cd_event_rec *e, void *ctx) {
  (void)ctx;
  __builtin_memcpy(&layout_rec, e, sizeof layout_rec);
}
EXPORT("t_layout") cd_id t_layout(void) {
  cd_id s = section("layout");
  cd_at(s, 0);
  cd_open(L("div")); cd_name_open(layout_box = cd_id_new());
  cd_open(L("i")); cd_close();
  cd_open(L("p"));
  cd_open(L("b")); cd_close(); cd_open(L("b")); cd_close();
  cd_open(L("b")); cd_text(L("target")); cd_close();
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
  const cd_val v[] = {cd_v_num(2.5), cd_v_str(L("two")), cd_v_bool(1), cd_v_id(layout_box), cd_v_null(),
                      cd_v_obj(2), cd_v_str(L("a")), cd_v_num(-1.25), cd_v_str(L("b")), cd_v_bool(0),
                      cd_v_num(1e300)};
  cd_call_args(CD_WINDOW, L("layoutVals"), 7, v);
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
static void rf_cb(void *ctx, uint32_t id, uint32_t ev, const uint8_t *p, uint32_t len) {
  enter();
  (void)p;
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
