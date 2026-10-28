/* Native round trip of the template compiler: tests/cdtc/feat.cdt is compiled by cdtc, linked
 * with cdom.c and the C sink, rendered, changed, rendered again; after each step the sink's
 * HTML must equal the string written here by hand. Events are dispatched to every listening
 * node and must reach the right handler with the right item.
 * Built and run by tests/cdtc/run.sh. */
#include "feat.h"
#include "cdom_html.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t seen[64], nseen; /* item ids the item handler was called with */
void item_click(Feat *F, Item *it, const cd_event_rec *e) { (void)F; (void)e; seen[nseen++] = it->id; }
void note_click(Feat *F, Note *o, const cd_event_rec *e) { (void)F; (void)e; o->open = !o->open; }
void toggle(Feat *F, const cd_event_rec *e) { (void)e; F->flag = 0; }
void bump(Feat *F, const cd_event_rec *e) { (void)e; F->n++; }
void reset(Feat *F, const cd_event_rec *e) { (void)e; F->n = 0; }

void cd_on_event(const cd_event_rec *e) {
  page_dispatch(e);
  page_render(&G);
}

void cd_main(void) {
  page_init();
  page_create(&G);
  page_render(&G);
}

static int fails;
static char *html(void) {
  static char b[1 << 16];
  uint32_t n = cd_html_write(b, sizeof b - 1);
  if (n >= sizeof b) { printf("FAIL html too long\n"); exit(1); }
  b[n] = 0;
  return b;
}

static void expect(const char *what, const char *want) {
  cd_flush();
  const char *got = html();
  if (strcmp(got, want)) {
    fails++;
    printf("FAIL %s\n  want %s\n  got  %s\n", what, want, got);
  } else {
    printf("ok   %s\n", what);
  }
}

static Item *item(uint32_t id, const char *name, uint32_t hot) {
  Item *it = Feat_items_push(&G);
  it->id = id;
  it->name = cd_internz(name);
  it->hot = hot;
  return it;
}

/* every node listening for slot s (the tag's top byte), clicked in id order */
static uint32_t click_all(uint32_t slot, int type) {
  uint32_t k = 0;
  for (cd_id id = 1; id < 4096; id++)
    if (cd_tag(id) >> 24 == slot) {
      cd_event_rec e = {type, (int32_t)id, 0, 0, 0, 0, 0, 0, 0};
      cd_on_event(&e);
      k++;
    }
  return k;
}

#define DIV0 "<div id=\"feat\" class=\"page\" title=\"T\">"
#define LI_A "<li class=\"item\" data-name=\"a\"><b>1</b><i>a</i></li>"
#define LI_B_SEL "<li class=\"item hot sel\" data-name=\"b\"><b>7</b><i>b</i></li>"
#define LI_B "<li class=\"item hot\" data-name=\"b\"><b>7</b><i>b</i></li>"
#define LI_C "<li class=\"item\" data-name=\"c\"><b>9</b><i>c</i></li>"

