/* The frames tier's page (docs/frames-design.md): what Netscape 2 to 4 and IE 3 and 4 can be given, a count and its
 * button, links under one listener, text fields and a checkbox, styles carried as period markup, and what is not drawn
 * there (display: none, an SVG). tests/frames.html boots it and tools/frames.mjs drives it, under LEGACY=frames. */
#include "../src/cdom.h"

#define EXPORT(n) __attribute__((export_name(n)))

enum { COUNT, INC, LINKS, NAME, ECHO, NOTE, CHECK, LIST, TICK, R1, R2, NIDS };
static cd_id ids[NIDS];
static uint32_t count, clicks_path = 0xffffffffu, inputs, changes, checks, focuses, blurs, items, radios;
static char io[256];
/* Markup characters in every place the shim writes text: each must read back in the frame as these characters. */
#define ESC "<b>\"&amp;x</b>'"

static cd_id el(const char *tag, const char *idattr) {
  cd_open(tag);
  cd_attr("id", idattr);
  cd_id id = cd_id_new();
  cd_name_el(id);
  return id;
}

void cd_main(void) {
  cd_open("h1"); cd_attr("id", "title"); cd_attr("style", "color:#800000"); cd_text("frames"); cd_close();
  cd_open("p"); cd_attr("id", "countp"); cd_text("count ");
  cd_text_num(0); ids[COUNT] = cd_id_new(); cd_name_text(ids[COUNT]);
  cd_close();
  ids[INC] = el("button", "inc"); cd_text("inc"); cd_close();
  /* One listener over three targets: two links and a span whose text the tier wraps in a link of its own. */
  ids[LINKS] = el("div", "links");
  cd_open("a"); cd_attr("id", "one"); cd_attr("href", "#one"); cd_text("one"); cd_close();
  cd_text(" ");
  cd_open("a"); cd_attr("id", "two"); cd_attr("href", "#two"); cd_text("two"); cd_close();
  cd_text(" ");
  cd_open("span"); cd_attr("id", "three"); cd_text("three"); cd_close();
  cd_close();
  ids[NAME] = el("input", "name"); cd_attr("value", "start"); cd_close();
  cd_open("p"); cd_attr("id", "echop"); cd_text("echo "); cd_text("start"); ids[ECHO] = cd_id_new(); cd_name_text(ids[ECHO]); cd_close();
  /* No listener: what is typed here only has to survive the rewrites. */
  ids[NOTE] = el("input", "note"); cd_close();
  ids[CHECK] = el("input", "check"); cd_attr("type", "checkbox"); cd_close();
  cd_open("span"); cd_attr("id", "styled"); cd_attr("style", "color:red;font-weight:bold;font-style:italic;text-decoration:underline");
  cd_text("styled"); cd_close();
  cd_open("div"); cd_attr("id", "hidden"); cd_attr("style", "display:none"); cd_text("hidden"); cd_close();
  cd_open("svg"); cd_attr("id", "pic"); cd_open("circle"); cd_attr("r", "4"); cd_close(); cd_close();
  cd_open("p"); cd_attr("id", "tickp"); cd_text("ticks "); cd_text_num(0); ids[TICK] = cd_id_new(); cd_name_text(ids[TICK]); cd_close();
  ids[LIST] = el("ul", "list"); cd_close();
  cd_open("p"); cd_attr("id", "esc"); cd_attr("title", ESC); cd_text(ESC); cd_close();
  cd_open("textarea"); cd_attr("id", "escta"); cd_text(ESC); cd_close();
  cd_open("select"); cd_attr("id", "escsel"); cd_open("option"); cd_text(ESC); cd_close(); cd_close();
  /* Two radios of one group: checking one unchecks the other. */
  ids[R1] = el("input", "r1"); cd_attr("type", "radio"); cd_attr("name", "grp"); cd_attr("value", "a"); cd_close();
  ids[R2] = el("input", "r2"); cd_attr("type", "radio"); cd_attr("name", "grp"); cd_attr("value", "b"); cd_close();
  /* A form of the app's, with a field in it and one after it: both must stay in the shim's one FORM. */
  cd_open("form"); cd_attr("id", "appform"); cd_open("input"); cd_attr("id", "inform"); cd_attr("value", "in"); cd_close(); cd_close();
  cd_open("input"); cd_attr("id", "afterform"); cd_attr("value", "after"); cd_close();
  cd_sel(ids[INC]); cd_listen(ids[INC], CD_EV_CLICK);
  cd_sel(ids[LINKS]); cd_listen(ids[LINKS], CD_EV_CLICK);
  cd_sel(ids[NAME]); cd_listen(ids[NAME], CD_EV_INPUT);
  cd_sel(ids[NAME]); cd_listen(ids[NAME], CD_EV_CHANGE);
  cd_sel(ids[NAME]); cd_listen(ids[NAME], CD_EV_FOCUS);
  cd_sel(ids[NAME]); cd_listen(ids[NAME], CD_EV_BLUR);
  cd_sel(ids[CHECK]); cd_listen(ids[CHECK], CD_EV_CHANGE);
  cd_sel(ids[R1]); cd_listen(ids[R1], CD_EV_CHANGE);
  cd_sel(ids[R2]); cd_listen(ids[R2], CD_EV_CHANGE);
}

