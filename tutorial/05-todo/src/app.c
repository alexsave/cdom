/* Lesson 5: the keyed list. The todos are a plain array of structs, and the list on the page shows the
 * ones the filter lets through. cd_list_update takes the ids to show, in order, and does what React does
 * when it reconciles: it creates a row for each new id, patches the rows that stayed, and destroys the rows
 * that went. A row that did not change is not written again. */
#include "cdom.h"
#include <stdio.h>
#include <string.h>


#define TODO_CAP 256
/* A field's value is copied out only for the length of one handler. A longer value is cut to the whole
 * characters that fit. */
#define TEXT_CAP 256

/* The model. An id names a todo for its whole life and is never reused, so a row whose id is gone can
 * never be mistaken for a newer todo. */
typedef struct { uint32_t id, len; char text[TEXT_CAP]; uint8_t done; } Todo;
static Todo todos[TODO_CAP];
static uint32_t ntodos;
static uint32_t next_id = 1;

enum { FILTER_ALL, FILTER_ACTIVE, FILTER_DONE };
static uint32_t filter = FILTER_ALL;

static cd_id form, input, list_ul, left_text, clear_btn;
static cd_id filter_btn[3];
static cd_list list;
static uint32_t keys[TODO_CAP]; /* the ids to show this render, in order; create reads them by index */

/* What is on the screen for one row: its checkbox's id (the row's own id is the cd_item's node), its remove
 * button's id, and whether the checkbox shows done. Rows get a slot from data, and a removed row gives its
 * slot back, the way the keyed bench keeps its views. */
typedef struct { cd_id check, remove; uint8_t done; } ItemView;
static ItemView views[TODO_CAP];
static uint32_t free_views[TODO_CAP], nfree_views, nviews;

static uint32_t view_new(void) {
  if (nfree_views) return free_views[--nfree_views];
  return nviews++;
}

static Todo *find(uint32_t id) {
  for (uint32_t i = 0; i < ntodos; i++) {
    if (todos[i].id == id) return &todos[i];
  }
  return 0;
}

static int shown(const Todo *t) {
  if (filter == FILTER_ACTIVE) return !t->done;
  if (filter == FILTER_DONE) return t->done;
  return 1;
}

static void remove_at(uint32_t i) {
  memmove(&todos[i], &todos[i + 1], (ntodos - i - 1) * sizeof(Todo));
  ntodos--;
}

static void on_check(const cd_event_rec *e, void *ctx);
static void on_remove(const cd_event_rec *e, void *ctx);

/* The three things React does for you when it reconciles, written out. cd_list_update calls them. */

/* create: a new id gets its whole row. cd_at is already set to the list, so the row lands in place.
 * The handlers are registered after the row closes, and their ctx is the todo's id, so an event finds its todo. */
static cd_id item_create(void *, uint32_t i, uint32_t *data) {
  Todo *t = find(keys[i]);
  uint32_t vi = view_new();
  ItemView *v = &views[vi];
  v->done = t->done;
  cd_id row = cd_open_id("li");
  cd_class(t->done ? "item done" : "item");
  v->check = cd_open_id("input"); cd_attr("type", "checkbox"); cd_close();
  cd_open("span"); cd_class("text"); cd_text(cd_tmp(t->text, t->len)); cd_close();
  v->remove = cd_open_id("button");
  cd_class("remove"); cd_attr("aria-label", "Remove"); cd_text("×");
  cd_close();
  cd_close();
  cd_set_bool(v->check, "checked", t->done);
  cd_on(v->check, CD_EV_CHANGE, on_check, (void *)(uintptr_t)t->id);
  cd_on(v->remove, CD_EV_CLICK, on_remove, (void *)(uintptr_t)t->id);
  *data = vi;
  return row;
}

/* patch: the row survived. Only what changed is written: here the class of the row and the checkbox, and
 * only when done flipped. The text of a todo never changes, so it is not compared. */
static void item_patch(void *, uint32_t, cd_item *it) {
  Todo *t = find(it->key);
  ItemView *v = &views[it->data];
  if (v->done == t->done) return;
  v->done = t->done;
  cd_set_class(it->node, t->done ? "item done" : "item");
  cd_set_bool(v->check, "checked", t->done);
}

