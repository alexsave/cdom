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
  cd_open(L("i")); cd_text_num(0); cd_name(vw_txt = cd_id_new()); cd_close();
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
static cd_id input_box, input_other;
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
  cd_open(L("div")); /* over input_other's left half, taking pointer events but listening nowhere */
  cd_attr(L("style"), L("position: fixed; left: 300px; top: 100px; width: 20px; height: 40px;"));
  cd_close();
  cd_open(L("input")); cd_close();
  cd_id freed = cd_id_new(); /* listened, then its id freed with the node left in place: no longer a target */
  cd_open(L("div"));
  cd_attr(L("style"), L("position: fixed; left: 400px; top: 100px; width: 40px; height: 40px;"));
  cd_listen(freed, CD_EV_CLICK);
  cd_close();
  cd_id_free(freed);
}
EXPORT("t_input_id") int32_t t_input_id(int32_t which) { return (int32_t)(which ? input_other : input_box); }
EXPORT("t_listen_win") void t_listen_win(int32_t type, int32_t on) {
  if (on) cd_listen_window((uint32_t)type);
  else cd_unlisten_window((uint32_t)type);
}
EXPORT("t_prevent") void t_prevent(int32_t on) { prevent_next = on; }
/* The last event's type, id, x, y, mods, key, ch, how many came (k = 7), and its time (k = 8). */
EXPORT("t_ev") int32_t t_ev(int32_t k) { return k == 7 ? ev_count : ev_rec[k]; }
EXPORT("t_hit") int32_t t_hit(int32_t x, int32_t y) { return (int32_t)cd_hit(x, y); }

static int32_t loaded_id = -1, loaded_key = -1;
void cd_on_event(const cd_event_rec *e) {
  enter();
  if (e->type == CD_EV_RESIZE && e->id == CD_MOUNT) {
    resized_w = e->x; resized_h = e->y;
    cd_set_text_num(vw_txt, (uint32_t)e->x); /* no cd_flush: the event export does it */
    hold(vw_txt);
  } else if (e->type == CD_EV_VISIBILITY) hidden = e->key;
  else {
    const int32_t rec[9] = {e->type, e->id, e->x, e->y, e->mods, e->key, e->ch, 0, e->ms};
    for (int k = 0; k < 9; k++) ev_rec[k] = rec[k];
    ev_count++;
    if (prevent_next) cd_prevent_default();
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
void cd_on_store_put(uint32_t token, uint32_t ok) { enter(); put_ok[token & 3] = (int32_t)ok; }
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
void cd_on_store_del(uint32_t token, uint32_t ok) { enter(); del_ok[token & 3] = (int32_t)ok; }
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
enum { WS_ECHO, WS_CYCLE, WS_BIG };
static int ws_mode;
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
  cd_open(L("i")); cd_text(L("waiting")); cd_name(net_txt = cd_id_new()); cd_close();
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

void cd_on_ws_open(uint32_t id) {
  enter();
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
  (void)id; (void)code;
  if (ws_mode == WS_ECHO) echo_closed = 1;
  if (ws_mode != WS_CYCLE) return;
  cycles_done++;
  if (--cycles_left) ws_open_here();
}
void cd_on_ws_msg(uint32_t id, const uint8_t *p, uint32_t len) {
  enter();
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

void cd_on_http(uint32_t token, uint32_t status, const uint8_t *p, uint32_t len) {
  enter();
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
  cd_open(L("i")); cd_text(L("audio")); cd_name(audio_txt = cd_id_new()); cd_close();
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
  cd_open(L("i")); cd_text(L("url")); cd_name(url_txt = cd_id_new()); cd_close();
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
  cd_open(L("i")); cd_text(L("prefs")); cd_name(prefs_txt = cd_id_new()); cd_close();
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
  cd_open(L("i")); cd_text(L("share")); cd_name(share_txt = cd_id_new()); cd_close();
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
 * a load was heard done. */
static int32_t fonts_status[16];
static uint32_t fonts_answers[16], fonts_changed;
static cd_id fonts_txt;
EXPORT("t_fonts_section") void t_fonts_section(void) {
  cd_id s = section("fonts");
  cd_at(s, 0);
  cd_open(L("i")); cd_text(L("fonts")); cd_name(fonts_txt = cd_id_new()); cd_close();
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
  hold(fonts_txt);
}
EXPORT("t_fonts_status") int32_t t_fonts_status(uint32_t token) { return fonts_status[token & 15]; }
EXPORT("t_fonts_answers") uint32_t t_fonts_answers(uint32_t token) { return fonts_answers[token & 15]; }
EXPORT("t_fonts_changed") uint32_t t_fonts_changed(void) { return fonts_changed; }
