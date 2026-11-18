# Shared state: views that follow data owned somewhere else

Status: design written 2026-11-18; approved by Alex the same day with the decisions in "The implementation", and implemented on this branch (`src/cdom.h`, `src/cdom.c`). The shim is unchanged.
Branch: `shared-state` (worktree `/Users/alex/Dev/cdom-shared-state`), from main at cfee569.
The prototypes, the tests and the benchmark are under `bench/shared-state/`; `bench/shared-state/run.sh` runs all of it.

What was built and run: the four badge apps, the `cd_state` prototype with its eight checks and four mutants, the benchmark app in six entries, the caret test.
What was not: the code in this file that uses the list design's calls (`cd_list_begin`, `cd_list_row`, `cd_list_reset`) or the in-core form of the API (the order by region depth, the tick that stops a second run, the build half of the check build).
Those are sketches, not compiled.

## The recommendation in one page

**cdom provides one small thing: `cd_state`, `cd_changed` and `cd_watch`.**
The data stays plain C, private to the module that owns it.

```c
/* name.c: the data and its one write path */
static char name[256]; static uint32_t name_len;
cd_state name_state;
void name_set(const char *p, uint32_t n) { memcpy(name, p, n); name_len = n; cd_changed(&name_state); }

/* badge.c: a view anywhere in the tree, while its region builds */
cd_watch(&name_state, badge_view, nullptr);
badge_view(nullptr);
```

1. **A state is a version and a list of watchers** (candidate E).
   `cd_changed` bumps the version and marks the state; the views are found in the flush and each runs once.
   A watch belongs to the region that made it and goes with it.
   `grep name_state` shows the whole wiring.
2. **A list is one view, and its rows are not views** (L).
   The list's render watches the collection and the shared states its rows show, and the pass skips unchanged rows by revision.
   A row's revision is the sum of the versions of what it shows.
3. **The version is candidate C's idea, kept as the thing revisions and cached derived values are made of**, not as the way views are found.
4. **Not recommended:** A (re-run everything) beyond a small app, B as written (it is quadratic), D (the writer names its readers) once `cd_watch` exists.

The earlier recommendation, C, was tested as a hypothesis.
On time it holds up: C is never slow.
It loses on what an app has to write and can get wrong: a seen version and a compare per state per view, a registration in a global list anyway, every view called on every change, no order between parent and child, and no way to check that the compare lists what the view reads.

**The measurements in three lines** (10,000 rows, script time per change, median of 5 passes):

| | A | B | C | E, rows as views | L, the list as one view |
| --- | ---: | ---: | ---: | ---: | ---: |
| every row changes (S1) | 2.10 ms | 27.4 ms | 1.77 ms | 1.89 ms | 1.71 ms |
| two rows change (S2) | 0.364 ms | 25.6 ms | 0.031 ms | 0.063 ms | 0.038 ms |
| one row changes (S3) | 0.363 ms | 0.0003 ms | 0.032 ms | 0.0004 ms | 0.039 ms |
| a keystroke nobody shows (S8) | 0.364 ms | 0.0003 ms | 0.031 ms | 0.0003 ms | 0.0003 ms |
| bytes a row beyond the model (S7) | 113 | 76 | 32 | 131 | 0 |

**Two of my own predictions were refuted**, and they are recorded as such under "Measurements": L is not within 1.2 of the best entry when one row changes (a view per row with a state per row is 100 times faster, at 131 bytes a row), and C beats L by 0.007 ms when two rows change.
Neither changes the recommendation, and "What the numbers changed" says why.

## What was asked

Alex's words: "Deep in a tree of regions, nested through a few different files, a textbox exists.
Somewhere completely different in the tree, nested as well, a view showing the value of the textbox also exists.
We need to update that view when we edit the textbox."

The server analogy: requests of any shape (a textbox anywhere) are routed to the right database row (the state), with hooks, and the row is read by views anywhere, which format it.

The constraints:

- Explicit watchers are fine: "we're not trying to replicate React, I'm just trying to make it reasonable for C to handle a complex DOM."
- No hidden wiring: no injection, no annotations, no registry that wires by name, no dependency tracking that a reader cannot grep.
  Explicit calls and plain data, so that a reader can follow where everything comes from.
- No backwards compatibility: cdom's API may change freely.

## What cdom has today, and where it stops

| Piece | What it does | Where it stops |
| --- | --- | --- |
| `cd_dirty(fn, ctx)` | a view runs once in the flush that ends the entry, however often it was asked | the asker must know the view: its function and its ctx. Dedup is a scan of the queue, so n distinct asks cost n squared over two compares: 0.27 ms for 1,000 and 25.6 ms for 10,000 (S12) |
| `cd_undirty(fn, ctx)` | drops a pending ask | the owner's cleanup must call it for each view, by hand (amgi's "two guards", `app.h:56`) |
| `cd_bound` | a node property that skips the write when the value is the same | 36 bytes and a copy of the bytes: for views, not for rows, and not for a megabyte |
| regions, `cd_on_free` | a view's lifetime, and where its cleanups hang | nothing ties a view to the data it draws from |
| amgi's cross-module calls | `deck_decks_changed()`, `form_langs_changed()`, `template_mode_changed()`: the writer calls each reader by name | the writer lists its readers, and a reader that exists many times (a row) has no name |

So today the textbox's file must include the badge's file, or both must go through a third that names the badge.
That is the missing piece: a place where a view says "I draw from this data", owned by the data, cleaned up with the view's region.

## The candidates

The same small app is built for each (`bench/shared-state/badge/`): `main.c` builds `header > nav > [badge region]` and `section > form > [profile region]`, the badge is in `badge.c`, the textbox in `profile.c`, and neither file includes the other.
`node drive.mjs $PWD` types "Ada" into the textbox in headless Chrome and reads the badge: all of them go from "Hi, " to "Hi, Ada".

### A. Re-run every live view

State is plain globals.
The app writes one `views.c`: a list of the live view functions, each added while its region builds and dropped by a `cd_on_free` cleanup.
Any writer calls `views_changed()`, which is one `cd_dirty(run_all, nullptr)`.
Every view formats every time, and writes through a `cd_bound`, so a text that came out the same costs no DOM write.

```c
/* state.h */
extern char name[NAME_CAP];  /* plain globals: anyone may read them, and a writer calls views_changed() */
extern uint32_t name_len;

/* profile.c, the textbox */
static void on_input(const cd_event_rec *, void *) {
  if (!cd_input_whole(input, name, sizeof name, &name_len))
    name_len = cd_get_str(input, "value", name, sizeof name);
  views_changed();
}

/* badge.c, the view */
static cd_bound hi;
static void badge_view(void) {
  char room[4 + NAME_CAP];
  cd_buf b = cd_buf_in(room, sizeof room);
  cd_buf_putz(&b, "Hi, ");
  cd_buf_put(&b, name, name_len);
  cd_bound_set_bytes(&hi, b.p, b.len);
}
void badge_show(void) {
  ...
  cd_bind_text(&hi, t);
  view_add(badge_view);
  badge_view();
  cd_region_end(&badge_region);
}
```

It is the least code per view and it cannot miss a dependency: a view never says what it reads.
Its cost is that every view formats on every change of anything, and that any writer, anywhere, can forget `views_changed()`, because the state is global.

### B. The data's owner keeps explicit watchers

`name.c` owns the bytes (file-static), and `name_set()` calls `cd_dirty` for each watcher.
`name_watch(fn, ctx)` is called while a region builds and removed, with `cd_undirty`, by a cleanup.

```c
/* name.c */
static char name[NAME_CAP];
static uint32_t name_len;
static struct { cd_render_fn fn; void *ctx; } watch[WATCH_CAP];

void name_set(const char *p, uint32_t len) {
  memcpy(name, p, len);
  name_len = len;
  for (uint32_t i = 0; i < WATCH_CAP; i++) if (watch[i].fn) cd_dirty(watch[i].fn, watch[i].ctx);
}
static void unwatch(void *slot) {
  uint32_t i = CD_CTX_INDEX(slot);
  cd_undirty(watch[i].fn, watch[i].ctx);
  watch[i].fn = nullptr;
}
void name_watch(cd_render_fn fn, void *ctx) {
  uint32_t i = 0;
  while (watch[i].fn) i++;
  watch[i].fn = fn;
  watch[i].ctx = ctx;
  cd_on_free(nullptr, unwatch, CD_CTX_OF(i));
}

/* badge.c */
static void badge_view(void *) { ...format...; cd_set_text_node(hi, cd_tmp(b.p, b.len)); }
void badge_show(void) {
  ...
  name_watch(badge_view, nullptr);
  badge_view(nullptr);
  cd_region_end(&badge_region);
}
```

The wiring reads well: `grep name_watch` lists every view of the name, and only `name.c` can write the bytes, so only one function can forget to notify.
What is wrong with it as written is everything under the wiring: 20 lines of table per field, repeated in every data module; a fixed `WATCH_CAP` that the code does not check (the 17th watcher writes past the table); a linear search for a free slot; and `cd_dirty`'s scan per watcher per set.

### C. Versions: the database row with an ETag

An owner module as in B, but with a version counter bumped on every set, and A's one list of live views.
Each view compares the version it drew with the current one before it formats: "304 Not Modified".

```c
/* name.c */
static uint32_t version;
uint32_t name_version(void) { return version; }
void name_set(const char *p, uint32_t len) {
  memcpy(name, p, len);
  name_len = len;
  version++;
  views_changed();
}

/* badge.c */
static uint32_t seen; /* the version this badge last drew */
static void draw(void) { seen = name_version(); ...format...; cd_set_text_node(hi, cd_tmp(b.p, b.len)); }
static void badge_view(void) { if (seen != name_version()) draw(); }
void badge_show(void) {
  ...
  view_add(badge_view);
  draw();
  cd_region_end(&badge_region);
}
```

A view that reads three fields keeps three seen versions and writes a three-way compare, and the compare must list exactly what the draw reads.
It is still one global list: every view is called on every change of anything.

### D. The writer calls its readers by name (amgi today)

No registration at all: `name_set()` ends in `badge_name_changed();`, and the badge guards it with its own `shown` flag and drops a pending ask in its cleanup.

```c
/* name.c */
void name_set(const char *p, uint32_t len) { ...; badge_name_changed(); title_name_changed(); }

/* badge.c */
static int shown;
void badge_name_changed(void) { if (shown) cd_dirty(badge_view, nullptr); }
static void badge_gone(void *) { shown = 0; cd_undirty(badge_view, nullptr); }
```

