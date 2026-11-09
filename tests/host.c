/* Exercises host access through the real shim: reserved ids, cd_get_* on dotted paths, SET, CALLA, the new
 * event types and the reserved-id listeners, prevent and stop flags, and events dispatched during a batch.
 * host.html probes paths through the exports below; tools/host.mjs drives real input and checks the log. */
#include "../src/cdom.h"

#define EXPORT(n) __attribute__((export_name(n)))
#define IMPORT(n) __attribute__((import_module("env"), import_name(n)))
/* Every delivered event is logged with two numbers the handler read; str hands over a string it read. */
IMPORT("ev") void ev(int32_t type, int32_t id, double a, double b);
IMPORT("str") void str(const char *p, uint32_t len);

enum { BOX, OTHER, TEXT, CHECK, FILE_IN, MEDIA, GOOD, BAD, SCROLLER, DEEP, PARA, FSBTN, ALL, ALL_CHILD,
       DBL_ONLY, LIST, ITEM_A, ITEM_B, FOCUSER, NIDS };
static cd_id ids[NIDS];
static char io[16384];
static double rect[4];
static cd_str s_fs_el, s_inner_w, s_inner_h, s_delta_y, s_key, s_vis, s_dur, s_scroll_top, s_drop_name,
    s_focused, s_capture;

static cd_str z(const char *s) { return cd_internz(s); }

static cd_id el(const char *tag, const char *idattr, const char *style) {
  cd_open(z(tag));
  cd_attr(z("id"), z(idattr));
  if (style) cd_attr(z("style"), z(style));
  cd_id id = cd_id_new();
  cd_name_el(id);
  return id;
}

/* A 1x1 transparent GIF. */
static const char *GIF = "data:image/gif;base64,R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7";