void cd_on_event(const cd_event_rec *e) {
  cd_id id = (cd_id)e->id;
  if (e->type == CD_EV_CLICK && id == ids[INC]) cd_set_text_node_num(ids[COUNT], ++count);
  else if (e->type == CD_EV_CLICK && id == ids[LINKS]) clicks_path = (uint32_t)(e->depth << 16 | e->path);
  else if (e->type == CD_EV_INPUT && id == ids[NAME]) {
    inputs++;
    uint32_t n = cd_get_str(id, "value", io, sizeof io);
    cd_set_text_node(ids[ECHO], cd_tmp(io, n));
  } else if (e->type == CD_EV_CHANGE && id == ids[NAME]) changes++;
  else if (e->type == CD_EV_CHANGE && id == ids[CHECK]) checks = checks * 10 + 1 + (uint32_t)cd_get_num(id, "checked");
  else if (e->type == CD_EV_CHANGE && (id == ids[R1] || id == ids[R2]))
    radios = radios * 10 + (uint32_t)cd_get_num(ids[R1], "checked") + 2 * (uint32_t)cd_get_num(ids[R2], "checked");
  else if (e->type == CD_EV_FOCUS && id == ids[NAME]) focuses++;
  else if (e->type == CD_EV_BLUR && id == ids[NAME]) blurs++;
}

/* n more items on the list, each an LI with a short text: the redraw cost's page. */
EXPORT("grow") void grow(uint32_t n) {
  for (uint32_t i = 0; i < n; i++) {
    cd_at(ids[LIST], CD_APPEND);
    cd_open("li"); cd_text("item "); cd_text_num(items++); cd_close();
  }
  cd_at(CD_MOUNT, CD_APPEND);
  cd_flush();
}
/* One text changed: a redraw whose C costs next to nothing. */
EXPORT("touch") void touch(void) { cd_set_text_node_num(ids[COUNT], ++count); cd_flush(); }
/* Nothing changed: no redraw. */
EXPORT("noop") void noop(void) { cd_flush(); }
/* A timer whose callback changes the tree: a redraw from a timer. */
static uint32_t ticks;
static void tick(void *) { cd_set_text_node_num(ids[TICK], ++ticks); }
EXPORT("tick_after") void tick_after(uint32_t ms) { cd_timer_cb(ms, tick, nullptr); }
/* C sets the note field's value, and focuses the name field. */
EXPORT("set_note") void set_note(void) { cd_set_str(ids[NOTE], "value", "from C"); cd_flush(); }
EXPORT("focus_name") void focus_name(void) { cd_call(ids[NAME], "focus"); cd_flush(); }
/* select on a checkbox, which has no selection to set. */
EXPORT("select_check") void select_check(void) { cd_call(ids[CHECK], "select"); cd_flush(); }
/* A trap after the count was set and flushed: the call into C never returns to the shim, which must recover. */
EXPORT("trap") void trap(void) { cd_set_text_node_num(ids[COUNT], ++count); cd_flush(); __builtin_trap(); }
EXPORT("stat") uint32_t stat(uint32_t k) {
  uint32_t v[] = {count, clicks_path, inputs, changes, checks, focuses, blurs, ticks, items, radios};
  return k < sizeof v / sizeof v[0] ? v[k] : 0;
}
EXPORT("note_len") uint32_t note_len(void) { return cd_get_str(ids[NOTE], "value", io, sizeof io); }
