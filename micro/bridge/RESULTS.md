# The bridge between C and the DOM: does a generic bridge, a function table, natively bound imports, fused ops or the Fast API beat one import per operation?

STATUS: code complete, not yet built or measured (the Mac was held by other executors).
Every table below marked PENDING is filled from `micro/bridge/results/tables.md`, which `bench.sh tables` writes after the steps `bench.sh plan` lists.

## The question

Alex asked for the hot paths (per-frame style writes for card motion, input-event records, createElement/setAttribute bulk) through four bridges, headless: (a) today's specialised imports (one import per DOM operation, the shipped port-fixes design), (b) a generic get/set/call by interned name id through one dispatcher, (c) a table of pre-resolved functions called with `fn.call(obj, ...)`, and (d) imports bound at instantiation as `Function.prototype.call.bind(Element.prototype.setAttribute)` and friends, the needed list generated at build time.
Two arms were added: (e) fused operations (superinstructions) and batch-shaped DOM calls, and (f) a check of whether any hot DOM function has a V8 Fast API C function that wasm could call directly.
The op list (cdom main's design) is re-measured beside them for context.
The page results judge every arm; the micro only explains them, because on the old op list the micro (39.5 ns against 41 per call) and the page (1.03-1.04x against 1.00-1.01x vanilla) disagreed (PERF_LOG.md, the direct-calls experiment).

## The arms (code pointers)

All arms are generated from the shipped `src/cdom.c`, `src/cdom.h` and `shim/cdom.js`, which stay untouched; `micro/bridge/gen_arms.py <arm> <dir> [--count]` writes the arm's copies.
Only the IMPORTS block of cdom.c (the `j_*` declarations) changes, so every C call site and the whole public API are the same in every arm; the fused arm adds its operations on top.
- (a) `a`: the shipped block, verbatim (gen_arms.py checks the generated cdom.c is byte-identical to src/cdom.c).
- `list`: every void DOM operation is a 32-byte record in C memory (`list_rec`), applied by one import, `apply`, through one switch; any import that returns a value or reads the DOM (rect, hit, now, input), every string batch, and every flush apply the pending list first (gen_arms.py `c_arm_block` "list", `js_list`).
- (b) `b`: one import, `bridge(name, i0, i1, i2, i3, d)`; the name id indexes entries resolved once at start: a property set (`target[member] = arg`), a method call (`member.call(target, arg, arg)`), with targets "node by id", "the last node" and "node by id's style", and argument codes (string by id, unsigned, number plus unit, empty string); the structural operations (open, close, clone, walk, listen, ...) are entries that call the shim's own function (`GENERIC`, `js_bridge`).
  The name ids are a generated enum shared by the C wrappers and the JS table, so "interned" here means interned at build time; interning the member names through cdom's string table at run time would only add a string decode per name.
- (c) `c`: a table of the setters and methods themselves (`Object.getOwnPropertyDescriptor(Node.prototype, "nodeValue").set`, `Element.prototype.setAttribute`, `CSSStyleDeclaration.prototype.setProperty`, ...), called as `fns[id].call(target, ...)` through six imports, one per argument shape (`FNS`, `TABLE`, `SHAPES`, `js_table`).
- (d) `d`: the natives themselves as imports, `Function.prototype.call.bind(method)`, so wasm passes the receiver as the first argument; nodes and strings are mirrored into wasm externref tables (the shim puts each node in as it names it and each string as it decodes it: exports `cd_ref_node`, `cd_ref_str`); the imports needed are generated from the IMPORTS declarations through the `NATIVE` and `DIRECT_NATIVE` tables.
  A number with a unit cannot go to a native `setProperty` without a string, so `style_num` goes through CSS Typed OM (`attributeStyleMap.set(prop, CSS.px(v))`, three native calls); the structural operations and the writes to "the last node" stay arrows, because C holds no reference to that node.
- (e) `e`: on top of a, a multi-property style op (`cd_style_begin`, `cd_style_num`, `cd_style_str`, `cd_style_end`: one crossing, one node lookup, one `.style`, the properties in the order written), used by the port's movers through `port_patch.py e` (a ghost's frame, a flight's arming, the drag shadow's move, a hand card's patch, the battle row's glide); and, transparent to every app, one queue (`pending_op`) of the operations that only need to land before the next crossing, applied in C's order: template slot texts (which cross with the clone that reads them, `clone_slots`), freed ids, and moves (a run of moves before the same node goes in as one DocumentFragment); a clear is `replaceChildren()`.
  Every crossing, held walk, read (rect, hit, input) and flush applies the queue first, so the DOM is arm a's at every crossing; a fused style's one-off strings must all be made after the op before it crossed (a string batch sent in between traps, as `tmp_check` does for one op).