The linker checks it: delete the badge and `name.c` does not link.
But the data module includes every view of it, adding a view means editing the data, each view writes the two guards by hand, and a view that exists many times (a row, a card, the same panel in two places) has no name to call.
It is the right amount of machinery for amgi's three calls, and it is what the others replace.

### E. `cd_state`: a version and a watcher list, provided by cdom

B's wiring and C's version, with the 20 lines of table written once, in cdom, where the region's takedown and the render queue are.

```c
/* name.h */
#define NAME_CAP 256
extern cd_state name_state;                   /* what a view of the name watches */
const char *name_get(uint32_t *len);
void name_set(const char *p, uint32_t len);

/* name.c: the whole module */
static char name[NAME_CAP];
static uint32_t name_len;
cd_state name_state;

const char *name_get(uint32_t *len) { *len = name_len; return name; }
void name_set(const char *p, uint32_t len) {
  memcpy(name, p, len);
  name_len = len;
  cd_changed(&name_state);
}

/* badge.c */
static void badge_view(void *) {
  uint32_t n;
  const char *p = name_get(&n);
  char room[4 + NAME_CAP];
  cd_buf b = cd_buf_in(room, sizeof room);
  cd_buf_putz(&b, "Hi, ");
  cd_buf_put(&b, p, n);
  cd_set_text_node(hi, cd_tmp(b.p, b.len));
}
void badge_show(void) {
  if (!cd_region_show(&badge_region, 1)) return;
  cd_open("span"); cd_name_open(badge_region.root);
  cd_text(""); cd_name_text(hi = cd_id_new());
  cd_close();
  cd_watch(&name_state, badge_view, nullptr);   /* this region draws badge_view from the name */
  badge_view(nullptr);                          /* and draws it now, from the name as it is */
  cd_region_end(&badge_region);
}

/* profile.c: the textbox, unchanged from B */
static void on_input(const cd_event_rec *, void *) {
  char buf[NAME_CAP];
  uint32_t n;
  if (!cd_input_whole(input, buf, sizeof buf, &n)) n = cd_get_str(input, "value", buf, sizeof buf);
  name_set(buf, n);
}
```

Read as the server analogy: `name.c` is the table, `name_set` is the one write path and `cd_changed` its commit hook, `name_state.ver` is the row's ETag, `cd_watch` is a subscription that lives as long as the region that asked, and `badge_view` is the handler that formats the row for one client.

- `grep name_state` finds the one `cd_changed` and every `cd_watch`: the whole wiring of the name, in the order a reader wants it.
- The bytes are file-static, so the compiler sends every writer through `name_set`.
- The watch goes with the region, with any run of the view still pending: amgi's two guards, owned once.
- `cd_changed` costs the same with no watcher or ten thousand: it bumps the version and puts the state on a list of changed states.
  The views are found in the flush.
  So a thousand sets in one event are a thousand increments.
- A view that watches several states runs once.

The prototype is `bench/shared-state/lib/cd_state.c`, about 90 lines on cdom's public API.

### L. A list is one view, and its rows carry versions

Not a sixth mechanism: it is how E meets `docs/list-design.md`.
A row is not a view.
The list's render is the view: it watches the collection and every shared state a row shows, and it runs the list's pass.
Each row's revision is made from the versions of what the row shows, so the pass skips every row that did not change with one compare.

```c
/* prices.c: 10,000 rows, each a price in the shared currency, one of them selected */
static void rows_view(void *) {
  cd_list_begin(tbody, CD_LIST_ALL);
  for (uint32_t i = 0; i < nrows; i++) {
    const Row *r = &rows[i];
    uint64_t rev = (r->rev + currency_state.ver) << 1 | (r->id == selected);
    cd_id root;
    uint64_t was;
    switch (cd_list_row_patch(r->id, rev, &root, &was)) {
      case CD_ROW_BUILD: cd_list_row_end(row_build(r)); break;
      case CD_ROW_PATCH: row_patch(r, root, was ^ rev); break;   /* the text if a high bit moved, the class if bit 0 did */
    }
  }
  cd_list_end();
}
void prices_show(void) {
  ...
  cd_watch(&rows_state, rows_view, nullptr);       /* a row was added, removed or changed */
  cd_watch(&currency_state, rows_view, nullptr);   /* every row shows the currency */
  cd_watch(&selected_state, rows_view, nullptr);   /* two rows show the selection */
  rows_view(nullptr);
  cd_region_end(&prices_region);
}
```

Two rules make the revision easy to get right:

1. **Versions only go up, so their sum is a version.**
   `r->rev + currency_state.ver` changes when, and only when, the row or the currency changed.
   No hash, no pair, no collision.
2. **A selection is a value, not a version.**
   `selected_state.ver` in the sum would redraw 10,000 rows per click.
   `r->id == selected` in the revision redraws the two rows whose answer changed.
   The rule: put in the revision what the row shows, at the grain the row shows it.

The same case with rows as views is what the benchmark compares L with.
This is entry E's row (`bench/shared-state/bench/src/entry_e.c`), the fastest of them when one row changes:

```c
typedef struct {
  cd_id tr, txt;
  uint32_t idx;
  cd_state st;                                  /* the row's own field */
  cd_watcher w[3];                              /* the currency, the selection, its own state */
  uint32_t drawn_cur, drawn_price, drawn_sel;   /* what it drew, to write only what changed */
} row_view_t;                                   /* 140 bytes a row */

static void row_view(void *ctx) { ...compare the three drawn values, write the text or the class... }
static cd_id row_create(void *, uint32_t i, uint32_t *) {
  ...
  cd_watch_at(&r->w[0], &currency_state, row_view, r, nullptr);
  cd_watch_at(&r->w[1], &selected_state, row_view, r, &r->w[0]);
  cd_watch_at(&r->w[2], &r->st, row_view, r, &r->w[0]);
  return r->tr;
}
static void row_destroy(void *, cd_item *it) { ...three cd_unwatch, in reverse... }
```

Against L it is a second copy of what the row shows, three links to make and break per row, and a view function that must agree with the row's build.

### Candidates that lose before measurement

