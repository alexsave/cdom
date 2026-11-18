# The keyed list: rows that are regions, built in a loop

Status: design, written 2026-11-17, not approved and not implemented.
Branch: `strings` (worktree `/Users/alex/Dev/cdom-strings`, at bfed813 when this was written).
Nothing here was measured: the benchmark section is a plan with predictions.
The code in "Lesson 5" and "The benchmark app" was type-checked against a copy of `cdom.h` with the new declarations (`clang --target=wasm32 -std=c23 -Wall -Wextra -Werror -fsyntax-only`), and nothing more.

## What was asked

Lesson 5 (`tutorial/05-todo/src/app.c`) is the heaviest lesson, and Alex wants three things.

1. Keyed-list rows that work like regions: a row records the ids made while it builds and frees them when it goes, so destroy is not needed, a changed row can simply be built again, and unchanged rows survive with their focus and DOM state.
2. One file for the app and one per row (`src/app.c`, `src/row.c`, a shared `src/todo.h`, `row_build(const Todo *t)`).
3. A recommendation on discovering the C files under `src/` as the page tool discovers the `.css` files.

The coordinator added amgi (`/Users/alex/Dev/amgi/ui/src`) as evidence from a real app, and asked that its screens get simpler too.

## Four findings that shape the design

### 1. `strings` is not the newest code for regions

`strings` forked from main at c386eb2.
It is 61 commits ahead of main's 9f0d928 and 28 behind it.
Those 28 commits are what amgi is written against, and they change the ground this design stands on:

| On main (9f0d928), not on `strings` | What it is |
| --- | --- |
| nested regions | `cd_region` has `up`, `kids` and `next`; a region shown while another builds is its child, and goes with it |
| `cd_on_free(r, fn, ctx)` | cleanups a region runs when it is taken away (stop a timer, stop audio, drop a binding) |
| `cd_region_forget(r)` | take a region away with no DOM op, when its DOM already went |
| `cd_dirty`, `cd_undirty`, `cd_render_now` | views drawn once in the flush that ends the entry |
| `src/cdom_ui.h` | `cd_el`, `cd_el_id`, `cd_el_text`, `cd_button`, `CD_ON_EVENT`, `CD_CTX_OF`, `CD_CTX_INDEX`, `cd_buf` |
| `cd_bound`, `cd_timer_slot` | bindings and timer slots |

`strings` still says "Regions do not nest (traps)".
So this design is written for the merged tree, and its first implementation step is the merge.
A row that "works like a region" has to mean main's region: ids, child regions and cleanups.

### 2. amgi never uses the keyed list

Every list in amgi is a region rebuilt whole on any change (`decks.c:62-104`, `deck.c:466-500`).
What that costs, in the code as it is:

- **It scales with the list.** One changed note rebuilds every row of the page, about 25 nodes a row.
- **It loses focus and typing.** `deck.c` guards against its own rebuild with a 64-bit FNV hash of everything the rows draw from (`page_hash`, `rows_hash`, 30 lines), so that an answer that changed nothing keeps the open editor's typing, caret and focus.
  When something did change, for example `deleting`, every row is rebuilt, the editor is built again by `editor_open`, and what was typed is gone.
- **Rows keep ids in side arrays.** `row_li[i]` and `row_edit[i]`, realloc'd on every build, exist so that `editor_open` and `editor_close` can reach a row later.
- **Handlers carry the row index.** `cd_on(id, CD_EV_CLICK, edit_pressed, CTX_OF(i))`, read back with `CTX_INDEX(ctx)`.
  The index is valid only because any change rebuilds every row.

amgi avoids `cd_list_update` because it costs more code than a loop: three callbacks, a side table, and a destroy that must free by hand.
That is the same weight lesson 5 shows.

### 3. A handler wired inline is valid, with one condition

