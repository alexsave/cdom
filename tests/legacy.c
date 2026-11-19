/* The fallback for browsers without WebAssembly, run on what the keyed benchmark does not touch: pointer
 * events (from PEP), text in and out of C beyond ASCII (the TextDecoder and TextEncoder polyfills), a failed
 * call reported (reportError), focus events dispatched during a batch (queueMicrotask) and listener phases.
 * tests/legacy.html boots it and tools/legacy.mjs drives it, with LEGACY=1 or without. */
#include "../src/cdom.h"
#include "../src/cdom_file.h"

#define EXPORT(n) __attribute__((export_name(n)))
#define IMPORT(n) __attribute__((import_module("env"), import_name(n)))
/* Every delivered event: its type, the listening id and four record fields. */
IMPORT("ev") void ev(int32_t type, int32_t id, int32_t x, int32_t y, int32_t pointer, int32_t buttons);

enum { PAD, TEXT, ECHO, ERR, FOCUS, FILE, NIDS };
static cd_id ids[NIDS];
static char io[256];

static cd_id el(const char *tag, const char *idattr, const char *style) {
  cd_open(tag);
  cd_attr("id", idattr);
  if (style) cd_attr("style", style);
  cd_id id = cd_id_new();
  cd_name_el(id);
  return id;
}

void cd_main(void) {
  ids[PAD] = el("div", "pad", "position:fixed;left:10px;top:10px;width:200px;height:100px;background:#888");
  cd_close();
  ids[TEXT] = el("input", "text", "position:fixed;left:10px;top:150px;width:200px"); cd_close();
  el("p", "echo", "position:fixed;left:10px;top:200px");
  cd_text(""); ids[ECHO] = cd_id_new(); cd_name_text(ids[ECHO]); /* the echo is the paragraph's text node */
  cd_close();
  ids[ERR] = el("button", "err", "position:fixed;left:10px;top:250px"); cd_text("err"); cd_close();
  ids[FOCUS] = el("button", "focus", "position:fixed;left:100px;top:250px"); cd_text("focus"); cd_close();
  ids[FILE] = el("input", "file", "position:fixed;left:10px;top:300px"); cd_attr("type", "file"); cd_close();
  static const uint32_t pad_evs[] = {CD_EV_POINTERDOWN, CD_EV_POINTERMOVE, CD_EV_POINTERUP, CD_EV_CLICK};
  for (uint32_t i = 0; i < sizeof pad_evs / 4; i++) { cd_sel(ids[PAD]); cd_listen(ids[PAD], pad_evs[i]); }
  cd_sel(ids[TEXT]); cd_listen(ids[TEXT], CD_EV_INPUT);
  cd_sel(ids[TEXT]); cd_listen(ids[TEXT], CD_EV_FOCUS);
  cd_sel(ids[ERR]); cd_listen(ids[ERR], CD_EV_CLICK);
  cd_sel(ids[FOCUS]); cd_listen(ids[FOCUS], CD_EV_CLICK);
}

void cd_on_event(const cd_event_rec *e) {
  cd_id id = (cd_id)e->id;
  ev(e->type, e->id, e->x, e->y, e->pointer, e->buttons);
  if (e->type == CD_EV_INPUT && id == ids[TEXT]) {
    /* The value comes in through encodeInto and goes back out through the decoder, as the echo's text. */
    uint32_t n;
    if (!cd_input_whole(id, io, sizeof io, &n)) n = cd_get_str(id, "value", io, sizeof io);
    cd_set_text_node(ids[ECHO], cd_tmp(io, n));
  } else if (e->type == CD_EV_CLICK && id == ids[ERR]) {
    /* A method that does not exist: reported, and the text set after it still applies. */
    cd_call_args(CD_BODY, "noSuchMethod", 0, 0);
    cd_set_attr(ids[ERR], "data-after", "yes");
  } else if (e->type == CD_EV_CLICK && id == ids[FOCUS]) {
    /* focus() fires focus while the batch applies; the shim queues it for after the batch. */
    cd_call(ids[TEXT], "focus");
  }
}

