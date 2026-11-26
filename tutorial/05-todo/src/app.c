/* Lesson 5: the keyed list. The todos are a plain array of structs, and the list on the page shows the
 * ones the filter lets through. cd_list_update takes the ids to show, in order, and does what React does
 * when it reconciles: it creates a row for each new id, patches the rows that stayed, and destroys the rows
 * that went. A row that did not change is not written again. */
#include "cdom_ui.h"
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

static cd_id form, input, list_ul, left_text;
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
 * The handlers' ctx is the todo's id, so an event finds its todo. */
static cd_id item_create(void *, uint32_t i, uint32_t *data) {
    Todo *t = find(keys[i]);
    uint32_t vi = view_new();
    ItemView *v = &views[vi];
    v->done = t->done;
    cd_id row = cd_open_id("li");
    cd_class(t->done ? "item done" : "item");
    v->check = cd_open_id("input"); cd_attr("type", "checkbox"); cd_close();
    cd_open("span"); cd_class("text"); cd_text(cd_tmp(t->text, t->len)); cd_close();
    /* cd_button (cdom_ui.h) opens the button, gives it its class and text, closes it and registers the click. */
    v->remove = cd_button("remove", "×", on_remove, CD_CTX_OF(t->id), nullptr);
    cd_set_attr(v->remove, "aria-label", "Remove");
    cd_close();
    cd_set_bool(v->check, "checked", t->done);
    cd_on(v->check, CD_EV_CHANGE, on_check, CD_CTX_OF(t->id));
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
static void render(void *) {
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
    cd_dirty(render, nullptr);
}

static void on_check(const cd_event_rec *, void *ctx) {
    Todo *t = find(CD_CTX_INDEX(ctx));
    if (!t) return;
    t->done = !t->done;
    cd_dirty(render, nullptr);
}

static void on_remove(const cd_event_rec *, void *ctx) {
    uint32_t id = CD_CTX_INDEX(ctx);
    for (uint32_t i = 0; i < ntodos; i++) {
        if (todos[i].id == id) { remove_at(i); break; }
    }
    cd_dirty(render, nullptr);
}

static void on_filter(const cd_event_rec *, void *ctx) {
    uint32_t k = CD_CTX_INDEX(ctx);
    filter = k;
    for (uint32_t i = 0; i < 3; i++) {
        cd_set_attr(filter_btn[i], "aria-pressed", i == k ? "true" : "false");
    }
    cd_dirty(render, nullptr);
}

static void on_clear(const cd_event_rec *, void *) {
    uint32_t kept = 0;
    for (uint32_t i = 0; i < ntodos; i++) {
        if (!todos[i].done) todos[kept++] = todos[i];
    }
    ntodos = kept;
    cd_dirty(render, nullptr);
}

static cd_id filter_button(const char *label, uint32_t k) {
    cd_id id = cd_button(CD_NONE, label, on_filter, CD_CTX_OF(k), nullptr);
    cd_set_attr(id, "aria-pressed", k == filter ? "true" : "false");
    return id;
}

/* The page's CSS is src/app.css. The build puts every .css file under src/ in a <style> in the page's
 * <head>, so the page is styled before this code runs. */

void cd_main(void) {
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
    cd_button("clear", "Clear done", on_clear, nullptr, nullptr);
    cd_close();
    cd_close();

    /* The buttons registered their own handlers (cd_button); the form is wired once it is closed. */
    cd_on(form, CD_EV_SUBMIT, on_submit, nullptr);

    cd_dirty(render, nullptr);
}