void cd_main(void) {
  cd_at(CD_MOUNT, 0);
  ids[BOX] = el("div", "box", "position:fixed;left:10px;top:10px;width:100px;height:100px;background:#888");
  cd_close();
  ids[OTHER] = el("div", "other", "position:fixed;left:300px;top:10px;width:100px;height:100px;background:#aaa");
  cd_close();
  ids[TEXT] = el("input", "text", "position:fixed;left:10px;top:150px;width:200px"); cd_close();
  ids[CHECK] = el("input", "check", "position:fixed;left:250px;top:150px");
  cd_attr(z("type"), z("checkbox")); cd_close();
  ids[FILE_IN] = el("input", "file", 0); cd_attr(z("type"), z("file")); cd_close();
  ids[MEDIA] = el("audio", "media", 0); cd_close();
  ids[GOOD] = el("img", "good", 0); cd_close();
  ids[BAD] = el("img", "bad", 0); cd_close();
  ids[SCROLLER] = el("div", "scroller", "position:fixed;left:450px;top:10px;width:100px;height:50px;overflow:auto");
  cd_open(z("div")); cd_attr(z("style"), z("height:500px")); cd_close();
  ids[DEEP] = el("div", "deep", "height:10px"); cd_close();
  cd_open(z("div")); cd_attr(z("style"), z("height:500px")); cd_close();
  cd_close();
  ids[PARA] = el("p", "para", 0); cd_text(z("a")); cd_text(z("b")); cd_text(z("c")); cd_close();
  ids[FSBTN] = el("button", "fs", "position:fixed;left:10px;top:200px;width:80px;height:30px");
  cd_text(z("fs")); cd_close();
  ids[ALL] = el("div", "all", 0);
  ids[ALL_CHILD] = el("span", "allchild", 0); cd_close();
  cd_close();
  ids[DBL_ONLY] = el("div", "dblonly", 0); cd_close();
  ids[LIST] = el("ul", "list", 0);
  ids[ITEM_A] = el("li", "a", 0); cd_close();
  ids[ITEM_B] = el("li", "b", 0); cd_close();
  cd_close();
  ids[FOCUSER] = el("button", "focuser", 0); cd_text(z("focus the text")); cd_close();

  /* Listen: the box for pointer capture, the menu and the wheel; the media and images through capture. */
  static const uint32_t box_evs[] = {CD_EV_POINTERDOWN, CD_EV_POINTERMOVE, CD_EV_POINTERUP, CD_EV_POINTEROVER,
                                     CD_EV_CONTEXTMENU, CD_EV_WHEEL, CD_EV_CLICK, CD_EV_DROP};
  for (uint32_t i = 0; i < sizeof box_evs / 4; i++) { cd_sel(ids[BOX]); cd_listen(ids[BOX], box_evs[i]); }
  cd_sel(ids[OTHER]); cd_listen(ids[OTHER], CD_EV_POINTERMOVE);
  cd_sel(ids[OTHER]); cd_listen(ids[OTHER], CD_EV_POINTERUP);
  cd_sel(ids[TEXT]); cd_listen(ids[TEXT], CD_EV_FOCUSIN);
  static const uint32_t media_evs[] = {CD_EV_LOADEDMETADATA, CD_EV_TIMEUPDATE, CD_EV_ENDED, CD_EV_PLAY,
                                       CD_EV_PAUSE};
  for (uint32_t i = 0; i < sizeof media_evs / 4; i++) { cd_sel(ids[MEDIA]); cd_listen(ids[MEDIA], media_evs[i]); }
  cd_sel(ids[GOOD]); cd_listen(ids[GOOD], CD_EV_LOAD);
  cd_sel(ids[BAD]); cd_listen(ids[BAD], CD_EV_ERROR);
  cd_sel(ids[SCROLLER]); cd_listen(ids[SCROLLER], CD_EV_SCROLL);
  cd_sel(ids[FSBTN]); cd_listen(ids[FSBTN], CD_EV_CLICK);
  for (uint32_t t = 0; t <= CD_EV_PAUSE; t++) {
    if (t == CD_EV_RESIZE || t == CD_EV_VISIBILITY) continue; /* the window's and the document's */
    cd_sel(ids[ALL]); cd_listen(ids[ALL], t);
  }
  cd_sel(ids[DBL_ONLY]); cd_listen(ids[DBL_ONLY], CD_EV_DBLCLICK);
  cd_sel(ids[FOCUSER]); cd_listen(ids[FOCUSER], CD_EV_CLICK);
  /* Host objects: no sel needed, the last node is not touched. */
  cd_listen(CD_WINDOW, CD_EV_RESIZE);
  cd_listen(CD_WINDOW, CD_EV_FOCUS);
  cd_listen(CD_WINDOW, CD_EV_BLUR);
  cd_listen(CD_DOCUMENT, CD_EV_FULLSCREENCHANGE);
  cd_listen(CD_DOCUMENT, CD_EV_VISIBILITY);
  cd_listen(CD_DOCUMENT, CD_EV_KEYDOWN);
  cd_listen(CD_DOCUMENT, CD_EV_KEYUP);
  cd_listen(CD_BODY, CD_EV_CLICK);
  cd_listen(CD_WINDOW, CD_EV_RESIZE); /* twice must not double the listener */

  /* Images load and fail after the batch, so their listeners are already in place. */
  cd_set_attr(ids[GOOD], z("src"), z(GIF));
  cd_set_attr(ids[BAD], z("src"), z("data:image/png;base64,AAAA"));

  s_fs_el = z("fullscreenElement"); s_inner_w = z("innerWidth"); s_inner_h = z("innerHeight");
  s_delta_y = z("deltaY"); s_key = z("key"); s_vis = z("visibilityState"); s_dur = z("duration");
  s_scroll_top = z("scrollTop"); s_drop_name = z("dataTransfer.files.0.name"); s_focused = z("dataset.focused");
  s_capture = z("setPointerCapture");
}

static void pass_str(cd_id id, cd_str path) { str(io, cd_get_str(id, path, io, sizeof io)); }