- DOM alternatives on the port, arm a's cdom with the movers rewritten (`port_patch.py`): `e_t` moves the ghost and the drag shadow by `transform: translate(...)` from where each was built instead of `left`/`top` (no layout per frame); `e_css` writes the ghost's whole style attribute as one string per frame, and the drag shadow's (left and top only) the same way.
- (f) `f`: arm a with `performance.now` imported as `Function.prototype.call.bind(Performance.prototype.now)` and the performance object passed from an externref table, the one hot-path function with a Fast API C function (below); measured with and without `--js-flags=--wasm-fast-api`.
- Not built as arms, with the reason: template-baked attributes and classList by bitmask are app changes with no hot-path user in either page (the port's faces already clone templates; the keyed rows have their class baked in); `open` fused with `class` and append would need a deferred open, and neither page opens elements in a hot path (both clone templates), so the micro's `mount` kind measures that sequence unfused for every arm.

Counting builds (`--count`): every import wrapped in a counter (`globalThis.__cdomCross`), with strings decoded (`$strings`, in `decode`) and op-list records applied (`$records`); untimed, one load per arm.

## Correctness

`run.mjs same` loads `same.html` (same.c on each arm) and compares every arm with arm a: the mount's HTML after each of 4 steps (every bridged operation, SVG, templates in and out of an open element, a keyed list reversed, swapped, cut and refilled, a region shown, hidden and shown, detach and attach, remove), the rects C read after writes in the same entry, two hits, the events through the record, and the live id count.
`run.mjs count` also hashes the page's DOM at the end of every port flow and keyed operation and compares it with arm a's (e_t and e_css differ by design while the drag is in the air).
PENDING: the result per arm, and the mutation check (one arm broken on purpose, the test red for its own reason, restored).

## Method

Headless Chrome 156.0.8078.74 (`--headless=new`, CDP, no window shown; each load's target brought to the front with `Page.bringToFront`, which makes headless render it and steals no focus), one Chrome per command killed by its own PID, its target closed in finally, `about:blank` between loads.
Every load fresh (cache off), the arms loaded once each per round in an order that rotates with the round, each load gated on the 1-minute load average below 4 (bounded at 180 s; a load that ran over is flagged and left out of the medians); medians over rounds, with the 25th-75th percentile.
Pages are served cross-origin isolated (COOP/COEP), as the port's timing.mjs does.
- The port (PRIMARY): `?notex&debug`, the kept game emptied before each load (IndexedDB cleared), the bot held (`app_debug_hold`); one trace (devtools.timeline, user timing) per load over its windows: `deal` (app_debug_deal, every card's flight), `select` (a real CDP tap on the 6s), `play` (a real click on the Attack pill and the run after it), `bot` (the held bot step released, and its run), `drag` (a real press on the 6s and 40 mouse moves 16 ms apart over the table and onto the 6d and back: the shadow follows every move), `drop` (the release over the table and its run), `transfer` (app_debug_transfer, the bot's transfer released: the shield flies as a ghost drawn every frame).
  Per window, the page's main-thread tasks summed: script (what is left of each task after the rest, framepace.mjs's rule), style (UpdateLayoutTree), layout, paint (Paint, PrePaint, Layerize), gc, frames (tasks that painted), long tasks (over 50 ms) and the longest task.
  The setup between windows (the golden board put back, the pill's position read) sits in windows of its own that are dropped.
- js-framework-benchmark (second page workload): cdom's keyed entry (bench/cdom-keyed, its app.c unchanged) per arm, the benchmark's operations as real CDP clicks, each in its own traced window: create 1k, replace 1k, update every 10th, select, swap, remove, clear 1k, create 10k, clear 10k, append 1k; summed as for the port.
  Not the harness itself (it drives its own Chrome through WebDriver and its own server): this is the same operations on the same entry, fresh loads, no warm-up clicks, so a first click also pays its tier-up.
- Micro: `bridge.html`, N = 20,000 operations of a kind inside one export (5,000 for mount, clone and frame), median of 15 runs after 3 warm-up passes, one fresh load per arm per round.
- CPU x1, and x4 throttled for the port (`Emulation.setCPUThrottlingRate`).
- Firefox 159: the micro only, headless through WebDriver BiDi (`ffmicro.mjs`, puppeteer-core from a jfb checkout's node_modules, nothing installed); arm d not run there (no CSS Typed OM in Firefox).
- Safari: not run (safaridriver drives a visible window only, and no window may be shown on this shared Mac).

## The port (PRIMARY)

PENDING (`results/tables.md`, port_x1 and port_x4): per window, script and total per arm with the ratio to arm a, then style, layout, paint, frames, long tasks.

## js-framework-benchmark operations

PENDING (`results/tables.md`, keyed_x1).

## Crossings and strings decoded

PENDING (`results/tables.md`, count_x1): import calls and strings decoded per port flow, per keyed operation and per micro operation, per arm.

## Micro (explains the pages)

PENDING (`results/tables.md`, micro_x1, micro_x1_fastapi, micro_firefox): ns per operation per kind per arm (style_num, style with a one-off string, set_text, attr, the mount burst open+attr+class+text, a template clone with two slots, one ghost's frame, one card-table frame of 2 ghosts, a coin and the drag shadow, now, and the event record's round trip).
For scale, the direct-calls experiment measured (PERF_LOG.md): nodeValue through an arrow import 41 ns, natively bound 48.5, op list 39.5; firstChild 15-18 / 18 / 9.5; cloneNode and insertBefore 570-1000 for all three; and cdom main's RESULTS.md "Step 0": an import crossing about 4.65 ns against 73-135 ns for the DOM work behind it, and a TextDecoder call per string about +126 ns.

## Calling Blink's C++ directly: the Fast API check (f)

A page cannot reach Blink's C++ except through V8's bindings; the one shortcut is V8's Fast API, where a binding carries a C function that optimized code calls without the generic callback, and V8 has a path from a wasm import straight to it.
Blink marks such methods with the IDL extended attribute `[NoAllocDirectCall]` (third_party/blink/renderer/bindings/IDLExtendedAttributes.md:1256-1262 at branch-heads/8078, Chrome 156: "usable with the fast API calls implemented in V8 ... better performance for methods with primitive-type parameters").
None of the DOM methods and attributes the hot paths use carries it at branch-heads/8078:
- `Element.setAttribute` (core/dom/element.idl:64-65), `className` (:55), `getBoundingClientRect` (:148);
- `Node.nodeValue` (core/dom/node.idl:55), `textContent` (:57), `cloneNode` (:60), `insertBefore` (:77), `appendChild` (:78), `parentNode`, `firstChild`, `nextSibling` (:45, :49, :52);
- `Document.createElement` (core/dom/document.idl:52, :171), `createElementNS` (:53), `createTextNode` (:55), `elementsFromPoint` (core/dom/document_or_shadow_root.idl:16);
- `CSSStyleDeclaration.setProperty` (core/css/css_style_declaration.idl:31) and `cssText` (:26); `ChildNode.remove` (core/dom/child_node.idl:28).
Their arguments are DOMStrings and nodes, which the Fast API does not convert (its C signatures take primitives, typed arrays and a few objects), and most are `[CEReactions]` (custom element reactions run around them) or `[RaisesException]`, which is why they stay on the slow path.
The one hot-path function that has one is `Performance.now()`: `[Affects=Nothing, NoAllocDirectCall] DOMHighResTimeStamp now();` (core/timing/performance.idl:38); the port reads `cd_now` in its frame loop and its timers (16 call sites in table_app.c).
Where it is used most is WebGL: 42 methods in modules/webgl/webgl_rendering_context_base.idl (bindBuffer :478, drawArrays :535, uniform4fv :699, viewport :721) and 28 in webgl2_rendering_context_base.idl, and 2D canvas's putImageData (modules/canvas/canvas2d/base_rendering_context_2d.idl:30-31).
On the V8 side (v8 292c5548f4ab, Chrome 156's DEPS), a wasm import takes the Fast API path only as `Function.prototype.call.bind(apiFunction)` (src/wasm/module-instantiate.cc:481), with the receiver as a reference-typed first parameter (:272) and a signature matching the C function's (`FindSupportedWasmFastApiFunction`, :188), and only when `v8_flags.wasm_fast_api` is set (:488); that flag is `DEFINE_EXPERIMENTAL_FEATURE(wasm_fast_api, ...)` (src/flags/flag-definitions.h:2106-2109), off by default (an experimental feature's default is false, :277-279).
The other path, a bound function imported directly, needs `wasm_unsafe_fast_api_wrapper` (module-instantiate.cc:349; flag-definitions.h:1982, false).
So in shipped Chrome 156 no import from a page's wasm reaches a Fast API function, and no DOM write could even with the flag; arm f measures `performance.now` with and without `--js-flags=--wasm-fast-api` to size what the path would be worth.
PENDING: arm f's numbers (micro `now`, both flag states; the port's windows with arm f).
Sources were fetched read-only over https from chromium.googlesource.com (chromium/src at refs/branch-heads/8078, v8/v8 at the DEPS revision) and are not in this repo.

## Skipping the DOM: canvas with WebGL or WebGPU (a written estimate, not a benchmark)

The only way to stop paying per DOM write is to stop having DOM for the table: draw the felt, the cards, the badges and the flights into one canvas, so a frame is a buffer upload and a few draw calls.
What the card table would lose:
- Text layout: every label, count, pill and badge would be glyphs from an atlas placed by the app, with its own shaping, wrapping and line breaking, per language (the port's strings are translated); the DOM does this for free today.
- Accessibility: a canvas is one opaque element; the table would need a parallel hidden DOM (or ARIA live regions) to be read by a screen reader at all, which brings DOM writes back for every state change.
- CSS transitions and the compositor: today a card's flight is one CSS transition armed with four or five writes and then run by the compositor off the main thread (flights_arm); on a canvas every frame of every flight is the app's own interpolation, on the main thread or in a worker.
- Hit testing, focus, keyboard navigation, text selection, zoom and the user's font size: all the browser's today (elementsFromPoint for the drop, the focus ring for keyboard play), all the app's on a canvas.
- The stylesheet: the port reuses the React app's CSS (src/styles) for its look; a canvas port would redraw it by hand.
What a frame would cost, roughly: about 60 quads (cards, badges, coins) as one instanced draw: one `bufferSubData` of about 60 x 32 bytes, a `uniform` or two and one `drawArraysInstanced`, a few microseconds of script per frame (WebGL calls are among the few Fast API functions: the wasm path above would apply to them with the flag); against that, text needs an atlas and a layout pass whenever a label changes, and the GPU raster of a full-screen canvas every frame instead of the compositor moving a few layers.
PENDING: the measured DOM per-frame costs above (the ghost and drag windows, per frame) to compare with that estimate.

## Conclusion

PENDING: a measured statement per arm from the page tables, with the micro and the crossing counts as the explanation; not a recommendation to rewrite.

## What was cut for the 15-minute rule

PENDING (the rounds actually run per workload and throttle; x4 for the keyed page; anything that ran over).
