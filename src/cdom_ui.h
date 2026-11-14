/* cdom_ui: the shorthand cdom apps otherwise write for themselves: elements with a class, a button,
 * a modal dialog, event handler macros and a small string builder. Link src/cdom_ui.c to use it
 * (APP_MODULES ui). It is plain C over cdom.h, with no import of its own: an app that does not link
 * it pays nothing, and one that does pays only for the calls it makes (the linker drops the rest).
 * Nothing here holds state of its own: what lives on (a dialog, a buffer) is the caller's struct. */
#ifndef CDOM_UI_H
#define CDOM_UI_H
#include "cdom.h"

/* ---- elements ---- */
void cd_el(cd_str tag, cd_str cls);                /* cd_open(tag), then cd_class(cls) when cls */
cd_id cd_el_id(cd_str tag, cd_str cls);            /* the same, named with a new id (cd_open_id) */
void cd_el_text(cd_str tag, cd_str cls, cd_str s); /* an element holding one text, closed */

/* A <button type="button"> (type="button": in a form it submits nothing), with class cls (0: none)
 * and the text label, built where the next node goes and closed; a click calls fn(e, ctx) (cd_on).
 * label_text, when not 0, receives the id of the label's text node, to change it later
 * (cd_set_text_node, or a cd_bound). Returns the button's id. */
cd_id cd_button(cd_str cls, cd_str label, cd_event_fn fn, void *ctx, cd_id *label_text);

/* ---- handlers ---- */
/* A cd_on handler, defined static; either argument may go unused. */
#define CD_ON_EVENT(name) \
  static void name(const cd_event_rec *e __attribute__((unused)), void *ctx __attribute__((unused)))
/* A small index carried in a ctx (a row, a slot), and back: no allocation, no pointer to keep alive. */
#define CD_CTX_OF(i) ((void *)(uintptr_t)(i))
#define CD_CTX_INDEX(ctx) ((uint32_t)(uintptr_t)(ctx))

/* ---- a modal dialog ----
 * A <dialog> opened with showModal: the browser makes the rest of the page inert and keeps the focus
 * inside it, closes it on Escape, and when it closes gives the focus back to what had it before. The
 * dialog is a region of its own (a child of the region building, if any), built at the end of parent:
 * build(ctx, dialog) fills it; class cls (0: none). Every way it closes (Escape, cd_dialog_close, a
 * form method="dialog" inside, the page calling close()) ends in on_close(ctx), after the dialog and
 * everything built in it are gone. On_close comes from the event loop, never inside cd_dialog_open
 * or cd_dialog_close. One dialog per cd_dialog at a time: opening one that is shown does nothing and
 * answers 0. on_close may be 0. The struct is the caller's, zeroed before first use, and must stay
 * where it is while the dialog is shown. */
typedef void (*cd_dialog_build_fn)(void *ctx, cd_id dialog);
typedef void (*cd_dialog_close_fn)(void *ctx);
typedef struct { cd_region r; cd_dialog_close_fn on_close; void *ctx; } cd_dialog;
int cd_dialog_open(cd_dialog *d, cd_id parent, cd_str cls, cd_dialog_build_fn build, cd_dialog_close_fn on_close,
                   void *ctx);
void cd_dialog_close(cd_dialog *d);
static inline int cd_dialog_shown(const cd_dialog *d) { return d->r.shown; }
static inline cd_id cd_dialog_node(const cd_dialog *d) { return d->r.root; } /* 0 when not shown */

/* ---- a string builder over the caller's buffer ----
 *   char room[64]; cd_buf b = cd_buf_in(room, sizeof room);
 *   cd_buf_putz(&b, "Line "); cd_buf_u64(&b, line); ... cd_text(cd_tmp(b.p, b.len));
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