| Candidate | Why not |
| --- | --- |
| a dirty bitset: one bit per state in a shared enum, a mask per view | C with masks: a central header that names every state (a registry by another name, and a recompile of everything per state), 32 or 64 states, and no "seen" that survives between passes, so a list's rows cannot use it |
| a `cd_state` per row, and each row a view that watches it (entry E in the benchmark) | the fastest for one changed row (0.0004 ms against L's 0.04 at 10,000 rows), and 131 bytes a row against 0; see "What the numbers changed" |
| signals: a read inside a view subscribes it | the dependency tracking Alex ruled out: nothing at the view says what it follows |
| publish and subscribe by name or by topic number | a registry that wires by name: the compiler cannot check a topic, and `grep` finds strings |
| hooks that run inside the setter (synchronous observers) | a view would draw half-updated state when two setters are called in a row, and a hook that sets state recurses; the flush is the one place where state is whole |
| batching per frame inside cdom | the entry is already the batch; a stream that sets state in a thousand separate entries a frame is the data module's to pace (see the edge cases) |

## The edge cases

"E" below means E for views and L for rows, the pair this design recommends.

### Many views of one field

| Case | A | B | C | E |
| --- | --- | --- | --- | --- |
| 10,000 rows show one currency, and it changes | 10,000 formats and 10,000 writes; the writes dominate | the same writes, after 10,000 `cd_dirty` scans | 10,000 compares, then the writes | one view runs the pass; 10,000 compares, then the writes |
| 10,000 rows, the selection moves (2 rows change) | 10,000 formats, 2 writes | 10,000 asks if rows watch the selection | 10,000 compares, 2 writes | 10,000 compares in the pass, 2 writes |
| the bookkeeping a row costs | a `cd_bound` and a copy of its bytes | three table slots | a seen version | nothing |

Nobody escapes one step per row for the selection without an index from key to row.
The step is a compare, and 10,000 of them cost 0.02 to 0.04 ms (S2, S13).

### One view of many fields, derived values, chains

- **One view, many fields.**
  One `cd_watch` line per state.
  The view runs once, however many of them changed.
- **A derived value** (a total from the items and the tax rate) is a module with three small functions and no state of cdom's:

  ```c
  /* total.c */
  static uint64_t made_from;   /* the versions the cached total was made from */
  static uint32_t cached;
  uint64_t total_ver(void) { return items_state.ver + tax_state.ver; }
  uint32_t total_get(void) {
    if (made_from != total_ver()) { cached = compute(); made_from = total_ver(); }
    return cached;
  }
  void total_watch(cd_render_fn fn, void *ctx) {   /* a view of the total watches what the total is made from */
    cd_watch(&items_state, fn, ctx);
    cd_watch(&tax_state, fn, ctx);
  }
  ```

  The value is computed on read and cached by the sum of its inputs' versions, so it is never stale and never computed twice.
  A view calls `total_watch(view, ctx)`, and a row puts `total_ver()` in its revision.
- **A chain** (a derived of a derived) is the same again: `grand_watch` calls `total_watch` and watches its own extra inputs, and `grand_ver()` adds `total_ver()`.
  A view is asked for once, directly by the leaf states, so there is no order in which derived values must be refreshed, and no moment at which a view can see a new input with an old total.
- **A cycle among derived values** is two `_watch` functions that call each other: it overflows the stack the first time a view is built, which is loud and immediate.

### Cycles and order among views

- **A view that sets state.**
  The state goes on the list of changed states, and its views run in the same flush, as another round.
  Past 16 rounds it traps, as `cd_dirty` does today.
- **A view never runs twice for one change.**
  If a view already ran after the last change of every state it watches, the next round skips it.
  cdom keeps one tick counter: a state remembers the tick of its last change and a view the tick of its last run.
  The prototype does not have this and may run a view a second time.
- **A parent's view runs before a child's.**
  The case this is for: a detail panel shows the selected item; the item is deleted and the selection cleared in one event.
  The panel's view, run first, would read an item that is gone.
  The parent's view, run first, hides the panel, and the panel's watch goes with its region before it ever runs.
  So within a round, views run outermost region first (by the depth of the region that registered them, then in the order they were registered).
  With that, "my region is shown" is a promise a view may rely on: its parent has already seen the state.
  This needs the region's depth, so it is in the API sketch and not in the prototype, which runs views in the order their states changed.

### Large data

| Case | What happens | Rule |
| --- | --- | --- |
| a 1 MB text mirrored elsewhere | the read and the write each cross a megabyte; a view that is not asked costs nothing | never put big data behind a `cd_bound` (A): it keeps a second megabyte and compares it on every run of every change (S10, S11) |
| a collection of 10,000 to 100,000 rows, one row changes | the row's setter bumps `r->rev` and calls `cd_changed(&rows_state)`; the pass compares every row and writes one | one `cd_state` for the collection (who to call), a `rev` in each row (what to do). A version for the collection alone would redraw every row; a `cd_state` and three watchers per row measured 131 bytes a row (S7) |
| a detail view of one row, somewhere else in the tree | it watches `rows_state` and compares the row's `rev` with the one it drew | a coarse watch and a fine check: being asked too often costs a compare |
| 1,000 setters in one event (a bulk import) | 1,000 increments and one pass | nothing to do: `cd_changed` does not look at the watchers |
| a setter per frame (a drag, a timer) | one pass a frame, of the views that watch that state | nothing to do |
| a setter per message, a thousand messages a frame in separate entries | a thousand passes | the data module paces it: it stores every message and calls `cd_changed` once from its own `cd_raf_request` callback. One line in the module that knows the stream, no flag in cdom |
| more rows than a page should hold | the pass is over the rows that are shown | a windowed list passes the window; the model can be any size |

### Large DOM

Thousands of live views, most of them unaffected by a change:

- A formats all of them.
- C calls all of them, and each returns after a compare.
- B and E call the affected ones.

Measured (S4, 1,000 views, 10 of them affected): A 0.050 ms, C 0.0045 ms, B and E 0.002 ms.
All of them are under 1% of a frame, so at this size time does not choose.
The memory is one watcher per view and state in E (32 bytes in the prototype), a list slot and a seen version in C, a `cd_bound` in A.

### Lifetimes

| Case | E's answer | Covered by |
| --- | --- | --- |
| a view's region is hidden while a run of the view is pending | the watch is unlinked in the region's takedown and the pending run is dropped with it; the app writes nothing | test 2 |
| a view hides the region of a view queued behind it | the same: the second never runs | test 3 |
| a view shows a region (a build inside the pass) | the new region's build draws from the state as it is and is not run again for the change being processed | test 4 |
| a region is rebuilt and must show current state | the rule is in the build: `cd_watch(...)`, then call the view once. `cd_watch` never calls the view itself: a view of two states would draw twice, and the build may not have named its nodes yet | test 6 |
| a watcher in a keyed-list row, created and destroyed at 10,000 | rows do not watch (L). A region inside a row (an open editor) may: it is a region, built in the row's build, and goes with the row | S7, S9 |
| an answer arrives after its view went | the data module owns the request (`cd_req`), not the view: the answer updates the data and calls `cd_changed`, which has no watcher to call. A request that only a view wants is dropped in that region's cleanup (`cd_req_drop`), as today | the URL case |
| a state's memory goes while views watch it (a state in a heap struct) | a state lives as long as its watchers: keep states static, or in a struct the region that watches owns. cdom traps on `cd_changed` of a state it can tell was freed only in a check build | rule |

### A textbox that watches its own field

A controlled input: the textbox writes the name, and it also shows the name, so that a "reset" button or a loaded profile changes what it holds.
Its view must not write back the value the user just typed.

```c
/* profile.c */
static cd_bound shown_value;                      /* what the input holds, as far as C knows */

static void on_input(const cd_event_rec *, void *) {
  char buf[NAME_CAP];
  uint32_t n;
  if (!cd_input_whole(input, buf, sizeof buf, &n)) n = cd_get_str(input, "value", buf, sizeof buf);
  cd_bound_shows_bytes(&shown_value, buf, n);     /* the input shows these bytes: the user put them there */
  name_set(buf, n);
}
static void field_view(void *) {
  uint32_t n;
  const char *p = name_get(&n);
  cd_bound_set_bytes(&shown_value, p, n);         /* writes only when the name is not what the input holds */
}
```

- Typing: the handler records what the input shows, the setter stores the same bytes, the view compares and writes nothing.
  The caret, the selection and an open IME composition are not touched, because nothing crosses.
- A reset from elsewhere: the bytes differ, the view writes, and the caret goes to the end, which is right for a value the user did not type.
- A setter that changes the value (upper case, a trim): the bytes differ and the view writes on every keystroke, with the caret at the end.
  That is the app's choice to normalise while typing, and the fix is the usual one: normalise on blur or on submit.

Measured in headless Chrome 157 (S14): the unguarded form, a view that writes the same value back with `cd_set_str` on every keystroke, leaves the caret where it was and does not end an IME composition.
So in Chrome the guard is not a correctness requirement.
It saves one crossing per keystroke (8 writes against 0 in the test), and it is the safe form for the browsers that were not measured (Safari, Firefox, the IE tier).
A setter that changed the value (lower case typed, upper case stored) moved the caret to the end, as predicted.

The guard needs two small additions to `cd_bound`: a binding for a string property (`cd_bind_prop(&b, input, "value")`, written with `cd_set_str`), and `cd_bound_shows_bytes`, the bytes form of `cd_bound_shows`.
Without them the guard is a `memcmp` against a copy the handler keeps, which is five lines per input.

### Failure modes

| Mistake | A | B | C | D | E and L | What catches it |
| --- | --- | --- | --- | --- | --- | --- |
| a writer forgets to notify | stale; any file can write a global | stale; one setter per field can forget | stale; two lines per setter to forget (the bump and the ask) | stale | stale; one line in the one function that can write | E, B, C: the data is file-static, so the compiler sends writers through the setter, and a test of the setter covers every writer. A: nothing |
| a view forgets to register or watch | stale | stale | stale | does not link when the view is gone; stale when the call was never added | stale, after a correct first draw | a test that changes the state and reads the view. In E, the check build (below) traps |
| a view watches or compares less than it reads | cannot happen | stale | stale (an incomplete compare) | stale | stale | E: the check build traps on the read |
| a view watches more than it reads | n/a | slow (an extra run) | slow | slow | slow; never wrong | nothing needed |
| a version check is missing entirely | n/a | n/a | slow, not wrong | n/a | n/a | a count of DOM writes in a test |
| a row's revision leaves out something the row shows | n/a | n/a | n/a | n/a | stale row, though the pass ran | a test; this is the one place where E and L ask for the same fact twice (the watch and the revision) |
| a selection put in the revision as a version | n/a | n/a | n/a | n/a | slow: every row redrawn per click | a count of DOM writes in a test |

Stale is wrong; slow is extra work.
A is the only candidate in which a view cannot be stale through its own fault, and the only one in which any file can make every view stale.
In E the two stale mistakes of a view are both "the watch list is shorter than the read list", and that is checkable:

**The check build.**
With `-DCD_STATE_CHECK`, a getter calls `cd_read(&its_state)`, and cdom traps when a view running in the flush reads a state it does not watch.
In cdom itself the same check can cover a build: a state read while a region builds must be watched by that region or one around it when the build ends.
It is an assertion over the explicit watches, compiled out of a release build; nothing is wired by it.
The prototype has the first half (test 8).
Whether a read-tracking assertion is welcome at all is Alex's call (see "Decisions").

## The URL case: type a URL, press Load, show the results as a big list

A text input holds a URL.
Load fetches it and shows the answer as a list of rows.
Changing the URL and loading again replaces the list.

### Two states behind one input

| State | Changes | Owner | Watched by |
| --- | --- | --- | --- |
| the draft: what is typed and not yet loaded | per keystroke | the `<input>` itself. C reads it once, when Load is pressed | nothing, so it is not a `cd_state` at all. If something does show it (a Load button that is disabled while it is empty), it becomes one, with that one watcher |
| the results: the loaded URL, the request, the rows or the error | on Load and on the answer | `results.c` | the status line and the list |

Typing costs the list nothing in B, E and L, because nothing the list watches changed and nothing is called.
In C the list's view is called and returns after one compare.
In A every keystroke runs every view, which with rows as views is 10,000 formats (S8).

### The code E makes the app write

```c
/* results.h */
enum { RESULTS_IDLE, RESULTS_LOADING, RESULTS_OK, RESULTS_FAILED };
typedef struct { uint32_t at, len; } Result;      /* a row: its bytes are a slice of the answer */
extern cd_state results_state;
extern uint32_t results_status, results_n, results_load;   /* results_load: which load the rows are from */
extern Result *results;
const char *result_bytes(const Result *r);
void results_load_url(const char *url, uint32_t len);

/* results.c */
static cd_req req;
static void on_answer(void *ctx, uint32_t status, const uint8_t *p, uint32_t len) {
  if (!cd_req_current(&req, ctx, nullptr)) return;   /* the answer to an older Load: dropped */
  cd_req_end(&req, status == 200);
  if (status == 200) {
    parse(p, len);            /* keeps the bytes once, fills results[] with slices */
    results_load++;           /* new rows: nothing of the last load is the same thing */
  }
  results_status = status == 200 ? RESULTS_OK : RESULTS_FAILED;
  cd_changed(&results_state);
}
void results_load_url(const char *url, uint32_t len) {
  results_status = RESULTS_LOADING;
  cd_http_cb(on_answer, cd_req_begin(&req, 0), "GET", 3, url, len, nullptr, 0, nullptr, 0, 30000);
  cd_changed(&results_state);
}

/* search.c: the input and the button */
static void on_load(const cd_event_rec *, void *) {
  char url[URL_CAP];
  uint32_t n;
  if (cd_input_whole(input, url, sizeof url, &n)) results_load_url(url, n);
}

/* list.c: the status line and the rows, anywhere in the tree */
static uint32_t drawn_load;
static void list_view(void *) {
  cd_bound_set(&status, STATUS_TEXT[results_status]);    /* "", "Loading", "", "Could not load" */
  if (drawn_load != results_load) {                      /* another load: other rows, whatever their keys */
    cd_list_reset(ul);
    drawn_load = results_load;
  }
  cd_list_begin(ul, CD_LIST_ALL);
  for (uint32_t i = 0; i < results_n; i++)
    if (cd_list_row(i, highlight_state.ver)) cd_list_row_end(result_build(&results[i]));
  cd_list_end();
}
void list_show(void) {
  ...
  cd_watch(&results_state, list_view, nullptr);
  cd_watch(&highlight_state, list_view, nullptr);   /* a shared field every row shows */
  drawn_load = results_load;
  list_view(nullptr);
  cd_region_end(&list_region);
}
```

### What each part answers

- **An old answer after a newer Load.**
  `cd_req`'s generation: `cd_req_current` is false, and the answer is dropped before it touches the data.
  This is the data module's, written once, and no view knows about it.
- **The list's region is hidden while a fetch is out.**
  The request belongs to the data, so the answer still lands in `results`, and `cd_changed` finds no watcher.
  When the region is shown again its build draws the results as they are.
  Nothing is lost and nothing is drawn into nodes that went.
  (If results are only worth having while they are shown, the region's cleanup calls a `results_cancel()` that does `cd_req_drop`.)
- **Loading and error states** are fields of the same state, set before the same `cd_changed`, and drawn by the same view.
  The old rows stay on screen while the new load is out, which is the choice a list should make by default; clearing them at once is `results_n = 0` in `results_load_url`.
- **Replace or diff.**
  A key says "this is the same thing as before".
  Row 7 of one URL is not the same thing as row 7 of another, so a keyed diff across two loads would hand row 7's DOM, focus, scroll position and child regions to unrelated data.
  The app knows which case it is in, and says so with one counter:
  - a different source bumps `results_load`, and the view calls `cd_list_reset`: one clear, every row's cleanups run, and the pass that follows fills an empty list (at the tail, off the document from the 64th row);
  - a reload of the same source (a refresh, a poll, a next page appended) does not bump it, and the rows' own keys and revisions decide: unchanged rows are not touched, and focus stays where it is.

  The alternative that needs no new call is to put the load in the key (`(uint64_t)results_load << 32 | i`).
  It is correct, but the pass then sees 10,000 unknown keys while 10,000 old rows are unclaimed: it builds the key map for nothing, builds every new row off the document one by one and inserts each at the end of the pass.
  `cd_list_reset` says the same thing before the pass starts, and gets the bulk path.
- **Scale.**
  With rows that are not views there is nothing per row to tear down but the row's ids: a reset is one clear.
  With a watcher per row, 10,000 to 100,000 watchers are unlinked and linked again per load (S9c).
- **Memory for the formatted strings.**
  A row is two numbers: a slice of the answer, which is kept once.
  The text is formatted when the row is built and read by that call (`cd_tmp`, or one `cd_tmp_ahead` around the pass for a few hundred rows or more); C keeps no formatted bytes.
  A `cd_bound` per row (A) keeps a copy of every row's text.
- **Rows that show shared state while being replaced** (a highlight, a filter, a locale).
  The shared state's version is in the revision, and the list's view watches it.
  A reset, a highlight change and an answer arriving in one event are one run of `list_view`.

### The same case in the other candidates

| | What the app writes | What goes wrong |
| --- | --- | --- |
| A | the draft and the results are globals; `on_input` copies the draft and calls `views_changed()`; every row is a live view with a `cd_bound` | a keystroke formats every row; each row keeps a copy of its text; a row view must be dropped and unbound per row per load |
| B | `results_watch`, `highlight_watch`, each a table; rows call both | the tables' code twice more; 10,000 asks per change through `cd_dirty`'s scan |
| C | `results_version()`, `highlight_version()`; the list is one live view that compares both, then loops | works, and is the closest to E here: the difference is that the list's view is also called for every keystroke of a draft that is a state, and for every other change in the app |
| D | `results.c` calls `list_results_changed()` and `status_results_changed()` | works while there is one list; `results.c` includes its views |

## Predictions

Written 2026-11-18, before any run, and committed as b88d5f1 before the benchmark existed; the text is unchanged since.
Machine: this Mac, headless Chrome, no CPU throttle, script time of one entry into C (an exported function that ends in `cd_flush`), median of the samples, with the quartiles.
"Total" adds a forced layout (`document.body.offsetHeight`) after the entry.

### The entries

One benchmark app, built five ways.
The DOM they build is the same.

| Entry | What it is |
| --- | --- |
| `A` | re-run every live view: one list of live views, any setter asks for all of them, each view formats every time and writes through a `cd_bound` |
| `B` | the owner module keeps a watcher table per field and calls `cd_dirty` for each watcher (the `b-module` pattern, its table grown to fit) |
| `C` | versions: one list of live views as in `A`, each view compares the version it drew with the field's version and returns when they are equal |
| `E` | the `cd_state` prototype (`bench/shared-state/lib`): a version and an intrusive list of watchers per state, the changed states queued, the views found in the flush |
| `L` | `E`, but a list's rows are not views: the list is one view, and its render loops over the rows comparing each row's revision (the row's own counter plus the versions of the shared state it shows) |

In the row scenarios `A`, `B`, `C` and `E` make every row a view of its own (10,000 live views), and `L` makes the list one view.
`L`'s loop is a stand-in for the unimplemented `cd_list_row` pass of `docs/list-design.md`: one call and two compares a row.

### The hypothesis under test

The earlier recommendation was `C`.
The hypothesis written here, to be refuted or confirmed:

1. Up to about 1,000 live views, time does not choose between `A`, `C`, `E` and `L`: every one of them is under 1% of a 16.6 ms frame per change, so the choice is made on code, failure modes and memory.
2. At 10,000 rows, rows must not be views.
   `L` is the only shape whose bookkeeping is zero bytes a row, and it is as fast as `C` and `E`.
3. `B` as written is quadratic through `cd_dirty`'s dedup scan and through its slot search, and is unusable at 10,000 watchers.
4. `A` stops being free when a view's value is large (1 MB) or the views are many (10,000 and up).
5. So the design to recommend is `E` for views plus `L` for rows, with `C`'s version as the thing a row's revision and a memo are made of.

### The numbers

| # | Scenario | Predicted | Built on | Refuted by |
| --- | --- | --- | --- | --- |
| S1 | 10,000 rows each show a price in one shared currency; the currency changes (10,000 text writes) | `L` 3 to 6 ms of script; `A`, `C`, `E` within 1.15 of `L`; `B` 30 to 100 ms more than `L` | a formatted text write is about 0.3 to 0.6 us; `cd_dirty` scans the queue: 10,000 distinct asks are 50 million compares | `A`, `C` or `E` over 1.3 of `L`; `B` under 2 times `L` |
| S2 | 10,000 rows, the selected id changes (2 class writes) | `L` 0.02 to 0.08 ms; `C` 0.03 to 0.10 ms; `E` 0.05 to 0.20 ms; `A` 0.5 to 1.5 ms; `B` 30 to 100 ms | a call and a compare are 2 to 5 ns; a format and a `cd_bound_set_bytes` that matches is 50 to 150 ns | `L` over 0.15 ms; `A` under 3 times `C` (then the version check buys nothing here) |
| S3 | one row's own field changes, in 10,000 and in 100,000 rows (1 text write) | 10,000: `L` 0.02 to 0.08 ms, `C` 0.03 to 0.10 ms, `A` 0.5 to 1.5 ms; 100,000: `L` 0.2 to 0.8 ms, `C` 0.3 to 1.0 ms, `A` 5 to 15 ms. `B` and `E` with one state per row: under 0.01 ms at both sizes | as S2; `B` and `E` call one watcher | `L` at 100,000 over 1.5 ms; `A` at 100,000 under 3 ms |
| S4 | 1,000 views in nested regions, 100 fields, 10 views a field; one field is set per entry (10 text writes) | `A` 0.05 to 0.15 ms; `C` 0.003 to 0.012 ms; `B`, `E` 0.002 to 0.008 ms | as S2 | any entry over 0.5 ms (hypothesis 1 is then wrong) |
| S5 | the same 1,000 views, all 100 fields set in one entry (1,000 text writes) | `A`, `C`, `E` 0.3 to 0.7 ms and within 1.15 of each other; `B` 0.3 to 1.2 ms more | 1,000 distinct `cd_dirty` asks are 0.5 million compares | `B` within 1.1 of `E` |
| S6 | 10,000 rows live; one setter of the state every row shows is called 1,000 times in one entry (a bulk import), then the flush | `L`, `C`, `E`: S1's time plus under 0.1 ms; `A` the same; `B`: over 30 ms a setter call, so it is run with 10 calls and must take over 300 ms | `cd_changed`, and `A`'s and `C`'s single `cd_dirty`, are O(1) when already asked; `B` asks 10,000 watchers per call, each a scan | `E` over S1 plus 0.5 ms; `B` at 10 calls under 100 ms |
| S7 | build 10,000 rows, then take them away, with each row a view (`A`, `B`, `C`, `E`) or not (`L`) | build: `A`, `C`, `E` add 0.03 to 0.25 us a row to `L`; `B` adds 30 to 80 ms in all (slot search). Memory a row beyond the model: `L` 0; `E` 32 bytes (the intrusive watcher); `C` 4 to 12 (the seen version and the list slot); `A` 60 to 100 (a `cd_bound`, its copy of the bytes, the heap's rounding) | sizes of the structs; `cd_bound` is 36 bytes and keeps the bytes | `E` over 48 bytes a row; `A` under 50 |
| S8 | the URL case: 10,000 result rows on screen, one keystroke in the URL field (the draft changes, nothing shown does) | `E`, `L`, `B`: under 0.005 ms (nothing watches the draft); `C` 0.03 to 0.10 ms with rows as views, under 0.005 with the list as one view; `A` 0.5 to 1.5 ms with rows as views | as S2 | `A` under 0.2 ms |
| S9 | the URL case: a second load replaces 10,000 rows with 10,000 others. (a) reset: one clear, then build; (b) keyed diff with the keys of load 1 reused, every row patched; (c) reset, with each row a watcher (`E`), against (a) | (a) 20 to 40 ms of script; (b) 3 to 7 ms of script, total 0.3 to 0.7 of (a)'s; (c) within 1.10 of (a) | jfb 07 create 10,000 is 19.9 ms of script on this Mac; S1 | (b) slower than (a); (c) over 1.2 of (a) |
| S10 | a 1 MB textarea mirrored in a text node elsewhere; one keystroke | `E` 1 to 5 ms of script; `A` within 1.15 of `E`, and 1 MB more wasm memory (16 pages) for the `cd_bound`'s copy | the read and the write each cross 1 MB of UTF-8; a `memcmp` and a `memcpy` of 1 MB are about 0.05 ms each | `A` over 1.3 of `E`; `A`'s memory under 0.9 MB more |
| S11 | the same mirror live, and an unrelated field changes | `A` 0.05 to 0.3 ms (it copies and compares 1 MB); `C` and `E` under 0.005 ms | as S10 | `A` under 0.02 ms |
| S12 | `cd_dirty` itself: n distinct (fn, ctx) asked in one entry, each an empty function | 1,000: 0.3 to 1.0 ms; 10,000: 30 to 100 ms; the ratio between 60 and 140 | the scan is n squared over two | a ratio under 30 |
| S13 | `L`'s loop alone over rows that did not change | 10,000: 0.02 to 0.08 ms; 100,000: 0.2 to 0.8 ms | list-design's L12 | over 0.12 ms at 10,000 |
| S14 | a text input that watches its own field. (a) the view writes the value back unguarded, the same value; (b) the same while an IME composition is open; (c) the setter changes the value (upper case) | (a) the caret stays where it was; (b) the composition survives (I give this 70%); (c) the caret jumps to the end | the HTML standard moves the caret only when the value changes | (a) the caret moves; (b) the composition is cancelled: then the guard is required, not a nicety |

The hypothesis is refuted as a whole if `C` or `A` beats `E` and `L` by more than the spread anywhere that matters (S1 to S9), or if `L` is not within 1.2 of the best entry in S1 to S3.

## Measurements

Run 2026-11-18 by one executor, alone on this Mac, in headless Chrome 157.0.8103.79, no throttle.
Every (scenario, entry) ran in a fresh navigation, 5 passes, the entries of a scenario rotated by scenario and pass, each navigation gated on a 1-minute load under 4 (it was between 1.63 and 3.07).
A number is the median of the 5 pass medians (15 samples a pass after 3 warm-ups), with the lowest and highest pass median in brackets.
Operations under 1 ms were timed as blocks of K calls.
The raw numbers are `bench/shared-state/results/times.tsv`, `memory.tsv`, `crossings.tsv` and `caret.txt`; `summary.tsv` is this section's source.
Every entry was checked to leave the same DOM after every step of a fixed script, and that check was mutation checked.
I reran ten (scenario, entry) pairs after the executor's report; each came back within its spread, or within 2 times for the sub-microsecond ones.

L is measured on today's `cd_list_update` with a `patch` that compares the revision, since the list design is not implemented.
S13 (ii) is a stand-in for the future pass.

### Times, ms of script per operation

| # | Scenario | A | B | C | E | L |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| S1 | 10,000 rows, the currency changes | 2.098 (1.940-2.155) | 27.375 (27.33-29.14) | 1.773 (1.710-1.800) | 1.885 (1.745-1.905) | 1.710 (1.683-1.815) |
| S2 | 10,000 rows, the selection moves | 0.3638 (0.3606-0.3681) | 25.595 (25.585-25.640) | 0.0314 (0.0309-0.0315) | 0.0630 (0.0627-0.0641) | 0.0380 (0.0379-0.0386) |
| S3 | one row of 10,000 changes | 0.3625 (0.3575-0.3644) | 0.00034 | 0.0316 (0.0315-0.0322) | 0.00037 (0.00036-0.00041) | 0.0392 (0.0386-0.0393) |
| S3 | one row of 100,000 changes | 3.190 (3.185-3.430) | 0.00085 | 0.2891 (0.2844-0.2953) | 0.00095 (0.00092-0.00098) | 0.566 (0.384-0.589) |
| S4 | 1,000 views, one field of 100 set | 0.0503 (0.0474-0.0506) | 0.0020 | 0.0045 (0.0042-0.0048) | 0.0021 (0.0019-0.0021) | as E |
| S5 | 1,000 views, all 100 fields set | 0.189 (0.184-0.201) | 0.446 (0.433-0.466) | 0.173 (0.170-0.187) | 0.184 (0.175-0.185) | as E |
| S6 | 10,000 rows, the currency set 1,000 times in one entry | 2.028 (1.803-2.115) | 25,647 (10 sets: 257.4) | 1.693 (1.650-1.775) | 1.758 (1.675-1.865) | 1.693 (1.660-1.858) |
| S7 | build 10,000 rows | 10.11 (9.78-11.99) | 46.70 (46.32-47.50) | 10.52 (9.29-11.31) | 9.93 (9.60-10.22) | 9.63 (9.38-10.98) |
| S7 | take 10,000 rows away | 1.225 | 1.190 | 1.175 | 1.200 | 1.160 |
| S8 | 10,000 rows, a keystroke in the draft | 0.3638 (0.3569-0.3819) | 0.00030 | 0.0309 (0.0307-0.0326); list as one view: 0.0027 | 0.00029 | 0.00029 |
| S10 | 1 MB mirror, one keystroke | 0.790 (0.780-0.855) | not run | 0.735 (0.685-0.765) | 0.675 (0.670-0.760) | as E |
| S11 | 1 MB mirror live, an unrelated field set | 0.401 (0.399-0.406) | not run | 0.0042 | 0.0020 | as E |

L at 100,000 rows in S3 is bimodal by pass (0.384, 0.385, 0.566, 0.576, 0.589); S13 (i), the same loop with nothing changed, is 0.383 (0.376-0.383).
S4, S5, S10 and S11 ran with no rows; with 10,000 rows live A would add S8's 0.36 ms to each.

Totals (script plus a forced layout), where layout matters:

| Scenario | A | B | C | E | L |
| --- | ---: | ---: | ---: | ---: | ---: |
| S1 | 40.8 | 64.2 | 38.5 | 38.8 | 39.0 |
| S7 build | 75.7 | 113.4 | 75.6 | 75.6 | 74.8 |
| S10 | 110.3 | | 110.3 | 110.7 | |

The browser's layout is 20 times the script when every row changes, and 150 times it for the megabyte.

### S9, a second load replaces 10,000 rows

| Case | Script | Total | Crossings |
| --- | ---: | ---: | ---: |
| (a1) reset, then build (L) | 11.07 (10.05-12.13) | 84.6 | 130,003 |
| (a2) one update to disjoint keys (L) | 11.34 (10.49-12.67) | 82.5 | 130,003 |
| (b) the same keys kept, every row patched (L) | 1.83 (1.64-1.88) | 37.4 | 10,000 |
| (c) reset, then build, each row three watchers (E) | 11.44 (10.26-12.03) | 83.3 | 130,003 |
| (d) the table's region hidden and shown (L) | 11.79 (11.38-12.79) | 85.2 | 130,011 |

### S12 and S13

| | 1,000 | 10,000 | 100,000 |
| --- | ---: | ---: | ---: |
| S12: n distinct `cd_dirty` asks of an empty function | 0.268 ms | 25.6 ms (ratio 95.5) | |
| S13 (i): today's `cd_list_update` over unchanged rows, with a revision compare in `patch` | | 0.0381 ms | 0.383 ms |
| S13 (ii): the stand-in for `cd_list_row` (one call, two 64-bit compares a row) | | 0.0212 ms | 0.210 ms |

### Memory

| Entry | Bytes a row beyond L, touched, after building 10,000 rows | sizeof the entry's per-row struct |
| --- | ---: | ---: |
| A | 112.6 | 88 |
| B | 75.8 | 40 |
| C | 31.5 | 24 |
| E | 131.5 | 140 |
| L | 0 | 8 |

Touched memory is counted in 4 KB pages of non-zero words, so it is good to a few bytes a row.
S10: after five keystrokes at 1 MB, A holds 79 wasm pages and C and E 63: A's `cd_bound` keeps a second megabyte.

### Crossings

Every entry makes the same DOM writes per operation: 10,000 for S1 and S6, 2 for S2, 1 for S3 and S8, 10 for S4, 1,000 for S5.
The one difference is A's build (S7): 120,002 against 110,002, because its rows are drawn by their view after they are created.
So no candidate saves or costs a DOM operation; they differ only in the C work that decides what to write.

### S14, the controlled input

| Variant | Typed "abcdef", caret to 3, typed X | Writes to `input.value` |
| --- | --- | ---: |
| (a) the view writes the same value back, unguarded | "abcXdef", caret at 4: it stayed | 8 |
| (g) guarded | "abcXdef", caret at 4 | 0 |
| (c) the setter upper-cases, a lower-case x typed at 3 | "ABCXXDEF", caret at 8: it jumped to the end | |
| (b) an IME composition ("ni", "nih", commit) under (a) | one `compositionend`, at the commit; the value is the committed character; the view wrote during the composition and nothing ended it early | |
| (b) under (g) | the same events, no write | 0 |

### Each prediction's verdict

| # | Verdict | Predicted | Measured |
| --- | --- | --- | --- |
| S1 | holds, with misses | L 3 to 6 ms; A, C, E within 1.15 of L; B 30 to 100 ms more | L 1.71 (faster than the range); C 1.04, E 1.10, A 1.23 of L (A outside 1.15, inside the 1.3 falsifier); B 25.7 ms more (just under the range, 16 times L) |
| S2 | holds | L 0.02 to 0.08; C 0.03 to 0.10; E 0.05 to 0.20; A 0.5 to 1.5; B 30 to 100 | 0.038; 0.031; 0.063; A 0.364 and B 25.6, both under their ranges; A is 11.6 times C |
| S3 | holds | 10,000: L 0.02 to 0.08, A 0.5 to 1.5; 100,000: L 0.2 to 0.8, C 0.3 to 1.0, A 5 to 15; B and E under 0.01 | 0.039, 0.36; 0.566, 0.289, 3.19 (A just over the 3 ms falsifier, so it did not fire); 0.0003 to 0.001 |
| S4 | holds | A 0.05 to 0.15; C 0.003 to 0.012; B, E 0.002 to 0.008 | 0.050; 0.0045; 0.0020, 0.0021 |
| S5 | holds, faster than the range | A, C, E 0.3 to 0.7 and within 1.15; B 0.3 to 1.2 more | 0.17 to 0.19, within 1.10; B 0.26 more (2.4 times E) |
| S6 | holds | S1 plus under 0.1 ms; B over 300 ms for 10 sets | within 0.13 ms of S1 (0.03 over the range, inside the spread); B 257 ms for 10 sets (under the range, over the 100 ms falsifier), 25.6 s for 1,000 |
| S7 time | holds | A, C, E add 0.03 to 0.25 us a row; B adds 30 to 80 ms | 0.048, 0.089, 0.030; 37 ms |
| S7 memory | **refuted for E**, off for A and C | L 0; E 32; C 4 to 12; A 60 to 100 | 0; **131.5** (the falsifier, over 48, fired: the entry has a state and three watchers a row, not the one watcher I counted); 31.5; 112.6 |
| S8 | holds | E, L, B under 0.005; C 0.03 to 0.10, list as one view under 0.005; A 0.5 to 1.5 | 0.0003; 0.031 and 0.0027; 0.364 (under the range, over the 0.2 falsifier) |
| S9 | holds, faster than the range | (a) 20 to 40 ms; (b) 3 to 7 ms and 0.3 to 0.7 of (a)'s total; (c) within 1.10 of (a) | 11.1; 1.83 and 0.44; 1.03 |
| S10 | holds, faster than the range | E 1 to 5 ms; A within 1.15 and 16 pages more | 0.675; A 1.17 of E (outside 1.15, inside the 1.3 falsifier), exactly 16 pages more |
| S11 | holds, A worse than predicted | A 0.05 to 0.3; C, E under 0.005 | 0.401; 0.0042, 0.0020 |
| S12 | holds | 0.3 to 1.0 and 30 to 100 ms, ratio 60 to 140 | 0.268 and 25.6 (both a little under), ratio 95.5 |
| S13 | holds | 0.02 to 0.08 and 0.2 to 0.8 | 0.038 and 0.383; the stand-in 0.021 and 0.210 |
| S14 | holds | (a) the caret stays; (b) the composition survives; (c) the caret jumps | all three as predicted, in Chrome only |

My absolute numbers were pessimistic by 1.5 to 2 times almost everywhere: a formatted text write is 0.17 us on this Mac, not 0.3 to 0.6.
The ratios, which the design rests on, held.

The two conditions under which the hypothesis as a whole was to count as refuted both fired:

1. **"L is within 1.2 of the best entry in S1 to S3": refuted.**
   In S3 a view per row with a state per row (B, E) is 100 times faster than L at 10,000 rows and 600 times at 100,000.
   In S2, L is 1.21 of C.
2. **"Neither C nor A beats E and L by more than the spread": refuted in S2.**
   C at 0.0314 (highest pass 0.0315) is under L at 0.0380 (lowest pass 0.0379), and E with rows as views is 0.063.

Of the five numbered hypotheses: 1 (time does not choose up to 1,000 views) holds, the slowest entry in S4 is 0.3% of a frame; 2 (rows must not be views) holds on memory and is wrong as a statement about speed; 3 (B is quadratic) holds; 4 (A stops being free) holds; 5 is the recommendation, argued next.

## What the numbers changed

**1. One row changing is not free in L, and I had said it was as good as the best.**
L pays one compare for every row to find the one that changed: 0.039 ms at 10,000 rows and 0.38 to 0.59 ms at 100,000.
A state and a watcher per row pay 0.0004 ms and 0.001 ms.
What decides between them is the other side of the same table: 131 bytes a row against none (13 MB against none at 100,000 rows), a link and an unlink per watcher whenever a row comes or goes, and a second mechanism for rows beside the list's own revision.
In absolute terms L's cost is 0.2% of a frame at 10,000 rows and 2 to 4% at 100,000, and a page that really holds 100,000 rows spends 20 times that in layout for anything it changes.
So L stays the recommendation, with the limit written down: at 100,000 rows and a change every frame, L costs about half a millisecond a frame.
The future pass is cheaper than what was measured (the stand-in is 0.21 ms at 100,000).
If that limit is ever met, the fix is one write through the ids the row's build kept (`cd_set_text_node(r->price_text, ...)` in the setter, with `r->rev` bumped so that the next pass agrees), or a lookup from key to row in the list; not a watcher per row.

**2. C is as fast as anything, and that was the earlier recommendation's whole case.**
C with rows as views beats L by 0.007 ms in S2 and by half in S3 at 100,000, because today's `cd_list_update` does more per row than a call and a compare.
The stand-in for the new pass (0.021 ms) is under C's 0.031, so I expect the order to turn once the list design is built, and I have not measured that.
Either way the difference is microseconds, and C costs 32 bytes a row.
C loses on code, not on time; see the recommendation.

**3. `cd_dirty` must not be what a watcher list calls.**
10,000 asks are 25.6 ms, and a bulk import of 1,000 sets with 10,000 watchers took 25.6 seconds in B.
The prototype's "queue the state, find the views in the flush" made the same import cost what one set costs (1.76 ms against 1.89).
This is why `cd_state` belongs in cdom and is not left as an app pattern over `cd_dirty`.

**4. A is fine for a small app and falls off in three measured places.**
At 1,000 views it costs 0.05 ms a change, which nobody will see.
At 10,000 rows as views it is 0.36 ms for every change of anything, a keystroke included (S8), and 3.2 ms at 100,000.
With one megabyte behind a `cd_bound` it is 0.4 ms for every unrelated change and a megabyte of memory (S10, S11).
And it holds 113 bytes a row.

**5. Replacing a list is dominated by building rows, not by watchers or by how the old rows go.**
Reset, a disjoint update and a region rebuild are the same within the spread (11.1, 11.3, 11.8 ms), and three watchers a row add 3% (S9c).
Reusing the old rows under the same keys is 6 times faster in script and 2.3 times in total (S9b), because no node is created.
So the choice between reset and diff is not about speed in the direction one might fear: the wrong one (a diff across unrelated data) is the fast one.
It has to be chosen on meaning, which is why the design makes the app say it (`results_load`).

**6. The controlled input does not need its guard in Chrome.**
The guard stays the recommended form for the crossing it saves and for the browsers not measured, and the two `cd_bound` additions become optional.

**7. The URL case separated the candidates exactly where the coordinator's notes pointed.**
Typing with 10,000 results on screen: A 0.36 ms a keystroke, C 0.031 ms with rows as views and 0.0027 ms with the list as one view, B, E and L 0.0003 ms.
Nothing else in that case discriminates: the async part is `cd_req`'s in every candidate, and the replace is the list's.

## The recommendation

E for views, L for rows, C's version as the currency between them.

| Question | Answer | Evidence |
| --- | --- | --- |
| Is it fast enough? | every scenario is within 1.10 of the best entry, except one row changing in a large list (0.04 ms at 10,000 rows) | S1 to S11 |
| What does a view write? | one `cd_watch` line per state it draws from, in its build, and the call that draws it the first time | the badge: `badge/e-state/src` |
| What does a data module write? | its data, its getter, and a setter that ends in `cd_changed` | `name.c`, 14 lines against B's 30 |
| What can be forgotten? | the `cd_changed` (one place per state, and the compiler keeps other writers out), a `cd_watch` (the check build traps), a term of a row's revision (a test) | "Failure modes" |
| What does it cost in memory? | a state is 24 bytes; a watch about 32; a row nothing | S7 |
| What is hidden? | nothing is wired by name, by type or by a read. `grep name_state` lists the writer's commit and every view | |

Why not C, which measured as well:

- A view in C keeps a seen version per state and writes the compare by hand; in E it writes one line per state and keeps nothing.
- C still registers every view in a list, with a cleanup, so it does not save the registration.
- C's compare can be wrong in the same way a watch list can (it lists less than the view reads), and nothing can check it; E's watch list can be checked against the reads.
- C calls every live view for every change.
  It measured cheap (0.0045 ms for 1,000 views), so this is an argument about 10,000 views, not about today.
- C has no place to hang the order between a parent's view and a child's, or the "runs once" rule for a view of several states; E has the view record.

Why cdom and not an app pattern: the watcher list has to be unlinked in the region's takedown, a pending run has to be dropped with it, the views have to run once and in an order, and none of it may go through `cd_dirty`'s scan.
That is 90 lines in the prototype, written once, against 20 lines per field in B that are quadratic and overflow their table.

## What cdom should provide

### The API as cdom.h would show it

```c
/* ---- shared state: data one module owns, shown by views anywhere ----
 * State stays plain C, private to the module that owns it. Beside it the module keeps a cd_state, which
 * is what the views of that data watch:
 *   static char name[256]; static uint32_t name_len;        (name.c)
 *   cd_state name_state;                                     (declared extern in name.h)
 *   void name_set(const char *p, uint32_t n) { memcpy(name, p, n); name_len = n; cd_changed(&name_state); }
 * A view says what it draws from while its region builds, and draws itself once:
 *   cd_watch(&name_state, badge_view, nullptr); badge_view(nullptr);
 *
 * cd_changed(s) moves s->ver on and marks s changed. It does not look at the watchers, so it costs the
 * same for none or ten thousand, and a thousand calls in one entry are a thousand increments.
 * Before the entry from JS returns (in the flush, where cd_dirty's renders run), every view watching a
 * changed state runs once, however many of its states changed and however often:
 * - outermost region first, then in the order the views were registered: a view may rely on its region
 *   being shown, because the views of every region around it ran before it and had the chance to hide it;
 * - a view that changes a state causes another round in the same flush, and a view that already ran
 *   after the last change of everything it watches is not run again; past 16 rounds it traps.
 * A cd_state starts zeroed and must stay where it is while anything watches it: a static, or a field of
 * a struct that outlives the regions that watch it.
 *
 * s->ver only goes up. So the sum of the versions of everything a value is made from is a version of
 * that value: a list row's revision (cd_list_row), or the key of a cached derived value. */
typedef struct cd_state {
  uint64_t ver;                    /* moves on at every cd_changed; anyone may read it */
  struct cd_view *first, *last;    /* cdom's: the views watching, in the order they began to */
  struct cd_state *changed;        /* cdom's: the list of states changed in this entry */
  uint32_t tick;                   /* cdom's: when it last changed */
} cd_state;
void cd_changed(cd_state *s);

/* The region building now draws fn(ctx) from s. Until that region is taken away, every cd_changed(s)
 * asks for fn(ctx); when it is taken away the watch goes, with a run still pending, before the region's
 * cleanups run. The same (fn, ctx) watched again in the same region is the same view, watching one state
 * more. cd_watch does not call fn: the build draws the first time itself, once its nodes are named.
 * Inside a list row's build the watch is the row's. Traps when no region builds. */
void cd_watch(cd_state *s, cd_render_fn fn, void *ctx);

/* The check build (-DCD_STATE_CHECK); nothing otherwise. A module's getter calls cd_read(&its_state).
 * A view that runs in the flush and reads a state it does not watch traps, and so does the end of a
 * region's build that read a state which neither that region nor one around it watches. */
void cd_read(const cd_state *s);

/* ---- bound values ---- (two optional additions, for the guarded form of an input that shows the state it writes) */
void cd_bind_prop(cd_bound *b, cd_id el, cd_str prop);                    /* a string property: "value" */
void cd_bound_shows_bytes(cd_bound *b, const char *p, uint32_t len);      /* the node already shows these bytes */
```

### How it sits in cdom.c

- A view record (`cd_view`: fn, ctx, the region's depth, the tick of its last run, its place in the round's queue) hangs off the region that registered it, in a list the region's takedown walks before its cleanups.
  A watch is a link between a view and a state (two pointers each way).
  Nothing is a `cd_on_free` entry, and nothing is searched.
- The flush: while states are on the changed list, a round collects their views (each once, skipping one that ran since its states last changed), orders them by depth with a counting sort, and runs them.
  Then `cd_dirty`'s own queue runs as today, in the same rounds.
- `cd_dirty`, `cd_undirty` and `cd_render_now` stay as the low-level queue, for a render that is not a view of shared state (a module that asks for its own redraw from a timer).
  Its header sentence "a handful per entry: the queue is scanned to dedup" stays true, and S12 is the reason not to build on it.
- `cd_region_dirty`, step 12 of `docs/list-design.md`, is not needed: `cd_watch` is the "view that lives as long as the region it draws" that step was for, and amgi's per-module `dirty()` wrappers, `shown` flags and `cd_undirty` lines go the same way.
- The cost for an app that uses none of it: nothing is linked (the pass is reached through the pointer `cd_dirty` already uses), and `cd_region` grows by one pointer.

### What stays the app's

- The data, its layout and its getters: `name_get`, `rows[]`, a slice into an answer.
- The setter, and the decision of when a change is worth saying (`cd_changed` once per message, or once per frame).
- Which states exist and how coarse they are: one per thing that changes together.
  One for a form, one for a collection, one for a selection.
- Derived values: a getter that caches by a sum of versions, and a `_watch` helper that watches the inputs.
- The view: what it formats, and whether it writes directly or through a `cd_bound`.
- Row revisions.

cdom adds no store, no reducer, no selector and no name for any state.
It adds the two things an app cannot write well by itself: the list of who watches, tied to the region's life, and the pass that runs each view once, in order.

## How it fits `docs/list-design.md`

The list design fits as it is, and this design leans on three things in it:

1. **A row is skipped by its revision, with no call into the app.**
   That is what makes "10,000 rows show one shared value" need no watcher per row.
2. **Revisions are 64 bits and are the app's to make.**
   A sum of versions fits.
3. **A row is a region the list owns.**
   So a region built inside a row (an editor, a detail panel) may `cd_watch`, and the watch goes with the row.

Amendments proposed:

| # | Amendment | Why |
| --- | --- | --- |
| 1 | Say in `cd_list_row`'s comment how to make a revision: "the sum of the versions (`cd_state.ver`, a row's own counter) of everything the row shows, since versions only go up; a value such as 'is selected' goes in as the value" | it is the one rule a reader needs, and the wrong form (the selection's version) is correct and slow |
| 2 | Add `void cd_list_reset(cd_id parent);`: take every row away now, as one clear, with the rows' cleanups; the next pass fills an empty list | the URL case: a new result set is not a diff of the old one, and saying so before the pass keeps the bulk build (tail appends, off the document) that a pass of all-new keys over unclaimed old rows loses |
| 3 | Replace step 12 (`cd_region_dirty`) by `cd_state`, `cd_changed` and `cd_watch` | one owner for "a view lives as long as its region", and it also knows what the view draws from |
| 4 | In "Composition", add the row: "a `cd_watch` in a row's build is the row's; the list's own view watches the shared state its rows show, and rows do not watch" | the hard case named in the brief, answered in one line |
| 5 | Lesson 5 after the change: `todos_state` in `todo.h`, `cd_changed(&todos_state)` in the three setters, and `cd_watch(&todos_state, render, nullptr)` twice (the list and the footer) | the lesson's `render()` calls after every mutation become one line per setter, and the footer stops being redrawn by hand |

Not proposed: a lookup from key to row (`cd_list_find`).
The pass over unchanged rows is cheap enough at the sizes a page holds (S13), and a windowed list bounds it.

## Decisions for Alex

1. **The direction.**
   `cd_state`, `cd_changed`, `cd_watch` in cdom's core (E), lists as one view with revisions made of versions (L); against C as an app pattern with nothing new in cdom.
   C is as fast; the case for E is the code a view writes and what can be checked.
2. **The check build** (`cd_read` in getters, a trap on a read that is not watched).
   It reads like dependency tracking, but it wires nothing: it is an assertion in a test build, and it is the only thing that catches a forgotten watch before a user does.
   Yes or no; and if yes, whether the build half (a region's build that reads what nothing around it watches) is wanted too.
3. **The order of views: outermost region first.**
   It is not in the prototype.
   It costs a depth per view and a counting sort per round, and it makes "my region is shown" something a view may rely on.
   The alternative is the documented order "as the states changed", and views that defend themselves.
4. **`cd_list_reset(parent)`** as an amendment to the list design, and the rule for revisions in `cd_list_row`'s comment.
5. **`cd_bind_prop` and `cd_bound_shows_bytes`** for a guarded controlled input.
   Chrome does not need the guard (S14); Safari and Firefox were not measured.
   Add them, or measure the other browsers first.
6. **The limit for one row changing in a very large list** (0.4 to 0.6 ms at 100,000 rows today, about 0.2 ms expected with the new pass).
   Accept it, or ask for a lookup from key to row in the list design now.
   I recommend accepting it.
7. **Small ones.**
   `ver` as 64 bits (24 bytes a state, never wraps, adds straight into a row's 64-bit revision) or 32 (16 bytes).
   A `cd_watch_key(s, fn, key)` beside `cd_on_key`, for a view inside a row whose ctx is the row's key: now, or when the first case appears.
8. **When.**
   `cd_state` does not depend on the list design: it can land before it (amgi's `dirty()` wrappers and its three `_changed` calls are its first users) or as that design's step 12, which it replaces.

## The implementation

Alex's decisions (2026-11-18): `cd_state`, `cd_changed` and `cd_watch` in cdom's core; the check build, with its build half; views outermost region first; the amendments to `docs/list-design.md` written in, the list design itself not built; `cd_bind_prop` and `cd_bound_shows_bytes` now; about 0.5 ms per single-row change at 100,000 rows accepted, no key lookup; `ver` 64-bit and no `cd_watch_key`; `cd_state` on its own, before the list design.
It is in `src/cdom.h` ("shared state") and `src/cdom.c` ("shared state: cd_state, cd_changed, cd_watch"), with its cases in `tests/unit.c` (29) and its mutants in `tools/mutate.py`.

### What landed, and where it differs from the sketch

The API is the sketch's, with `ver` 64-bit and `cd_read` an inline nothing outside the check build.
Where the code differs from "How it sits in cdom.c", and why:

1. **The order within one depth is the order the views were found, not the order they were registered.**
   A round finds its views by walking each changed state's watches, in the order the states changed, and puts them in order of depth with a stable counting sort: one pass over the round, and none at all when the round is in order already (one view, or one depth).
   Registration order within a depth would need a sort by a sequence number, O(n log n) a round.
   The order is still fixed, and `cdom.h` says which it is; the promise Alex decided on, outermost region first, is kept.
2. **The tick counts within a pass, and is put back to 0 at the pass's end.**
   A global 32-bit counter would wrap after four billion changes and runs, and a view that last ran before the wrap would then be skipped.
   The pass keeps the views it touched (queued, or made in the pass) and zeroes their `ran` at its end, so no count outlives its pass.
3. **A view taken away during a pass is unlinked at once and freed at the pass's end.**
   The sketch had a slot per view to clear a pending run; here a round skips a view whose `fn` is 0, and since its memory is not reused before the pass ends, a view made later in the same pass cannot take its place in the round (a case covers it).
   A view is 24 bytes and a watch 20, each in the heap's 32-byte class.
4. **Views and renders share one pass and its 16 rounds.**
   A round runs the views of the states changed before it, then the renders asked before it.
   A view may ask for a render and a render may change a state; both run in the same flush.
5. **`cd_watch` also traps inside a cleanup**, as `cd_on_free` does: a cleanup releases, it does not subscribe.
6. **A region's depth is its `r->up` chain.**
   A region opened in a list row's create is no region's child, so it is depth 1 unless the app set its `r->up`.
7. **The check build's build half looks in two places**: the region's own `r->up` chain, and the regions building under it.
   The second covers a region opened in a list row's create, inside another region's build, which has no `r->up`.
   A read in a region shown by a running view is that build's read, checked at its end, not the view's.
8. **`cd_dirty` changed after all.**
   The sketch kept its scan; the scan is now a lookup in cdom's pair table (the one that holds `cd_on`'s handlers and the timers), keyed by `(ctx ^ fn * K, fn)`, emptied at the pass's end.
   It costs 70 bytes over the scan at `-Oz` (a table of its own cost 415), and S12 below gives the time.
9. **Linked only when used**, as the sketch said: the views' pass and their takedown are reached through two `volatile` pointers that `cd_changed` and `cd_watch` set, and `tests/link/run.sh` checks that an app which renders and hides regions but never uses `cd_state` is 1,456 bytes smaller than the same app with one view.
10. **No tutorial lesson changed.**
    Lesson 5 builds with no region, and a watch needs one; lesson 4's state lives in its handlers and has one reader.
    The amendment's lesson 5 (with a page region, `todos_state` and `filter_state`) is written in `docs/list-design.md`, for the lesson the list design rewrites.

### Predictions for the implementation

Written before the implementation was measured, from the prototype's numbers above.
Two entries are added to the benchmark: `LI` is `L` on cdom's own `cd_state`, the same source built with `-DCD_STATE_REAL` and no prototype; `EI` is `E` on cdom's own `cd_state`, where a watch belongs to a region, so each row's create shows a region of its own (its parent is the row's `<tr>`, it makes no node) and watches the currency, the selection and the row's own state in its build, and the row's destroy forgets it.
`A` to `L` are built on cdom's `src/` as it was measured (main at cfee569), so `L` against `LI` in S12 is `cd_dirty`'s scan against its table.
One run, 5 passes, `E`, `L`, `EI` and `LI` interleaved in each pass (`ENTRIES=E,L,EI,LI`), into `results/implemented/`.

The hypothesis: where the shape is the prototype's (`L`; `E`'s views in regions in S4, S5, S10 and S11), the implementation costs what the prototype did, within the spread; the depth order, the tick and the pass's bookkeeping are a few loads per view.
Where the real API changes the shape (`EI`'s rows, each a region with a view and three watches on the heap), it costs more than `E` did.

| # | Predicted | Refuted by |
| --- | --- | --- |
| S1 | `LI` 1.6 to 1.9 ms (`L` 1.71); `EI` 1.9 to 2.3 (`E` 1.89) | `LI` over 1.2 of `L`, or `EI` over 1.3 of `E`, outside the spread |
| S2 | `LI` 0.035 to 0.045; `EI` 0.07 to 0.10 (`E` 0.063: it adds a stored tick and a reset per view) | the same |
| S3 | `LI` 0.035 to 0.045 at 10,000 and 0.38 to 0.6 at 100,000; `EI` 0.0003 to 0.001 at both | `LI` over 1.2 of `L` |
| S4, S5 | `EI` 0.0019 to 0.003, and 0.17 to 0.22 | `EI` over 1.3 of `E` |
| S6 | `LI` and `EI`: their S1 plus under 0.02 ms | over S1 plus 0.1 ms |
| S7 build | `LI` as `L` (9.6); `EI` 10.5 to 12 ms: a region, four allocations and three links a row, 0.1 to 0.2 us | `EI` over 14 ms |
| S7 take away | `LI` as `L` (1.16); `EI` 2 to 2.5 ms: a forget, three unlinks and four frees a row | `EI` over 3.5 ms |
| S7 memory | `LI` 0 bytes a row beyond `L`; `EI` 210 to 240 (a 104-byte row struct, with its region and state, and a view and three links of 32 bytes each on the heap) | `EI` under 190 or over 260 |
| S8 | both under 0.0005 | over 0.001 |
| S9 | `LI` as `L` in (a1), (a2), (b) and (d); `EI` (c) 13 to 16 ms (`E` 11.4 plus the regions) | `LI` over 1.2 of `L` |
| S10, S11 | `EI` as `E` (0.675, 0.002) | over 1.3 of `E` |
| S12 | `cd_dirty`'s table: `LI` 0.02 to 0.06 ms for 1,000 distinct asks and 0.2 to 0.6 for 10,000 (ratio 8 to 14), against the scan's 0.27 and 25.6 in `L`; for 4 asks, `LI` within 1.5 of `L` | `LI` at 10,000 over 1 ms, a ratio over 20, or 4 asks over 1.5 of `L` |
| S13 (i) | `LI` as `L` (0.038, 0.383) | over 1.2 of `L` |

### Measurements, implemented

Run 2026-11-18, alone on this Mac, headless Chrome 157, the same harness as above: 5 passes, `E`, `L`, `EI` and `LI` rotated within each scenario and pass, each in a fresh navigation, loads 2.59 to 3.93 at the navigations.
`E` and `L` are the prototype again, in the same passes, so the columns side by side ran under the same conditions; their earlier numbers are in the tables above.
A number is the median of the 5 pass medians, with the lowest and highest pass median in brackets; the raw numbers are `bench/shared-state/results/implemented/` (`times.tsv`, `memory.tsv`, `summary.tsv`, `run.log`).

| # | Scenario | E, prototype | EI, implemented | L, prototype | LI, implemented |
| --- | --- | ---: | ---: | ---: | ---: |
| S1 | 10,000 rows, the currency changes | 1.865 (1.693-1.950) | 1.830 (1.693-1.945) | 1.825 (1.695-1.925) | 1.860 (1.720-1.990) |
| S2 | 10,000 rows, the selection moves | 0.0639 (0.0638-0.0648) | 0.0858 (0.0845-0.0867) | 0.0390 (0.0388-0.0391) | 0.0390 (0.0389-0.0399) |
| S3 | one row of 10,000 changes | 0.00046 (0.00043-0.00082) | 0.00048 (0.00043-0.00084) | 0.0396 (0.0393-0.0413) | 0.0397 (0.0394-0.0403) |
| S3 | one row of 100,000 changes | 0.00104 (0.00100-0.00115) | 0.00132 (0.00115-0.00150) | 0.404 (0.389-0.614) | 0.574 (0.391-0.588) |
| S4 | 1,000 views, one field of 100 set | 0.00209 (0.00192-0.00210) | 0.00211 (0.00196-0.00222) | 0.00209 (0.00195-0.00225) | 0.00211 (0.00204-0.00219) |
| S5 | 1,000 views, all 100 fields set | 0.186 (0.178-0.192) | 0.189 (0.177-0.192) | 0.184 (0.175-0.193) | 0.192 (0.189-0.254) |
| S6 | the currency set 1,000 times in one entry | 1.833 (1.775-1.903) | 1.873 (1.815-1.893) | 1.860 (1.738-1.978) | 1.823 (1.785-1.923) |
| S7 | build 10,000 rows | 9.97 (9.10-11.34) | 10.63 (9.46-11.50) | 9.71 (9.55-11.63) | 10.05 (9.59-10.41) |
| S7 | take 10,000 rows away | 1.285 (1.215-1.350) | 1.360 (1.330-1.405) | 1.190 (1.115-1.280) | 1.190 (1.160-1.290) |
| S7 | bytes a row beyond L, touched | 131.5 | 224.9 | 0 | -0.8 |
| S8 | a keystroke in the draft | 0.00029 | 0.00029 | 0.00030 | 0.00029 |
| S9 | (a1) reset, then build; for E and EI (c) | 11.03 (10.59-11.95) | 11.31 (10.69-12.44) | 12.19 (10.99-12.97) | 11.27 (10.33-11.91) |
| S9 | (a2) one update to disjoint keys | | | 12.47 (11.47-12.73) | 13.25 (11.50-13.37) |
| S9 | (b) the same keys, every row patched | | | 1.800 (1.605-1.865) | 1.765 (1.680-1.985) |
| S9 | (d) the table's region hidden and shown | | | 11.94 (11.17-12.36) | 11.59 (10.89-12.34) |
| S10 | 1 MB mirror, one keystroke | 0.695 (0.690-0.720) | 0.705 (0.695-0.850) | | |
| S11 | 1 MB mirror live, an unrelated field set | 0.00203 | 0.00208 | | |
| S13 (i) | the list's view over 10,000 unchanged rows | | | 0.0384 (0.0382-0.0389) | 0.0384 (0.0382-0.0386) |
| S13 (i) | the same over 100,000 | | | 0.387 (0.384-0.404) | 0.399 (0.388-0.586) |

S3 at 100,000 rows is bimodal by pass in both `L` and `LI`, as it was in the first run: each has passes near 0.39 and passes near 0.58, so their medians differ by which mode three passes fell in.
S10's memory: `E` 63 pages after five keystrokes, `EI` 62.

`cd_dirty`, with `L` on the scan as it was and `LI` on cdom's pair table (S12, n distinct asks of an empty function, then the flush):

| n | L, the scan | LI, the table |
| ---: | ---: | ---: |
| 4 | 0.00015 (0.00014-0.00015) | 0.00018 (0.00017-0.00018) |
| 1,000 | 0.274 (0.273-0.278) | 0.0164 (0.0163-0.0167) |
| 10,000 | 26.28 (26.15-26.34) | 0.334 (0.330-0.345) |

### Each prediction's verdict, implemented

| # | Verdict | Predicted | Measured |
| --- | --- | --- | --- |
| S1 | holds | `LI` 1.6 to 1.9, `EI` 1.9 to 2.3 | 1.86 (1.02 of `L`); 1.83, a little under the range (0.98 of `E`) |
| S2 | **refuted for `EI`**, holds for `LI` | `LI` 0.035 to 0.045; `EI` 0.07 to 0.10 and within 1.3 of `E` | 0.0390 (1.00 of `L`); 0.0858, in the range, but **1.34 of `E`** with no overlap of the spreads: the implementation costs 2.2 ns more a view than the prototype when 10,000 views run (the round's array, the depth check, the stored tick and its reset at the pass's end) |
| S3 | holds, `EI` a little over the range at 100,000 | `LI` 0.035 to 0.045 and 0.38 to 0.6; `EI` 0.0003 to 0.001 | 0.0397 and 0.574 (bimodal, as `L`); 0.00048 and 0.00132 (a little over at 100,000) |
| S4, S5 | holds | `EI` 0.0019 to 0.003 and 0.17 to 0.22 | 0.00211 and 0.189 |
| S6 | holds | S1 plus under 0.02 ms | `EI` S1 plus 0.04, `LI` S1 minus 0.04: within the spread of S1 |
| S7 build | holds | `LI` as `L`; `EI` 10.5 to 12 | 10.05 (1.04 of `L`, inside its spread); 10.63, 0.09 us a row over `L` |
| S7 take away | holds, faster than the range | `EI` 2 to 2.5 | 1.36: a row's forget, three unlinks and four frees are 0.017 us |
| S7 memory | holds | `LI` 0; `EI` 210 to 240 | -0.8; 224.9 |
| S8 | holds | under 0.0005 | 0.00029 |
| S9 | holds, `EI` faster than the range | `LI` as `L`; `EI` (c) 13 to 16 | (a1) 11.27, (a2) 13.25, (b) 1.765, (d) 11.59, each inside `L`'s spread or under it; `EI` 11.31 |
| S10, S11 | holds | as `E` | 0.705 and 0.00208 |
| S12 | **refuted on the ratio**, faster than the range at 1,000 | `LI` 0.02 to 0.06 and 0.2 to 0.6, ratio 8 to 14; 4 asks within 1.5 of `L` | 0.0164 and 0.334, **ratio 20.4** (the falsifier was 20); 4 asks 1.2 of `L`. The table for 10,000 asks is 640 KB, probed at random, and grows from 64 entries in every pass (the pass's end drops a grown table), so the large case pays cache misses and nine rehashes; it is still 79 times faster than the scan |
| S13 (i) | holds | as `L` | 0.0384 and 0.399 |

The hypothesis holds where the shape is the prototype's: `LI` is within the spread of `L` everywhere, and `EI`'s views in regions (S4, S5, S10, S11) are within 0.03 of `E`.
Rows as views (`EI`) cost what the real API makes them cost: 93 bytes a row more than the prototype's intrusive watchers (a region and a heap view with three watches), and 2.2 ns a view more when ten thousand run; the design's answer, a list as one view, does not pay either.

## What is in `bench/shared-state/`

| Path | What |
| --- | --- |
| `badge/a-rerun`, `b-module`, `c-version`, `e-state`, `drive.mjs` | the badge example in A, B, C and E, driven in headless Chrome |
| `lib/cd_state.h`, `cd_state.c` | the prototype, on cdom's public API |
| `test/` | its eight checks (`test.mjs`) and `mutate.sh`: four mutants, each caught by the check written for it |
| `bench/src/` | the benchmark app: one model and DOM, the entries A, B, C, CL, E, L |
| `bench/check.mjs`, `mutate-dom.sh` | every entry leaves the same DOM after every step; three mutants caught |
| `bench/run.mjs`, `summarize.mjs`, `results/` | the timed passes and their TSVs; `results/implemented/` is the implementation's run (`E`, `L`, `EI`, `LI`) |
| `caret/` | S14 |
| `run.sh` | everything, or `run.sh one S2 L` for one scenario of one entry |
