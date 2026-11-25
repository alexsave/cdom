/* The views of shared state cost an app only when it uses cd_state: built as is, this app shows and
 * hides a region and asks for a render (cd_dirty), but never watches or changes a state; with -DWATCH its
 * render is a view instead (cd_watch in the region's build, cd_changed in the click). run.sh checks the
 * second is larger by at least the views' pass and their takedown, so a change that links either into
 * every app that renders or hides a region (the volatile pointers in cdom.c dropped, or the takedown
 * called directly) fails here. */
#include "../../src/cdom.h"

static cd_id txt;
static uint32_t n;
static cd_region box;
#ifdef WATCH
static cd_state n_state;
#endif
static void view(void *) { cd_set_text_node_num(txt, n); }
static void box_show(void) {
    if (!cd_region_show(&box, 1)) return;
    cd_open("p"); cd_name_open(box.root);
    cd_text_num(n); cd_name_text(txt = cd_id_new());
    cd_close();
#ifdef WATCH
    cd_watch(&n_state, view, nullptr);
#endif
    cd_region_end(&box);
}
static void click(const cd_event_rec *, void *) {
    n++;
#ifdef WATCH
    cd_changed(&n_state);
#else
    cd_dirty(view, nullptr);
#endif
}
static void toggle(const cd_event_rec *, void *) {
    if (box.shown) cd_region_hide(&box); else box_show();
}
void cd_main(void) {
    cd_id b = cd_open_id("button"); cd_close();
    cd_on(b, CD_EV_CLICK, click, nullptr);
    cd_id t = cd_open_id("button"); cd_close();
    cd_on(t, CD_EV_CLICK, toggle, nullptr);
    box_show();
}
