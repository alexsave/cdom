/* The fallback for browsers without WebAssembly, run on what the keyed benchmark does not touch: pointer
 * events (from PEP), text in and out of C beyond ASCII (the TextDecoder and TextEncoder polyfills), a failed
 * call reported (reportError), focus events dispatched during a batch (queueMicrotask) and listener phases;
 * and, for the third tier (C's memory in plain words, no typed arrays), every other way the shim reads or writes
 * that memory: a wheel event's floats, a rect's doubles, the viewport, a number set on the page, pixels, and
 * the seed. tests/legacy.html boots it and tools/legacy.mjs drives it, with LEGACY=1, ie9 or none. */
#include "../src/cdom.h"

#define EXPORT(n) __attribute__((export_name(n)))
#define IMPORT(n) __attribute__((import_module("env"), import_name(n)))
/* Every delivered event: its type, the listening id and four record fields. */
IMPORT("ev") void ev(int32_t type, int32_t id, int32_t x, int32_t y, int32_t pointer, int32_t buttons);

enum { PAD, TEXT, ECHO, ERR, FOCUS, CANVAS, NIDS };
static cd_id ids[NIDS];
static char io[256];

static cd_str z(const char *s) { return cd_internz(s); }

static cd_id el(const char *tag, const char *idattr, const char *style) {
  cd_open(z(tag));
  cd_attr(z("id"), z(idattr));
  if (style) cd_attr(z("style"), z(style));
  cd_id id = cd_id_new();
  cd_name(id);
  return id;
}

void cd_main(void) {
  cd_at(CD_MOUNT, 0);
  ids[PAD] = el("div", "pad", "position:fixed;left:10px;top:10px;width:200px;height:100px;background:#888");
  cd_close();
  ids[TEXT] = el("input", "text", "position:fixed;left:10px;top:150px;width:200px"); cd_close();
  el("p", "echo", "position:fixed;left:10px;top:200px");
  cd_text(z("")); ids[ECHO] = cd_id_new(); cd_name(ids[ECHO]); /* the echo is the paragraph's text node */
  cd_close();
  ids[ERR] = el("button", "err", "position:fixed;left:10px;top:250px"); cd_text(z("err")); cd_close();
  ids[FOCUS] = el("button", "focus", "position:fixed;left:100px;top:250px"); cd_text(z("focus")); cd_close();
  ids[CANVAS] = el("canvas", "cv", "position:fixed;left:300px;top:10px");
  cd_attr(z("width"), z("2")); cd_attr(z("height"), z("2"));
  cd_close();
  static const uint32_t pad_evs[] = {CD_EV_POINTERDOWN, CD_EV_POINTERMOVE, CD_EV_POINTERUP, CD_EV_CLICK, CD_EV_WHEEL};
  for (uint32_t i = 0; i < sizeof pad_evs / 4; i++) { cd_sel(ids[PAD]); cd_listen(ids[PAD], pad_evs[i]); }
  cd_sel(ids[TEXT]); cd_listen(ids[TEXT], CD_EV_INPUT);
  cd_sel(ids[TEXT]); cd_listen(ids[TEXT], CD_EV_FOCUS);
  cd_sel(ids[ERR]); cd_listen(ids[ERR], CD_EV_CLICK);
  cd_sel(ids[FOCUS]); cd_listen(ids[FOCUS], CD_EV_CLICK);
}

static float wheel_dx, wheel_dy;

void cd_on_event(const cd_event_rec *e) {
  cd_id id = (cd_id)e->id;
  ev(e->type, e->id, e->x, e->y, e->pointer, e->buttons);
  if (e->type == CD_EV_WHEEL) { wheel_dx = e->dx; wheel_dy = e->dy; }
  if (e->type == CD_EV_INPUT && id == ids[TEXT]) {
    /* The value comes in through encodeInto and goes back out through the decoder, as the echo's text. */
    uint32_t n = cd_input_value(id, io, sizeof io);
    cd_set_text(ids[ECHO], cd_intern(io, n));
  } else if (e->type == CD_EV_CLICK && id == ids[ERR]) {
    /* A method that does not exist: reported, and the text set after it still applies. */
    cd_call_args(CD_BODY, z("noSuchMethod"), 0, 0);
    cd_set_attr(ids[ERR], z("data-after"), z("yes"));
  } else if (e->type == CD_EV_CLICK && id == ids[FOCUS]) {
    /* focus() fires focus while the batch applies; the shim queues it for after the batch. */
    cd_call(ids[TEXT], z("focus"));
  }
}

EXPORT("id") cd_id id_of(uint32_t k) { return k < NIDS ? ids[k] : CD_DETACHED; }
/* The input's value read into cap bytes: encodeInto must stop before a character that does not fit. */
EXPORT("cut") uint32_t cut(uint32_t cap) { return cd_input_value(ids[TEXT], io, cap < sizeof io ? cap : sizeof io); }
/* The last wheel event's deltaX (0) or deltaY (1), as the record's floats carried them. */
EXPORT("wheel") double wheel(uint32_t k) { return k ? wheel_dy : wheel_dx; }
/* The pad's box, field k of cd_rect's four doubles; -1 when cd_rect says it has none. */
EXPORT("rect") double rect(uint32_t k) {
  double r[4];
  return cd_rect(ids[PAD], r) && k < 4 ? r[k] : -1;
}
/* cd_viewport's field k: width, height, hidden, device pixel ratio times 100. */
EXPORT("viewport") int32_t viewport(uint32_t k) {
  cd_viewport_rec v;
  cd_viewport(&v);
  return k == 0 ? v.w : k == 1 ? v.h : k == 2 ? v.hidden : v.dpr100;
}
/* A number through a cd_val: window.cdomNum. */
EXPORT("setnum") void setnum(void) { cd_set_num(CD_WINDOW, z("cdomNum"), -1234.5678); }
/* Four pixels onto the canvas: red, green, blue and a half-transparent white. */
EXPORT("blit") void blit(void) {
  static const uint8_t px[16] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 128};
  cd_blit(ids[CANVAS], px, 2, 2);
}
EXPORT("seed") uint32_t seed(void) { return cd_seed; }
