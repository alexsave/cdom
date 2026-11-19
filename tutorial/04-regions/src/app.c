/* Lesson 4: regions, which come and go. A region is a group of nodes that C builds when it is shown and
 * takes away when it is hidden. Taking it away removes its nodes from the document, and frees every id
 * that was made while it was building: the region records those ids, so nothing is left behind. Nothing is
 * diffed: the disclosure builds its section when it opens, and the tabs rebuild their panel on each change. */
#include "cdom_ui.h"


/* Two regions, of the two kinds. The disclosure's region is one element (its root), so its parent stays 0.
 * The panel's region is all the children of the panel, so its parent is the panel, set once it exists.
 * shown says whether a region is built right now. */
static cd_region details_region, panel_region;

static cd_id disclosure, toggle, toggle_text, panel;
static cd_id tabs[3];
static uint32_t selected; /* 0 Overview, 1 Usage, 2 Notes */

/* The section is built into the disclosure, which holds only the button before it. A node appended there
 * lands right after the button, so the button is never moved and no placeholder node is needed. */
static void build_details(void) {
  cd_at(disclosure, CD_APPEND);
  cd_open("section"); cd_attr("id", "details"); cd_class("details");
  /* cd_name_open names the section as the region's root: hiding the region removes it in one step. */
  cd_name_open(details_region.root);
  cd_open("h2"); cd_text("What a region is"); cd_close();
  cd_open("p"); cd_text("A region is a part of the page that comes and goes. It is built when it is shown and taken away when it is hidden."); cd_close();
  cd_open("p"); cd_text("Taking a region away removes its nodes and frees every id it recorded, so nothing is left behind."); cd_close();
  cd_close();
}

/* The click flips the state: shown is 1 while the section is there, so the next state is its opposite.
 * cd_region_show returns 1 only when the region must be built now; with 0 it takes the section away. */
static void on_toggle(const cd_event_rec *, void *) {
  int open = !details_region.shown;
  if (cd_region_show(&details_region, open)) {
    build_details();
    cd_region_end(&details_region);
  }
  cd_set_text_node(toggle_text, open ? "Hide details" : "Show details");
  cd_set_attr(toggle, "aria-expanded", open ? "true" : "false");
}

/* One panel per tab. Any id made while a region builds (cd_id_new, cd_open_id) is recorded and freed with
 * the region; these nodes need none, so they have none. */
static void build_panel(uint32_t k) {
  cd_at(panel, CD_APPEND);
  if (k == 0) {
    cd_open("h2"); cd_text("Overview"); cd_close();
    cd_open("p"); cd_text("Each tab builds its own panel. Only the selected tab's content is in the page."); cd_close();
  } else if (k == 1) {
    cd_open("h2"); cd_text("Usage"); cd_close();
    cd_open("p");
    cd_text("Call ");
    cd_open("code"); cd_text("cd_region_show"); cd_close();
    cd_text(" to build a region, and ");
    cd_open("code"); cd_text("cd_region_hide"); cd_close();
    cd_text(" to take it away.");
    cd_close();
  } else {
    cd_open("h2"); cd_text("Notes"); cd_close();
    cd_open("p"); cd_text("The panel has no diff. Its old nodes go and the new ones are built, which is a few nodes and cheap."); cd_close();
  }
}

/* Selects tab k: the buttons say which one is current, then the panel is rebuilt. The panel is hidden
 * first because cd_region_show does nothing while the region is still shown. */
static void select_tab(uint32_t k) {
  selected = k;
  for (uint32_t i = 0; i < 3; i++) {
    cd_set_attr(tabs[i], "aria-selected", i == k ? "true" : "false");
    cd_set_class(tabs[i], i == k ? "tab active" : "tab");
  }
  cd_region_hide(&panel_region);
  if (cd_region_show(&panel_region, 1)) {
    build_panel(k);
    cd_region_end(&panel_region);
  }
}

/* The tab's index is the ctx. Clicking the tab that is already selected does nothing: its panel is built. */
static void on_tab(const cd_event_rec *, void *ctx) {
  uint32_t k = CD_CTX_INDEX(ctx);
  if (k != selected) select_tab(k);
}

static cd_id tab_button(const char *label) {
  cd_id id = cd_open_id("button");
  cd_attr("role", "tab");
  cd_text(label);
  cd_close();
  return id;
}

/* The page's CSS is src/app.css. The build puts every .css file under src/ in a <style> in the page's
 * <head>, so the page is styled before this code runs. */

void cd_main(void) {
  cd_open("main"); cd_class("page");

  /* The disclosure is a wrapper around the button. The section, while it is open, is its second child. */
  disclosure = cd_open_id("div"); cd_class("disclosure");
  toggle = cd_open_id("button");
  cd_class("toggle");
  cd_attr("aria-expanded", "false");
  cd_attr("aria-controls", "details");
  cd_text("Show details"); cd_name_text(toggle_text = cd_id_new());
  cd_close();
  cd_close();

  cd_open("div"); cd_attr("role", "tablist"); cd_class("tabs");
  tabs[0] = tab_button("Overview");
  tabs[1] = tab_button("Usage");
  tabs[2] = tab_button("Notes");
  cd_close();

  panel = cd_open_id("div"); cd_class("panel"); cd_attr("role", "tabpanel"); cd_close();
  cd_close();

  /* The panel's region holds all of the panel's children, so it names the panel as its parent. */
  panel_region.parent = panel;

  /* The handlers are registered once the nodes they name are closed. */
  cd_on(toggle, CD_EV_CLICK, on_toggle, nullptr);
  for (uint32_t k = 0; k < 3; k++) cd_on(tabs[k], CD_EV_CLICK, on_tab, CD_CTX_OF(k));

  select_tab(0);
}
