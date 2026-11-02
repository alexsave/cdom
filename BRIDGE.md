# Why cdom calls the DOM with one import per operation

cdom's bridge between C and the DOM is one wasm import per DOM operation (`style_num`, `set_text`, `attr`, `clone`, ...), each a small JS function that makes one DOM call.
On 2026-11-01 we measured every alternative we could think of against it, on real pages, and none was faster beyond the noise.
This file says what was tried, what each did, and why the plain design is the fastest one available to a web page today.

The benchmark code is not on any branch; it is kept at the tag `bridge-bench-2026-11-01` (commit 7b18697, `micro/bridge/`), with its full tables in `micro/bridge/RESULTS.md` and raw rows in `micro/bridge/results/` there.
It was cdom PR #3, closed without merging the code.

## The short answer

A DOM operation costs 90-440 ns, and almost all of that is the browser's own work: building the style change, the node, the attribute.
The crossing from wasm into JS that cdom pays per operation is a few nanoseconds of it (4.65 ns measured earlier in RESULTS.md, "Step 0").
A game action spends its time in style recalculation, layout and paint, which no bridge touches.
So every bridge design moves a page's time by a few percent at most, which is inside the run-to-run noise.
The one-import-per-operation design also has properties the others lack: each JS function is monomorphic, so V8 compiles it with one cached property lookup and a direct call into Blink's binding; nothing is held between C and the DOM, so reads (`cd_rect`, `cd_hit`) never wait on a flush; and it has no tables, mirrors or queues to keep correct.

## What was tried

Each alternative was a generated variant of the shipped `src/cdom.c` and `shim/cdom.js`; only the import layer changed, so every app call site was the same.

| Arm | Design | Verdict |
| --- | --- | --- |
| a | Today's: one import per operation | The baseline, and the fastest overall |
| list | The old op list: C writes 32-byte records, one import applies a batch | Even at full speed, the only arm clearly slower at 4x CPU throttle (1.18-1.38 of a in drag, bot and drop) |
| b | One generic dispatcher, `bridge(name_id, args)`, over pre-resolved entries | +14-18 ns per write, +145 ns per mount burst; within noise on the page |
| c | A table of pre-resolved functions, `fns[id].call(node, ...)` | Flat on single operations, 46% slower on the ghost frame (one call site, several shapes, so V8 cannot specialise it) |
| d | Imports bound at load time to the browser's own methods, `Function.prototype.call.bind(...)` | Number writes need CSS Typed OM (+54 ns per write, a rendered frame 120 us vs 67), and Typed OM refuses some properties (`rotate`), so it cannot replace every write |
| e | Fused operations ("superinstructions"): one call for several style writes on a node, batched slot texts, moves as one fragment | The only bridge change the micro sees: 15-18% off a frame's writes; invisible on the page |
| e_t | e, moving the drag ghost and shadow by `transform` instead of `left`/`top` | The one real page effect, and it is not a bridge: drag layout 0.45x of a at normal speed, 0.63x at 4x throttle |
| e_css | e, writing the ghost's whole style attribute as one string per frame | Within noise |
| f | `performance.now` bound to the browser's method, with and without V8's wasm Fast API path | Halves that one call (118.5 to 60.5 ns, 49 with the flag): a few hundred ns per frame |

Fusion cuts the number of crossings a lot (js-framework-benchmark create 1k: 5,004 to 3,004 imports; clear 10k: 10,001 to 11) without moving the time, which is the clearest evidence that crossings are not where the time goes.

## Why the plain design wins, step by step

1. **The crossing is not the cost.** An import call is a few nanoseconds; the DOM call behind it is tens to hundreds. Removing crossings (list, e) removes the cheap part.
2. **Batching delays the work but does not shrink it.** The op list still makes every DOM call, now from one big loop, and it must apply everything before any read; under CPU pressure that one long apply is what made it slower.
3. **A generic call site is a slow call site.** V8 caches a property lookup per call site. One function serving every method name (b) or every method (c) sees many shapes at one site, so it falls back to slower lookups; one function per operation (a) never does.
4. **Binding the browser's methods directly (d) skips the JS glue, but the glue is cheap and well optimised.** A monomorphic JS function compiles to a cached lookup and a direct call into Blink's binding; a bound native called from wasm goes through V8's generic call path instead, and number-with-unit writes need Typed OM objects.
5. **There is no way to call Blink's C++ directly for DOM work.** V8's Fast API lets wasm call a binding's C function directly, but in Chrome 156 no DOM method cdom uses carries Blink's `[NoAllocDirectCall]` attribute (setAttribute, setProperty, nodeValue, textContent, createElement, cloneNode, insertBefore, getBoundingClientRect: all checked against the IDL), because they take strings and nodes and run custom-element reactions. The marked methods are mostly WebGL and canvas, plus `performance.now`, and V8 takes the wasm-to-Fast-API path only behind the off-by-default `--wasm-fast-api` flag.

## What does make cdom faster

Doing less browser work, not cheaper calls:
- Move things with `transform` (compositor-only) instead of `left`/`top` (layout every frame): the drag result above.
- Build from templates and clone them (cdom already does), so a mount is one clone and a few slot writes instead of many createElement calls.
- Fuse several style writes on one node only where it removes DOM calls or string decodes, not to save crossings.
- The one large jump left is to stop having DOM for the table and draw it in one canvas with WebGL or WebGPU, where a frame is a few draw calls; it would give up text layout, accessibility, compositor transitions, hit testing and the shared stylesheet, so it is a different product decision, estimated in RESULTS.md and not built.

## How it was measured

- Headless Chrome 156 over CDP, every load in a fresh target, arms in a shuffled order each round, page randomness fixed so every arm saw the same deal.
- Primary: the foolish card-table port's real flows (deal, select, play, bot step, a 49-frame drag, drop, a transfer), traced per window: script, style, layout, paint, frames, long tasks; at normal speed (7 rounds) and 4x CPU throttle (5 rounds).
- Second page: cdom's js-framework-benchmark keyed entry, its standard operations as real clicks (7 rounds).
- Micro: 20,000 operations of a kind per run, median of 15 runs after warm-up, to explain the pages; Firefox 159 ranked the arms the same way.
- Correctness: every arm had to produce arm a's DOM, rects, hits, events and input reads; three deliberately broken arms were each caught. Every timed load was checked against the reference DOM; none failed.
- The noise floor came from the data itself: two arms run arm a's exact code in most windows and still landed at 0.85-1.39 of it, so any ratio between about 0.8 and 1.2 is noise.
- 9 of 213 loads were dropped because another process (mostly Spotlight) was busy; no command ran longer than 13 minutes.