amgi writes `cd_on(el_id(...), ...)` right after the element is opened, before it is closed.
`cd_on` (`src/cdom.c:559`) registers the handler, then calls `cd_sel(id)` and `cd_listen(id, ev)`.
`cd_sel` holds a walk of zero steps from `id`; `cd_listen` sends it (`j_walk(id, 0, 0)`, the shim's `walk`: `last = nodes[id]`) and then names and marks that node.
The shim's stack of open elements (`stack`, `depth`) is not touched, so everything built after the call still goes into the open element.

So it is valid whenever `id` already names a node, which `cd_open_id` and `cd_el_id` guarantee.
The condition: after `cd_on(id, ...)` the last node is `id`'s node.
Right after `cd_open_id` that changes nothing.
After a child was opened, a later `cd_attr`, `cd_class`, `cd_value`, `cd_slot` or `cd_listen` lands on `id`'s node, not on the child.
`cd_name_el` and `cd_name_text` are not affected (they use `lastElement` and `lastText`).
An id made with `cd_id_new()` and not yet named throws in the shim (`nodes[id]` is undefined).
The header's sentence for `cd_on` gains this, and the lessons wire inline, which is shorter and keeps the handler next to its element.

### 4. The weight is not nearly all bookkeeping

Counted on `tutorial/05-todo/src/app.c` as it is today (266 lines, 200 of them code):

| Part | Code lines |
| --- | ---: |
| keyed-list bookkeeping (`keys[]`, `ItemView`, the free list, `view_new`, the forward declarations, patch, destroy, `TODO_FNS`, the casts) | about 45 |
| the CSS string in `style()` | 32 |
| the page (`cd_main`, `filter_button`) | about 38 |
| the model, the form's submit, the filters, the footer | about 85 |

This design removes the first row.
The page-tool design removes the second.
The rest is the app, and React's version is shorter there because of JSX, controlled inputs and the garbage collector, not because of the list.
React's version in the lesson's README is 53 lines, not about 30.

## The design in one page

```c
static void render(void) {
  cd_list_begin(list_ul, CD_LIST_ALL);
  for (uint32_t i = 0; i < ntodos; i++) {
    const Todo *t = &todos[i];
    if (shown(t) && cd_list_row(t->id, t->done)) cd_list_row_end(row_build(t));
  }
  cd_list_end();
}
```

1. **A list is a loop.**
   The app walks its own data and names each row to show, in order: its key and its revision.
   `cd_list_row` answers whether the row must be built now, exactly as `cd_region_show` does for a region.
   There is no callback, no table of functions, no `keys[]` array, no `ctx`, and the build is a plain call with a typed argument.
2. **A row is a region the list owns.**
   Every id made between `cd_list_row` and `cd_list_row_end` is the row's.
   A region shown in between is the row's child, and a `cd_on_free` cleanup registered in between is the row's cleanup.
   When the row goes, all of it goes, in the order a region is taken away.
3. **The revision decides.**
   A key that is new is built.
   A key whose revision is the one its row was built with is not touched: no call into the app, no DOM operation.
   A key whose revision changed is built again and the new row replaces the old one in one DOM operation, which carries the keyboard focus across.
   A hot list patches in place instead (`cd_list_row_patch`), and is told the revision the row shows.
4. **A list belongs to its parent node.**
   The rows are kept by cdom under the parent's id, the way a node's `cd_on` handlers are.
   When that id is freed (a region or a row that made it goes), the list goes with it.
   There is no `cd_list` struct in the app and no `cd_list_forget` to remember.
5. **A handler gets a key.**
   `cd_on_key(id, ev, fn, key)` registers `fn(e, key)` with a 64-bit number, so a row's handler is given the row's key, with no cast and no pointer into an array that moves.
6. **Ids are recorded in one table.**
   Regions and rows record their ids as a chain through one id-indexed array, 4 bytes an id.
   A region no longer owns a heap array of ids, and a row costs no allocation.

The diff itself (trimmed ends, the swap of the two ends, the key map, the longest increasing run) is kept; what changes is when it learns the keys.

## The API as cdom.h would show it

Written for the merged tree (main's regions and `cdom_ui.h`, this branch's strings).

```c
/* ---- events ---- (added, after cd_on) */
/* cd_on for a handler that needs a number, not a pointer: a row's key, an index, a tab. fn(e, key) is
 * called with the key given here. It is cd_on in every other way: the same (id, ev) holds one handler,
 * of either form, and registering again replaces it. */
typedef void (*cd_key_fn)(const cd_event_rec *e, uint64_t key);
void cd_on_key(cd_id id, uint32_t ev, cd_key_fn fn, uint64_t key);

/* ---- regions ---- (the struct, and two paragraphs of the comment) */
typedef struct cd_region {
  cd_id parent, root;
  cd_id ids;                            /* cdom's: the id recorded last; the others follow it in cdom's chain */
  int shown;
  struct cd_region *up;                 /* the parent region (set by the app, or by showing inside one) */
  struct cd_region *kids, *next;        /* cdom's: the child regions shown now, newest first */
  cd_cleanup *fns; uint32_t nfns, fcap; /* cdom's: the cleanups */
} cd_region;
/* In the comment, the sentence about lists becomes:
 * A list whose parent node was made while the region built is the region's: it goes when the region
 * does, before the region's cleanups, with its rows' regions and cleanups.
 * A region shown while a list's row builds is that row's child, and goes when the row goes or is
 * built again. cd_on_free(0, ...) while a row builds registers the cleanup on the row.
 * A region is not handed to a row by r->up: a region inside a row is shown by the row's build. */

/* ---- keyed list: the rows among one node's children, one row per key ----
 *   cd_list_begin(ul, CD_LIST_ALL);
 *   for (each thing to show, in order)
 *     if (cd_list_row(key, rev)) cd_list_row_end(row_build(thing));
 *   cd_list_end();
 * The app names the rows to show, first to last; cdom brings the page in line when the pass ends: a key
 * that is new gets a row, a key that is gone loses its row, and rows whose order changed are moved, with
 * as few moves as the change needs. A key names one row for as long as it is shown; the same key twice
 * in one pass traps when cdom can tell, and is wrong when it cannot.
 * A row is a region the list owns. Between cd_list_row and cd_list_row_end, every id made is the row's,
 * a region shown is the row's child, and a cd_on_free cleanup is the row's. When the row goes they go, in
 * a region's order: child regions and lists, cleanups, the row's node, its ids. So nothing outside a
 * row's build keeps one of its ids, except through cd_list_row_patch below.
 * The rows are kept under the parent's id, like a node's handlers: there is nothing for the app to hold,
 * and when that id is freed the list goes with it. A pass must end before the entry from JS returns
 * (traps in the flush). Passes nest 8 deep: a row's build may run the pass of a list inside the row. */

/* cd_list_begin's end: where the rows are among the parent's children.
 * CD_LIST_ALL: while the list has rows they are all of the parent's children, so taking every row away
 *   is one clear, and a first fill of 64 rows or more is built off the document. With no rows the
 *   parent may hold something else (an empty state), which the app takes away after a pass that added rows.
 * CD_APPEND: fixed children may come before the rows, which run to the parent's end.
 * an id: the first fixed child after the rows.
 * It is the same in every pass of one parent (traps). */
#define CD_LIST_ALL 0xfffffffeu
void cd_list_begin(cd_id parent, cd_id end);

/* The next row to show. rev is the row's revision: any number that changes whenever what the row shows
 * changes (a flag, a counter in the model, a hash of the fields).
 * Returns 1 when the row must be built now: the key is new, or rev is not the one its row was built
 * with. cd_at is then set (leave it), and the caller builds one element, the row's root, and passes
 * its id to cd_list_row_end. A row built again takes the old row's place in one operation; if the
 * keyboard focus was inside the old row it moves to the node at the same path in the new one (the same
 * child at each level, as cd_event_rec's path counts), when that node has the same tag. Nothing else of
 * the old row is carried: not a field's typed text or caret, not a scroll position, not a running
 * transition. A row that holds such state patches (below).
 * A row being built is not in the document until the pass ends: do not measure or focus it in the build.
 * Returns 0 when the row is on the page and rev is the one it shows: nothing is called and nothing is written. */
int cd_list_row(uint64_t key, uint64_t rev);
void cd_list_row_end(cd_id root);

/* The same for a list that writes a changed row in place. CD_ROW_SAME and CD_ROW_BUILD are
 * cd_list_row's 0 and 1. CD_ROW_PATCH: the row is on the page and its rev changed; *root is its root
 * and *was the rev it shows. The caller writes what changed, through the root (cd_sel and a walk,
 * cd_set_class) or through ids the build kept in the model, and makes no id. The row then shows rev.
 * There is no cd_list_row_end after a patch. */
enum { CD_ROW_SAME, CD_ROW_BUILD, CD_ROW_PATCH };
int cd_list_row_patch(uint64_t key, uint64_t rev, cd_id *root, uint64_t *was);

/* Ends the pass: rows not named are taken away, and the others are put in the order named. */
void cd_list_end(void);

/* The list's DOM already went by a call of the app's own (cd_clear of the parent or of an ancestor):
 * take every row away as a region is forgotten, with no DOM op. Not needed when the parent's id is
 * freed, which does the same. */
void cd_list_forget(cd_id parent);
```

### What is deleted

| Deleted | Replaced by |
| --- | --- |
| `cd_item` | nothing in the header; cdom's own row record (`cd_row`, in `cdom.c`) |
| `cd_list` (the app's struct), `owns_parent`, `end` as fields | the parent's id, and `cd_list_begin`'s `end` |
| `cd_list_fns`, `create`, `patch`, `destroy`, their `ctx` | the app's loop; `cd_list_row`, `cd_list_row_patch`; the row's region |
| `cd_list_update(L, keys, n, f)` | `cd_list_begin` ... `cd_list_end` |
| `cd_list_forget(L, f)` | freeing the parent's id; `cd_list_forget(parent)` for the one case left |
| `cd_region`'s `ids` array, `n`, `cap` | `ids`, the head of the chain |
| `CD_CTX_OF`, `CD_CTX_INDEX` (`cdom_ui.h`) | `cd_on_key` |
| `CD_ON_EVENT` (`cdom_ui.h`) | C23's unnamed parameters, which `strings` already builds with: `static void f(const cd_event_rec *, void *)` |
| the header's "drop a list that lived inside a region with cd_list_forget" | the list goes with its parent's id |

There is no compatibility layer: every caller in the tree changes in the same series (the lessons, the benchmark app, `tests/listfuzz.c`, `tests/unit.c`).

### Why keys and revisions are 64 bits

amgi's keys are Anki note ids, which are milliseconds since 1970 and do not fit in 32 bits.
Its natural revision is a hash of the note's fields, and a 32-bit hash would leave one changed row in four billion unredrawn.
wasm32 has 64-bit integers, so a compare costs the same.
The row record is 24 bytes (`key`, `rev`, `node`, `ids`), which is what the benchmark app spends per row today: 12 for the `cd_item`, 8 for its `RowView`, 4 for the free list.
A 32-bit id passes with no cast: `cd_list_row(t->id, t->done)`.

## How a row records its ids

### One chain for regions and rows

Today a region records into a heap array it owns, and the list switches recording off.
The new rule has one owner:

```c
static cd_id *chain;   /* by id, beside tags[] and alive[]: the id recorded before this one in the same scope, or 0 */
static cd_id *rec;     /* where the scope building now keeps its newest id: &region->ids, or the pass's row; 0: none */

cd_id cd_id_new(void) {
  ...
  if (rec) { chain[id] = *rec; *rec = id; }
  return id;
}
```

- A scope's ids are a chain from its newest id back to its first; taking the scope away walks it.
- Recording an id is two stores, and no scope allocates for its ids.
  The table costs 4 bytes an id, grown with `tags[]`.
- `cd_region.ids` becomes the chain's head, and the region's `n` and `cap` go.
  amgi already met the cost of the arrays (its commit 087e883 is about region id buffers).
- A row's record holds `node` (its root) and `ids` (its chain's head).
  A row with one id, as in the benchmark, is a chain of one.
- `cd_list_row_end(root)` checks that `root` is in the chain just recorded (a short walk) and traps otherwise, so a stale or foreign id is caught at the call.

### What a row owns beyond ids

Main's `take()` is the model: child regions newest first, then cleanups, then the one DOM op, then the ids.
A row is taken the same way, with the list doing the DOM op (a remove, a replace, or its share of one clear).

A plain row (ids only) has no record but its chain.
A row whose build showed a region or registered a cleanup needs `kids` and `fns`, so the list allocates a `cd_region` for that row alone, the first time one is needed, and keeps its address in cdom's pair table under the row's root id.
One flag byte per id says what hangs off it, extending today's `on_ids[]`:

| Bit | Meaning | Checked when |
| --- | --- | --- |
| 1 | the id has `cd_on` or `cd_on_key` handlers | the id is freed (today's rule) |
| 2 | the id is a list's parent | the id is freed, and when its scope is taken |
| 4 | the id is a row's root with a region record | the row is taken |

"Freeing an id with no handler costs one byte read" stays true, for all three.

### Composition

| Case | Rule | Why it is sound |
| --- | --- | --- |
| a region shown inside a row's build | the row's child; taken when the row goes or is built again, with its cleanups | main's rule for a region shown inside a region, with the row as the parent |
| a cleanup in a row (`cd_on_free(0, ...)`, a `cd_bound`, a chip's audio) | the row's; runs before the row's node and ids go | the same |
| a region shown later, from an event, inside a row | not supported: `r->up` names a region, and a row is not one the app can name | write the `if` in the row's build and put what it tests in the row's `rev`; the row is the unit that comes and goes |
| a list inside a row (its parent made in the row's build) | the inner list's parent id is in the row's chain; when the row is taken, the lists on its ids are taken first, rows and all, with no DOM op | the row's one DOM op removes their nodes; their state is cdom's, so no struct of the app is left stale |
| a list inside a region | the same; `cd_list_forget` from a cleanup is no longer needed | the same |
| a pass inside a row's build | passes are a stack of 8; the inner pass's rows record into themselves; ids made between the inner rows record into the outer row | each `cd_list_row` saves `rec` and each `cd_list_row_end` restores it |
| moves | a move is `cd_move` of the root; ownership is the chain, which a move does not touch | position and ownership are separate |
| the clear fast path (`CD_LIST_ALL`, every row gone) | one `cd_clear`, then each row is taken with no DOM op | as today, minus the destroy call per row |
| the off-document first fill | the list began the pass empty; at the 64th appended row the parent is detached, and attached again in `cd_list_end` | the pass does not know its length up front; 63 rows are built in the document, of 1,000 or 10,000 |
| a row built again | the old row's child regions, lists and cleanups are taken and its ids other than the root freed when `cd_list_row` returns 1; the new row is built off the document; `cd_list_row_end` replaces the old root with the new one and frees the old root's id | freed ids are reused by the new build, so `cd_ids_high` stays flat |

Traps: `cd_list_row` outside a pass or before the last row's `cd_list_row_end`; `cd_list_begin` on a parent whose pass is open; `cd_list_end` with a row open; a pass still open at the flush; a row or a list's parent freed while its pass is open; the 9th nested pass.

## The diff as a pass

`cd_list_update` saw every key before it built anything.
A pass sees one key at a time, and must build a new row when it is named.
What is kept, and what moves:

**At `cd_list_row(key, rev)`**, with `old` the rows from the last pass and `cur` a cursor into it:

1. `old[cur].key == key`: the row is in order.
   This is every row of an unchanged list, and it is one compare.
2. Else `old[cur + 1].key == key`: one row was dropped.
   `cur` skips it, and no map is built.
   This is "remove one row".
3. Else, if every old row is already claimed, the key is new.
4. Else the first time builds the key map over the unclaimed old rows (today's `scratch` and `map_put`, kept per list so that a nested pass cannot overwrite it), and looks the key up.

A claimed old row is copied to the new array with its old index; if its `rev` differs, the answer is build (or patch).

**Where a row that must be built goes**:

- A new row, when every old row is already claimed: at the list's tail (`cd_at(parent, end)`).
  That is the final place, because every later row is new too.
  It covers the first fill, the append, and adding a todo.
- Any other new row, and every row built again: off the document (`cd_at(CD_DETACHED, CD_APPEND)`).
  `cd_list_end` places a new one with one insert; `cd_list_row_end` puts a rebuilt one in the old row's place.

**At `cd_list_end`**, with every key known:

1. Old rows nobody claimed are taken: one `cd_clear` when `CD_LIST_ALL` and none was claimed, else a `cd_remove` each.
2. Survivors are put in order by today's code, driven by the old indexes instead of the keys: the trimmed ends and the swap of the two ends when the pass was a pure reorder, else the longest increasing run.
   The moves are the same moves.
3. Rows built off the document are inserted before their successor.

Two imports are added to the shim: `put(parent, id, before)`, which inserts a node that has no parent yet (`move` uses the node's own parent), and `replace(old, new)`, below.
The row buffer is allocated at `cd_list_begin` with the last pass's length, doubles when it must, and the old buffer is freed at `cd_list_end`, so a list at rest holds one buffer, as today.

What a pass cannot do that `cd_list_update` did: trim a matching tail before it has seen it.
The cost is the key map on a swap (999 puts), which today's code avoids, and one extra call per row that is new in the middle of a list or replaces the whole list.
Both are priced under "Predictions".

## Build again, or patch in place

### The rule

| | Build again (`cd_list_row`) | Patch (`cd_list_row_patch`) |
| --- | --- | --- |
| The code | one function, the same for a new row and a changed one | a build and a patch that must agree |
| Per changed row | the whole row is made, one replace, the old row's ids freed | only the changed values are written |
| Kept across a change | the keyboard focus, by path | everything: the element is the same |
| Lost across a change | typed text and the caret in a field, IME composition, scroll inside the row, a running transition or animation, text selection, media playback | nothing |
| Right when | the row is text, buttons, links and checkboxes whose state is in the model | the row holds a text field or anything else the model does not, or changes are frequent and small against the row (a counter per row, a progress bar, the benchmark) |

Build again is the default because it is one function and cannot drift.
A row that the user types in must patch, or must not change its `rev` while it is being typed in (commit on change or blur).

### Focus

A rebuilt row is the row the user just acted on: in lesson 5, a keyboard user presses Space on a checkbox and that row's `rev` changes.
Without care the focused checkbox is removed, focus falls to the body, and the user's place is lost.
That is the accessibility regression the whole-list rebuild has, moved to one row.

So the replace is one shim operation that owns the problem:

```js
// replace(oldId, newId): the new row takes the old row's place. If the focus was inside the old row, it
// moves to the node at the same path in the new one.
function replace(oldId, newId) {
  const was = nodes[oldId], now = nodes[newId], active = doc.activeElement;
  let path = null;
  if (active && active !== doc.body && was.contains(active)) {
    path = [];
    for (let n = active; n !== was; n = n.parentNode) path.push(indexAmongElements(n));
  }
  was.replaceWith(now);
  forget(oldId);
  if (path) {
    let n = now;
    for (let i = path.length; n && i--;) n = n.children[path[i]];
    if (n && n.tagName === active.tagName) n.focus({ preventScroll: true });
  }
}
```

- A row with no focus inside pays one read of `activeElement` and one `contains`.
- The path is the one `cd_event_rec.path` already uses: the element-child index at each level.
- The tag must match, so a build that changed shape never puts the focus on a different kind of control.
  It can still land on another control of the same tag when the build puts something that comes and goes before the controls.
  The rule for a row is therefore: what comes and goes is built after the controls it would shift, or the row patches.
  The header says so, and the test plan has the case.
- When no node matches, or it cannot take focus (a button that is now disabled), the focus is lost as it is today.
  An app that disables the control the user is on should use `aria-disabled`.
- The removal fires `focusout` and the `focus()` fires `focusin`; cdom already queues events raised while C runs and delivers them after, and the old ids' handlers are gone by then.

### The cost on the benchmark

The benchmark patches, so its numbers do not change (see "Predictions").
What it would pay to build again, from today's per-row costs (create is 2.2 ms of script per 1,000 rows; a replace is one DOM call and two reads):

| Operation | Patch (today and after) | Built again | Why |
| --- | --- | --- | --- |
| update every 10th row (1,000 rows, 4x CPU throttle) | 100 text writes: script 0.9 ms, total 15.7 ms | 100 rows made and replaced, about 2.8 us each, times 4: script 1.6 to 2.4 ms; total 17.0 to 19.5 ms | 100 new `<tr>` need style and layout that a text write does not |
| select row (4x) | 2 class writes: script 0.4 ms, total 4.1 ms | 2 rows made and replaced: script 0.4 to 0.5 ms; total within 5% | two rows are nothing against the click's own cost |

So building again is free for a change that touches a row or two, and costs about a millisecond of script per hundred table rows.
The measurement plan has an entry that measures exactly this.

## How a row's handlers find their data

Today: `cd_on(v->check, CD_EV_CHANGE, on_check, (void *)(uintptr_t)t->id)` and `find((uint32_t)(uintptr_t)ctx)`.
amgi: `CTX_OF(i)` and `CTX_INDEX(ctx)`, with the row index.

A pointer to the model cannot be the ctx: `todos[]` moves when a todo is removed, and amgi's notes are replaced by every answer.
An index cannot either once rows survive a change: after the row above is deleted, a surviving row's handler still holds its old index.
amgi's index is correct today only because every change rebuilds every row, so it must move to keys in the same change that moves it to this list.

What a handler needs is the row's key, as a number:

```c
static void on_check(const cd_event_rec *, uint64_t id) { todo_toggle(id); }
...
cd_on_key(cd_open_id("input"), CD_EV_CHANGE, on_check, t->id);
```

- The key is passed where the handler is registered, as a ctx is, so the wiring is in the code.
- It is 64 bits because amgi's keys are, and a pointer is 32 on wasm32.
- It also replaces the cast for a tab or a filter index (lessons 4 and 5, `on_tab` and `on_filter`).
- The rule for the two forms: a pointer to something that outlives the node goes in `cd_on`'s ctx; a number goes in `cd_on_key`'s key.

Rejected:

| Alternative | Why not |
| --- | --- |
| the node's tag (`cd_tag_set`, `cd_tag(e->id)`) | 32 bits; and amgi poisons `cd_tag` on purpose (`app.h:68`), because a handler should get its data from its registration |
| cdom hands the handler the key of the row its node was built in (`e->row`) | hidden capture: nothing at the registration says where the number comes from, and it needs a table from every id to its row |
| a typed ctx through a macro that generates a trampoline | a second way to write a handler, and the generated function is the hidden wiring Alex does not want |
| stable model storage, so a pointer can be the ctx | changes every app's model to suit the list |

## Lesson 5, before and after

### Before: `src/app.c`, 266 lines

The file is `tutorial/05-todo/src/app.c` on `strings`.
The parts this design changes, in full (lines 25 to 122 and 155 to 177); the rest (the model's struct, `on_submit`, `on_clear`, `filter_button`, `style()`, `cd_main`) changes only as marked under "After".

```c
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

static const cd_list_fns TODO_FNS = {0, item_create, item_patch, item_destroy};

/* The view of the model: the keys the filter lets through, then the footer's count. */
static void render(void) {
  uint32_t n = 0, left = 0;
  for (uint32_t i = 0; i < ntodos; i++) {
    if (shown(&todos[i])) keys[n++] = todos[i].id;
    if (!todos[i].done) left++;
  }
  cd_list_update(&list, keys, n, &TODO_FNS);

  /* The count changes with every edit, so it is bytes with a length: cd_tmp, read by the call that uses it. */
  char buf[32];
  int len = snprintf(buf, sizeof buf, "%u %s left", left, left == 1 ? "item" : "items");
  cd_set_text_node(left_text, cd_tmp(buf, (uint32_t)len));
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
```

### After: `src/todo.h`, 17 lines

```c
/* Lesson 5's shared header: the todo, and what app.c and row.c call in each other. */
#include "cdom.h"

/* A field's value is copied out only for the length of one handler. A longer value is cut to the whole
 * characters that fit. */
#define TEXT_CAP 256

/* An id names a todo for its whole life and is never reused, so a row whose id is gone can never be
 * mistaken for a newer todo. */
typedef struct { uint64_t id; uint32_t len; char text[TEXT_CAP]; uint8_t done; } Todo;

/* row.c: builds the row of one todo where the list put the cursor, and returns its root. */
cd_id row_build(const Todo *t);

/* app.c: what a row's controls do. Each changes the todos and draws the list again. */
void todo_toggle(uint64_t id);
void todo_remove(uint64_t id);
```

### After: `src/row.c`, 28 lines

```c
/* Lesson 5, one row: the <li> of one todo, the way a React component is one file. The row is built from
 * the todo and from nothing else. When the todo changes, the list builds the row again and takes the old
 * one away, with every id and handler made here. */
#include "todo.h"

/* A handler gets the key it was registered with: the todo's id, never a pointer into the array, which
 * moves when a todo is removed. */
static void on_check(const cd_event_rec *, uint64_t id) { todo_toggle(id); }
static void on_remove(const cd_event_rec *, uint64_t id) { todo_remove(id); }

cd_id row_build(const Todo *t) {
  cd_id row = cd_open_id("li");
  cd_class(t->done ? "item done" : "item");

  cd_on_key(cd_open_id("input"), CD_EV_CHANGE, on_check, t->id);
  cd_attr("type", "checkbox");
  if (t->done) cd_attr("checked", "");
  cd_close();

  cd_open("span"); cd_class("text"); cd_text(cd_tmp(t->text, t->len)); cd_close();

  cd_on_key(cd_open_id("button"), CD_EV_CLICK, on_remove, t->id);
  cd_class("remove"); cd_attr("aria-label", "Remove"); cd_text("×");
  cd_close();

  cd_close();
  return row;
}
```

The checkbox's state is the `checked` attribute, which is the state of an element that is new; a rebuilt row is always new.

### After: `src/app.c`, 196 lines

In full, except `on_submit` and `on_clear` (unchanged but for `t->id` being 64 bits), and `style()`'s string, which is today's.

```c
/* Lesson 5: the keyed list. The todos are a plain array of structs, and the list on the page shows the
 * ones the filter lets through. render() walks the array and names each todo to show, in order, with its
 * key and its revision. The list does what React does when it reconciles: a new key gets a row, a key
 * whose revision changed gets its row built again, a key that is gone loses its row, and a row that did
 * not change is not touched. The row itself is src/row.c. */
#include "todo.h"
#include <stdio.h>
#include <string.h>

#define TODO_CAP 256

static Todo todos[TODO_CAP];
static uint32_t ntodos;
static uint64_t next_id = 1;

enum { FILTER_ALL, FILTER_ACTIVE, FILTER_DONE };
static uint32_t filter = FILTER_ALL;

static cd_id form, input, list_ul, left_text, clear_btn;
static cd_id filter_btn[3];

static Todo *find(uint64_t id) {
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

/* The view of the model. Each todo the filter lets through is one cd_list_row call: its key is the todo's
 * id, and its revision is the one thing a row shows that can change, the done flag. cd_list_row says yes
 * when the row must be built now: the key is new, or its revision is not the one its row was built with. */
static void render(void) {
  uint32_t left = 0;
  cd_list_begin(list_ul, CD_LIST_ALL);
  for (uint32_t i = 0; i < ntodos; i++) {
    const Todo *t = &todos[i];
    if (!t->done) left++;
    if (shown(t) && cd_list_row(t->id, t->done)) cd_list_row_end(row_build(t));
  }
  cd_list_end();

  /* The count changes with every edit, so it is bytes with a length: cd_tmp, read by the call that uses it. */
  char buf[32];
  int len = snprintf(buf, sizeof buf, "%u %s left", left, left == 1 ? "item" : "items");
  cd_set_text_node(left_text, cd_tmp(buf, (uint32_t)len));
}

/* ---- what the rows call (todo.h) ---- */

void todo_toggle(uint64_t id) {
  Todo *t = find(id);
  if (!t) return;
  t->done = !t->done;
  render();
}

void todo_remove(uint64_t id) {
  for (uint32_t i = 0; i < ntodos; i++) {
    if (todos[i].id != id) continue;
    memmove(&todos[i], &todos[i + 1], (ntodos - i - 1) * sizeof(Todo));
    ntodos--;
    break;
  }
  render();
}

/* ---- handlers ---- */

static int is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

static void on_submit(const cd_event_rec *, void *) { /* ... today's 24 lines, unchanged ... */ }

static void on_filter(const cd_event_rec *, uint64_t k) {
  filter = k;
  for (uint32_t i = 0; i < 3; i++) {
    cd_set_attr(filter_btn[i], "aria-pressed", i == k ? "true" : "false");
  }
  render();
}

static void on_clear(const cd_event_rec *, void *) { /* ... today's 8 lines, unchanged ... */ }

static cd_id filter_button(const char *label, uint32_t k) {
  cd_id id = cd_open_id("button");
  cd_attr("aria-pressed", k == filter ? "true" : "false");
  cd_text(label);
  cd_close();
  return id;
}

static void style(void) { /* ... today's 32 lines, until the page tool moves them to src/app.css ... */ }

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

  /* The list's own element: it holds nothing but the rows, which render() says with CD_LIST_ALL. */
  list_ul = cd_open_id("ul"); cd_class("items"); cd_close();

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
  cd_on(form, CD_EV_SUBMIT, on_submit, 0);
  for (uint32_t k = 0; k < 3; k++) cd_on_key(filter_btn[k], CD_EV_CLICK, on_filter, k);
  cd_on(clear_btn, CD_EV_CLICK, on_clear, 0);

  render();
}
```

### The line counts

| | Lines | Code lines (no blanks, no comments) | Code lines without the CSS |
| --- | ---: | ---: | ---: |
| before: `app.c` | 266 | 200 | 168 |
| after: `todo.h` | 17 | 6 | 6 |
| after: `row.c` | 28 | 17 | 17 |
| after: `app.c` | 196 | 151 | 119 |
| after, the three together | 241 | 174 | 142 |
| React, in the README | 58 | 53 | 53 |

The list's part of the lesson goes from about 75 code lines (the bookkeeping, the three callbacks, the two row handlers) to 31: `render`'s 10 and `row.c`'s 17, and the 4 of `todo.h`'s declarations.
The lesson as a whole loses 26 code lines of 200.
With the CSS in `src/app.css` it is 142 code lines of C against React's 53.

Gone: `ItemView`, `views[]`, the free list, `view_new`, `keys[]`, `find()` in every callback, the three callbacks, `TODO_FNS`, `cd_id_free` by hand, the `uintptr_t` casts, the forward declarations, and the `cd_list` struct with its two fields.

Lesson 6's laps become `if (cd_list_row(lap, 0)) cd_list_row_end(lap_build(lap));` in a loop, and its empty `lap_patch` and `lap_destroy` go.

## amgi, before and after

Not compiled: amgi is on main's API and on `CD_LIT`, and this shows the screen after the strings change and this one.

### `decks.c`, the deck rows, before (lines 62 to 104)

```c
ON_RENDER(rows_view) {
  if (setup) {
    firstrun_show(page_root());
    return;
  }
  cd_region_hide(&rows);
  if (!cd_region_show(&rows, 1)) return;
  cd_at(rows.parent, 0);
  if (!ready()) {
    el(CD_LIT("div"), CD_LIT("empty-deck-state"));
    cd_open(CD_LIT("p"));
    cd_attr(CD_LIT("style"), CD_LIT("max-width: 32rem; text-align: center; color: var(--text-secondary);"));
    cd_text(cd_internz(present(mode)->title));
    cd_close();
    cd_close();
  } else if (!ndecks) {
    el(CD_LIT("div"), CD_LIT("empty-deck-state"));
    cd_on(el_id(CD_LIT("button"), CD_LIT("create-first-deck-btn")), CD_EV_CLICK, new_deck, 0);
    icon(ICON_PLUS, 0, CD_LIT("height: 20px; width: 20px;"));
    el_text(CD_LIT("span"), 0, CD_LIT("Create your first deck"));
    cd_close();
    cd_close();
  } else {
    for (uint32_t i = 0; i < ndecks; i++) {
      cd_on(el_id(CD_LIT("div"), CD_LIT("deck-item")), CD_EV_CLICK, open_deck, CTX_OF(i));
      el(CD_LIT("div"), CD_LIT("deck-info"));
      el_text(CD_LIT("h3"), 0, decks[i].name);
      cd_open(CD_LIT("small"));
      cd_text_num(decks[i].count);
      cd_text(CD_LIT(" note"));
      if (decks[i].count != 1) cd_text(CD_LIT("s"));
      cd_close();
      cd_close();
      cd_close();
    }
  }
  cd_region_end(&rows);
  if (ready()) starter_show(page_root());
}

ON_EVENT(open_deck) {
  uint32_t i = CTX_INDEX(ctx);
  if (i < ndecks) go_deck(decks[i].id);
}
```

### After

```c
static cd_region empty = {.up = &page_region}; /* what stands in for the rows: one element in .deck-list */
static cd_id deck_list;                        /* .deck-list, made in decks_build */

static void open_deck(const cd_event_rec *, uint64_t id) { go_deck(id); }

static cd_id deck_row(const deck *d) {
  cd_id row = cd_el_id("div", "deck-item");
  cd_on_key(row, CD_EV_CLICK, open_deck, d->id);
  cd_el("div", "deck-info");
  cd_el_text("h3", CD_NONE, cd_tmp(d->name.p, d->name.len));
  cd_open("small"); /* three text nodes, as DeckItem's JSX makes them */
  cd_text_num(d->count);
  cd_text(" note");
  if (d->count != 1) cd_text("s");
  cd_close();
  cd_close();
  cd_close();
  return row;
}

static void rows_view(void *) {
  if (setup) {
    firstrun_show(page_root());
    return;
  }
  uint32_t n = ready() ? ndecks : 0; /* not ready wins over whatever decks are still in hand */
  cd_list_begin(deck_list, CD_LIST_ALL);
  for (uint32_t i = 0; i < n; i++)
    if (cd_list_row(decks[i].id, deck_rev(&decks[i]))) cd_list_row_end(deck_row(&decks[i]));
  cd_list_end();

  cd_region_hide(&empty); /* it says the mode, which may have changed */
  if (!n && cd_region_show(&empty, 1)) {
    cd_at(deck_list, CD_APPEND);
    cd_el("div", "empty-deck-state");
    cd_name_open(empty.root);
    if (!ready()) {
      cd_open("p");
      cd_attr("style", "max-width: 32rem; text-align: center; color: var(--text-secondary);");
      cd_text(present(mode)->title);
      cd_close();
    } else {
      cd_on(cd_el_id("button", "create-first-deck-btn"), CD_EV_CLICK, new_deck, 0);
      icon(ICON_PLUS, 0, "height: 20px; width: 20px;");
      cd_el_text("span", CD_NONE, "Create your first deck");
      cd_close();
    }
    cd_close();
    cd_region_end(&empty);
  }
  if (ready()) starter_show(page_root());
}
```

`deck_rev` is the FNV-1a `mix` that `deck.c` already has, over the name's bytes and the count.

What changed for the user: a deck whose count changed is the one row replaced, the other rows keep their nodes, and a new deck is one insert.
What changed in the code: the row is a function of a deck, the handler gets the deck's id, and the index and its bounds check are gone.
The file is about as long as before; this screen was already simple, and the gain is behaviour.

### `deck.c`, the note rows

This is where the list pays in lines as well.

| Today | After |
| --- | --- |
| `page_hash()`, `mix`, `mix_strs`, `rows_hash`, `rows_gone` (about 35 lines): a whole-page hash so that an unchanged answer does not rebuild every row | a per-note `rev` from the same `mix` (the note's type, fields and tags, with `deleting == n->id`, `editing == n->id` and `langs`); an unchanged row is not touched, by the list |
| `row_li[]`, `row_edit[]`, realloc'd per build | gone: nothing outside a row's build needs its ids |
| `editor_open(i)`, `editor_close()`: set the row's class and the button's text by hand, then show the `editor` region under `row_li[i]` | the row's build writes `is-open`, "Close" and the panel when `editing == n->id`; opening and closing change that row's `rev`, and the list builds that one row again, with the focus carried to the same button |
| `cd_region editor = {.up = &body}` and `editor_gone` | the panel's regions, chips and cleanups are the row's children: `panel_build` is called inside the row's build |
| `CTX_OF(i)` on Edit and Delete | `cd_on_key(edit, CD_EV_CLICK, edit_pressed, n->id)` |
| a change to any note rebuilds every row and the open editor | a change to one note rebuilds that row; an open editor in another row keeps its typing, caret and focus |

One case needs a decision by amgi's manager: a save changes the edited note's own fields, so its row's `rev` changes and the open editor in it is built again.
Either the editor closes on save (then nothing is lost), or that row uses `cd_list_row_patch` and writes its two spans, keeping the panel.

### Which of amgi's helpers cdom should absorb

| amgi (`app.h`, `dom.c`) | Verdict |
| --- | --- |
| `el`, `el_id`, `el_text` | already in main's `cdom_ui.h` as `cd_el`, `cd_el_id`, `cd_el_text`; amgi's copies are deleted. In the merge each becomes a macro that passes its strings through `cd_s()`, and "no class" is `CD_NONE` |
| `ON_EVENT` (`CD_ON_EVENT`), `ON_RENDER`, `ON_ANSWER` | not absorbed, and `CD_ON_EVENT` is deleted: C23 leaves an unused parameter unnamed, and a plain signature is one way to write a handler |
| `CTX_OF`, `CTX_INDEX` (`CD_CTX_OF`, `CD_CTX_INDEX`) | deleted from both: `cd_on_key` |
| `tmp_text`, `attr_tmp` | gone with the strings change: `cd_text(cd_tmp(p, n))` |
| `row_li[i] = el_id(...)`, `cd_name_text(row_edit[i] = cd_id_new())` | not a helper to absorb: the arrays go. A row that patches keeps the id it writes through in its model struct, set by every build |
| `text_bound`, `text_bound_num`, `bool_bound` | stay amgi's, as `app.h` says; a binding made in a row's build is the row's (its unbind cleanup is registered on the row) |
| `icon`, `icon_with` | stay amgi's |
| `dirty(view, &alive)` with `cd_undirty` in the owner's cleanup | absorbed, as one call (below) |

### amgi's views and the two guards

amgi's views are `cd_dirty` renders, and each module guards its ask twice (`app.h:56-63`): `dirty()` asks only while the owner is up, and the owner's cleanup calls `cd_undirty` on each view.
The comment says why both are needed, and they do cover different vectors, so they are not a double fix to collapse.
But they are one rule, "a view lives as long as the region it draws", written by hand in every module, and it has an owner in cdom:

```c
/* cd_dirty for a view that draws r: asked only while r is shown, and a pending ask is dropped when r
 * is taken away. */
void cd_region_dirty(cd_region *r, cd_render_fn fn, void *ctx);
```

It replaces the per-module `dirty()` wrappers, the `shown` flags kept only for them, and the `cd_undirty` lines in the cleanups.
It is next to this design and not part of it: a list pass runs inside a view exactly as inside any function, and needs nothing from `cd_dirty`.
It is step 12, on its own.

## Files: one for the app, one per row

`src/app.c` holds the model, the page and `render()`.
`src/row.c` holds the row: its build and its handlers.
`src/todo.h` holds the model's type and the calls that cross: `row_build` one way, `todo_toggle` and `todo_remove` the other.
A row's stylesheet is `src/row.css`, found by the page tool's rule.

The header carries calls in both directions, which is the honest shape: in React the parent passes `toggle` and `remove` down as props, and here the row calls them by name.
There is no registration and no table: `render()` calls `row_build`, and the row's handlers call `todo_toggle`.

## Should the C files under `src/` be discovered?

**Recommendation: yes, by the page tool's rule, and `APP_SRC` keeps one meaning.**

> Every file whose name ends in `.c` under the app's `src/` directory, in any subfolder, is compiled into the module, in byte order of its path; a file or folder whose name starts with a dot is skipped, and nothing else is.
> `APP_SRC` names the sources that live outside `src/` (a library's files); naming a file under `src/` in it is an error.

- **Why discover.**
  A row is a `.c` and a `.css`; if one is found and the other must be listed, adding a row is a Makefile edit that is easy to forget for exactly one of the two.
  amgi already discovers by hand: `APP_SRC := $(sort $(wildcard src/*.c)) wire/wire.c`.
- **Why `APP_SRC` stays.**
  amgi's `wire/wire.c` is outside `src/`, and so are the sources of any library an app links.
  The variable keeps that one job, so there are not two ways to add a file of `src/`.
- **Headers.**
  Every `.h` under `src/` is a prerequisite of the module.
  Today an app's own header is not, so editing `todo.h` would not rebuild.
- **Order.**
  Byte order of the path, as for the CSS, so the link line and the string table's slot order are the same on every machine.
- **Add, remove, rename.**
  The same list file as the CSS: `$(APP_WASM).srclist`, written by the page tool's walk only when its text changes, and a prerequisite of the module.
  The tool's list mode takes the suffix, so one walk and one sort serve both.
  This step comes after the page tool exists.
- **The default.**
  `tools/new.sh` writes no `APP_SRC` line.

Is this the hidden wiring Alex dislikes?
No object is created and no call is made by discovery: every call between the files is still written out and resolved by the linker, and a missing or doubled definition is a link error.
What is found is which files are compiled, by a rule of one sentence, the one Alex chose for the CSS.

What it costs, plainly:

- A `.c` under `src/` that is not the app's (a scratch file, a host tool with a `main`) is compiled for wasm and usually fails loudly.
  It belongs outside `src/`, as the CSS rule says of a stylesheet that must not ship.
- One quiet case: a stray file that defines one of cdom's weak hooks (`cd_on_event`, `cd_on_frame`, `cd_on_timer`) changes the app with no error.
  The tests cover the rule, not this; it is the price of discovery and it is small.

## The benchmark app, before and after

`bench/cdom-keyed/src/app.c`, the view (lines 75 to 152 today; everything else is unchanged but for `keys` and `list` going).

### Before

```c
typedef struct { uint32_t rev; uint8_t sel; } RowView;
static RowView *views;
static uint32_t nviews, views_cap, *free_views, nfree_views;
static cd_list list;

static uint32_t view_new(void) {
  if (nfree_views) return free_views[--nfree_views];
  if (nviews == views_cap) {
    views_cap = views_cap ? views_cap * 2 : 1024;
    views = cd_realloc(views, views_cap * sizeof(RowView));
    free_views = cd_realloc(free_views, views_cap * 4);
  }
  return nviews++;
}

static cd_id row_create(void *, uint32_t i, uint32_t *data) {
  Row *r = &rows[i];
  uint32_t vi = view_new();
  RowView *v = &views[vi];
  cd_id tr = cd_id_new();
  v->rev = r->rev;
  v->sel = r->id == selected;
  cd_tag_set(tr, K_ROW | r->id);
  /* ... the row, from the template or from open, class and text ... */
  *data = vi;
  return tr;
}

static void row_patch(void *, uint32_t i, cd_item *it) {
  Row *r = &rows[i];
  RowView *v = &views[it->data];
  if (v->rev != r->rev) {
    v->rev = r->rev;
    cd_sel(it->node); /* tr > td[1] > a > text */
    cd_down(); cd_next(); cd_down(); cd_down();
    cd_value(LABEL(r));
  }
  uint8_t sel = r->id == selected;
  if (v->sel != sel) {
    v->sel = sel;
    cd_set_class(it->node, sel ? cd_s("danger") : cd_s(""));
  }
}

static void row_destroy(void *, cd_item *it) {
  free_views[nfree_views++] = it->data;
}

static const cd_list_fns ROW_FNS = {0, row_create, row_patch, row_destroy};

static void render(void) {
  for (uint32_t i = 0; i < nrows; i++) keys[i] = rows[i].id;
  cd_list_update(&list, keys, nrows, &ROW_FNS);
}
```

### After

```c
static cd_id tbody;

static cd_id row_build(const Row *r) {
  cd_id tr = cd_id_new();
  cd_tag_set(tr, K_ROW | r->id); /* a click reports which cell: 1 label, 2 remove */
  /* ... the row, exactly as today, with (r->id == selected) where v->sel was ... */
  return tr;
}

/* A row's revision is its label's revision with, in the low bit, whether the row is selected. The list
 * hands back the revision the row shows, so the two XORed say which of the two changed. */
static uint64_t row_rev(const Row *r) { return (uint64_t)r->rev << 1 | (r->id == selected); }

static void row_patch(const Row *r, cd_id tr, uint64_t changed) {
  if (changed >> 1) {
    cd_sel(tr); /* tr > td[1] > a > text */
    cd_down(); cd_next(); cd_down(); cd_down();
    cd_value(LABEL(r));
  }
  if (changed & 1) cd_set_class(tr, r->id == selected ? cd_s("danger") : cd_s(""));
}

static void render(void) {
  cd_list_begin(tbody, CD_LIST_ALL);
  for (uint32_t i = 0; i < nrows; i++) {
    const Row *r = &rows[i];
    uint64_t rev = row_rev(r), was;
    cd_id tr;
    int what = cd_list_row_patch(r->id, rev, &tr, &was);
    if (what == CD_ROW_BUILD) cd_list_row_end(row_build(r));
    else if (what == CD_ROW_PATCH) row_patch(r, tr, was ^ rev);
  }
  cd_list_end();
}
```

The file goes from 276 lines to 252.
`RowView`, `views`, the free list, `view_new`, `row_destroy`, `ROW_FNS`, `keys` and its realloc go: the benchmark carried the same side table as the lesson.
The click handling keeps its tag and its `cd_on_event`; the benchmark is the one place where one listener on the row and the cell's path are the point.

### What each operation does, before and after

| Operation | DOM operations | What changes in C |
| --- | --- | --- |
| create 1,000 / 10,000 | the same, except that the first 63 rows are built in the document | per row: no `view_new`, no indirect call; two stores for the chain; one call to `cd_list_row` and one to `cd_list_row_end` |
| replace 1,000 | the same DOM calls; 1,000 more imports (`put` after a build off the document, where today the row's close inserts) | the key map is over the old rows, built once |
| update every 10th | the same 100 text writes | 1,000 direct calls with one compare, where today there are 1,000 indirect calls that each load a `RowView` |
| select | the same 2 class writes | the same |
| swap | the same 2 moves | the key map is built (999 puts) where today the trimmed ends avoid it |
| remove one | the same remove | no map: the one-row lookahead |
| append 1,000 | the same | the same as create |
| clear | the same clear and 1,000 frees | no destroy call per row |

## Predictions

Written 2026-11-17, before anything is built or measured.
The baseline is the `strings` build of the keyed app as measured for the strings change (`bench/strings/results/jfb-cpu.tsv`, entry `st-s`, 15 iterations), built again from the commit before this change and run paired with the new build, twice over for the noise.
RESULTS.md's `cdom` column (2026-10-27, another harness pass and an older build) is given for scale; ratios are taken against the paired baseline, not against it.
"Beyond the noise" means by more than the gap between the two baseline entries of that run.
Each prediction is recorded as confirmed, refuted or not measurable in `docs/list-results.md`.

| # | Operation | RESULTS.md total / script | `st-s` total / script | Predicted after | Built on | Refuted by |
| --- | --- | --- | --- | --- | --- | --- |
| L1 | 01 create 1,000 | 29.9 / 3.1 | 29.9 / 2.2 | total 0.98 to 1.02 of baseline; script 2.1 to 2.4 | the per-row work is a few stores each way; 63 of 1,000 rows lose the off-document build | total over 1.03 beyond the noise in two passes, or script over 2.5 |
| L2 | 02 replace 1,000 | 33.1 / 5.3 | 31.6 / 4.8 | total 0.98 to 1.02; script 4.8 to 5.0 | 1,000 more imports at 20 to 50 ns each is 0.02 to 0.05 ms | total over 1.03 beyond the noise in two passes, or script over 5.1 |
| L3 | 03 update every 10th (x16, 4x) | 16.0 / 0.9 | 15.7 / 0.9 | total 0.97 to 1.03; script 0.8 to 1.0 | the same DOM writes; fewer indirect calls | script over 1.1, or total over 1.04 beyond the noise |
| L4 | 04 select (4x) | 5.40 / 0.4 | 4.10 / 0.4 | total 0.95 to 1.05; script 0.3 to 0.5 | the same two writes | script over 0.6 |
| L5 | 05 swap (4x) | 18.7 / 0.7 | 19.2 / 0.7 | total 0.98 to 1.02; script 0.6 to 0.8 | 999 map puts at about 5 ns, times 4 for the throttle, is 0.02 ms; the two moves are the same | script over 0.9, or total over 1.03 beyond the noise in two passes |
| L6 | 06 remove one (2x) | 16.1 / 0.3 | 15.3 / 0.3 | total 0.98 to 1.02; script 0.2 to 0.4 | the lookahead: no map | script over 0.5 |
| L7 | 07 create 10,000 | 320.2 / 25.2 | 304.0 / 19.9 | total 0.99 to 1.01; script 19.5 to 20.4 | 10,000 rows at under 20 ns of bookkeeping each way is 0.2 ms at most | script over 20.6 beyond the noise, or total over 1.02 in two passes |
| L8 | 08 append 1,000 (x2) | 35.4 / 3.1 | 34.5 / 2.5 | total 0.98 to 1.02; script 2.4 to 2.7 | as create; the rows go to the tail directly | script over 2.8 |
| L9 | 09 clear (x8, 4x) | 13.8 / 10.6 | 13.2 / 10.1 | total 0.98 to 1.02; script 9.9 to 10.3 | the same clear and frees | script over 10.5 beyond the noise |
| L10 | without templates, 01 and 07 | 32.6 / 4.0 and 329.0 / 37.2 | strings-results: script 4.3 and 35.7 | script 4.2 to 4.6 and 35.0 to 36.4 | the list's share is the same as with templates | 4.8 or 37.0, beyond the noise |
| L11 | memory (21, 22, 25) | 0.61, 1.84, 0.80 MB | run-clear 0.79 MB | each within 0.02 MB of baseline; wasm memory touched after five create-and-clear cycles 135 KB or less (116 today) | 24 bytes a row and 4 an id, against 12 + 8 + 4 and the 4 of `keys` today | run-clear 0.82 MB or more in two iterations, or touched over 160 KB |
| L12 | a pass over 10,000 unchanged rows, in the page (the micro below) | none | today's `cd_list_update`, measured in the same run | 0.03 to 0.08 ms, and no slower than today's | one direct call and two compares a row | over 0.12 ms, or over 1.2 of today's |
| L13 | built again, not patched (`st-l-rb`): 03 and 04 | none | 15.7 / 0.9 and 4.10 / 0.4 | 03: script 1.6 to 2.4, total 17.0 to 19.5; 04: script 0.4 to 0.5, total 0.95 to 1.05 of the patched build | "The cost on the benchmark" above | 03 script under 1.3 or over 3.0; 04 total over 1.08 beyond the noise |
| L14 | sizes | shim 6,672 bytes brotli; lesson 5 page 14,370; lesson 1 page 8,810 and module 2,452 | the same | shim +90 to +190 (`replace` with the focus path, `put`); lesson 1 page +90 to +200, module +0 to +80; lesson 5 page within 150 of today's, either way | `replace` is about 300 bytes minified; lesson 5 loses its bookkeeping and gains the pass | shim over +250; lesson 1 module over +120; lesson 5 page over +250 |
| L15 | ids | `cd_ids_live` returns to its start after any fill and clear, and `cd_ids_high` does not grow across 1,000 rebuilds of one row | the same | exactly that | a rebuilt row's old ids are freed before the new build | any growth |

The one prediction that is a cost, not a tie: swap and replace are slower by an amount under the harness's 0.1 ms script resolution.
If either is refuted, the fix is in the pass, not in the app: a tail check before the map for swap, and a direct insert for the rows of a whole replace.

Not predicted to improve: any total.
The list's bookkeeping is under 1% of every operation's time; this change is for the code that uses the list.

## The measurement plan

To be run later, by one executor, with nothing else benchmarking on this Mac.
It reuses the strings change's harness as it is.

1. **Entries.**
   A new `bench/list/build-l.sh`, which sources `bench/strings/build-st.sh`'s `build()` the way that script took over `bench/build.sh`'s.
   - `st-s` and `st-s2`: the keyed app and cdom from the commit before step 3 below, in a worktree, built twice (the baseline and its noise).
   - `st-l`: this tree.
   - `st-l-n`: with `-DCDOM_TEMPLATES=0`, against `st-s-n` built from the baseline worktree.
   - `st-l-rb`: a copy of the app, made in `bench/list/out` and never in the tree, in which `render()` calls `cd_list_row` and builds a changed row again.
2. **The runs**, in a new `bench/list/jfb.sh` shaped like `bench/strings/jfb.sh`, through `tools/pair.sh` (15 iterations, the load gate at 4, entries interleaved benchmark by benchmark):
   ```sh
   TAG=list-cpu   FWS="st-s st-s2 st-l"      "$pair" 01_ 02_ 03_ 04_ 05_ 06_ 07_ 08_ 09_
   TAG=list-cpu2  FWS="st-s st-s2 st-l"      "$pair" 01_ 02_ 05_ 07_
   TAG=list-notpl FWS="st-s-n st-l-n"        "$pair" 01_ 07_
   TAG=list-rb    FWS="st-l st-l-rb"         "$pair" 03_ 04_
   TAG=list-mem1  FWS="st-s st-s2 st-l"      "$pair" 21_ 22_ 25_
   TAG=list-mem2  FWS="st-s st-s2 st-l"      "$pair" 21_ 22_ 25_
   ```
   `list-cpu2` is the second pass that L1, L2, L5 and L7's falsifiers need.
3. **Summaries**: `node bench/strings/jfbsum.mjs results/pairs/list-cpu st-s st-s2 st-l` and the same for the other tags, kept as `bench/list/results/*.tsv`.
4. **The micro (L12)**: `tools/quick.mjs`'s in-page timing on both builds, with one added case: 10,000 rows, then 200 renders with nothing changed, timed as a block and divided.
5. **Memory (L11)**: `bench/strings/startup.mjs`'s touched-memory read after five create-and-clear cycles.
6. **Sizes (L14) and ids (L15)**: `tutorial/run.sh`, `brotli -q 11`, `wc -c`; L15 is the unit test in the test plan.
7. If a falsifier fires: profile the operation with `bench/strings/prof.mjs` before changing anything, and record the profile with the verdict.

## Test plan

End to end first: the tests drive a built page in headless Chrome.
Every new test is mutation checked: the mutant is applied, the named test must fail on the named assertion, and the code is restored by re-editing.
Per step, run only what the step can break.

| Test | What it proves | Mutant that must turn it red |
| --- | --- | --- |
| `node tools/fuzz.mjs` with `tests/listfuzz.c` ported (exists) | random transitions through a pass, 40,000 per seed: the page's rows equal the model's keys in order, for `CD_LIST_ALL`, `CD_APPEND` and an `end` sibling | in `cd_list_end`, place a detached row before the wrong successor; skip the removal of unclaimed rows; treat the lookahead hit as claimed without advancing `cur` |
| the same, revisions | each round changes the `rev` of random survivors; a rebuilt row shows the new label and has a new node, an unchanged row has the same node (the page keeps a `WeakSet`), a patched row has the same node and the new label | compare `rev` with `<` instead of `!=`; rebuild every survivor (the same-node check fails); never rebuild |
| the same, ops counted | the page counts DOM mutations (a `MutationObserver`): a swap of the ends is 2 moves, one removal is 1 remove, clearing is 1 clear, an unchanged pass is 0 | drop the trimmed-ends case (the swap costs more); make the tail fast path build off the document (an append costs 2 records a row) |
| `tools/unit.mjs` + `tests/unit.c`, a row's ids | a row whose build makes 3 ids and 2 handlers: after its key goes, `cd_ids_live` is back, and a click on the removed node's old id reaches nothing; 1,000 fills and clears, and 1,000 rebuilds of one row, leave `cd_ids_live` and `cd_ids_high` flat (L15) | free the chain from its second id on; free the old row's ids after the new build (`cd_ids_high` grows) |
| the same, the chain in regions | tests 8 and 9 as they are (regions and lists filled and taken away thousands of times), on the chain | in `cd_id_new`, record without linking (only the newest id is freed) |
| the same, a row as a region | a row's build shows a region and registers two cleanups; removing the row, rebuilding it, clearing the list and freeing the list's parent each run the cleanups once, newest first, before the row's ids go, and leave the region not shown | skip the region record on rebuild; run cleanups after the ids are freed (the cleanup reads a freed id and traps) |
| the same, nested lists | a list of 3 groups, each row holding a list of 4 cards built in the row's build: removing a group, rebuilding a group and hiding a region that holds the whole thing leave `cd_ids_live` at its start; an inner pass run later, from an event, on one group's parent changes only that group | on freeing a parent id, do not take its list (ids leak); share the key map between passes (the outer pass loses its map and misplaces a row) |
| the same, `cd_on_key` | a handler gets the 64-bit key it was registered with (a value over 2^32); registering `cd_on` on the same (id, ev) replaces it; a freed id's key handler is gone | pass the low 32 bits; keep two tables so that both handlers fire |
| the same, focus | in Chrome: a row with a checkbox and a button; focus the button, change the row's `rev`, run a pass: `document.activeElement` is the new row's button. The same with the build's shape changed so that the path leads to a `<span>`: the focus is not moved. A row with no focus inside: `activeElement` is unchanged | drop the focus restore (the first case fails); walk the path from the wrong end (the first case fails); drop the tag check (the span case fails only if the span can take focus, so the fixture gives it a `tabindex`) |
| the same, the off-document fill | a first fill of 63 rows never detaches the parent; of 64 it does, and the parent is attached again after `cd_list_end`, at its place | never attach; detach at 1 |
| the same, traps | each traps: a row without its end, an end without a row, a pass left open at the flush, the 9th nested pass, `cd_list_row_end` with an id the build did not make, `end` changed between passes | remove each check in turn |
| the same, inline `cd_on` | `cd_on(cd_open_id("button"), ...)` before the close, then `cd_class`: the class is on the button and the click arrives; `cd_on` on an outer element after a child was opened, then `cd_class`: the class is on the outer element (the documented condition) | none: it pins today's behaviour |
| `tutorial/test.mjs`, lesson 5 (exists) | add, toggle, filter, remove and clear behave as today; added: after Space on a checkbox, `activeElement` is that todo's checkbox | in the lesson's build, replace `cd_list_row_end`'s row with one built by a copy of `row_build` that puts the remove button first only when the todo is done (the path now leads to a button, the tag check stops the move, and the assertion fails) |
| `tests/link/run.sh`, discovery | a fixture app with `src/app.c`, `src/sub/row.c`, `src/.hidden.c` and `src/todo.h`, and no `APP_SRC`: it links, the hidden file is not compiled, and after an edit of `todo.h`, a file added, removed and renamed, `make -q` is not up to date; a file of `src/` named in `APP_SRC` fails with the message | drop the list file from the prerequisites; take dot-names; do not sort |
| a gate in `tests/link/run.sh` | `grep` finds no `cd_list_update`, `cd_list_fns`, `cd_item`, `CD_CTX_OF`, `CD_CTX_INDEX`, `CD_ON_EVENT` outside `docs/`, RESULTS.md, PERF_LOG.md | reintroduce one |

## Implementation steps

Each step is one executor and leaves the tree building and its tests green.
Sonnet unless marked.
Every executor: work in `/Users/alex/Dev/cdom-strings`, commit and push after the step, no em dash, each Markdown sentence on its own line, no `Co-Authored-By` and no session trailer, regenerate `shim/cdom.min.js` with `node tools/minshim.mjs`, restore a mutant by re-editing, and run builds and browsers one at a time on this Mac.

1. **Merge main into `strings`** (the repo manager, not an executor: it is conflict resolution with design in it).
   Main's nested regions, cleanups, renders, `cdom_ui`, bindings and timer slots come in; `cdom_ui.h`'s functions get their `cd_s()` macros.
   Verify: the full suite once, since everything moved.
2. **Record the baseline** (Haiku).
   Build `st-s` and `st-s-n` from this commit, record the lesson and shim sizes, and write `docs/list-results.md` with "Before" and the L1 to L15 table with an empty "Measured" column.
3. **The id chain.**
   `chain[]` beside `tags[]`; `rec`; regions record through it; `cd_region` loses `ids` as an array, `n` and `cap`; `take()` walks the chain.
   The list still switches recording off.
   Verify: `tools/unit.mjs` (tests 8, 9 and the region tests), with the chain mutant.
4. **`cd_on_key`.**
   One handlers table with a 64-bit payload; `CD_CTX_OF`, `CD_CTX_INDEX` and `CD_ON_EVENT` deleted; lessons 4 and 5 and the tests that used a cast move to it; the header's `cd_on` comment gains the inline condition.
   Verify: the `cd_on_key` and inline rows of the test plan; `tutorial/run.sh tutorial/04-regions tutorial/05-todo`.
5. **The shim's `put` and `replace`.**
   Both imports, `replace` with the focus path; reached in this step only through two test exports in `tests/unit.c`.
   Verify: the focus row of the test plan, in Chrome; the shim's size against L14.
6. **The pass, beside the old list.**
   `cd_list_begin`, `cd_list_row`, `cd_list_row_patch`, `cd_list_row_end`, `cd_list_end`, the list's state under its parent's id, plain rows only (ids, no child regions, no cleanups, no nesting).
   `tests/listfuzz.c` gains a second set of exports that drive the pass with the same seeds as the old list, and the fuzz compares both against the model.
   Verify: `node tools/fuzz.mjs`, with the three diff mutants, the revision mutants and the counted-ops mutants.
7. **Move every caller, then delete the old list** (two commits; the first is Haiku with this document's code).
   The benchmark app, lessons 5 and 6 (still one file each), `tests/unit.c` tests 7 to 9, `tests/listfuzz.c`.
   Then `cd_item`, `cd_list`, `cd_list_fns`, `cd_list_update` and the old `cd_list_forget` go, and the grep gate comes.
   Verify: `bench/build.sh && node tools/smoke.mjs && node tools/domcheck.mjs`; `node tools/fuzz.mjs`; `tutorial/run.sh tutorial/05-todo tutorial/06-stopwatch`.
8. **A row is a region.**
   The row's region record and its flag, child regions and cleanups in a row, lists taken when their parent's id is freed or their scope is taken, nested passes with a map per list, `cd_list_forget(parent)`, the traps.
   The header's region and list comments as in "The API".
   Verify: the row-as-a-region, nested-lists and traps rows.
9. **Lesson 5 as three files** (Haiku, with this document's files).
   `src/todo.h`, `src/row.c`, `src/app.c`, `APP_SRC := src/app.c src/row.c` until step 11, the README rewritten around the loop and the revision, lesson 6's README, the tutorial index.
   Verify: `tutorial/run.sh tutorial/05-todo` with the focus assertion and its mutant.
10. **Measure.**
    `bench/list/build-l.sh`, `bench/list/jfb.sh`, the micro case; run the plan; fill `docs/list-results.md` with each verdict; RESULTS.md and PERF_LOG.md get the summary.
11. **Discover the C files** (after the page tool has landed).
    The list mode takes a suffix; `cdom.mk` builds from the discovered files and `APP_SRC`; the app's headers are prerequisites; `tools/new.sh` and every lesson lose their `APP_SRC` line; the README's "Building an app".
    Verify: the discovery row of the test plan; `tests/link/run.sh`.
12. **`cd_region_dirty`** (optional, on its own).
    The call, its test (an ask after the region went is dropped; a pending ask is dropped when it goes), each with a mutant.
13. **amgi** (amgi's manager, through the super-coordinator, after cdom's pin moves).
    `decks.c` as above; `deck.c`'s rows on the list with a per-note revision, the hash guard, the row arrays and the editor's region gone; every `CTX_OF` to `cd_on_key`; amgi's `el` helpers deleted for `cdom_ui`'s.

## What I think is wrong, or incomplete, in the idea

1. **"Nearly all of its weight is keyed-list bookkeeping" is not what the file shows.**
   The bookkeeping is about 45 of 200 code lines.
   This design removes it, and the lesson goes from 200 to 174 code lines in three files, 142 with the CSS out.
   It will not be near React's 53 (not 30), and the list is not why.
2. **"Destroy becomes unnecessary" is true for ids and false for everything else a row can hold.**
   A timer, a request, a playing clip or a binding made for a row must be stopped when the row goes.
   Main already has the answer (`cd_on_free`), so destroy is not dropped: it is replaced by the region's cleanups, registered where the thing is made.
   That is why a row has to be main's region and not `strings`' simpler one.
3. **"Patch can simply rebuild" costs more than focus.**
   Focus is the one thing the design can carry, and it does.
   Typed text, the caret, IME composition, scroll, transitions and selection are lost, and the row that is rebuilt is usually the one the user is touching.
   So rebuilding is the default for rows of text and buttons, and patching stays in the API for rows that hold state; it could not be deleted even if the benchmark did not need it.
4. **Keeping the three callbacks and only dropping destroy would have left most of the weight.**
   `keys[]`, `find()` in every callback, the `ctx` and the function table come from the list calling the app.
   They go only when the app calls the list, which is also what makes `row_build(const Todo *t)` a typed, plain call.
   The price is that the diff learns the keys one at a time; the predictions say that costs nothing measurable, and they may be wrong (see the risks).
5. **The casts are not a list problem.**
   The same `(void *)(uintptr_t)` is in lesson 4's tabs, lesson 5's filters and all of amgi.
   The fix is in events (`cd_on_key`), not in rows.
6. **Keys of 32 bits do not fit the real app.**
   amgi's note ids need 64, so the list's keys, its revisions and the handler's key are 64 bits.
7. **amgi's index in the ctx turns from correct to wrong the moment rows survive.**
   It is not a style point: moving amgi to this list without moving its handlers to keys would send a click to the wrong note after a delete.
8. **Discovery of C files is right, and it is a small step away from "every mechanism visible".**
   It names no object and makes no call, so I recommend it, but the weak-hook case above is a real, quiet way for a stray file to change an app.

## Alternatives, and why each loses

| Alternative | Why not |
| --- | --- |
| keep `cd_list_update`, delete only `destroy` (ids recorded per row) | `keys[]`, `find()`, `ctx`, the table and an untyped build stay; and the row's revision still needs a second array or a callback |
| keep `cd_list_update`, pass `{key, rev, data}` per row and one `build(const void *data)` | zero risk to the diff, and the honest second choice; but the build is untyped, the app still fills an array, and a row does not read like a region |
| a pass that defers the builds to `cd_list_end` | the builds would need the data again, so a callback and a pointer per row come back |
| the app's `cd_list` struct, as today | a list inside a row or a region then has a struct that goes stale when its owner goes; amgi's and lesson 5's arrays move, so cdom cannot keep the struct's address either; under the parent's id there is nothing to go stale |
| a per-row heap array of ids, as regions have today | an allocation per row: 10,000 on create 10,000 |
| morph the old row into the new one instead of replacing it | a second diff, inside rows, with its own edge cases; patch in place already serves the rows that need their nodes kept |
| rebuild the whole list as one region (amgi today) | scales with the list and loses focus and typing; it is what this design is for |
| tag every id of a row with the row's key automatically | hidden, 32 bits, and it takes the tag away from the app |

## Risks

- **The diff is restructured, not just re-skinned.**
  The moves are today's code, but the claiming of old rows, the placement of rows built off the document and the lazy detach are new, and this is the main risk.
  The guards: step 6 runs the old and the new list on the same seeds before anything is deleted, and the fuzz counts DOM operations, so a correct but slower diff fails too.
- **Swap and replace are predicted slower by less than can be measured.**
  If the measurement disagrees, the fixes named under "Predictions" are local to the pass.
- **Focus by path can pick the wrong control of the same tag** when a row's build puts something that comes and goes ahead of its controls.
  The rule is in the header and a test pins the safe behaviour, but it is a rule the author must follow.
- **The merge comes first and is large.**
  61 commits against 28, with `cdom.c`, `cdom.h` and the shim changed on both sides.
  Nothing in this design can start before it.
- **amgi must move its handlers and its rows together** (point 7 above).
- **The shim grows for every page** (L14), including pages with no list, because `replace` cannot be left out by the linker.
