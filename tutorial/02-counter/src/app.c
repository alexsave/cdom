/* Lesson 2: state and events. The count is a plain C variable. A click changes it and then updates the
 * one text node that shows it: there is no re-render and no diff, because C knows exactly what changed. */
#include "cdom.h"

#define L(s) CD_LIT(s)

/* The state. React keeps it in a hook; here it is a global that lives as long as the page. */
static int32_t count;

/* Ids name the nodes the app touches later. cd_id_new hands them out; the shim keeps one table entry
 * per id, so an id is a direct index, not a selector. */
static cd_id count_text, minus, plus, reset;

/* The view of the state: one DOM operation. A count below zero gets a "-" in front, since
 * cd_set_text_node_num takes an unsigned number. */
static void show(void) {
  if (count >= 0) {
    cd_set_text_node_num(count_text, (uint32_t)count);
  } else {
    char buf[12];
    uint32_t n = cd_fmt(buf, count, 0, CD_UNIT_NONE);
    cd_set_text_node(count_text, cd_tmp(buf, n)); /* a one-off string: it never grows the string table */
  }
}

/* Handlers: (event, ctx). ctx is whatever was passed to cd_on; here it carries the step. */
static void step(const cd_event_rec *e, void *ctx) {
  (void)e;
  count += (int32_t)(intptr_t)ctx;
  show();
}

static void on_reset(const cd_event_rec *e, void *ctx) {
  (void)e; (void)ctx;
  count = 0;
  show();
}

static void style(void) {
  cd_open(L("style"));
  cd_text(L(":root { color-scheme: light dark; font-family: system-ui, sans-serif; }"
            "body { margin: 0; }"
            ".counter { display: flex; flex-direction: column; align-items: center; gap: 1rem; margin-top: 4rem; }"
            ".counter output { font-size: 4rem; font-variant-numeric: tabular-nums; }"
            ".counter .row { display: flex; gap: 0.5rem; }"
            ".counter button { font: inherit; font-size: 1.25rem; min-width: 3rem; padding: 0.5rem 1rem; }"));
  cd_close();
}

void cd_main(void) {
  cd_at(CD_MOUNT, 0);
  style();

  cd_open(L("div")); cd_class(L("counter"));
  /* cd_text_num makes the text node, and cd_name_text names it, so show() can reach it later. */
  cd_open(L("output")); cd_text_num(0); cd_name_text(count_text = cd_id_new()); cd_close();
  cd_open(L("div")); cd_class(L("row"));
  minus = cd_open_id(L("button")); cd_attr(L("aria-label"), L("decrement")); cd_text(L("\u2212")); cd_close();
  plus = cd_open_id(L("button")); cd_attr(L("aria-label"), L("increment")); cd_text(L("+")); cd_close();
  reset = cd_open_id(L("button")); cd_text(L("reset")); cd_close();
  cd_close();
  cd_close();

  /* Wire the handlers once the tree is built. One listener on the mount delivers every click to C,
   * which routes it to the handler registered for that node. */
  cd_on(minus, CD_EV_CLICK, step, (void *)(intptr_t)-1);
  cd_on(plus, CD_EV_CLICK, step, (void *)(intptr_t)1);
  cd_on(reset, CD_EV_CLICK, on_reset, 0);
}