/* destroy: the row's root is already removed from the page. Freeing the checkbox's and the button's ids
 * drops their handlers too, so a freed control can never send a click to a todo that is gone. */
static void item_destroy(void *, cd_item *it) {
  ItemView *v = &views[it->data];
  cd_id_free(v->check);
  cd_id_free(v->remove);
  free_views[nfree_views++] = it->data;
}

static const cd_list_fns TODO_FNS = {nullptr, item_create, item_patch, item_destroy};

/* The view of the model: the keys the filter lets through, then the footer's count. */
static void render(void) {
  uint32_t n = 0, left = 0;
  for (uint32_t i = 0; i < ntodos; i++) {
    if (shown(&todos[i])) keys[n++] = todos[i].id;
    if (!todos[i].done) left++;
  }
  cd_list_update(&list, keys, n, &TODO_FNS);

  /* The count changes with every edit. snprintf ends the buffer with a 0, so it is passed as it is. */
  char buf[32]; /* up to 10 digits, a space, "items left" (10), and the 0 */
  snprintf(buf, sizeof buf, "%u %s left", left, left == 1 ? "item" : "items");
  cd_set_text_node(left_text, buf);
}

/* ---- handlers ---- */

static int is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

/* The form's submit: Enter in the field, or a click on Add. The page would reload on a submit, so the form's
 * default is stopped and the todo is added here. */
static void on_submit(const cd_event_rec *, void *) {
  cd_prevent_default();
  char text[TEXT_CAP];
  uint32_t n;
  if (!cd_input_whole(input, text, sizeof text, &n)) n = cd_get_str(input, "value", text, sizeof text);

  uint32_t a = 0, b = n;
  while (a < b && is_space(text[a])) a++;
  while (b > a && is_space(text[b - 1])) b--;
  if (a == b || ntodos == TODO_CAP) return; /* nothing to add: the field keeps what was typed */

  /* Kept in the todo, unlike lesson 3's greeting. A todo's text lives as long as the todo, and the row is built
   * again whenever the filter changes, so the text is used many times. Each use is a cd_tmp over these bytes, so
   * nothing grows with what the user types and the bytes go when the todo does. */
  Todo *t = &todos[ntodos++];
  t->id = next_id++;
  t->len = b - a;
  for (uint32_t k = 0; k < t->len; k++) t->text[k] = text[a + k];
  t->done = 0;

  cd_set_str(input, "value", "");
  cd_call(input, "focus"); /* a click on Add moved the focus to the button; the next todo is typed here */
  render();
}

static void on_check(const cd_event_rec *, void *ctx) {
  Todo *t = find((uint32_t)(uintptr_t)ctx);
  if (!t) return;
  t->done = !t->done;
  render();
}

static void on_remove(const cd_event_rec *, void *ctx) {
  uint32_t id = (uint32_t)(uintptr_t)ctx;
  for (uint32_t i = 0; i < ntodos; i++) {
    if (todos[i].id == id) { remove_at(i); break; }
  }
  render();
}

static void on_filter(const cd_event_rec *, void *ctx) {
  uint32_t k = (uint32_t)(uintptr_t)ctx;
  filter = k;
  for (uint32_t i = 0; i < 3; i++) {
    cd_set_attr(filter_btn[i], "aria-pressed", i == k ? "true" : "false");
  }
  render();
}

static void on_clear(const cd_event_rec *, void *) {
  uint32_t kept = 0;
  for (uint32_t i = 0; i < ntodos; i++) {
    if (!todos[i].done) todos[kept++] = todos[i];
  }
  ntodos = kept;
  render();
}

static cd_id filter_button(const char *label, uint32_t k) {
  cd_id id = cd_open_id("button");
  cd_attr("aria-pressed", k == filter ? "true" : "false");
  cd_text(label);
  cd_close();
  return id;
}

