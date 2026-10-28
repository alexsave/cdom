# SHIM_LINES: why each line of shim/cdom.js is JavaScript

shim/cdom.js is the only JavaScript a cdom page runs.
This file justifies every line or tight group of lines in it: what the line does, which capability it reaches that C cannot reach from wasm, and which feature owns it.
Line numbers refer to shim/cdom.js at f7735c3 (unchanged by the commit that adds this file).

## Counts

| shim/cdom.js | lines | bytes | minified (terser -c passes=2 -m) | minified, gzip -9 |
| --- | ---: | ---: | ---: | ---: |
| perf's final shim (branch perf, 0d0bdd7) | 138 | 6901 | | 1772 |
| platform before the rebase (337ccb4, op stream) | 277 | 14777 | | 3270 |
| after the port to direct calls (before this audit) | 312 | 16689 | 8623 | 3569 |
| after this audit | 277 | 15249 | 7567 | 3178 |
| after this audit, hydration taken out (measured, not shipped) | 168 | 8341 | | 1983 |
| resumability instead of hydration (1cc247e) | 213 | 11206 | 5212 | 2408 |

The audit took 35 lines and 391 gzip bytes out.
The hydration walk was 109 of the remaining lines and 1195 of the gzip bytes; resumability replaced it with 45 lines and 425 gzip bytes over the shim without either (last section).
The line-by-line table below is for f7735c3; its hydration rows (10-13, 44-136, 265-273 and the `if (hy)` tests) are gone, and the last section lists the lines that took their place.

## What only JavaScript can do

Wasm cannot touch the DOM, JS objects or the event loop; it can only call the functions it imports and read and write its own linear memory.
So a line belongs in the shim only when it does one of these:

