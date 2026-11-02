# The bridge between C and the DOM: does a generic bridge, a function table, natively bound imports, fused ops or the Fast API beat one import per operation?

Measured 2026-11-01 on this Mac (8 cores), every step of `bench.sh plan` run in order; the raw rows are in `results/*.jsonl` and every table here is `summary.mjs` over them (`bench.sh tables` writes them all to `results/tables.md`).

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
It also checks a click whose default C prevents through the record, a field's value read by C during its input event (`cd_input_value`, non-ASCII), a template slot set from a one-off string, numbers no format holds exactly (1/3 px, 2/3 em, 1/7 deg: arm d's Typed OM serializes numbers itself), and the computed values of the touched properties after every step.
`run.mjs count` hashes the page's DOM at the end of every port flow and keyed operation (arm a loaded twice: a flow whose DOM differs between those two loads is unstable and checked by its assertions only) and fails unless every arm's equals arm a's; e_t and e_css may differ only in the movers' style text (the ghosts' and the drag shadow's), and only after the drag window; these hashes are also the reference every timed load is checked against.
Result: every arm the same as arm a in `same` (list, b, c, d, e, f), and in `count` every arm's DOM equal to arm a's after every port flow and every keyed operation, e_t and e_css included under their rule; arm a's two loads gave the same DOM after every flow, so no flow was unstable.
Two things the test found on the way: the test's own step export did not flush, so the list arm (which holds operations until a flush) lagged one step behind (every cdom export flushes; the port's debug exports did not either, which is why port_patch.py's base rewrite adds it to every arm); and CSS Typed OM refuses an angle for `rotate` ("Invalid type for property"), so arm d cannot set every property a number-with-unit write can (the test uses `outline-offset` there; the port and the keyed page write only px lengths, which Typed OM takes).
Typed OM and `setProperty` serialize 1/3 px the same way (0.333333px), so the numeric paths agree.
Mutation check (each made in gen_arms.py, built, run, the copy put back, and `same` green again): arm e's fragment applied in reverse order (caught: the run of four siblings, DOM after step 2), the list arm's rect read without applying the pending list (caught: rects), arm d's set_class written as textContent (caught: DOM after step 2); the first of these was not caught until the test gained a run of moves before one node, which the keyed list's reorderings never produce.

## Method

