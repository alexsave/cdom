/* Shim-vs-sink conformance of the templated rows app: bench/cdom-tpl/app.c and its compiled
 * rows.cdt run the same benchmark actions in Chrome (wasm, through shim/cdom.js) and natively
 * (into the C sink); tests/conform.mjs compares the DOMs after every step and resumes over the
 * native first frame. Row clicks go through cd_on_event and page_dispatch, so the path that
 * finds a row's item from its node is exercised too.
 * Run: CONFORM_SRCS="tests/cdtc/conform_rows.c bench/cdom-tpl/app.c <gen>/rows.c" CONFORM_INC="src <gen>" node tests/conform.mjs */
#include "rows.h"
#ifdef __wasm__
#define EXPORT(n) __attribute__((export_name(n)))
#else
#define EXPORT(n)
#endif

#define STEPS 60

static uint32_t rng = 0x2545f491u;
static uint32_t rnd(uint32_t m) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng % m; }

/* a click through the shim's path: a random row's tr, at cell c (1 the label, 2 the remove icon) */
static void row_cell(uint32_t c) {
  static cd_id ids[1 << 14];
  uint32_t n = 0;
  for (cd_id id = 1; id < (1u << 16) && n < (1u << 14); id++)
    if (cd_tag(id) >> 24 == 1) ids[n++] = id;
  if (!n) return;
  cd_event_rec e = {CD_EV_CLICK, (int32_t)ids[rnd(n)], 0, 0, 0, 0, 0, (int32_t)c, 1};
  cd_on_event(&e);
}

static void act(void (*h)(Rows *, const cd_event_rec *)) {
  cd_event_rec e = {CD_EV_CLICK, 0, 0, 0, 0, 0, 0, 0, 0};
  h(&S, &e);
  page_render(&S);
}

EXPORT("step") void step(uint32_t k) {
  if (k == 1) { S.rng = 12345; act(run); cd_flush(); return; } /* the same labels whatever the seed */
  switch (rnd(10)) {
  case 0: act(S.rows_n < 2500 ? add : run); break;
  case 1: act(update); break;
  case 2: case 3: row_cell(1); break;
  case 4: row_cell(2); break;
  case 5: act(swaprows); break;
  case 6: act(rnd(3) ? run : clear); break;
  case 7: /* a few rows by hand at random places, ids past next_id */
    for (uint32_t j = 1 + rnd(3); j; j--) {
      Row *r = Rows_rows_push(&S);
      r->id = S.next_id++;
      r->label = cd_internz(j & 1 ? "hand & made" : "<odd>");
      uint32_t at = rnd(S.rows_n);
      Row t = *r; for (uint32_t i = S.rows_n - 1; i > at; i--) S.rows[i] = S.rows[i - 1]; S.rows[at] = t;
    }
    page_render(&S);
    break;
  case 8: /* reverse a stretch: moves off the longest run */
    if (S.rows_n > 10) {
      uint32_t a = rnd(S.rows_n - 10), b = a + 2 + rnd(9);
      for (; a < b; a++, b--) { Row t = S.rows[a]; S.rows[a] = S.rows[b]; S.rows[b] = t; }
    }
    page_render(&S);
    break;
  default: S.selected = 0; page_render(&S); break;
  }
  cd_flush();
}

EXPORT("steps") uint32_t steps(void) { return STEPS; }

#ifndef __wasm__
#include "cdom_html.h"
#include <stdio.h>
#include <stdlib.h>
static void dump(void) {
  uint32_t n = cd_html_write(0, 0);
  char *b = malloc(n + 1);
  cd_html_write(b, n);
  printf("%u\n", n);
  fwrite(b, 1, n, stdout);
  free(b);
}
static void blob(void) {
  static const char A[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  uint32_t n = cd_state_save(0, 0);
  uint8_t *s = malloc(n + 3);
  cd_state_save(s, n);
  s[n] = s[n + 1] = 0;
  putchar('S');
  for (uint32_t i = 0; i < n; i += 3) {
    uint32_t v = (uint32_t)s[i] << 16 | (uint32_t)s[i + 1] << 8 | s[i + 2];
    putchar(A[v >> 18]); putchar(A[(v >> 12) & 63]);
    putchar(i + 1 < n ? A[(v >> 6) & 63] : '='); putchar(i + 2 < n ? A[v & 63] : '=');
  }
  putchar('\n');
  free(s);
  char ev[256];
  uint32_t ne = cd_html_resume_events(ev, sizeof ev);
  if (ne <= sizeof ev) printf("E%.*s\n", (int)ne, ev);
}
int main(void) {
  cd_html_reset();
  cd_main();
  cd_flush();
  blob();
  dump();
  for (uint32_t k = 1; k <= steps(); k++) { step(k); dump(); }
  return 0;
}
#endif