void cd_on_event(const cd_event_rec *e) {
  double a = 0, b = 0;
  cd_id id = (cd_id)e->id;
  switch (e->type) {
  case CD_EV_RESIZE: a = cd_get_num(CD_WINDOW, s_inner_w); b = cd_get_num(CD_WINDOW, s_inner_h); break;
  case CD_EV_FULLSCREENCHANGE: {
    cd_id f = cd_get_id(CD_DOCUMENT, s_fs_el);
    a = f == CD_DETACHED ? -1.0 : (double)f;
    if (f != CD_DETACHED) cd_call(CD_DOCUMENT, z("exitFullscreen"));
    break;
  }
  case CD_EV_VISIBILITY: pass_str(CD_DOCUMENT, s_vis); break;
  case CD_EV_KEYDOWN: case CD_EV_KEYUP: a = e->key; pass_str(CD_EVENT, s_key); break;
  case CD_EV_CONTEXTMENU: if (id == ids[BOX]) cd_prevent_default(); break;
  case CD_EV_WHEEL:
    a = e->dy; b = cd_get_num(CD_EVENT, s_delta_y);
    if (id == ids[BOX]) cd_prevent_default();
    break;
  case CD_EV_POINTERDOWN:
    a = e->pointer; b = e->buttons;
    if (id == ids[BOX]) { cd_val p = cd_v_num(e->pointer); cd_call_args(id, s_capture, 1, &p); }
    break;
  case CD_EV_POINTERMOVE: case CD_EV_POINTERUP: a = e->x; b = e->buttons; break;
  case CD_EV_LOADEDMETADATA: if (id == ids[MEDIA]) a = cd_get_num(id, s_dur); break;
  case CD_EV_SCROLL: if (id == ids[SCROLLER]) a = cd_get_num(id, s_scroll_top); break;
  case CD_EV_DROP: if (id == ids[BOX]) { pass_str(CD_EVENT, s_drop_name); cd_prevent_default(); } break;
  case CD_EV_FOCUSIN: if (id == ids[TEXT]) cd_set_str(id, s_focused, z("yes")); break;
  case CD_EV_CLICK:
    if (id == ids[FSBTN]) {
      cd_val opt[3] = {cd_v_obj(1), cd_v_str(z("navigationUI")), cd_v_str(z("hide"))};
      cd_call_args(id, z("requestFullscreen"), 1, opt);
    }
    if (id == ids[ALL]) cd_stop_propagation(); /* the body listener must not hear it */
    /* focus() fires focusin while this handler runs: delivered then, it would reset this click's flags
     * and overwrite the record that e points to, so the log below would read focusin's type */
    if (id == ids[FOCUSER]) { cd_prevent_default(); cd_call(ids[TEXT], z("focus")); }
    break;
  }
  if (id == ids[ALL]) { a = e->path; b = e->depth; }
  ev(e->type, (int32_t)id, a, b);
}

/* ---- probes: host.html writes a path into io and names an id by index ---- */
EXPORT("io") char *io_ptr(void) { return io; }
EXPORT("rect") double *rect_ptr(void) { return rect; }
EXPORT("id") cd_id id_of(uint32_t k) { return k < NIDS ? ids[k] : CD_DETACHED; }
static cd_str P(uint32_t len) { return len ? cd_intern(io, len) : 0; }
EXPORT("g_num") double g_num(cd_id id, uint32_t len) { return cd_get_num(id, P(len)); }
EXPORT("g_str") uint32_t g_str(cd_id id, uint32_t len) { return cd_get_str(id, P(len), io, sizeof io); }
EXPORT("g_strcap") uint32_t g_strcap(cd_id id, uint32_t len, uint32_t cap) { return cd_get_str(id, P(len), io, cap); }
EXPORT("g_id") cd_id g_id(cd_id id, uint32_t len) { return cd_get_id(id, P(len)); }
EXPORT("g_rect") int g_rect(cd_id id) { return cd_rect(id, rect); }
EXPORT("g_value") uint32_t g_value(cd_id id, uint32_t cap) { return cd_input_text(id, io, cap); }
/* Sets take the path at io[0, len) and a string value at io[len, len + vlen). */
EXPORT("s_num") void s_num(cd_id id, uint32_t len, double v) { cd_set_num(id, P(len), v); cd_flush(); }
EXPORT("s_bool") void s_bool(cd_id id, uint32_t len, int v) { cd_set_bool(id, P(len), v); cd_flush(); }
EXPORT("s_null") void s_null(cd_id id, uint32_t len) { cd_set(id, P(len), cd_v_null()); cd_flush(); }
EXPORT("s_str") void s_str(cd_id id, uint32_t len, uint32_t vlen) {
  cd_set_str(id, P(len), cd_intern(io + len, vlen));
  cd_flush();
}

