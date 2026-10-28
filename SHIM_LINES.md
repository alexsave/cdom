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

The audit took 35 lines and 391 gzip bytes out.
The hydration walk is 109 of the remaining lines and 1195 of the gzip bytes; resumability replaces it (last section).

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

## Hydration: what it costs today and what resumability must keep

Hydration is lines 10-13, 44-136, the `if (hy)` test in 16 ops (open, close, text, text_num, attr, class, name, value, value_num, tpl_save, clone, walk, walk_value, walk_value_num, detach, attach) and lines 265-273 of start.
It is 109 lines and 1195 of the 3178 minified gzip bytes.
At load it walks the whole server DOM once, in order, as the first render's ops arrive: every server node is visited, every element is matched, and with verify every tag, attribute, class and text is compared.
The hydrate test's numbers: n=10000 adopts 100029 server nodes and creates 0; the hydrate call itself took 18.7 ms at milestone 1.
Each op pays one `if (hy)` test after hydration too; it is a predictable branch on a closure variable.

Where the shim touches the DOM during a hydrated first render:

- `open`, `text`, `text_num` and `clone` under a parent in the document take the parent's cursor node instead of creating one (`adopt`, `adoptText`, `fit`); under a detached parent (a template being built) they create as usual and count `templateNodes`.
- `adoptText` removes the `<!---->` comment the sink writes between two adjacent text nodes, and creates the one node HTML cannot carry, an empty text node (the only nodes hydration creates).
- `fit` matches a clone's template against the server subtree child by child: separators removed, empty text created, a node after `<!--i-->` (inserted into the clone before an anchor, an `{#if}` branch) taken out of the walk order and parked on its parent until the insert that places it.
- `attr`, `class`, `value` and the walks' values on an adopted node only compare (verify) and write nothing.
- `close` does not place an adopted root; `detach` and `attach` do nothing (the server's list is already in place).

Where ids get bound: `name` and `listen` bind an id to `last`, which during hydration is the adopted server node; templates' slot nodes are named while the template is built detached, and are marked `$s` so verify can compare a clone's slot texts with the server's.

How walks resolve: against adopted nodes, from a named node or from `last`, skipping parked nodes (`$x`) on firstChild and nextSibling steps, because the template the walk was computed from never had them.

How templates and slots meet adopted nodes: templates are always built detached (C enforces it now), so they are real DOM subtrees even on a hydrated page; a clone under a document parent adopts the server subtree and never calls cloneNode; slot values set before the clone land in the detached template only, and verify compares them with the adopted subtree's texts.

What the replacement (no walk at load, nodes resolved by path when C first touches them) must keep:

- the `<!---->` rule: the sink writes a separator before every text node that follows a text node, so a path through text children stays countable after the parser would otherwise merge them;
- empty text nodes: the server cannot write them, so a path that reaches one must create it in place (between the separator and the next node) rather than miss it;
- the `<!--i-->` mark for nodes inserted into a clone before its template's end, so a path computed from the template skips them;
- template nodes created detached and real: templates are cloned for every new row after load, hydrated or not;
- the guarantee that adopting creates nothing else: the tests count 0 created nodes besides the empty texts, and `#main` byte-identical before and after;
- ids bound at first touch to the server's node, never to a copy, since `free`, `remove` and events use `nodes[id]` identity (the event path now checks `nodes[$i] === node`).
