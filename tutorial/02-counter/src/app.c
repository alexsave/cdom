/* Lesson 2: state and events. The count is a plain C variable. A click changes it and then updates the
 * one text node that shows it: there is no re-render and no diff, because C knows exactly what changed. */
#include "cdom_ui.h"


/* The state. React keeps it in a hook; here it is a global that lives as long as the page. */
static int32_t count;

/* Ids name the nodes the app touches later. cd_id_new hands them out; the shim keeps one table entry
 * per id, so an id is a direct index, not a selector. */
static cd_id count_text, minus, plus, reset;

/* The view of the state: one DOM operation. cd_buf (cdom_ui.h) writes the number, with its "-" when it is
 * below zero, into a buffer on the stack; cd_tmp hands those bytes (a length, no terminator) to the one call. */
static void show(void) {
  char room[12];
  cd_buf b = cd_buf_in(room, sizeof room);
  cd_buf_i64(&b, count);
  cd_set_text_node(count_text, cd_tmp(b.p, b.len));
}

/* Handlers: (event, ctx). ctx is whatever was passed to cd_on; here it carries the step. */
static void step(const cd_event_rec *, void *ctx) {
  count += (int32_t)(intptr_t)ctx;
  show();
}

static void on_reset(const cd_event_rec *, void *) {
  count = 0;
  show();
}

/* The page's CSS is src/app.css. The build puts every .css file under src/ in a <style> in the page's
 * <head>, so the page is styled before this code runs. */

void cd_main(void) {
  cd_open("div"); cd_class("counter");
  /* cd_text_num makes the text node, and cd_name_text names it, so show() can reach it later. */
  cd_open("output"); cd_text_num(0); cd_name_text(count_text = cd_id_new()); cd_close();
  cd_open("div"); cd_class("row");
  minus = cd_open_id("button"); cd_attr("aria-label", "decrement"); cd_text("\u2212"); cd_close();
  plus = cd_open_id("button"); cd_attr("aria-label", "increment"); cd_text("+"); cd_close();
  reset = cd_open_id("button"); cd_text("reset"); cd_close();
  cd_close();
  cd_close();

  /* Wire the handlers once the tree is built. One listener on the mount delivers every click to C,
   * which routes it to the handler registered for that node. */
  cd_on(minus, CD_EV_CLICK, step, (void *)(intptr_t)-1);
  cd_on(plus, CD_EV_CLICK, step, (void *)(intptr_t)1);
  cd_on(reset, CD_EV_CLICK, on_reset, nullptr);
}
