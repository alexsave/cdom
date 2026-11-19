/* cdom_ui: the shorthand of cdom_ui.h, over cdom.h alone. Separate from cdom.c so an app that does not
 * use it does not link it. The names in parentheses are the functions, not cdom_ui.h's macros. */
#include "cdom_ui.h"

void (cd_el)(cd_str tag, cd_str cls) {
  cd_open(tag);
  if (cls.v) cd_class(cls);
}
cd_id (cd_el_id)(cd_str tag, cd_str cls) {
  cd_id id = cd_open_id(tag);
  if (cls.v) cd_class(cls);
  return id;
}

cd_id (cd_button)(cd_str cls, cd_str label, cd_event_fn fn, void *ctx, cd_id *label_text) {
  cd_id b = cd_el_id("button", cls);
  cd_attr("type", "button");
  cd_text(label);
  if (label_text) cd_name_text(*label_text = cd_id_new());
  cd_close();
  cd_on(b, CD_EV_CLICK, fn, ctx);
  return b;
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
