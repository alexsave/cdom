/* Op-stream conformance: the same C drives the shim (wasm, in Chrome) and the C sink
 * (native, cdom_html.c). cd_main builds a first frame with every hydration edge (adjacent
 * and empty text nodes, escapes, non-ASCII, mixed-case names, a leading newline in pre, a
 * template, DETACH/ATTACH around a fill); step(k) then exercises every one of the 27 ops,
 * and steps past the scripted ones run a keyed list of adjacent text nodes through random
 * updates. tests/conform.mjs compares the two after every step, and hydrates the native
 * first frame and replays the steps over it. */
#include "../src/cdom.h"
#ifdef __wasm__
#define EXPORT(n) __attribute__((export_name(n)))
#else
#define EXPORT(n)
#endif

#define SCRIPTED 8
#define RANDOM 300

static cd_str z(const char *s) { return cd_internz(s); }
static cd_id ROOT, UL, INPUT, P, NUMTXT, LBL, ITEM[8], SUB[8], LATE;
static uint32_t rng = 0x9e3779b9u;
static uint32_t rnd(uint32_t m) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng % m; }

void cd_on_event(const cd_event_rec *e) { (void)e; }

static void item(uint32_t i) { /* a clone of template 1 with its parts filled */
  ITEM[i] = cd_id_new();
  SUB[i] = cd_id_new();
  cd_clone(1);
  cd_listen(ITEM[i], CD_EV_CLICK);
  if (i & 1) cd_class(z("t odd"));
  cd_down(); cd_down(); cd_value_num(i * 11); cd_name(SUB[i]); cd_up();
  cd_next(); cd_attr(z("title"), z(i ? "i>0" : "first"));
  cd_close();
}

void cd_main(void) {
  cd_at(CD_DETACHED, 0); /* template 1: <li class="t"><b> </b><i></i></li> */
  cd_open(z("li")); cd_class(z("t"));
  cd_open(z("b")); cd_text(z(" ")); cd_close();
  cd_open(z("i")); cd_close();
  cd_close();
  cd_tpl_save(1);

  cd_at(CD_MOUNT, 0);
  cd_open(z("div")); ROOT = cd_id_new(); cd_name(ROOT);
  cd_attr(z("id"), z("root")); cd_attr(z("data-x"), z("a&b<c>\"d' e"));
  cd_text(z("one")); cd_text(z("two")); cd_text(z("")); cd_text(z("three")); cd_text(z(""));
  cd_open(z("p")); P = cd_id_new(); cd_name(P);
  cd_text_num(42); NUMTXT = cd_id_new(); cd_name(NUMTXT);
  cd_text(z("<&>\"\xc3\xa9\xe6\x97\xa5\xe6\x9c\xac\xf0\x9f\x98\x80")); LBL = cd_id_new(); cd_name(LBL);
  cd_close();
  cd_open(z("ul")); UL = cd_id_new(); cd_name(UL); cd_close();
  cd_open(z("input")); cd_attr(z("value"), z("v&1")); INPUT = cd_id_new(); cd_name(INPUT); cd_close();
  cd_open(z("span")); cd_text(z("")); cd_close();
  cd_open(z("span")); cd_close();
  cd_open(z("pre")); cd_text(z("\nafter a newline")); cd_close();
  cd_open(z("Section")); cd_attr(z("Data-Up"), z("1")); cd_class(z("")); cd_close();
  cd_close();
  cd_detach(UL); /* a fill off the document, as cd_list_update does for big first fills */
  cd_at(UL, 0);
  for (uint32_t i = 0; i < 4; i++) item(i);
  cd_attach(UL);
  cd_at(CD_MOUNT, 0);
  cd_text(z("tail")); /* text at depth 0 */
  cd_text_num(7);
}

/* ---- the random keyed list: <li>key label ""</li>, adjacent text in every row ---- */
static cd_list list;
static uint32_t keys[64], labels[64], nkeys;
static cd_id *lbl_ids; /* per item data index -> its label text id */
static uint32_t nlbl;
static const char *WORDS[] = {"a", "b&c", "<d>", "\"e\"", "", "\xc3\xa9t\xc3\xa9", "f g", "\xf0\x9f\x98\x80", "x&lt;y"};