/* ---- calls ---- */
EXPORT("c_select") void c_select(void) { /* number, number, string */
  cd_val a[3] = {cd_v_num(1), cd_v_num(3), cd_v_str(z("backward"))};
  cd_call_args(ids[TEXT], z("setSelectionRange"), 3, a);
  cd_flush();
}
EXPORT("c_attr") void c_attr(void) { /* a string and a number */
  cd_val a[2] = {cd_v_str(z("data-n")), cd_v_num(42.5)};
  cd_call_args(ids[BOX], z("setAttribute"), 2, a);
  cd_flush();
}
EXPORT("c_toggle") void c_toggle(int on) { /* a method on a path, with a string and a boolean */
  cd_val a[2] = {cd_v_str(z("on")), cd_v_bool(on)};
  cd_call_args(ids[BOX], z("classList.toggle"), 2, a);
  cd_flush();
}
EXPORT("c_flag") void c_flag(int on) { cd_set_bool(ids[BOX], z("dataset.flag"), on); cd_flush(); }
EXPORT("c_insert") void c_insert(void) { /* a node and null: move item a after item b */
  cd_val a[2] = {cd_v_id(ids[ITEM_A]), cd_v_null()};
  cd_call_args(ids[LIST], z("insertBefore"), 2, a);
  cd_flush();
}
EXPORT("c_contains") double c_contains(void) { /* a result read back through CD_RESULT */
  cd_val a = cd_v_id(ids[BOX]);
  cd_call_args(CD_BODY, z("contains"), 1, &a);
  return cd_get_num(CD_RESULT, 0);
}
EXPORT("c_getattr") uint32_t c_getattr(void) {
  cd_val a = cd_v_str(z("id"));
  cd_call_args(ids[BOX], z("getAttribute"), 1, &a);
  return cd_get_str(CD_RESULT, 0, io, sizeof io);
}
/* CD_RESULT holds a named node; cd_get_id still names it by its own id. */
EXPORT("c_query") cd_id c_query(void) {
  cd_val a = cd_v_str(z("#b"));
  cd_call_args(ids[LIST], z("querySelector"), 1, &a);
  return cd_get_id(ids[LIST], z("firstElementChild"));
}
EXPORT("c_scroll") void c_scroll(void) { /* an object argument */
  cd_val a[3] = {cd_v_obj(1), cd_v_str(z("block")), cd_v_str(z("end"))};
  cd_call_args(ids[DEEP], z("scrollIntoView"), 1, a);
  cd_flush();
}
/* A call that throws and a method that does not exist are reported; the set after them still applies. */
EXPORT("c_bad") void c_bad(void) {
  cd_val p = cd_v_num(9999);
  cd_call_args(ids[BOX], s_capture, 1, &p);
  cd_call_args(ids[BOX], z("noSuchMethod"), 0, 0);
  cd_set_str(CD_DOCUMENT, z("nowhere.at.all"), z("x"));
  cd_set_str(ids[BOX], z("dataset.after"), z("bad"));
  cd_flush();
}
EXPORT("c_play") void c_play(void) {
  cd_set_bool(ids[MEDIA], z("muted"), 1);
  cd_call_args(ids[MEDIA], z("play"), 0, 0);
  cd_flush();
}
/* focus() dispatches focus events while the batch is being applied. The p before it must be created once,
 * and the handler's set must still land. */
EXPORT("c_focus") void c_focus(void) {
  cd_at(CD_MOUNT, 0);
  cd_open(z("p")); cd_class(z("mark")); cd_close();
  cd_call_args(ids[TEXT], z("focus"), 0, 0);
  cd_set_str(ids[TEXT], z("dataset.after"), z("yes"));
  cd_flush();
}
/* A read sees ops emitted before it in the same batch, including a path never sent before. */
EXPORT("c_rw") uint32_t c_rw(void) {
  cd_set_str(ids[BOX], z("title"), z("fresh title"));
  return cd_get_str(ids[BOX], z("title"), io, sizeof io);
}
/* Ids: a thousand new ones never hit the reserved range, and freeing a reserved id does nothing. */
EXPORT("c_ids") uint32_t c_ids(void) {
  cd_id_free(CD_WINDOW);
  cd_remove(CD_DOCUMENT);
  cd_id_free(CD_BODY);
  cd_flush();
  static cd_id got[1000];
  uint32_t bad = 0;
  for (uint32_t i = 0; i < 1000; i++) if ((got[i] = cd_id_new()) < CD_FIRST_ID) bad++;
  for (uint32_t i = 0; i < 1000; i++) cd_id_free(got[i]);
  for (uint32_t i = 0; i < 1000; i++) if (cd_id_new() < CD_FIRST_ID) bad++;
  cd_flush();
  return bad;
}
/* A one-off path (cd_tmp) reuses its id from one crossing to the next, so the shim must not keep its
 * segments by id: the second read names a different property under the same id. Returns the two lengths
 * read, the second's bytes left in io. */
EXPORT("c_tmp_paths") uint32_t c_tmp_paths(void) {
  uint32_t a = cd_get_str(ids[BOX], cd_tmpz("id"), io, sizeof io);
  uint32_t b = cd_get_str(ids[BOX], cd_tmpz("tagName"), io, sizeof io);
  return a << 16 | b;
}
/* cd_unlisten on a node: it stops hearing dblclick, and starts again when listened for anew. */
EXPORT("c_unlisten") void c_unlisten(int on) {
  if (on) { cd_sel(ids[DBL_ONLY]); cd_listen(ids[DBL_ONLY], CD_EV_DBLCLICK); }
  else cd_unlisten(ids[DBL_ONLY], CD_EV_DBLCLICK);
  cd_flush();
}