int main(void) {
  cd_html_reset();
  G.title = cd_internz("T");
  G.link = cd_internz("/x");
  item(1, "a", 0);
  item(7, "b", 1);
  Note *o = Feat_notes_push(&G);
  o->id = 3;
  o->text = cd_internz("x");
  o->open = 1;
  cd_main();
  expect("first render: bound attribute, toggles, adjacent text, else branches, both lists, an if inside an item",
         DIV0 "<h1>T</h1><p>count: <!---->7<!----> of <!---->2</p><b>off</b>"
         "<section><i>not two</i><kbd>7</kbd></section>"
         "<ul>" LI_A LI_B_SEL "</ul>"
         "<table><tbody><tr><td>#<!---->3<!---->: <!---->x<!---->!</td><td><!--i--><span class=\"open\">x</span><u>7</u></td></tr></tbody></table>"
         "<button>bump</button></div>");

  G.title = cd_internz("U");
  G.n = 8;
  page_render(&G);
  expect("title and n change: attribute, text, the item's class, text after an if (by id)",
         "<div id=\"feat\" class=\"page\" title=\"U\"><h1>U</h1><p>count: <!---->8<!----> of <!---->2</p><b>off</b>"
         "<section><i>not two</i><kbd>8</kbd></section>"
         "<ul>" LI_A LI_B "</ul>"
         "<table><tbody><tr><td>#<!---->3<!---->: <!---->x<!---->!</td><td><!--i--><span class=\"open\">x</span><u>8</u></td></tr></tbody></table>"
         "<button>bump</button></div>");

  G.flag = 1;
  G.link = cd_internz("/y");
  G.mode = 1;
  page_render(&G);
  expect("flag and mode 1: the root's toggle, else -> then before its anchor, an if with no else appears",
         "<div id=\"feat\" class=\"page flag\" title=\"U\"><h1>U</h1><p>count: <!---->8<!----> of <!---->2</p><a href=\"/y\">on</a>"
         "<section><span>one <!---->1</span><i>not two</i><kbd>8</kbd></section>"
         "<ul>" LI_A LI_B "</ul>"
         "<table><tbody><tr><td>#<!---->3<!---->: <!---->x<!---->!</td><td><!--i--><span class=\"open\">x</span><u>8</u></td></tr></tbody></table>"
         "<button>bump</button></div>");

  G.link = cd_internz("/z");
  G.mode = 2;
  page_render(&G);
  expect("mode 2: the first if goes, the second turns to then; a branch's own binding patches",
         "<div id=\"feat\" class=\"page flag\" title=\"U\"><h1>U</h1><p>count: <!---->8<!----> of <!---->2</p><a href=\"/z\">on</a>"
         "<section><em>two</em><kbd>8</kbd></section>"
         "<ul>" LI_A LI_B "</ul>"
         "<table><tbody><tr><td>#<!---->3<!---->: <!---->x<!---->!</td><td><!--i--><span class=\"open\">x</span><u>8</u></td></tr></tbody></table>"
         "<button>bump</button></div>");

  G.mode = 1;
  page_render(&G);
  expect("mode 1 again: an if inserts before a later if's present branch",
         "<div id=\"feat\" class=\"page flag\" title=\"U\"><h1>U</h1><p>count: <!---->8<!----> of <!---->2</p><a href=\"/z\">on</a>"
         "<section><span>one <!---->1</span><i>not two</i><kbd>8</kbd></section>"
         "<ul>" LI_A LI_B "</ul>"
         "<table><tbody><tr><td>#<!---->3<!---->: <!---->x<!---->!</td><td><!--i--><span class=\"open\">x</span><u>8</u></td></tr></tbody></table>"
         "<button>bump</button></div>");

  /* reorder, add, remove: [1, 7] -> [7, 9] */
  G.items[0] = G.items[1];
  G.items[1] = (Item){9, cd_internz("c"), 0};
  G.notes[0].open = 0;
  G.notes[0].text = cd_internz("y");
  page_render(&G);
  expect("items [1,7] -> [7,9]; the note closes: the item's if swaps branches before its anchor",
         "<div id=\"feat\" class=\"page flag\" title=\"U\"><h1>U</h1><p>count: <!---->8<!----> of <!---->2</p><a href=\"/z\">on</a>"
         "<section><span>one <!---->1</span><i>not two</i><kbd>8</kbd></section>"
         "<ul>" LI_B LI_C "</ul>"
         "<table><tbody><tr><td>#<!---->3<!---->: <!---->y<!---->!</td><td><!--i--><em>closed <!---->3</em><u>8</u></td></tr></tbody></table>"
         "<button>bump</button></div>");

  /* events: every item listener reaches item_click with its own item */
  nseen = 0;
  uint32_t k = click_all(1, CD_EV_CLICK);
  int both = nseen == 2 && ((seen[0] == 7 && seen[1] == 9) || (seen[0] == 9 && seen[1] == 7));
  printf("%s item clicks: %u listeners, handler saw %u items (%u, %u)\n", k == 2 && both ? "ok  " : "FAIL", k, nseen, seen[0], seen[1]);
  fails += !(k == 2 && both);
  /* a click type nobody listens for on that node does nothing */
  nseen = 0;
  click_all(1, CD_EV_DBLCLICK);
  printf("%s an unlistened event type reaches no handler\n", nseen ? "FAIL" : "ok  ");
  fails += nseen != 0;
  /* the note's listener: its handler opens it, the render swaps the branch back */
  click_all(2, CD_EV_CLICK);
  click_all(3, CD_EV_CLICK); /* toggle: flag off */
  click_all(4, CD_EV_CLICK); /* bump: n 8 -> 9 */
  expect("note_click, toggle, bump through page_dispatch",
         "<div id=\"feat\" class=\"page\" title=\"U\"><h1>U</h1><p>count: <!---->9<!----> of <!---->2</p><b>off</b>"
         "<section><span>one <!---->1</span><i>not two</i><kbd>9</kbd></section>"
         "<ul>" LI_B "<li class=\"item sel\" data-name=\"c\"><b>9</b><i>c</i></li></ul>"
         "<table><tbody><tr><td>#<!---->3<!---->: <!---->y<!---->!</td><td><!--i--><span class=\"open\">y</span><u>9</u></td></tr></tbody></table>"
         "<button>bump</button></div>");
  click_all(4, CD_EV_DBLCLICK); /* reset: n -> 0 */
  G.items_n = 0;
  G.notes_n = 0;
  G.title = 0; /* an unset str renders as "" */
  page_render(&G);
  expect("dblclick on the same button reaches its second handler; both lists empty; a zero str is \"\"",
         "<div id=\"feat\" class=\"page\" title=\"\"><h1></h1><p>count: <!---->0<!----> of <!---->0</p><b>off</b>"
         "<section><span>one <!---->1</span><i>not two</i><kbd>0</kbd></section>"
         "<ul></ul><table><tbody></tbody></table><button>bump</button></div>");

  /* the state blob: save, load into a fresh state, save again: same bytes */
  item(5, "five", 1);
  G.title = cd_internz("t\xc3\xa9");
  uint8_t a[256], b[256];
  uint32_t na = Feat_save(&G, a, sizeof a);
  Feat f2 = Feat_INIT;
  int lr = Feat_load(&f2, a, na);
  uint32_t nb = Feat_save(&f2, b, sizeof b);
  int same = !lr && na == nb && !memcmp(a, b, na) && f2.items_n == 1 && f2.items[0].id == 5 && f2.n == G.n;
  printf("%s state blob round trip (%u bytes)\n", same ? "ok  " : "FAIL", na);
  fails += !same;
  /* every truncation is refused and leaves the state alone */
  int refused = 1;
  for (uint32_t cut = 0; cut < na; cut++) {
    Feat f3 = Feat_INIT;
    refused &= Feat_load(&f3, a, cut) == -1 && f3.n == 7 && f3.items_n == 0;
  }
  printf("%s every truncated blob is refused\n", refused ? "ok  " : "FAIL");
  fails += !refused;
  Feat f4 = Feat_INIT;
  a[na] = 0;
  int extra = Feat_load(&f4, a, na + 1) == -1 && f4.items_n == 0;
  printf("%s a blob with a trailing byte is refused\n", extra ? "ok  " : "FAIL");
  fails += !extra;
  uint32_t need = Feat_save(&G, a, 3);
  printf("%s save with no room returns the need (%u)\n", need == na ? "ok  " : "FAIL", need);
  fails += need != na;
  printf(fails ? "%d FAILED\n" : "all ok\n", fails);
  return fails != 0;
}
