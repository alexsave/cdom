/* Shim-vs-sink conformance of compiled templates: tests/cdtc/feat.cdt, compiled by cdtc, runs
 * the same steps in Chrome (wasm, through shim/cdom.js) and natively (into the C sink);
 * tests/conform.mjs compares the DOMs after every step and hydrates the native first frame.
 * The first frame holds the page, three items and three notes (whose template has an {#if}
 * inserted before an anchor and adjacent text, one note with an empty text), so hydration
 * covers clones with inserted branches and separators. Steps change every field at random
 * and click random listeners.
 * Run: CONFORM_SRCS="tests/cdtc/conform_feat.c <gen>/feat.c" CONFORM_INC="src <gen>" node tests/conform.mjs */
#include "feat.h"
#ifdef __wasm__
#define EXPORT(n) __attribute__((export_name(n)))
#else
#define EXPORT(n)
#endif

#define STEPS 300

void item_click(Feat *F, Item *it, const cd_event_rec *e) { (void)e; F->n = it->id; it->hot = !it->hot; }
void note_click(Feat *F, Note *o, const cd_event_rec *e) { (void)F; (void)e; o->open = !o->open; }
void toggle(Feat *F, const cd_event_rec *e) { (void)e; F->flag = 0; }
void bump(Feat *F, const cd_event_rec *e) { (void)e; F->n++; }
void reset(Feat *F, const cd_event_rec *e) { (void)e; F->n = 0; }

void cd_on_event(const cd_event_rec *e) {
  page_dispatch(e);
  page_render(&G);
}

static uint32_t rng = 0x9e3779b9u;
static uint32_t rnd(uint32_t m) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng % m; }
static const char *WORDS[] = {"a", "b&c", "<d>", "\"e\"", "\xc3\xa9t\xc3\xa9", "f g", "\xf0\x9f\x98\x80", "x&lt;y", "long word here"};
static cd_str word(void) { return cd_internz(WORDS[rnd(sizeof WORDS / sizeof *WORDS)]); }

static int has_item(uint32_t id) {
  for (uint32_t i = 0; i < G.items_n; i++)
    if (G.items[i].id == id) return 1;
  return 0;
}
static int has_note(uint32_t id) {
  for (uint32_t i = 0; i < G.notes_n; i++)
    if (G.notes[i].id == id) return 1;
  return 0;
}

void cd_main(void) {
  if (!G.items_n && !G.title) { /* a fresh start; a loaded state keeps what it has */
    G.title = cd_internz("feat");
    G.link = cd_internz("/a?b=1&c=2");
    for (uint32_t i = 1; i <= 3; i++) {
      Item *it = Feat_items_push(&G);
      it->id = i * 3;
      it->name = cd_internz(WORDS[i]);
      it->hot = i & 1;
    }
    for (uint32_t i = 1; i <= 3; i++) {
      Note *o = Feat_notes_push(&G);
      o->id = i * 5;
      o->text = cd_internz(i == 2 ? "" : WORDS[i + 3]);
      o->open = i != 3;
    }
  }
  page_init();
  page_create(&G);
  page_render(&G);
}

/* a click on the k-th node (in id order) listening in slot s, if any */
static void click(uint32_t s, int type) {
  cd_id ids[256];
  uint32_t n = 0;
  for (cd_id id = 1; id < 4096 && n < 256; id++)
    if (cd_tag(id) >> 24 == s) ids[n++] = id;
  if (!n) return;
  cd_event_rec e = {type, (int32_t)ids[rnd(n)], 0, 0, 0, 0, 0, 0, 0};
  cd_on_event(&e);
}

EXPORT("step") void step(uint32_t k) {
  (void)k;
  switch (rnd(16)) {
  case 0: G.flag = !G.flag; break;
  case 1: G.mode = rnd(4); break;
  case 2: G.title = rnd(4) ? word() : 0; break;
  case 3: G.link = word(); break;
  case 4: G.n = rnd(30); break;
  case 5: { /* add items */
    for (uint32_t j = rnd(4); j; j--) {
      uint32_t id = 1 + rnd(30);
      if (has_item(id)) continue;
      Item *it = Feat_items_push(&G);
      it->id = id; it->name = word(); it->hot = rnd(2);
    }
    break;
  }
  case 6: if (G.items_n) Feat_items_remove(&G, rnd(G.items_n)); break;
  case 7: /* shuffle items */
    for (uint32_t i = G.items_n; i > 1; i--) { uint32_t j = rnd(i); Item t = G.items[i - 1]; G.items[i - 1] = G.items[j]; G.items[j] = t; }
    break;
  case 8: if (G.items_n) { Item *it = &G.items[rnd(G.items_n)]; it->hot = !it->hot; it->name = word(); } break;
  case 9: { /* add notes */
    for (uint32_t j = 1 + rnd(3); j; j--) {
      uint32_t id = 1 + rnd(20);
      if (has_note(id)) continue;
      Note *o = Feat_notes_push(&G);
      o->id = id; o->text = word(); o->open = rnd(2);
    }
    break;
  }
  case 10: if (G.notes_n) Feat_notes_remove(&G, rnd(G.notes_n)); break;
  case 11: if (G.notes_n) { Note *o = &G.notes[rnd(G.notes_n)]; o->open = !o->open; if (rnd(2)) o->text = word(); } break;
  case 12: /* reverse the notes: moves, with their branches inside */
    for (uint32_t i = 0; i < G.notes_n / 2; i++) { Note t = G.notes[i]; G.notes[i] = G.notes[G.notes_n - 1 - i]; G.notes[G.notes_n - 1 - i] = t; }
    break;
  case 13: click(1 + rnd(4), CD_EV_CLICK); break;
  case 14: click(4, CD_EV_DBLCLICK); break;
  default: G.items_n = rnd(2) ? G.items_n : 0; G.notes_n = rnd(3) ? G.notes_n : 0; break;
  }
  page_render(&G);
  cd_flush();
}

EXPORT("steps") uint32_t steps(void) { return STEPS; }

#ifndef __wasm__
#include "cdom_html.h"
#include <stdio.h>
#include <stdlib.h>
static void dump(void) { /* <length>\n<html> */
  uint32_t n = cd_html_write(0, 0);
  char *b = malloc(n + 1);
  cd_html_write(b, n);
  printf("%u\n", n);
  fwrite(b, 1, n, stdout);
  free(b);
}
static void blob(void) { /* S<base64>\n: the state after the first frame, for hydration */
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