static void style(void) {
  cd_open("style");
  cd_text(":root { color-scheme: light dark; font-family: system-ui, sans-serif; }"
          "body { margin: 0; }"
          "button { font: inherit; color: inherit; cursor: pointer; background: light-dark(#ffffff, #2a2a2e);"
          "  border: 1px solid light-dark(#8a8a8f, #6e6e73); border-radius: 0.375rem; }"
          "input:focus, button:focus-visible { outline: 2px solid light-dark(#1f5fd0, #8ab4ff); outline-offset: 1px; }"
          ".todo { max-width: 32rem; margin: 3rem auto; padding: 0 1rem; }"
          ".add { display: flex; gap: 0.5rem; }"
          ".add .new { flex: 1; min-width: 0; font: inherit; font-size: 1.125rem; padding: 0.5rem 0.75rem;"
          "  color: inherit; background: light-dark(#ffffff, #1c1c1e); border: 1px solid light-dark(#8a8a8f, #6e6e73);"
          "  border-radius: 0.375rem; }"
          ".add button { font-size: 1.125rem; padding: 0.5rem 1rem; }"
          ".items { list-style: none; margin: 1.5rem 0 0; padding: 0; }"
          ".item { display: flex; align-items: center; gap: 0.75rem; padding: 0.75rem 0.25rem;"
          "  border-bottom: 1px solid light-dark(#e5e5ea, #3a3a3e); }"
          ".item input { flex: none; width: 1.25rem; height: 1.25rem; margin: 0; accent-color: light-dark(#1f5fd0, #8ab4ff); }"
          ".item .text { flex: 1; min-width: 0; overflow-wrap: anywhere; font-size: 1.125rem; }"
          ".item.done .text { color: light-dark(#8a8a8f, #8e8e93); text-decoration: line-through; }"
          ".item .remove { flex: none; width: 2rem; height: 2rem; padding: 0; font-size: 1.25rem; line-height: 1;"
          "  color: light-dark(#5a5a60, #a1a1a8); background: none; border-color: transparent; }"
          ".item .remove:hover { color: light-dark(#c4302b, #ff8a85); }"
          "footer { display: flex; flex-wrap: wrap; align-items: center; gap: 0.5rem 0.75rem; margin-top: 1rem;"
          "  color: light-dark(#5a5a60, #a1a1a8); }"
          "footer .left { flex: 1; }"
          ".filters { display: flex; gap: 0.25rem; }"
          ".filters button, footer .clear { padding: 0.25rem 0.75rem; font-size: 0.9375rem; }"
          ".filters button[aria-pressed=true] { color: light-dark(#1f5fd0, #8ab4ff); border-color: light-dark(#1f5fd0, #8ab4ff);"
          "  font-weight: 600; }"
          "footer .clear { background: none; border-color: transparent; }");
  cd_close();
}

void cd_main(void) {
  style();

  cd_open("main"); cd_class("todo");

  form = cd_open_id("form"); cd_class("add");
  input = cd_open_id("input");
  cd_class("new");
  cd_attr("placeholder", "What needs doing?");
  cd_attr("aria-label", "New todo");
  cd_attr("autocomplete", "off");
  cd_close();
  cd_open("button"); cd_attr("type", "submit"); cd_text("Add"); cd_close();
  cd_close();

  /* The list's own element: it holds nothing but the items, so owns_parent lets a clear be one operation. */
  list_ul = cd_open_id("ul"); cd_class("items"); cd_close();
  list.parent = list_ul;
  list.owns_parent = 1;

  cd_open("footer");
  cd_open("span"); cd_class("left"); cd_text(""); cd_name_text(left_text = cd_id_new()); cd_close();
  cd_open("div"); cd_class("filters");
  filter_btn[0] = filter_button("All", FILTER_ALL);
  filter_btn[1] = filter_button("Active", FILTER_ACTIVE);
  filter_btn[2] = filter_button("Done", FILTER_DONE);
  cd_close();
  clear_btn = cd_open_id("button"); cd_class("clear"); cd_text("Clear done"); cd_close();
  cd_close();
  cd_close();

  /* The handlers are registered once the nodes they name are closed. */
  cd_on(form, CD_EV_SUBMIT, on_submit, nullptr);
  for (uint32_t k = 0; k < 3; k++) cd_on(filter_btn[k], CD_EV_CLICK, on_filter, (void *)(uintptr_t)k);
  cd_on(clear_btn, CD_EV_CLICK, on_clear, nullptr);

  render();
}
