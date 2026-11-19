/* cdom_ui: the shorthand cdom apps otherwise write for themselves: elements with a class, a button,
 * event handler macros and a small string builder. Link src/cdom_ui.c to use it
 * (APP_MODULES ui). It is plain C over cdom.h, with no import of its own: an app that does not link
 * it pays nothing, and one that does pays only for the calls it makes (the linker drops the rest).
 * Nothing here holds state of its own: what lives on (a buffer) is the caller's. */
#ifndef CDOM_UI_H
#define CDOM_UI_H
#include "cdom.h"

/* ---- elements ----
 * Strings are plain C, as in cdom.h: each of these is also a macro that passes its strings through
 * cd_s(). cls CD_NONE is no class. */
void cd_el(cd_str tag, cd_str cls);                /* cd_open(tag), then cd_class(cls) unless CD_NONE */
cd_id cd_el_id(cd_str tag, cd_str cls);            /* the same, named with a new id (cd_open_id) */

/* A <button type="button"> (type="button": in a form it submits nothing), with class cls (CD_NONE: none)
 * and the text label, built where the next node goes and closed; a click calls fn(e, ctx) (cd_on).
 * label_text, when not 0, receives the id of the label's text node, to change it later
 * (cd_set_text_node, or a cd_bound). Returns the button's id. */
cd_id cd_button(cd_str cls, cd_str label, cd_event_fn fn, void *ctx, cd_id *label_text);
#define cd_el(tag, cls) (cd_el)(cd_s(tag), cd_s(cls))
#define cd_el_id(tag, cls) (cd_el_id)(cd_s(tag), cd_s(cls))
/* An element holding one text, closed: cd_el(tag, cls); cd_text(s); cd_close(). A macro, not a function, so
 * that s is made after the element and class calls: a runtime string (cd_tmp) is good only for the next call
 * that takes a string (cdom.h, strings), and a function would have it made before those calls. s may be any
 * string, and is written once, in its place. A statement, not an expression. */
#define cd_el_text(tag, cls, s) \
  do { \
    cd_el(tag, cls); \
    cd_text(s); \
    cd_close(); \
  } while (0)
#define cd_button(cls, label, fn, ctx, label_text) (cd_button)(cd_s(cls), cd_s(label), fn, ctx, label_text)

/* ---- handlers ---- */
/* A cd_on handler, defined static; either argument may go unused. */
#define CD_ON_EVENT(name) \
  static void name(const cd_event_rec *e __attribute__((unused)), void *ctx __attribute__((unused)))
/* A small index carried in a ctx (a row, a slot), and back: no allocation, no pointer to keep alive. */
#define CD_CTX_OF(i) ((void *)(uintptr_t)(i))
#define CD_CTX_INDEX(ctx) ((uint32_t)(uintptr_t)(ctx))

/* ---- a string builder over the caller's buffer ----
 *   char room[64]; cd_buf b = cd_buf_in(room, sizeof room);
 *   cd_buf_putz(&b, "Line "); cd_buf_u64(&b, line); ... cd_text(cd_tmp(b.p, b.len));
 * The bytes stay in C memory, and cd_tmp hands them to the one call that reads them (cdom.h, strings).
 * Each put writes the whole piece or, when it does not fit in what is left, none of it, and sets over;
 * after that every put writes nothing, so b.p always holds whole pieces, the first ones. Numbers in
 * decimal, as many digits as they take (u64 at most 20, i64 at most 20 with its sign). */
typedef struct { char *p; uint32_t len, cap; int over; } cd_buf;
static inline cd_buf cd_buf_in(char *p, uint32_t cap) { cd_buf b = {p, 0, cap, 0}; return b; }
void cd_buf_put(cd_buf *b, const char *s, uint32_t n);
void cd_buf_putz(cd_buf *b, const char *s);
void cd_buf_u64(cd_buf *b, uint64_t v);
void cd_buf_i64(cd_buf *b, int64_t v);
#endif