static cd_id row_create(void *ctx, uint32_t i, uint32_t *data) {
  (void)ctx;
  cd_id li = cd_id_new(), t = cd_id_new();
  cd_open(z("li")); cd_name(li);
  cd_text_num(keys[i]);
  cd_text(z(WORDS[labels[i]])); cd_name(t);
  cd_text(z(""));
  cd_close();
  lbl_ids[nlbl] = t;
  *data = nlbl++;
  return li;
}
static void row_patch(void *ctx, uint32_t i, cd_item *it) {
  (void)ctx;
  cd_set_text(lbl_ids[it->data], z(WORDS[labels[i]]));
}
static void row_destroy(void *ctx, cd_item *it) { (void)ctx; cd_id_free(lbl_ids[it->data]); }
static const cd_list_fns FNS = {0, row_create, row_patch, row_destroy};

EXPORT("step") void step(uint32_t k) {
  char buf[64];
  switch (k) {
  case 1: /* AT with a before, SET_TEXT, SET_TEXT_NUM, SET_ATTR (replace and add), SET_CLASS */
    cd_at(CD_MOUNT, ROOT);
    cd_open(z("header")); cd_text(z("head")); cd_close();
    cd_set_text(LBL, z("changed <label>"));
    cd_set_text_num(NUMTXT, 4294967295u);
    cd_set_attr(ROOT, z("data-x"), z("y"));
    cd_set_attr(ROOT, z("data-new"), z("&"));
    cd_set_class(ROOT, z("c1 c2"));
    cd_set_class(P, z(""));
    break;
  case 2: /* MOVE, REMOVE, FREE */
    cd_move(ITEM[3], ITEM[0]);
    cd_move(ITEM[1], 0);
    cd_remove(ITEM[2]);
    cd_id_free(SUB[2]);
    break;
  case 3: /* CLEAR, then a refill with SEL/DOWN/NEXT/UP/VALUE, VALUE on an element */
    cd_clear(UL);
    for (uint32_t i = 0; i < 4; i++) { cd_id_free(ITEM[i]); if (i != 2) cd_id_free(SUB[i]); }
    cd_at(UL, 0);
    for (uint32_t i = 0; i < 3; i++) item(i);
    cd_sel(ITEM[1]); cd_down(); cd_next(); cd_up(); cd_down(); cd_down(); cd_value(z("via sel"));
    cd_sel(ITEM[0]); cd_value(z("on an element: no effect"));
    cd_sel(P); cd_down(); cd_next(); cd_value_num(5);
    break;
  case 4: /* DETACH, change while detached, ATTACH */
    cd_detach(ROOT);
    cd_set_text(LBL, z("while detached"));
    cd_at(UL, 0); item(3);
    cd_attach(ROOT);
    break;
  case 5: /* adjacent and empty text at depth 0, before a node */
    cd_at(CD_MOUNT, 0);
    cd_text(z("more")); cd_text(z(""));
    cd_text(z("x")); LATE = cd_id_new(); cd_name(LATE);
    cd_at(CD_MOUNT, LATE); cd_text(z("before x"));
    break;
  case 6: { /* js_value: an input's value, natively its value attribute */
    uint32_t n = cd_input_value(INPUT, buf, sizeof buf);
    cd_at(CD_MOUNT, 0);
    cd_open(z("output")); cd_text(cd_intern(buf, n)); cd_close();
    break;
  }
  case 7: /* CLEAR on a text node, an empty attribute value */
    cd_clear(LATE);
    cd_set_attr(INPUT, z("placeholder"), z(""));
    break;
  case 8: /* the list's home */
    cd_at(CD_MOUNT, 0);
    cd_open(z("ol")); list.parent = cd_id_new(); cd_name(list.parent); cd_close();
    break;
  default: { /* random keyed updates over rows of adjacent text nodes */
    if (!lbl_ids) lbl_ids = cd_alloc(4 * 4096);
    uint32_t n = rnd(12), used = 0;
    nkeys = 0;
    while (nkeys < n) {
      uint32_t key = 1 + rnd(16);
      if (used & (1u << key)) continue;
      used |= 1u << key;
      keys[nkeys] = key;
      labels[nkeys++] = rnd(sizeof WORDS / sizeof *WORDS);
    }
    list.owns_parent = rnd(2);
    cd_list_update(&list, keys, nkeys, &FNS);
  }
  }
  cd_flush();
}

EXPORT("steps") uint32_t steps(void) { return SCRIPTED + RANDOM; }

#ifndef __wasm__
#include "../src/cdom_html.h"
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
int main(void) {
  cd_html_reset();
  cd_main();
  cd_flush();
  dump();
  for (uint32_t k = 1; k <= steps(); k++) { step(k); dump(); }
  return 0;
}
#endif