Headless Chrome 156.0.8078.74 (`--headless=new`, CDP, no window shown; each load's target brought to the front with `Page.bringToFront`, which makes headless render it and steals no focus), one Chrome per command killed by its own PID (also on SIGINT, SIGTERM and any uncaught error), every load in a new target (the last one closed), cache off, `Math.random` a fixed sequence installed before any page script (so cdom.start's seed, the keyed labels and the port's deal are the same in every arm and load).
One command per round per workload: every arm loaded once, in an order shuffled with the round as the seed (no arm always first, none always after the same one); each command stops starting loads past 780 s of wall time and records what it cut.
The gate: the 1-minute load average below 4 before Chrome starts (waited for at most 120 s), then per load the CPU of every other process (ps, this node and its Chrome's tree left out) recorded, and a load above 150% of a core marked busy and left out of the medians, so the measured Chrome's own load biases no arm.
Every window ends in a check of what it left (in a window of its own, not measured): the flow's own assertion (the hand dealt, the Attack offered, the attack landed, the bot moved, the shadow up, the drop attacked, a ghost flew; the keyed row counts, the replaced first row, the updated label, the one selected row, the swapped ids) and the DOM it left against count mode's reference hash for that arm; a load failing one is kept in the file, marked invalid, and left out of the medians.
Medians over loads (quantiles interpolated, so an even count's median is the mean of the middle two), with the 25th-75th percentile and the number of loads kept per arm.
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

Per window: the median over loads (one load per arm per round, 7 rounds at x1 and 5 at x4), the 25th-75th percentile, and the median over arm a's.
"script" is the window's main-thread task time less style, layout, paint and gc; "total" is every task in the window, and includes the driver's own few polling evaluations, the same for every arm.

How big the noise is, measured inside this table: arms e_t and e_css run arm a's cdom byte for byte and differ from it only in the ghost and drag-shadow writers, so in the deal, select, play and bot windows they execute the same code as arm a; there they land at 0.85-1.39 of arm a.
So any ratio from about 0.8 to 1.2 in a window of tens of milliseconds is noise, not the bridge.

CPU x1:

results/port_x1.jsonl: 59 of 63 loads kept; per arm a 7, list 6, b 6, c 7, d 7, e 6, f 7, e_t 7, e_css 6; load averages 1.38-2.73; Chrome/156.0.8078.74, CPU x1
  left out: list r2: busy 154.9%
  left out: e r4: busy 197.7%
  left out: b r4: busy 158%
  left out: e_css r6: busy 171.9%

script (ms, median over loads; 25th-75th; / arm a)
| window | a (n=7) | list (n=6) | b (n=6) | c (n=7) | d (n=7) | e (n=6) | f (n=7) | e_t (n=7) | e_css (n=6) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| deal | 237.39 [227.38-244.31] | 229.19 [223.93-239.23] (0.97) | 233.92 [226.62-243.02] (0.99) | 234.20 [226.59-245.73] (0.99) | 236.41 [229.52-245.04] (1.00) | 239.90 [234.59-244.61] (1.01) | 229.12 [210.17-237.84] (0.97) | 232.07 [216.97-247.45] (0.98) | 239.73 [231.72-241.35] (1.01) |
| select | 7.56 [6.88-7.72] | 7.24 [6.48-7.89] (0.96) | 7.37 [5.72-7.64] (0.97) | 7.58 [7.42-7.83] (1.00) | 6.91 [5.91-7.25] (0.91) | 9.94 [8.07-11.95] (1.31) | 7.35 [7.30-7.70] (0.97) | 7.43 [7.24-7.74] (0.98) | 10.52 [7.90-12.35] (1.39) |
| play | 38.00 [36.09-39.18] | 36.18 [30.95-37.51] (0.95) | 32.15 [30.65-35.23] (0.85) | 32.30 [30.49-37.63] (0.85) | 33.63 [32.12-34.80] (0.89) | 33.09 [31.93-34.39] (0.87) | 32.08 [29.29-35.94] (0.84) | 33.32 [30.73-34.91] (0.88) | 33.57 [31.21-36.10] (0.88) |
| bot | 27.56 [26.07-33.34] | 31.43 [30.73-31.96] (1.14) | 31.38 [26.10-32.97] (1.14) | 35.27 [31.27-36.69] (1.28) | 28.35 [23.56-30.67] (1.03) | 28.59 [27.65-31.35] (1.04) | 31.19 [20.90-34.32] (1.13) | 27.27 [19.65-31.79] (0.99) | 30.08 [22.02-35.72] (1.09) |
| drag | 61.72 [59.36-63.11] | 57.33 [56.60-59.99] (0.93) | 60.39 [58.93-64.17] (0.98) | 56.57 [54.93-61.06] (0.92) | 61.11 [57.60-64.10] (0.99) | 54.34 [49.82-56.30] (0.88) | 59.25 [58.15-62.86] (0.96) | 56.27 [51.72-57.46] (0.91) | 55.14 [52.70-56.80] (0.89) |
| drop | 30.08 [26.76-31.27] | 29.55 [25.06-30.10] (0.98) | 34.86 [31.74-35.22] (1.16) | 30.74 [27.94-32.95] (1.02) | 33.93 [33.07-34.59] (1.13) | 26.25 [23.83-34.46] (0.87) | 27.85 [18.06-30.17] (0.93) | 33.16 [31.99-36.28] (1.10) | 31.50 [27.41-34.37] (1.05) |
| transfer | 33.79 [32.23-36.11] | 30.52 [28.99-32.91] (0.90) | 29.27 [28.01-30.51] (0.87) | 32.46 [28.79-33.86] (0.96) | 30.41 [27.85-32.52] (0.90) | 27.42 [26.68-29.30] (0.81) | 29.10 [28.29-32.17] (0.86) | 32.03 [29.31-32.53] (0.95) | 32.09 [28.77-33.92] (0.95) |

total (ms, median over loads; 25th-75th; / arm a)
| window | a (n=7) | list (n=6) | b (n=6) | c (n=7) | d (n=7) | e (n=6) | f (n=7) | e_t (n=7) | e_css (n=6) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| deal | 496.24 [465.99-509.36] | 473.25 [457.55-497.48] (0.95) | 488.78 [455.07-507.94] (0.98) | 486.55 [462.93-514.03] (0.98) | 494.54 [477.34-504.03] (1.00) | 500.96 [481.28-521.42] (1.01) | 474.44 [435.38-491.42] (0.96) | 488.30 [447.72-504.99] (0.98) | 493.09 [479.58-495.34] (0.99) |
| select | 12.62 [12.02-12.90] | 12.46 [10.37-13.87] (0.99) | 12.39 [9.34-12.85] (0.98) | 12.96 [12.58-13.14] (1.03) | 11.69 [9.52-12.01] (0.93) | 16.44 [13.15-19.81] (1.30) | 12.03 [11.80-12.83] (0.95) | 12.60 [11.94-13.49] (1.00) | 17.29 [13.53-20.44] (1.37) |
| play | 68.08 [63.86-68.85] | 64.54 [55.01-65.79] (0.95) | 56.54 [52.79-61.29] (0.83) | 57.14 [53.82-66.60] (0.84) | 57.92 [56.83-60.74] (0.85) | 58.35 [56.97-59.37] (0.86) | 56.25 [51.19-64.49] (0.83) | 57.69 [54.37-61.66] (0.85) | 59.39 [55.92-63.93] (0.87) |
| bot | 48.49 [46.28-59.61] | 55.02 [53.93-55.98] (1.13) | 55.77 [46.48-59.02] (1.15) | 62.50 [56.12-63.86] (1.29) | 49.80 [41.21-54.98] (1.03) | 51.05 [48.73-55.18] (1.05) | 55.07 [36.85-60.51] (1.14) | 48.86 [35.12-57.75] (1.01) | 52.89 [39.26-63.13] (1.09) |
| drag | 97.55 [93.67-99.10] | 90.82 [86.51-94.84] (0.93) | 93.94 [92.40-100.29] (0.96) | 86.22 [84.53-97.06] (0.88) | 96.35 [92.03-100.89] (0.99) | 85.03 [77.83-89.41] (0.87) | 93.54 [89.39-98.08] (0.96) | 83.72 [79.85-87.16] (0.86) | 86.00 [83.95-87.66] (0.88) |
| drop | 52.81 [47.26-54.97] | 51.56 [44.14-53.40] (0.98) | 61.23 [56.31-61.73] (1.16) | 54.81 [49.60-57.45] (1.04) | 59.61 [58.73-61.09] (1.13) | 46.81 [42.85-60.05] (0.89) | 50.04 [32.23-53.56] (0.95) | 58.08 [56.83-63.53] (1.10) | 55.73 [47.95-60.48] (1.06) |
| transfer | 59.53 [56.42-63.98] | 53.12 [50.71-58.04] (0.89) | 51.46 [49.04-54.38] (0.86) | 56.06 [50.58-59.77] (0.94) | 53.27 [48.50-57.59] (0.89) | 47.68 [45.71-51.44] (0.80) | 51.25 [49.41-57.50] (0.86) | 56.58 [50.77-57.27] (0.95) | 55.87 [49.96-59.49] (0.94) |

layout (ms, median over loads; 25th-75th; / arm a)
| window | a (n=7) | list (n=6) | b (n=6) | c (n=7) | d (n=7) | e (n=6) | f (n=7) | e_t (n=7) | e_css (n=6) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| deal | 43.67 [41.10-46.11] | 43.22 [41.55-44.67] (0.99) | 44.12 [39.99-46.36] (1.01) | 43.55 [39.89-47.99] (1.00) | 43.77 [42.80-45.03] (1.00) | 45.39 [42.92-47.46] (1.04) | 42.48 [39.01-43.98] (0.97) | 42.47 [37.73-43.20] (0.97) | 43.74 [43.36-44.59] (1.00) |
| select | 0.25 [0.22-0.26] | 0.21 [0.20-0.24] (0.85) | 0.21 [0.19-0.24] (0.84) | 0.25 [0.22-0.26] (1.02) | 0.22 [0.20-0.22] (0.87) | 0.22 [0.21-0.26] (0.88) | 0.26 [0.23-0.26] (1.03) | 0.22 [0.21-0.28] (0.88) | 0.22 [0.22-0.25] (0.89) |
| play | 4.68 [4.28-4.87] | 4.32 [3.58-4.54] (0.92) | 3.81 [3.61-4.08] (0.81) | 3.81 [3.65-4.45] (0.81) | 3.94 [3.80-4.12] (0.84) | 4.06 [3.85-4.09] (0.87) | 3.64 [3.45-4.53] (0.78) | 3.78 [3.73-4.06] (0.81) | 4.06 [3.89-4.45] (0.87) |
| bot | 3.47 [3.22-4.21] | 3.90 [3.78-4.04] (1.12) | 4.11 [3.26-4.43] (1.18) | 4.51 [3.97-4.66] (1.30) | 3.34 [2.89-3.73] (0.96) | 3.64 [3.50-4.08] (1.05) | 3.83 [2.59-4.60] (1.10) | 3.27 [2.49-3.92] (0.94) | 3.64 [2.85-4.45] (1.05) |
| drag | 9.37 [9.33-9.57] | 8.90 [8.23-9.41] (0.95) | 9.25 [8.22-10.18] (0.99) | 9.05 [7.97-9.69] (0.97) | 9.59 [8.89-9.62] (1.02) | 7.99 [7.53-8.93] (0.85) | 8.95 [8.71-10.00] (0.96) | 4.18 [3.17-5.32] (0.45) | 8.04 [7.43-8.75] (0.86) |
| drop | 3.40 [3.17-3.80] | 3.34 [3.02-3.56] (0.98) | 4.08 [3.80-4.14] (1.20) | 3.87 [3.32-4.09] (1.14) | 4.03 [3.86-4.22] (1.18) | 3.23 [2.94-4.05] (0.95) | 3.35 [2.27-3.66] (0.98) | 4.07 [3.91-4.23] (1.20) | 3.66 [2.96-4.09] (1.08) |
| transfer | 4.13 [3.97-4.53] | 3.58 [3.57-3.98] (0.87) | 3.70 [3.50-3.86] (0.90) | 3.81 [3.69-4.27] (0.92) | 3.62 [3.46-4.17] (0.88) | 3.50 [3.16-3.73] (0.85) | 3.58 [3.48-3.98] (0.87) | 3.86 [3.50-4.05] (0.94) | 3.87 [3.55-4.13] (0.94) |

frames / long tasks / longest task (ms), medians
| window | a (n=7) | list (n=6) | b (n=6) | c (n=7) | d (n=7) | e (n=6) | f (n=7) | e_t (n=7) | e_css (n=6) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| deal | 423 / 0 / 4.6 | 423 / 0 / 5.2 | 423 / 0 / 4.6 | 423 / 0 / 4.8 | 423 / 0 / 4.9 | 423.5 / 0 / 4.8 | 423 / 0 / 5.0 | 424 / 0 / 4.9 | 424 / 0 / 4.7 |
| select | 11 / 0 / 1.9 | 11 / 0 / 1.9 | 11 / 0 / 1.8 | 11 / 0 / 1.9 | 10 / 0 / 1.8 | 10 / 0 / 2.5 | 11 / 0 / 1.9 | 11 / 0 / 2.0 | 11 / 0 / 2.3 |
| play | 36 / 0 / 3.8 | 36 / 0 / 3.4 | 36 / 0 / 3.7 | 36 / 0 / 3.7 | 36 / 0 / 3.1 | 36 / 0 / 3.8 | 36 / 0 / 3.6 | 36 / 0 / 3.8 | 36 / 0 / 3.7 |
| bot | 32 / 0 / 2.6 | 32 / 0 / 2.4 | 32 / 0 / 3.0 | 32 / 0 / 2.9 | 32 / 0 / 2.4 | 32 / 0 / 2.8 | 32 / 0 / 2.6 | 32 / 0 / 2.4 | 32 / 0 / 2.4 |
| drag | 49 / 0 / 10.1 | 49 / 0 / 8.8 | 49 / 0 / 8.9 | 49 / 0 / 10.3 | 49 / 0 / 10.0 | 49 / 0 / 8.5 | 49 / 0 / 9.3 | 49 / 0 / 7.8 | 49 / 0 / 8.6 |
| drop | 33 / 0 / 2.5 | 33 / 0 / 2.4 | 32.5 / 0 / 2.5 | 33 / 0 / 2.5 | 33 / 0 / 2.5 | 33 / 0 / 2.6 | 33 / 0 / 2.4 | 33 / 0 / 2.6 | 33 / 0 / 2.5 |
| transfer | 33 / 0 / 2.8 | 33 / 0 / 3.0 | 33 / 0 / 2.7 | 33 / 0 / 3.8 | 33 / 0 / 3.8 | 33 / 0 / 2.8 | 33 / 0 / 2.8 | 33 / 0 / 3.7 | 33 / 0 / 3.5 |

CPU x4:

results/port_x4.jsonl: 42 of 45 loads kept; per arm a 4, list 5, b 5, c 4, d 5, e 5, f 4, e_t 5, e_css 5; load averages 1.27-2.87; Chrome/156.0.8078.74, CPU x4
  left out: f r0: busy 388.1%
  left out: a r0: busy 159%
  left out: c r4: busy 356.3%

script (ms, median over loads; 25th-75th; / arm a)
| window | a (n=4) | list (n=5) | b (n=5) | c (n=4) | d (n=5) | e (n=5) | f (n=4) | e_t (n=5) | e_css (n=5) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| deal | 146.92 [144.13-149.34] | 154.86 [140.41-158.27] (1.05) | 148.46 [147.53-177.24] (1.01) | 149.18 [142.76-174.45] (1.02) | 147.90 [144.31-152.54] (1.01) | 142.95 [138.43-149.17] (0.97) | 137.62 [135.94-141.61] (0.94) | 138.21 [134.65-189.55] (0.94) | 149.76 [148.66-163.62] (1.02) |
| select | 7.41 [6.94-8.82] | 11.70 [9.56-11.89] (1.58) | 11.85 [7.65-12.23] (1.60) | 9.90 [7.51-11.82] (1.34) | 7.76 [6.13-8.54] (1.05) | 12.35 [10.69-12.94] (1.67) | 10.61 [10.16-11.21] (1.43) | 13.86 [12.16-14.98] (1.87) | 11.93 [11.43-13.12] (1.61) |
| play | 29.28 [26.99-30.66] | 26.85 [25.64-35.66] (0.92) | 28.86 [28.40-30.48] (0.99) | 25.76 [24.12-26.93] (0.88) | 28.39 [28.09-29.79] (0.97) | 30.42 [29.09-30.88] (1.04) | 27.14 [26.78-27.73] (0.93) | 29.48 [27.63-32.90] (1.01) | 26.56 [25.45-26.57] (0.91) |
| bot | 22.51 [21.12-24.42] | 31.65 [27.73-33.89] (1.41) | 25.54 [23.55-25.88] (1.13) | 22.88 [20.99-25.47] (1.02) | 24.09 [23.27-25.58] (1.07) | 23.49 [22.73-23.98] (1.04) | 21.11 [20.00-24.85] (0.94) | 20.95 [20.17-21.26] (0.93) | 25.26 [17.96-30.65] (1.12) |
| drag | 55.49 [54.57-56.65] | 70.89 [67.36-71.40] (1.28) | 55.38 [52.09-56.09] (1.00) | 60.84 [58.77-61.91] (1.10) | 58.76 [58.64-61.01] (1.06) | 57.35 [53.87-60.25] (1.03) | 55.70 [52.91-57.47] (1.00) | 52.65 [51.71-55.24] (0.95) | 60.97 [60.51-61.30] (1.10) |
| drop | 19.00 [17.54-22.02] | 25.22 [21.23-28.00] (1.33) | 20.79 [18.64-22.77] (1.09) | 23.72 [23.46-24.07] (1.25) | 22.27 [19.64-25.52] (1.17) | 25.73 [23.23-26.63] (1.35) | 19.53 [17.79-20.78] (1.03) | 19.89 [17.90-24.09] (1.05) | 22.20 [19.76-22.99] (1.17) |
| transfer | 31.15 [30.20-31.55] | 36.57 [30.79-40.48] (1.17) | 31.09 [29.19-31.54] (1.00) | 31.69 [30.41-35.50] (1.02) | 34.02 [33.96-36.31] (1.09) | 29.64 [28.97-35.00] (0.95) | 28.63 [26.32-30.09] (0.92) | 30.70 [29.82-30.83] (0.99) | 30.91 [30.20-33.45] (0.99) |

total (ms, median over loads; 25th-75th; / arm a)
| window | a (n=4) | list (n=5) | b (n=5) | c (n=4) | d (n=5) | e (n=5) | f (n=4) | e_t (n=5) | e_css (n=5) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| deal | 335.44 [328.66-341.30] | 365.58 [317.37-383.54] (1.09) | 367.76 [333.89-388.07] (1.10) | 351.78 [337.94-406.43] (1.05) | 334.83 [332.78-354.42] (1.00) | 335.05 [325.81-342.26] (1.00) | 316.90 [308.54-327.56] (0.94) | 319.96 [313.31-435.48] (0.95) | 348.00 [336.01-357.20] (1.04) |
| select | 12.01 [11.84-14.46] | 19.93 [16.24-20.00] (1.66) | 20.82 [13.57-21.08] (1.73) | 15.75 [11.36-20.37] (1.31) | 12.83 [10.45-13.07] (1.07) | 21.07 [21.00-21.12] (1.75) | 19.05 [18.55-20.08] (1.59) | 22.87 [21.01-23.27] (1.90) | 19.96 [19.27-21.55] (1.66) |
| play | 54.65 [53.46-55.53] | 57.27 [54.34-60.55] (1.05) | 60.52 [56.17-63.10] (1.11) | 52.81 [50.61-55.09] (0.97) | 53.15 [52.47-57.33] (0.97) | 58.48 [55.16-63.07] (1.07) | 54.40 [51.71-57.39] (1.00) | 62.40 [52.86-62.45] (1.14) | 51.45 [50.21-54.10] (0.94) |
| bot | 45.35 [41.84-49.42] | 62.49 [57.17-63.95] (1.38) | 52.07 [46.53-53.38] (1.15) | 47.46 [42.05-52.83] (1.05) | 48.00 [45.04-49.96] (1.06) | 43.97 [43.60-46.39] (0.97) | 45.72 [44.59-50.06] (1.01) | 44.63 [39.93-45.53] (0.98) | 46.33 [37.22-60.32] (1.02) |
| drag | 96.20 [93.06-98.80] | 114.20 [108.06-114.47] (1.19) | 96.09 [89.82-96.30] (1.00) | 100.97 [98.39-102.43] (1.05) | 104.36 [101.61-107.78] (1.08) | 96.85 [94.58-106.48] (1.01) | 96.32 [94.02-99.42] (1.00) | 89.00 [88.51-90.70] (0.93) | 103.58 [97.76-106.75] (1.08) |
| drop | 42.38 [40.23-47.26] | 50.02 [44.60-56.50] (1.18) | 39.59 [37.62-45.13] (0.93) | 48.38 [47.90-48.81] (1.14) | 44.10 [43.54-54.34] (1.04) | 46.02 [45.72-49.08] (1.09) | 41.52 [38.19-44.75] (0.98) | 40.54 [40.52-47.61] (0.96) | 43.71 [40.09-45.03] (1.03) |
| transfer | 60.16 [58.52-61.66] | 65.04 [62.90-71.42] (1.08) | 58.51 [58.45-62.94] (0.97) | 59.65 [58.46-68.59] (0.99) | 64.15 [63.23-64.44] (1.07) | 63.62 [59.08-65.73] (1.06) | 55.56 [54.30-56.22] (0.92) | 58.31 [57.57-58.63] (0.97) | 62.25 [56.79-63.09] (1.03) |

layout (ms, median over loads; 25th-75th; / arm a)
| window | a (n=4) | list (n=5) | b (n=5) | c (n=4) | d (n=5) | e (n=5) | f (n=4) | e_t (n=5) | e_css (n=5) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| deal | 35.43 [34.31-35.60] | 36.92 [35.98-39.22] (1.04) | 37.26 [35.92-39.14] (1.05) | 38.09 [35.43-43.72] (1.08) | 35.59 [34.94-35.81] (1.00) | 34.54 [34.10-36.31] (0.97) | 34.90 [32.86-35.43] (0.98) | 33.29 [29.74-39.46] (0.94) | 34.87 [33.25-36.88] (0.98) |
| select | 0.27 [0.10-0.53] | 0.53 [0.10-0.78] (1.95) | 0.57 [0.10-0.73] (2.08) | 0.63 [0.31-0.89] (2.30) | 0.13 [0.12-0.55] (0.49) | 0.10 [0.10-0.23] (0.36) | 0.16 [0.10-0.38] (0.57) | 0.51 [0.15-0.75] (1.86) | 0.67 [0.29-0.71] (2.45) |
| play | 4.12 [2.59-5.41] | 4.51 [4.37-5.87] (1.10) | 4.87 [4.11-5.28] (1.18) | 5.22 [4.81-5.64] (1.27) | 4.64 [4.40-5.48] (1.13) | 5.56 [4.97-6.23] (1.35) | 2.91 [2.58-3.20] (0.71) | 4.89 [4.10-5.54] (1.19) | 4.44 [4.28-5.26] (1.08) |
| bot | 3.63 [3.35-3.96] | 4.49 [4.48-5.30] (1.24) | 4.22 [3.21-4.62] (1.16) | 3.59 [3.41-4.05] (0.99) | 3.09 [2.84-3.63] (0.85) | 3.64 [3.04-5.87] (1.00) | 4.75 [4.28-5.44] (1.31) | 4.36 [3.92-5.60] (1.20) | 3.54 [3.22-4.02] (0.97) |
| drag | 11.02 [10.78-11.56] | 12.18 [11.38-13.29] (1.11) | 12.67 [11.68-13.72] (1.15) | 10.39 [10.02-10.84] (0.94) | 10.82 [10.00-12.08] (0.98) | 11.59 [9.89-12.91] (1.05) | 10.79 [10.60-11.21] (0.98) | 6.91 [6.53-7.03] (0.63) | 12.03 [10.12-12.35] (1.09) |
| drop | 5.30 [4.76-5.41] | 3.51 [3.50-3.69] (0.66) | 3.95 [3.13-4.20] (0.75) | 3.02 [2.90-3.48] (0.57) | 5.34 [5.08-6.08] (1.01) | 3.44 [3.27-4.13] (0.65) | 2.01 [1.39-2.99] (0.38) | 4.62 [3.77-5.14] (0.87) | 3.12 [3.06-3.75] (0.59) |
| transfer | 4.68 [4.49-4.90] | 5.13 [4.95-5.57] (1.10) | 4.83 [4.28-5.31] (1.03) | 6.24 [5.73-6.67] (1.33) | 5.28 [4.35-8.13] (1.13) | 5.53 [4.82-6.06] (1.18) | 5.47 [4.64-5.81] (1.17) | 5.14 [3.29-5.81] (1.10) | 5.14 [4.90-6.17] (1.10) |

frames / long tasks / longest task (ms), medians
| window | a (n=4) | list (n=5) | b (n=5) | c (n=4) | d (n=5) | e (n=5) | f (n=4) | e_t (n=5) | e_css (n=5) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| deal | 422 / 0 / 6.7 | 421 / 0 / 7.0 | 422 / 0 / 6.5 | 423 / 0 / 6.9 | 422 / 0 / 7.1 | 422 / 0 / 6.8 | 421.5 / 0 / 7.0 | 421 / 0 / 6.9 | 421 / 0 / 7.3 |
| select | 11 / 0 / 2.8 | 9 / 0 / 4.6 | 11 / 0 / 3.8 | 9.5 / 0 / 4.6 | 9 / 0 / 4.8 | 11 / 0 / 5.2 | 10.5 / 0 / 4.6 | 10 / 0 / 3.4 | 9 / 0 / 4.3 |
| play | 36 / 0 / 7.4 | 36 / 0 / 7.3 | 36 / 0 / 8.0 | 36 / 0 / 6.9 | 36 / 0 / 7.8 | 36 / 0 / 7.5 | 36.5 / 0 / 7.6 | 36 / 0 / 7.4 | 36 / 0 / 7.3 |
| bot | 31.5 / 0 / 4.2 | 32 / 0 / 4.8 | 32 / 0 / 4.0 | 32 / 0 / 4.2 | 32 / 0 / 4.2 | 32 / 0 / 4.1 | 32 / 0 / 4.1 | 32 / 0 / 4.0 | 32 / 0 / 3.7 |
| drag | 48.5 / 0 / 12.7 | 49 / 0 / 12.5 | 49 / 0 / 12.5 | 48 / 0 / 12.8 | 48 / 0 / 12.9 | 49 / 0 / 12.5 | 49 / 0 / 12.0 | 49 / 0 / 12.3 | 49 / 0 / 12.8 |
| drop | 33.5 / 0 / 3.7 | 34 / 0 / 3.8 | 33 / 0 / 3.9 | 33 / 0 / 3.9 | 33 / 0 / 3.7 | 33 / 0 / 3.8 | 33 / 0 / 3.7 | 33 / 0 / 3.7 | 34 / 0 / 3.8 |
| transfer | 33.5 / 0 / 3.9 | 33 / 0 / 3.9 | 33 / 0 / 4.1 | 33 / 0 / 4.3 | 34 / 0 / 4.2 | 33 / 0 / 3.9 | 34 / 0 / 3.8 | 33 / 0 / 3.8 | 33 / 0 / 4.0 |


## js-framework-benchmark operations

Each operation one real click on a fresh load (no warm-up clicks), 7 rounds, CPU x1; x4 was not run (below).

results/keyed_x1.jsonl: 41 of 42 loads kept; per arm a 6, list 7, b 7, c 7, d 7, e 7; load averages 1.45-3.14; Chrome/156.0.8078.74, CPU x1
  left out: a r2: busy 164.9%

script (ms, median over loads; 25th-75th; / arm a)
| window | a (n=6) | list (n=7) | b (n=7) | c (n=7) | d (n=7) | e (n=7) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| create1k | 38.91 [29.61-40.23] | 37.18 [29.59-38.90] (0.96) | 36.57 [35.85-38.73] (0.94) | 36.81 [35.64-37.57] (0.95) | 38.36 [37.02-40.53] (0.99) | 38.00 [30.01-38.93] (0.98) |
| replace1k | 13.73 [12.36-13.94] | 13.80 [12.53-14.30] (1.01) | 14.28 [12.14-14.42] (1.04) | 14.12 [13.81-16.29] (1.03) | 14.50 [12.60-15.11] (1.06) | 13.87 [11.11-14.21] (1.01) |
| update | 5.94 [4.66-6.68] | 5.70 [4.67-6.49] (0.96) | 6.49 [6.28-6.76] (1.09) | 6.43 [5.45-6.65] (1.08) | 6.28 [5.40-6.91] (1.06) | 6.30 [6.01-6.61] (1.06) |
| select | 6.53 [6.45-6.58] | 6.71 [6.48-6.96] (1.03) | 6.64 [6.42-7.03] (1.02) | 6.72 [6.39-6.85] (1.03) | 6.59 [6.35-6.80] (1.01) | 6.22 [4.99-6.65] (0.95) |
| swap | 5.88 [5.70-7.82] | 5.63 [5.12-6.12] (0.96) | 5.42 [5.15-5.86] (0.92) | 5.72 [5.48-5.85] (0.97) | 5.80 [5.55-7.08] (0.99) | 5.81 [5.61-7.21] (0.99) |
| remove | 6.50 [6.25-7.04] | 6.74 [6.64-7.07] (1.04) | 6.49 [6.38-6.73] (1.00) | 6.60 [6.46-7.22] (1.01) | 6.72 [6.61-6.97] (1.03) | 7.06 [6.05-7.19] (1.09) |
| clear1k | 9.49 [9.20-9.97] | 9.42 [8.66-10.09] (0.99) | 9.59 [9.14-9.66] (1.01) | 9.42 [9.38-9.95] (0.99) | 9.70 [9.41-9.79] (1.02) | 9.66 [8.80-9.81] (1.02) |
| create10k | 59.02 [58.66-59.34] | 59.48 [58.18-59.92] (1.01) | 61.54 [61.21-62.22] (1.04) | 59.60 [58.79-60.15] (1.01) | 61.66 [59.88-62.90] (1.04) | 60.66 [60.10-61.25] (1.03) |
| clear10k | 62.64 [61.45-63.58] | 62.80 [60.32-63.12] (1.00) | 63.46 [61.94-64.29] (1.01) | 62.80 [62.02-63.60] (1.00) | 64.95 [64.60-65.26] (1.04) | 62.79 [61.91-63.01] (1.00) |
| append | 45.04 [40.92-46.66] | 42.74 [17.04-47.46] (0.95) | 45.65 [11.15-47.79] (1.01) | 39.19 [12.49-44.62] (0.87) | 43.85 [11.70-47.31] (0.97) | 46.83 [45.95-47.48] (1.04) |

total (ms, median over loads; 25th-75th; / arm a)
| window | a (n=6) | list (n=7) | b (n=7) | c (n=7) | d (n=7) | e (n=7) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| create1k | 84.84 [75.66-86.21] | 83.18 [75.69-84.94] (0.98) | 82.87 [79.76-84.73] (0.98) | 81.09 [79.74-83.44] (0.96) | 84.43 [78.44-86.21] (1.00) | 82.86 [69.89-84.20] (0.98) |
| replace1k | 49.11 [46.26-49.47] | 49.22 [45.36-49.83] (1.00) | 49.13 [43.41-49.63] (1.00) | 49.95 [49.67-52.92] (1.02) | 49.72 [44.30-50.22] (1.01) | 49.27 [41.55-49.88] (1.00) |
| update | 12.15 [9.72-14.05] | 11.46 [9.83-14.05] (0.94) | 13.93 [13.74-14.10] (1.15) | 13.94 [11.03-14.19] (1.15) | 13.57 [12.02-14.62] (1.12) | 14.09 [12.47-14.37] (1.16) |
| select | 10.55 [10.33-10.79] | 10.84 [10.55-11.18] (1.03) | 10.64 [10.25-11.29] (1.01) | 10.69 [10.43-10.96] (1.01) | 10.61 [10.33-10.97] (1.01) | 9.93 [7.84-10.76] (0.94) |
| swap | 16.17 [15.75-16.44] | 15.59 [13.78-16.36] (0.96) | 15.04 [14.70-15.66] (0.93) | 15.40 [14.96-15.63] (0.95) | 15.38 [15.21-18.34] (0.95) | 15.79 [15.52-18.41] (0.98) |
| remove | 20.13 [19.59-21.27] | 20.72 [20.24-21.49] (1.03) | 20.06 [19.88-20.22] (1.00) | 21.19 [20.38-21.42] (1.05) | 20.68 [19.87-20.86] (1.03) | 21.99 [17.96-22.35] (1.09) |
| clear1k | 12.36 [12.07-12.91] | 12.31 [11.31-13.07] (1.00) | 12.40 [11.93-12.56] (1.00) | 12.40 [12.29-13.04] (1.00) | 12.55 [12.39-12.77] (1.01) | 12.66 [11.61-12.83] (1.02) |
| create10k | 330.97 [329.82-331.58] | 332.31 [328.63-333.07] (1.00) | 332.53 [331.01-334.73] (1.00) | 330.95 [330.15-332.06] (1.00) | 334.97 [331.37-337.22] (1.01) | 333.81 [332.94-335.30] (1.01) |
| clear10k | 69.66 [68.84-70.58] | 70.10 [68.12-70.53] (1.01) | 70.94 [69.40-72.96] (1.02) | 70.18 [69.25-70.82] (1.01) | 72.20 [72.09-72.56] (1.04) | 69.72 [69.11-70.08] (1.00) |
| append | 90.23 [77.24-91.36] | 78.27 [56.10-93.03] (0.87) | 85.64 [50.35-93.74] (0.95) | 73.28 [54.96-86.66] (0.81) | 89.71 [52.89-95.06] (0.99) | 91.46 [87.17-92.55] (1.01) |

frames / long tasks / longest task (ms), medians
| window | a (n=6) | list (n=7) | b (n=7) | c (n=7) | d (n=7) | e (n=7) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| create1k | 4 / 1 / 51.8 | 4 / 0 / 50.0 | 4 / 1 / 51.8 | 4 / 0 / 49.6 | 4 / 1 / 52.1 | 4 / 0 / 49.8 |
| replace1k | 4 / 0 / 41.8 | 4 / 0 / 40.3 | 4 / 0 / 41.7 | 4 / 0 / 41.8 | 4 / 0 / 42.2 | 4 / 0 / 41.8 |
| update | 4 / 0 / 5.2 | 4 / 0 / 4.9 | 4 / 0 / 6.3 | 4 / 0 / 6.2 | 4 / 0 / 6.2 | 4 / 0 / 6.0 |
| select | 4 / 0 / 5.3 | 4 / 0 / 5.2 | 4 / 0 / 5.4 | 4 / 0 / 5.3 | 4 / 0 / 5.3 | 4 / 0 / 5.0 |
| swap | 4 / 0 / 6.1 | 4 / 0 / 6.6 | 4 / 0 / 6.4 | 4 / 0 / 6.3 | 4 / 0 / 6.4 | 4 / 0 / 6.6 |
| remove | 4 / 0 / 9.3 | 4 / 0 / 9.4 | 4 / 0 / 9.3 | 4 / 0 / 9.4 | 4 / 0 / 9.1 | 4 / 0 / 9.9 |
| clear1k | 4 / 0 / 5.5 | 4 / 0 / 5.3 | 4 / 0 / 5.5 | 4 / 0 / 5.5 | 4 / 0 / 5.5 | 4 / 0 / 5.1 |
| create10k | 4 / 1 / 297.4 | 4 / 1 / 297.9 | 4 / 1 / 299.4 | 4 / 1 / 297.5 | 4 / 1 / 300.8 | 4 / 1 / 300.4 |
| clear10k | 4 / 0 / 29.2 | 4 / 0 / 29.2 | 4 / 0 / 29.5 | 4 / 0 / 29.2 | 4 / 0 / 30.5 | 4 / 0 / 29.0 |
| append | 4 / 0 / 42.0 | 4 / 0 / 41.7 | 4 / 0 / 42.8 | 4 / 0 / 42.0 | 4 / 0 / 43.2 | 4 / 0 / 41.4 |


## Crossings and strings decoded

Import calls / strings decoded, per flow (the counting builds, one load per arm, untimed); the list arm's records are the operations its `apply` ran, arm d's exports the calls the shim makes into wasm to mirror references.

port: import calls / strings decoded per flow (list: + op-list records applied; d: + calls into wasm to mirror references), Chrome/156.0.8078.74
| flow | a | list | b | c | d | e | f | e_t | e_css |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| deal | 2435 / 258 | 1574 / 262 (+1225 records) | 2449 / 262 | 2435 / 258 | 2614 / 258 (+510 exports) | 2082 / 258 | 2435 / 258 | 2339 / 262 | 2219 / 203 |
| select | 32 / 4 | 15 / 4 (+20 records) | 32 / 4 | 32 / 4 | 32 / 4 (+8 exports) | 31 / 4 | 32 / 4 | 32 / 4 | 32 / 4 |
| play | 197 / 23 | 137 / 23 (+90 records) | 197 / 23 | 197 / 23 | 205 / 23 (+39 exports) | 179 / 23 | 197 / 23 | 197 / 23 | 197 / 23 |
| bot | 150 / 15 | 110 / 15 (+59 records) | 150 / 15 | 150 / 15 | 155 / 15 (+25 exports) | 140 / 15 | 148 / 15 | 148 / 15 | 150 / 15 |
| drag | 209 / 12 | 148 / 12 (+109 records) | 209 / 12 | 209 / 12 | 279 / 12 (+14 exports) | 171 / 12 | 209 / 12 | 209 / 47 | 209 / 47 |
| drop | 165 / 15 | 126 / 15 (+61 records) | 163 / 15 | 165 / 15 | 173 / 15 (+29 exports) | 159 / 15 | 163 / 15 | 165 / 15 | 165 / 15 |
| transfer | 517 / 87 | 353 / 86 (+285 records) | 515 / 86 | 517 / 87 | 578 / 87 (+102 exports) | 393 / 86 | 515 / 86 | 461 / 87 | 397 / 57 |

keyed: import calls / strings decoded per flow (list: + op-list records applied; d: + calls into wasm to mirror references), Chrome/156.0.8078.74
| flow | a | list | b | c | d | e | f | e_t | e_css |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| create1k | 5004 / 835 | 3 / 835 (+5003 records) | 5004 / 835 | 5004 / 835 | 5004 / 835 (+1835 exports) | 3004 / 835 | - | - | - |
| replace1k | 6004 / 636 | 3 / 636 (+6003 records) | 6004 / 636 | 6004 / 636 | 6004 / 636 (+2636 exports) | 3005 / 636 | - | - | - |
| update | 101 / 97 | 2 / 97 (+100 records) | 101 / 97 | 101 / 97 | 101 / 97 (+97 exports) | 101 / 97 | - | - | - |
| select | 1 / 0 | 1 / 0 (+1 records) | 1 / 0 | 1 / 0 | 1 / 0 | 1 / 0 | - | - | - |
| swap | 2 / 0 | 1 / 0 (+2 records) | 2 / 0 | 2 / 0 | 2 / 0 | 1 / 0 | - | - | - |
| remove | 1 / 0 | 1 / 0 (+1 records) | 1 / 0 | 1 / 0 | 1 / 0 (+1 exports) | 1 / 0 | - | - | - |
| clear1k | 1000 / 0 | 1 / 0 (+1000 records) | 1000 / 0 | 1000 / 0 | 1000 / 0 (+999 exports) | 2 / 0 | - | - | - |
| create10k | 49997 / 1678 | 14 / 1678 (+49996 records) | 49997 / 1678 | 49997 / 1678 | 49997 / 1678 (+11678 exports) | 30003 / 1678 | - | - | - |
| clear10k | 10001 / 0 | 3 / 0 (+10001 records) | 10001 / 0 | 10001 / 0 | 10001 / 0 (+10000 exports) | 11 / 0 | - | - | - |
| append | 5001 / 22 | 3 / 22 (+5000 records) | 5001 / 22 | 5001 / 22 | 5001 / 22 (+1022 exports) | 3001 / 22 | - | - | - |

micro: import calls / strings decoded per operation (list: + op-list records applied; d: + calls into wasm to mirror references), Chrome/156.0.8078.74
| op | a | list | b | c | d | e | f | e_t | e_css |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| style_num | 1 / 0 | 0 / 0 (+1 records) | 1 / 0 | 1 / 0 | 2 / 0 (+0 exports) | 1 / 0 | 1 / 0 | - | - |
| style_tmp | 2 / 1 | 2 / 1 (+1 records) | 2 / 1 | 2 / 1 | 2 / 1 (+1 exports) | 2 / 1 | 2 / 1 | - | - |
| set_text | 1 / 0 | 0 / 0 (+1 records) | 1 / 0 | 1 / 0 | 1 / 0 | 1 / 0 | 1 / 0 | - | - |
| attr | 1 / 0 | 0 / 0 (+1 records) | 1 / 0 | 1 / 0 | 1 / 0 (+0 exports) | 1 / 0 | 1 / 0 | - | - |
| mount | 5.01 / 0 | 0 / 0 (+5 records) | 5.01 / 0 | 5.01 / 0 | 5.01 / 0 (+0 exports) | 5.01 / 0 | 5.01 / 0 | - | - |
| clone | 4 / 0 | 0 / 0 (+4 records) | 4 / 0 | 4 / 0 | 4 / 0 | 2 / 0 | 4 / 0 | - | - |
| ghost | 6 / 2 | 4 / 2 (+4 records) | 6 / 2 | 6 / 2 | 8 / 2 (+2 exports) | 2 / 2 | 6 / 2 | - | - |
| frame | 16 / 5 | 10 / 5 (+11 records) | 16 / 5 | 16 / 5 | 22 / 5 (+5 exports) | 7 / 5 | 16 / 5 | - | - |
| now | 1 / 0 | 1 / 0 | 1 / 0 | 1 / 0 | 1 / 0 | 1 / 0 | 1 / 0 | - | - |
| event | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | - | - |


## Micro (explains the pages)

ns per operation, the median of 5 fresh loads per arm (each load the median of 15 runs of 20,000, or 5,000 for mount, clone and frame): style_num, style with a one-off string, set_text, attr, the mount burst (open, attr, class, text, close), a template clone with two slots, one ghost's frame, one card-table frame (2 ghosts, a coin, the drag shadow), now, the event record's round trip; frame_rendered is one card-table frame with style and layout forced, ns, median of 60 frames (performance.now steps 5 us here).

Chrome 156:

results/micro_x1.jsonl: 34 of 35 loads kept; per arm a 5, list 5, b 4, c 5, d 5, e 5, f 5; load averages 1.73-2.43; Chrome/156.0.8078.74, CPU x1
  left out: b r0: busy 190.6%
| ns per op | a (n=5) | list (n=5) | b (n=4) | c (n=5) | d (n=5) | e (n=5) | f (n=5) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| style_num | 161.7 | 162.0 | 161.2 | 159.7 | 215.8 | 167.5 | 155.3 |
| style_tmp | 437.5 | 430.0 | 444.0 | 431.7 | 436.8 | 439.0 | 434.3 |
| set_text | 91.8 | 96.0 | 105.8 | 101.7 | 99.5 | 94.5 | 92.5 |
| attr | 100.2 | 100.8 | 118.0 | 111.0 | 110.0 | 104.8 | 99.8 |
| mount | 543.0 | 514.0 | 687.5 | 600.0 | 554.0 | 563.0 | 558.0 |
| clone | 918.0 | 869.0 | 1023.5 | 926.0 | 915.0 | 888.0 | 928.0 |
| ghost | 2265.8 | 2290.2 | 2290.0 | 3315.3 | 2379.0 | 1850.2 | 2254.5 |
| frame | 5388.0 | 5477.0 | 5517.0 | 7513.0 | 5828.0 | 4592.0 | 5388.0 |
| now | 118.5 | 121.2 | 125.5 | 120.2 | 117.8 | 118.0 | 60.5 |
| event | 791.0 | 789.0 | 787.5 | 783.0 | 784.0 | 792.0 | 786.0 |
| frame_rendered | 67499.9 | 65000.1 | 68749.9 | 62500.0 | 119999.9 | 64999.9 | 65000.1 |
| isolated | 1.0 | 1.0 | 1.0 | 1.0 | 1.0 | 1.0 | 1.0 |

Chrome 156 with `--js-flags=--wasm-fast-api` (arms a and f):

results/micro_x1_fastapi.jsonl: 10 of 10 loads kept; per arm a 5, f 5; load averages 1.57-2.12; Chrome/156.0.8078.74, CPU x1, --js-flags=--wasm-fast-api
| ns per op | a (n=5) | f (n=5) |
| --- | ---: | ---: |
| style_num | 157.2 | 158.2 |
| style_tmp | 429.5 | 431.0 |
| set_text | 93.0 | 92.5 |
| attr | 101.0 | 99.0 |
| mount | 558.0 | 542.0 |
| clone | 902.0 | 902.0 |
| ghost | 2323.8 | 2268.3 |
| frame | 5494.0 | 5436.0 |
| now | 120.0 | 49.0 |
| event | 796.0 | 804.0 |
| frame_rendered | 64999.8 | 69999.9 |
| isolated | 1.0 | 1.0 |

Firefox 159 (arm d not run: no CSS Typed OM):

results/micro_firefox.jsonl: 18 of 18 loads kept; per arm a 3, list 3, b 3, c 3, e 3, f 3; load averages 1.66-2.09; firefox/159.0, CPU x1
| ns per op | a (n=3) | list (n=3) | b (n=3) | c (n=3) | e (n=3) | f (n=3) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| style_num | 266.0 | 257.0 | 318.0 | 268.0 | 260.0 | 258.0 |
| style_tmp | 595.0 | 632.0 | 602.0 | 589.0 | 592.0 | 592.0 |
| set_text | 83.0 | 87.0 | 93.0 | 89.0 | 85.0 | 82.0 |
| attr | 140.0 | 141.0 | 162.0 | 145.0 | 143.0 | 140.0 |
| mount | 652.0 | 664.0 | 780.0 | 656.0 | 672.0 | 648.0 |
| clone | 868.0 | 872.0 | 932.0 | 852.0 | 892.0 | 852.0 |
| ghost | 2963.0 | 3052.0 | 3049.0 | 2976.0 | 2467.0 | 2965.0 |
| frame | 7164.0 | 7396.0 | 7416.0 | 7048.0 | 6200.0 | 7060.0 |
| now | 45.0 | 46.0 | 49.0 | 45.0 | 46.0 | 63.0 |
| event | 696.0 | 696.0 | 684.0 | 680.0 | 692.0 | 684.0 |
| frame_rendered | 70000.0 | 80000.0 | 80000.0 | 80000.0 | 80000.0 | 80000.0 |
| isolated | 1.0 | 1.0 | 1.0 | 1.0 | 1.0 | 1.0 |

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
Measured: `now` through an arrow import (arm a) 118.5 ns; through `Function.prototype.call.bind(Performance.prototype.now)` with the receiver from an externref table (arm f) 60.5 ns without the flag and 49.0 with `--wasm-fast-api` (a's stays 120.0 there).
So the bound import halves the call even on the generic path, and the Fast API path takes another 11 ns off; the port calls `cd_now` a few times per frame, so on the page arm f is a few hundred nanoseconds per frame, invisible in its windows above (0.84-1.14 of arm a, inside the noise).
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
Against the DOM as measured: the drag window (49 frames) costs 97.6 ms of main thread at x1 in arm a, about 2.0 ms a frame, of which about 1.26 is script (C, the input events and their DOM writes), 0.19 layout and 0.42 paint; one micro card-table frame of writes is 5.4 us and with its style and layout 65-70 us.
So the bridge's share of a frame is microseconds and the browser's rendering is the rest; a canvas would trade that rendering for its own draw and raster, which is where the real difference would be, and that is not measured here.

## Conclusion

On both pages, in Chrome 156 at x1, no bridge arm moves any window beyond the noise this bench shows between identical code (0.8-1.2 of arm a); the one page effect larger than that is a DOM change, not a bridge: moving the drag shadow and the ghosts by transform (e_t) cuts the drag window's layout to 0.45 of arm a at x1 (4.2 ms against 9.4 over 49 frames) and 0.63 at x4, and its total to 0.86 and 0.93.
The op list (cdom main's design) is the one arm that is slower beyond the noise somewhere: at x4 its drag, bot and drop windows run 1.18-1.38 of arm a in total, as the direct-calls experiment found for its one big apply (PERF_LOG.md).
The micro explains why the rest is invisible: an operation costs 90-440 ns, almost all of it the DOM work, and the arms move that by single nanoseconds to tens (b's generic dispatch +14 ns on set_text, +18 on attr, +145 on the mount burst; c's table +10 on set_text; d's natives about the same as a for strings and +54 ns for a number with a unit through Typed OM, whose frame with style and layout also costs 120 us against 67; and c's table, flat on single kinds, is +46% on the ghost frame, where its two shapes alternate at one call site); while the port's busiest flow, the transfer, makes 517 imports and the drag 209 (count tables), so even b's worst case is tens of microseconds per flow against windows of 50-100 ms.
Fusion (e) is the one bridge change the micro sees clearly: one ghost frame 1,850 ns against 2,266 (-18%) and a card-table frame 4,592 against 5,388 (-15%), from 6 crossings to 2 and 16 to 7, with the same DOM work; on the keyed page it cuts the crossings of create 1k from 5,004 to 3,004 and of clear 10k from 10,001 to 11, and neither page's windows move beyond the noise.
Fast API: no DOM function the bridge calls has one in Chrome 156; `performance.now` does, and the bound import is the faster `now` with or without the flag, a few hundred nanoseconds a frame.
In Firefox 159 the micro ranks the arms the same way: the generic dispatcher slowest on mount (780 ns against 652), fusion fastest on the ghost frame (2,467 against 2,963).

## What was cut for the 15-minute rule

- Run: the port at x1 7 rounds and at x4 5 rounds, the keyed page at x1 7 rounds, the micro 5 rounds, the Fast API micro 5 rounds, Firefox 3 rounds; no command ran over its 780 s budget, nothing was cut by it.
- Left out of the medians: 9 of 213 loads, every one busy (other processes over 150% of a core as the load ended: mostly corespotlightd, the Spotlight indexer; once PerfPowerServices, once airportd and secd together); no load failed a window's check.
- Not run: the keyed page at x4, Safari (headed only), arm d in Firefox (no Typed OM), and the canvas route (estimated only).
- The x4 port has 4 loads for arms a, c and f (one busy each), so its medians are the least certain here.