- calls a DOM method or sets a DOM property (createElement, cloneNode, insertBefore, nodeValue, className, addEventListener, encodeInto into a form value's bytes);
- holds a reference to a DOM node or a JS object between calls (wasm has numbers only: the `nodes` array is how an id becomes a node);
- reads something only JS sees: an event's fields, `Math.random` for the seed, a script element's text, an input's value;
- turns bytes in linear memory into JS values (strings, numbers, objects for a widget) or the reverse;
- starts the module (fetch, instantiate, wait for the DOM).

Everything else, every decision that only needs numbers, is C's.

## Line by line

| lines | what | why it is JS | owner |
| --- | --- | --- | --- |
| 1-4, 277 | strict mode, the header comment, the module closure that exposes only `cdom` | a page needs one global to call `cdom.start` | start |
| 5 | `DETACHED = -1` (CD_DETACHED as an i32), `SMALL` (the batch size under which strings decode by hand) | constants the lines below compare against | core ops, strings |
| 6 | the document, one TextDecoder and one TextEncoder | DOM and encoding objects, made once | core ops, strings, input |
| 7 | `nodes` (id to node), `strs` (string id to JS string), `tpl` (template number to node), `stack` (open elements) | wasm cannot hold a node or a JS string; these four tables are how an integer from C names one | core ops, strings, templates |
| 8 | the instance's exports, its memory and typed views, and the three addresses cd_layout gives | the views read linear memory; memory.grow detaches them, so they are remade on demand | start, strings, events, HOST |
| 9 | the build state: open depth, the last node, the last closed root, the AT parent and before | each op refers to the node the previous one made (`last`), so the reference stays where the node is | core ops |
| 10-13 | hydration's state: the cursor map, verify, the stats the tests read, template sizes | hydration (see the last section) | hydration |
| 15-18 | `views()`: typed arrays over the current memory buffer | reading linear memory from JS needs views, and a grown memory needs new ones | strings, events, HOST |
| 19-37 | `strings(n, nbytes)`: decode a batch of strings (id, byte length, UTF-16 length) into `strs` with one TextDecoder call, or a char loop for a batch under 64 bytes | making a JS string from UTF-8 bytes is a JS operation; C has already chosen every id (a per-op string's too) and counted each string's UTF-16 length, so JS only slices | strings |
| 39-42 | `place(n)`: a node closed at depth 0 goes to the AT parent before `ref`, or nowhere when the parent is DETACHED (a template) | insertBefore and appendChild are DOM calls; `ref ? nodes[ref] : null` stays because id 0 is the mount and cannot mean "append" | core ops |
| 44-136 | hydration's helpers and the hydrating version of every op that creates, places or writes | hydration (see the last section) | hydration |
| 138-154 | `delegate(kind, name, capture)`: one listener per event type or name at the mount, installed by C on the first listen of it; walking up from the target, it asks C (`cd_wants`) about the first node with an id that is still the node of that id, and records the element-child index at each level | addEventListener, `e.target`, `parentNode` and `previousElementSibling` are DOM; which ids take which events, and whether a listener is already installed, are C's (moved there by this audit) | events |
| 155-163 | `post`: write the event record (type, id, x, y, button, key, modifiers, path, depth) into the inbox and call `cd_event` | the fields of a DOM event exist only in JS | events |
| 165-183 | `val()`: one cdv value (cdom.h) from linear memory into null, booleans, numbers, strings, Float64Array, arrays and plain objects | a widget wants JS values; C builds the tree and JS turns words into objects with one recursive function | HOST |
| 185-203 | `host`: MOUNT `new window.<path>(node, value)`, CALL a method with the decoded arguments, SET a dotted path inside the instance, PROP a DOM property, UNMOUNT; the two throws name what is missing | reaching `window.Chart`, calling a JS method and setting a JS property are JS operations | HOST |
| 205 | `at`: remember the parent and before for the next root | the shim places nodes, so it keeps where | core ops |
| 206-211 | `open`, `close`, `text`, `text_num`, `attr`, `class`: createElement, createTextNode, setAttribute, className, appendChild through `place` | DOM calls; `>>> 0` makes a u32 that wasm passed as a signed i32 a JS number again | core ops |
| 212-213 | `name`: `nodes[id] = last`; `listen`: the same, plus the node's own `$i` so an event can find its id | the id-to-node table is JS; `$i` is the one expando the event path needs to go from a node back to its id | core ops, events |
| 214-215 | `value`, `value_num`: `last.nodeValue` | DOM | core ops, walks |
| 216 | `tpl_save`: the last closed root becomes template t | the template is a detached DOM subtree held by reference; C guarantees it was built detached, so nothing is removed | templates |
| 217-220 | `clone`: a deep clone of template t, appended when an element is open | cloneNode and appendChild are DOM; the `sp` check stays because C does not track open depth | templates |
| 221-228 | `walk`: up to 16 steps of firstChild, nextSibling or parentNode from a named node or the last one | DOM navigation; C packs the steps (2 bits each) and sends a whole walk as one call | walks |
| 229-230 | `walk_value`, `walk_value_num`: a walk and the nodeValue it leads to, in one call | the update benchmark's label patch is one crossing instead of two (perf's measurement) | walks |
| 231-238 | `set_text`, `set_text_num`, `set_attr`, `set_class`, `remove`, `free`, `move`, `clear`: the update ops on named nodes | DOM calls; `free` and `remove` drop the reference so the node can be collected; `move` reads `parentNode` because cd_move's API names only the node and its new neighbour | core ops |
| 239-240 | `detach`, `attach`: take a list's parent off the document for a large first fill and put it back where it was | DOM calls; the old place is kept on the node (`$p`, `$s`) because C does not know a node's DOM neighbours | core ops |
| 242-245 | `input`: copy an input's current value into C's buffer as UTF-8 | the value the user typed exists only in the DOM | input |
| 247-264 | `start`: build the import object, instantiate the streamed module, wait for the DOM when given a mount id, read cd_layout, seed and call `cd_start` | fetch and instantiate are JS; `Math.random` is the only entropy a page has; `env` is passed through for perf's listfuzz oracle (shipped pages pass nothing) | start |
| 265-273 | the hydrating start: copy the page's base64 state text into C's buffer, `cd_start_state` with the cursor map on, verify's count, the stats | hydration (see the last section); the bytes are now decoded by C | hydration |
| 275-276 | the module's public object: `start` and, once started, `stats` | the page's entry point | start |

## Moved into C or deleted by this audit

Each step has its own commits (core, then shim, then sink) and the test that proves nothing broke.

1. **Event bookkeeping moved into C** (`cd_listen`, `cd_listen_named`, `cd_wants`): e6d5238 core, 3d40c6e shim, 0f66e89 sink, 14062e8 tests; 312 -> 290 lines.
   The per-id event mask, its growth, its reset on free and remove, the per-node set of event names, the "is a listener installed yet" checks and the table of event type names all left JS.
   The two delegate functions (built-in and named) became one, `listen_named` stopped being an import, and `delegate(kind, name, capture)` is called by C once per type or name.
   C also clears an id's events when the id is freed, which the JS mask did, and the shim now checks that a node still owns its id, which only the named path did before.
   Proved by new real-event checks in tests/conform.mjs (a bubbling click on a row's child, a non-bubbling named event from a child, an event nobody asked for, a freed and reused id) and 6 new mutants in tests/conform_mutants.sh.
2. **Template slots became named text nodes owned by C**: fa9ce68 core, 4bd1dc6 shim, 809315f sink, f56d3d1 tests; 290 -> 286 lines.
   `cd_slot` names the slot's text node with an id of its own and `cd_tpl_text` sets it with `set_text`; the imports `slot`, `tpl_text` and `tpl_text_num` and the per-template slot tables left JS (and the sink).
   Hydration's verify marks a template's named text nodes, which is how it still compares slot texts with the server's.
   Proved by tests/conform.mjs (slots in the hydrated first frame and after it, the slot-text tamper), tests/cdtc/run.sh (op counts still equal the hand-written app) and the slot mutants.
3. **Per-op strings share the one string table**: 04ab392 core, 3495304 shim, 7251425 sink, 0f0eaa0 test; 286 -> 285 lines.
   With CDOM_INTERN_TEXT=0, C gives a per-op string an id just above the interned ones instead of a separate transient range, so `S()` lost its branch (it was on every string op) and `tmp`, `put` and `TRANSIENT` went.
   Proved by tests/cdv.sh in its per-op build and a new mutant.
4. **The state blob is decoded by C**: 6645d29 core, d6b891c shim, 2e2d5dc tests; 285 -> 283 lines.
   The shim copies the page's base64 text into C's buffer and `cd_start_state` decodes it, so the `Uint8Array.fromBase64` / `atob` feature test left JS; the server's C wrote that base64 in the first place.
   Proved by tests/state.sh (every padding form, six malformed shapes refused before the loader runs; 8 mutants) and both hydrate runs.
5. **`root.remove()` in tpl_save deleted**: bd47ea0 core, 2c27cc8 shim, 6738090 sink, 0f315c8 tests (a removed call, same line count).
   A template's root is closed while AT points at CD_DETACHED, so it was never placed and the removal was a no-op; C now traps when a template is saved while AT points into the document.
   Proved by a new trap check in tests/cdv.sh and its mutant.
6. **The `instantiateStreaming` fallback deleted**: d348bf2; 283 -> 280 lines.
   Every browser that runs the rest of the shim (`??`, typed-array `subarray`, `encodeInto`) has `WebAssembly.instantiateStreaming` (Safari since 15, 2021); the fallback only served Safari 13.1 and 14.
7. **`>>> 0` on the AT parent deleted** by comparing with CD_DETACHED as wasm passes it (-1): 9e7fbb6.
8. **`S()` and `decode` folded**: `S(a)` had become `strs[a]`, and `decode` was only the body of `strings`: f7735c3; 280 -> 277 lines.

## Looked at and kept

- `>>> 0` on numbers (text_num, value_num, the num walks, set_text_num): wasm passes a u32 as a signed i32. Passing an f64 instead would move the conversion into C, but a wasm-to-JS call may box an f64 argument into a heap number, an allocation on the row hot path that the shift does not have (not measured; the shift costs nothing measurable).
- `walk_value` and `walk_value_num`: two calls (walk, then value) would be shorter JS, but perf measured the single crossing on the update benchmark.
- `text_num` and the other `_num` imports: C could format numbers into strings, but every row id would then cross as a string and be decoded.
- `move` reads `parentNode`: cd_list_update knows the list's parent and could pass it, but cd_move's public signature does not have it, and cdom.h stays stable.
- `place`'s `ref ? nodes[ref] : null`: passing -1 for "append" would let JS index `nodes[-1]` (undefined, which insertBefore treats as null), but a negative index is a slow dictionary lookup in V8.
- `clone`'s `if (sp)`: C does not track open depth, and one compare is cheaper than a second import.
- the event path computed in JS: it walks DOM siblings; C cannot.
- `env` in `start`: perf's tests/listfuzz.html passes its oracle through it. A shipped page passes nothing; the platform rule against per-app JS holds for pages, not for test harnesses.
- the TextDecoder / char-loop split in `strings`: perf's measurement (one decode per batch, a loop for tiny batches).

## Resuming: the lines it takes and why each is JavaScript

A server-rendered page runs `cdom.resume(url, mountId, stateId, types, names)` and nothing else: no DOM walk, no wasm fetch.
The first event loads the module; C renders the first frame virtually (cdom.h, resuming) and finds a server node by its path only when an op first takes its id.
Hydration's 109 lines are gone; these are the lines resuming adds to the shim (line numbers at 1cc247e), 45 in all, comments included.

| lines | what | why it is JS | owner |
| --- | --- | --- | --- |
| 7-8 (part) | `on` (event type to C's kind and name id), `queue`, `PEND`, `TRAIL`, `page`, `loading` | the listeners exist before the module does, so the type table and the waiting events live in JS until C is there | resume, events |
| 40-49 | `skip(c)`: passes over `<!---->`, turns `<!--e-->` into the empty text node it stands for | a comment and a text node are DOM objects; replacing one is a DOM call | resume |
| 50-56 | `hear(t, capture)` and `delegate`: one listener per type at the mount, installed by C's first listen or by resume | addEventListener is DOM; hear's check is for the types resume installed before C ran (C's own check saves a call per listen) | events, resume |
| 61 | events before the module: queued (64 at most), and the first one boots | only JS sees an event before the wasm exists | resume |
| 66-74 | the walk up: each level's index as a walk counts it and its element index into C's trail, up to the first JS-known listener, or to the mount while server listeners are unresolved (`u32[PEND]`) | `previousSibling`, `nodeType` and `$i` are DOM; which node takes the event, the element path and the resume match are C's | events |
| 145 | a walk reads the server's comments as `skip` does | a walk is DOM navigation; this is how C resolves a server node (a walk along its path, then name or listen), so resolving needs no import of its own | walks, resume |
| 173-176 | `init`: exports, memory and the five layout addresses (two more than before: the pending-listener count and the trail) | reading linear memory needs the exports | start, resume |
| 188-208 | `resume`: the listeners now; `boot`: fetch and instantiate, copy the base64 state, `cd_start_state`, deliver the queue | fetch and instantiate are JS; the state text is in the page | resume |
| 209 | `cdom.loaded`, `cdom.boot` | the page (or a test) can tell whether the module was fetched, and start it at once | resume |

What C took, so the shim did not need it:

- the first render: C runs cd_main with no import for nodes the server wrote, keeping a virtual tree (parent, first, next, child index; a clone's inside copied from its template only when a walk or an append goes in), and keeps it afterwards as the map from ids to paths;
- resolving: a walk along the path with the existing `walk` import (16 steps a call; the first walk of a batch starts at the nearest node JS already knows, the rest go on from the one before), then `name` or `listen`; JS's last node is named before and restored after;
- the structural rule: before an insert, move, remove, clear or detach under a parent, every unresolved id below it resolves in one batch, since paths hold only against the untouched DOM;
- events: one record form for every page; C matches the trail against the virtual tree, resolves the deepest unresolved listener and builds the element path the app sees;
- which event types to listen to before the module: the sink records them during the server's render and the server writes them into the resume call;
- template slots set during the first render: sent once at its end, not once per virtual clone.

What resuming keeps from hydration:

- `<!---->` between two text nodes, so the parser keeps them apart; it is never removed, and walks, paths and event trails pass over it;
- empty text nodes: written as `<!--e-->` (hydration's position-read `<!---->` broke after an insert or remove next to it); the first walk that reaches one replaces it with an empty text node, the only node resuming creates;
- templates built off the document and real: their ops reach JS during the virtual render as always;
- ids bound to the server's own nodes, never copies: the rows test checks the clicked row is the server's node and that no node in `#main` is new.

What it dropped: `<!--i-->` (C's virtual tree renumbers the children an insert shifts) and verify (a dev-build mismatch report is program item 5).

## Eager: a widget page that loads the module at once (0 lines)

On a resumed page a widget does not exist until the first interaction: its MOUNT is one of the first render's host calls, and that render runs when the wasm loads (cdom.h, HOST).
A page that wants the widget at once (apps/chart with `?eager=1`) writes `cdom.boot();` right after its `cdom.resume(...)` call; src/ssr_page.c does it when the shell's `eager` is set.
That is the cheapest honest version: `boot` was already exported for tests (line 209 above), and calling it at once is exactly the first event's path without the event, so the shim gains no line and no byte (213 lines, 5213 bytes minified, 2400 gzip -9 before and after).
The page gains 12 bytes, written by C.
An `{eager: true}` option on `resume` was not added: it would be a branch in the shim doing what one call on the page already does.
Proved by tests/host.mjs (the eager page draws the chart with no interaction from one app.wasm request) and its mutant in tests/host_mutants.sh (ssr_page ignoring `eager` fails it).
