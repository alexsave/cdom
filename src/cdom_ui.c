/* cdom_ui: the shorthand of cdom_ui.h, over cdom.h alone. Separate from cdom.c so an app that does not
 * use it does not link it. */
#include "cdom_ui.h"

void cd_el(cd_str tag, cd_str cls) {
  cd_open(tag);
  if (cls) cd_class(cls);
}
cd_id cd_el_id(cd_str tag, cd_str cls) {
  cd_id id = cd_open_id(tag);
  if (cls) cd_class(cls);
  return id;
}
void cd_el_text(cd_str tag, cd_str cls, cd_str s) {
  cd_el(tag, cls);
  cd_text(s);
  cd_close();
}

cd_id cd_button(cd_str cls, cd_str label, cd_event_fn fn, void *ctx, cd_id *label_text) {
  cd_id b = cd_el_id(CD_LIT("button"), cls);
  cd_attr(CD_LIT("type"), CD_LIT("button"));
  cd_text(label);
  if (label_text) cd_name_text(*label_text = cd_id_new());
  cd_close();
  cd_on(b, CD_EV_CLICK, fn, ctx);
  return b;
}

/* Every way out ends here: the dialog goes (its region, so what was built in it), then on_close. */
static void dialog_closed(const cd_event_rec *e, void *ctx) {
  (void)e;
  cd_dialog *d = ctx;
  cd_dialog_close_fn fn = d->on_close;
  void *c = d->ctx;
  cd_region_hide(&d->r);
  if (fn) fn(c);
}
int cd_dialog_open(cd_dialog *d, cd_id parent, cd_str cls, cd_dialog_build_fn build, cd_dialog_close_fn on_close,
                   void *ctx) {
  if (d->r.shown) return 0;
  d->r.parent = 0; /* one element, the <dialog>: taking it away is one remove */
  d->on_close = on_close;
  d->ctx = ctx;
  cd_region_show(&d->r, 1);
  cd_at(parent, 0);
  cd_open(CD_LIT("dialog"));
  cd_name_open(d->r.root);
  if (cls) cd_class(cls);
  if (build) build(ctx, d->r.root);
  cd_close();
  cd_region_end(&d->r);
  cd_on(d->r.root, CD_EV_CLOSE, dialog_closed, d);
  cd_call(d->r.root, CD_LIT("showModal"));
  return 1;
}
void cd_dialog_close(cd_dialog *d) {
  if (d->r.shown) cd_call(d->r.root, CD_LIT("close"));
}

void cd_buf_put(cd_buf *b, const char *s, uint32_t n) {
  if (b->over || n > b->cap - b->len) { b->over = 1; return; }
  __builtin_memcpy(b->p + b->len, s, n);
  b->len += n;
}
void cd_buf_putz(cd_buf *b, const char *s) {
  uint32_t n = 0;
  while (s[n]) n++;
  cd_buf_put(b, s, n);
}
void cd_buf_u64(cd_buf *b, uint64_t v) {
  char t[20];
  uint32_t k = sizeof t;
  do t[--k] = (char)('0' + v % 10); while ((v /= 10));
  cd_buf_put(b, t + k, sizeof t - k);
}
void cd_buf_i64(cd_buf *b, int64_t v) {
  char t[21];
  uint64_t u = v < 0 ? 0 - (uint64_t)v : (uint64_t)v;
  uint32_t k = sizeof t;
  do t[--k] = (char)('0' + u % 10); while ((u /= 10));
  if (v < 0) t[--k] = '-';
  cd_buf_put(b, t + k, sizeof t - k);
}
