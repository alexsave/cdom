/* Lesson 4: regions, which come and go. A region is a group of nodes that C builds when it is shown and
 * takes away when it is hidden. Taking it away removes its nodes from the document, and frees every id
 * that was made while it was building: the region records those ids, so nothing is left behind. Nothing is
 * diffed: the disclosure builds its section when it opens, and the tabs rebuild their panel on each change. */
#include "cdom.h"

#define L(s) CD_LIT(s)

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
  cd_at(disclosure, 0);
  cd_open(L("section")); cd_attr(L("id"), L("details")); cd_class(L("details"));
  /* cd_name_open names the section as the region's root: hiding the region removes it in one step. */
  cd_name_open(details_region.root);
  cd_open(L("h2")); cd_text(L("What a region is")); cd_close();
  cd_open(L("p")); cd_text(L("A region is a part of the page that comes and goes. It is built when it is shown and taken away when it is hidden.")); cd_close();
  cd_open(L("p")); cd_text(L("Taking a region away removes its nodes and frees every id it recorded, so nothing is left behind.")); cd_close();
  cd_close();
}

/* The click flips the state: shown is 1 while the section is there, so the next state is its opposite.
 * cd_region_show returns 1 only when the region must be built now; with 0 it takes the section away. */
static void on_toggle(const cd_event_rec *e, void *ctx) {
  (void)e; (void)ctx;
  int open = !details_region.shown;
  if (cd_region_show(&details_region, open)) {
    build_details();
    cd_region_end(&details_region);
  }
  cd_set_text_node(toggle_text, open ? L("Hide details") : L("Show details"));
  cd_set_attr(toggle, L("aria-expanded"), open ? L("true") : L("false"));
}

/* One panel per tab. Any id made while a region builds (cd_id_new, cd_open_id) is recorded and freed with
 * the region; these nodes need none, so they have none. */
static void build_panel(uint32_t k) {
  cd_at(panel, 0);
  if (k == 0) {
    cd_open(L("h2")); cd_text(L("Overview")); cd_close();
    cd_open(L("p")); cd_text(L("Each tab builds its own panel. Only the selected tab's content is in the page.")); cd_close();
  } else if (k == 1) {
    cd_open(L("h2")); cd_text(L("Usage")); cd_close();
    cd_open(L("p"));
    cd_text(L("Call "));
    cd_open(L("code")); cd_text(L("cd_region_show")); cd_close();
    cd_text(L(" to build a region, and "));
    cd_open(L("code")); cd_text(L("cd_region_hide")); cd_close();
    cd_text(L(" to take it away."));
    cd_close();
  } else {
    cd_open(L("h2")); cd_text(L("Notes")); cd_close();
    cd_open(L("p")); cd_text(L("The panel has no diff. Its old nodes go and the new ones are built, which is a few nodes and cheap.")); cd_close();
  }
}

/* Selects tab k: the buttons say which one is current, then the panel is rebuilt. The panel is hidden
 * first because cd_region_show does nothing while the region is still shown. */
static void select_tab(uint32_t k) {
  selected = k;
  for (uint32_t i = 0; i < 3; i++) {
    cd_set_attr(tabs[i], L("aria-selected"), i == k ? L("true") : L("false"));
    cd_set_class(tabs[i], i == k ? L("tab active") : L("tab"));
  }
  cd_region_hide(&panel_region);
  if (cd_region_show(&panel_region, 1)) {
    build_panel(k);
    cd_region_end(&panel_region);
  }
}

/* The tab's index is the ctx. Clicking the tab that is already selected does nothing: its panel is built. */
static void on_tab(const cd_event_rec *e, void *ctx) {
  (void)e;
  uint32_t k = (uint32_t)(uintptr_t)ctx;
  if (k != selected) select_tab(k);
}

static cd_id tab_button(cd_str label) {
  cd_id id = cd_open_id(L("button"));
  cd_attr(L("role"), L("tab"));
  cd_text(label);
  cd_close();
  return id;
}

static void style(void) {
  cd_open(L("style"));
  cd_text(L(":root { color-scheme: light dark; font-family: system-ui, sans-serif; }"
            "body { margin: 0; }"
            ".page { max-width: 40rem; margin: 3rem auto; padding: 0 1rem; }"
            ".toggle { font: inherit; padding: 0.5rem 1rem; color: inherit; cursor: pointer;"
            "  background: light-dark(#ffffff, #1c1c1e); border: 1px solid light-dark(#8a8a8f, #6e6e73);"
            "  border-radius: 0.375rem; }"
            ".toggle:focus-visible, .tab:focus-visible { outline: 2px solid light-dark(#1f5fd0, #8ab4ff); outline-offset: 2px; }"
            ".details { margin-top: 1rem; padding: 1rem 1.25rem; border-radius: 0.5rem;"
            "  background: light-dark(#f2f2f5, #2a2a2e); }"
            ".details h2, .panel h2 { margin: 0; font-size: 1.25rem; }"
            ".details p, .panel p { margin: 0.5rem 0 0; color: light-dark(#3a3a40, #c7c7cc); }"
            ".tabs { display: flex; gap: 0.25rem; margin-top: 2rem; border-bottom: 1px solid light-dark(#c7c7cc, #3a3a3e); }"
            ".tab { font: inherit; padding: 0.5rem 1rem; color: light-dark(#5a5a60, #a1a1a8); cursor: pointer;"
            "  background: none; border: 0; border-bottom: 2px solid transparent; margin-bottom: -1px; }"
            ".tab.active { color: light-dark(#1c1c1e, #f2f2f5); border-bottom-color: light-dark(#1f5fd0, #8ab4ff);"
            "  font-weight: 600; }"
            ".panel { padding: 1rem 0; }"
            ".panel code { font-family: ui-monospace, monospace; font-size: 0.9em; padding: 0.1em 0.3em;"
            "  border-radius: 0.25rem; background: light-dark(#ececf0, #3a3a3e); }"));
  cd_close();
}

void cd_main(void) {
  cd_at(CD_MOUNT, 0);
  style();

  cd_open(L("main")); cd_class(L("page"));

  /* The disclosure is a wrapper around the button. The section, while it is open, is its second child. */
  disclosure = cd_open_id(L("div")); cd_class(L("disclosure"));
  toggle = cd_open_id(L("button"));
  cd_class(L("toggle"));
  cd_attr(L("aria-expanded"), L("false"));
  cd_attr(L("aria-controls"), L("details"));
  cd_text(L("Show details")); cd_name_text(toggle_text = cd_id_new());
  cd_close();
  cd_close();

  cd_open(L("div")); cd_attr(L("role"), L("tablist")); cd_class(L("tabs"));
  tabs[0] = tab_button(L("Overview"));
  tabs[1] = tab_button(L("Usage"));
  tabs[2] = tab_button(L("Notes"));
  cd_close();

  panel = cd_open_id(L("div")); cd_class(L("panel")); cd_attr(L("role"), L("tabpanel")); cd_close();
  cd_close();

  /* The panel's region holds all of the panel's children, so it names the panel as its parent. */
  panel_region.parent = panel;

  /* The handlers are registered once the nodes they name are closed. */
  cd_on(toggle, CD_EV_CLICK, on_toggle, 0);
  for (uint32_t k = 0; k < 3; k++) cd_on(tabs[k], CD_EV_CLICK, on_tab, (void *)(uintptr_t)k);

  select_tab(0);
}
