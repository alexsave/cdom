#include "cdom_ui.h"
#include "model.h"

row_t rows[N_MAX];
uint32_t keys[N_MAX];
uint32_t nrows, currency, selected, f[NF], draft_len, big_len;
char draft[DRAFT_CAP];
char big[BIG_CAP];
const char *const sel_cls[2] = {"", "sel"};

static const char *const prefix[3] = {"$", "EUR ", "GBP "};

uint32_t price_text(char *dst, uint32_t cap, const row_t *r) {
  cd_buf b = cd_buf_in(dst, cap);
  cd_buf_putz(&b, prefix[currency]);
  cd_buf_u64(&b, r->price_cents / 100);
  uint32_t c = r->price_cents % 100;
  char two[3] = {'.', (char)('0' + c / 10), (char)('0' + c % 10)};
  cd_buf_put(&b, two, 3);
  return b.len;
}
uint32_t leaf_text(char *dst, uint32_t cap, uint32_t k) {
  cd_buf b = cd_buf_in(dst, cap);
  cd_buf_putz(&b, "f");
  cd_buf_u64(&b, k);
  cd_buf_putz(&b, "=");
  cd_buf_u64(&b, f[k]);
  return b.len;
}
uint32_t draft_text(char *dst, uint32_t cap) {
  cd_buf b = cd_buf_in(dst, cap);
  cd_buf_u64(&b, draft_len);
  return b.len;
}