/* Memory grows, then a new string crosses: the shim must see its views are stale. On wasm2js the old buffer
 * keeps its length (a grow copies into a bigger ArrayBuffer), so the shim cannot test for length 0 there. */
EXPORT("grow") void grow(void) {
  uint8_t *big = cd_alloc(4u << 20);
  big[0] = 1;
  cd_set_text_node(ids[ECHO], cd_tmpz("read after the memory grew \xc3\xa9"));
  cd_flush();
}
/* Strings the macro cannot see, which are ids of their own address (cd_tmpz): shown and shown again. */
EXPORT("ro") void ro(uint32_t i) { cd_set_text_node(ids[ECHO], i ? "ro-one \xc3\xa9" : "ro-zero"); cd_flush(); }
EXPORT("id") cd_id id_of(uint32_t k) { return k < NIDS ? ids[k] : CD_DETACHED; }
/* The input's value read into cap bytes: encodeInto must stop before a character that does not fit
 * (cut: the bytes written), and cd_input_whole says the whole length however much fits (whole). */
EXPORT("cut") uint32_t cut(uint32_t cap) { return cd_get_str(ids[TEXT], "value", io, cap < sizeof io ? cap : sizeof io); }
EXPORT("whole") uint32_t whole(uint32_t cap) { /* 1 << 16 when it fitted, and the length */
  uint32_t n, fits = (uint32_t)cd_input_whole(ids[TEXT], io, cap < sizeof io ? cap : sizeof io, &n);
  return fits << 16 | n;
}

/* Picked files on the old tiers (cdom_file.h): IE 10 and 11 have FileReader, IE 9 does not; tools/legacy.mjs
 * takes it away. seen(0..2): the read's status and length, and whether every byte was i + 1. */
static int32_t file_seen[3] = {-1, -1, -1};
static void on_read(void *, uint32_t status, const uint8_t *p, uint32_t len) {
  int32_t same = p != 0;
  for (uint32_t i = 0; p && i < len; i++) same &= p[i] == (uint8_t)(i + 1);
  file_seen[0] = (int32_t)status, file_seen[1] = (int32_t)len, file_seen[2] = same;
}
EXPORT("file_ask") void file_ask(void) {
  for (int k = 0; k < 3; k++) file_seen[k] = -1;
  cd_file_read_cb(ids[FILE], 0, 100, on_read, nullptr);
}
EXPORT("file_seen") int32_t file_seen_k(uint32_t k) { return file_seen[k]; }

/* The image module's js way out of a libjpeg error (src/cdom_image_sjlj.c) on this tier too, wasm2js included: a body
 * that throws from 20 frames down answers 1, 500 times in one call, the stack pointer where it was after; a body that
 * returns answers 0. */
#include "../src/cdom_image_int.h"
static void *lg_slot;
static __attribute__((noinline)) void lg_deep(uint32_t n) { volatile char pad[200]; pad[0] = (char)n; if (n) lg_deep(n - 1); else cd_image_throw(lg_slot); }
static void lg_throws(void *ctx) { lg_deep(*(uint32_t *)ctx); }
static void lg_returns(void *ctx) { (*(uint32_t *)ctx)++; }
static __attribute__((noinline)) uintptr_t lg_sp(void) { volatile char probe = 0; uintptr_t at = (uintptr_t)&probe; return at + probe; }
EXPORT("sj") uint32_t sj(void) {
  uintptr_t before = lg_sp();
  uint32_t n = 20, all = 1, k = 0;
  for (int i = 0; i < 500; i++) all &= (uint32_t)cd_image_guard(lg_throws, &n, &lg_slot);
  uint32_t r = (uint32_t)cd_image_guard(lg_returns, &k, &lg_slot);
  return all && r == 0 && k == 1 && lg_sp() == before;
}
