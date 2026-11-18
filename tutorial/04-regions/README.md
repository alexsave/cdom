# 4. Show and hide: regions

A region is a group of nodes that C builds when it is shown and takes away when it is hidden.
The disclosure builds its section when it opens, and the tabs rebuild their panel on every change.

```sh
make -C tutorial/04-regions && open tutorial/04-regions/build/index.html
```

## The React version

```jsx
import { useState } from "react";

const CONTENT = {
  Overview: <p>Each tab builds its own panel. Only the selected tab's content is in the page.</p>,
  Usage: <p>Call <code>cd_region_show</code> to build a region, and <code>cd_region_hide</code> to take it away.</p>,
  Notes: <p>The panel has no diff. Its old nodes go and the new ones are built, which is a few nodes and cheap.</p>,
};

export default function Regions() {
  const [open, setOpen] = useState(false);
  const [tab, setTab] = useState("Overview");
  return (
    <main className="page">
      <div className="disclosure">
        <button className="toggle" aria-expanded={open} aria-controls="details" onClick={() => setOpen(!open)}>
          {open ? "Hide details" : "Show details"}
        </button>
        {open && (
          <section id="details" className="details">
            <h2>What a region is</h2>
            <p>A region is a part of the page that comes and goes. It is built when it is shown and taken away when it is hidden.</p>
            <p>Taking a region away removes its nodes and frees every id it recorded, so nothing is left behind.</p>
          </section>
        )}
      </div>
      <div role="tablist" className="tabs">
        {Object.keys(CONTENT).map((name) => (
          <button
            key={name}
            role="tab"
            aria-selected={tab === name}
            className={tab === name ? "tab active" : "tab"}
            onClick={() => setTab(name)}
          >
            {name}
          </button>
        ))}
      </div>
      <div className="panel" role="tabpanel">
        <h2>{tab}</h2>
        {CONTENT[tab]}
      </div>
    </main>
  );
}
```

## The cdom version

[`src/app.c`](src/app.c), the parts that matter:

```c
static cd_region details_region, panel_region;  /* shown says whether a region is built now */

static void on_toggle(const cd_event_rec *e, void *ctx) {
  int open = !details_region.shown;
  if (cd_region_show(&details_region, open)) {    /* 1: build the section now */
    build_details();                              /* cd_name_open(details_region.root) names it */
    cd_region_end(&details_region);
  }                                               /* 0 with open 0: the section is removed */
  cd_set_text_node(toggle_text, open ? cd_s("Hide details") : cd_s("Show details"));
  cd_set_attr(toggle, "aria-expanded", open ? cd_s("true") : cd_s("false"));
}

static void select_tab(uint32_t k) {
  ...
  cd_region_hide(&panel_region);                  /* show does nothing while the region is shown */
  if (cd_region_show(&panel_region, 1)) {
    build_panel(k);
    cd_region_end(&panel_region);
  }
}

void cd_main(void) {
  ...
  panel_region.parent = panel;                    /* the panel's region is all of the panel's children */
  cd_on(toggle, CD_EV_CLICK, on_toggle, 0);
  for (uint32_t k = 0; k < 3; k++) cd_on(tabs[k], CD_EV_CLICK, on_tab, (void *)(uintptr_t)k);
  select_tab(0);
}
```

## What to notice

- **Removal is real.** React unmounts `{open && <section>}`, and the section leaves the page.
  Here `cd_region_show` with 0 removes the section's node from the document, so `#details` is gone, not hidden with `display: none`.
- **The region frees its ids.** Every id made while a region builds is recorded by it.
  Taking the region away frees them, so an id is never left in the table after its node has gone.
  React's state is freed by the garbage collector; in cdom the ids are numbers that the app owns, so the region does that bookkeeping for it.
- **Rebuilding is hide and show.** React reconciles the panel: it compares the old and new elements and changes only what differs.
  cdom has no diff, so `select_tab` removes the panel's few nodes and builds the new ones.
  At this size that is cheap, and it is the simplest code that is correct.
- **No diff to reason about.** After a click the page holds exactly what the region's last build (or hide) made.
  Each state of the page is one build, so there is no reconciliation step that could keep stale nodes.
- **Two kinds of region.** The disclosure's region is one element, so it names its root and one remove takes it away.
  The panel's region is all of the panel's children, so the parent is the region's owner and one clear takes them away.
