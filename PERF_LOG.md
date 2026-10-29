# cdom perf log (track 2: the plain-JS floor)

## API changes for track 1

- **The op stream is gone (#4).** Each DOM operation is now its own wasm import (module "cdom": at, open, close, text, text_num, attr, class, name, listen, value, value_num, tpl_save, clone, slot, tpl_text, tpl_text_num, walk, walk_value, walk_value_num, set_text, set_text_num, set_attr, set_class, remove, free, move, clear, detach, attach), called the moment C emits the op.
  Only strings are batched (import "strings"), and interned strings are queued when interned.
  The input-value import is renamed from "value" to "input". cd_layout() now returns 3 words: string bytes, string meta, inbox.
  cdom.h is unchanged; cd_flush() now sends pending strings and any held walk. Walks (cd_sel/cd_down/cd_next/cd_up) are held in C and cross as one call.

- `cdom.start(url, mount, env)`: `mount` may now be an element id (a string) as well as an element.
  With an id, the wasm is fetched, compiled and instantiated at once, and the app starts when the DOM is ready.
  Passing an element works as before.
- The repo root now has a package.json with one devDependency (terser 5.44.0); run `npm install` once at the root before bench/build.sh, which minifies the shim before inlining it.
  terser is a build-time minifier only: nothing from node_modules is shipped or loaded at run time, the shipped page carries only the minified output of shim/cdom.js, and shim/cdom.js stays the readable source.
- bench/build.sh builds -Oz by default; the cdom-oz variant became cdom-o2.
- New in cdom.h (additive): template slots. `cd_slot(k)` marks the last built text node as slot k of the template being built; `cd_tpl_text(t, k, s)` / `cd_tpl_text_num(t, k, n)` set it in the template itself before `cd_clone(t)`, so the clone needs no walk. Unchanged values are not re-sent. A slot keeps its last value: set every slot before each clone.
- The benchmark page inlines the shim (bench/build.sh replaces `/*CDOM_SHIM*/` in index.html); there is no separate cdom.js in the entry any more.

## How things are measured

- Paired, load-gated harness runs: tools/pair.sh (vanillajs and the cdom entries in the same run, results in results/pairs/<tag>), tools/ratio.mjs (ratio to vanillajs per benchmark, weighted geometric mean).
- First paint and bytes: tools/fpruns.sh, the harness's own 40_ size run, R rounds interleaved.
- Gates: tools/gate.sh (harness keyed check, smoke, DOM identity against the frozen baseline entry cdomb, listfuzz all seeds).
- Ratios are to vanillajs in the same run; loads are the 1-minute load at the start of each run.

## Baseline (results/pairs/base, loads 2.34-3.99)

Weighted geomean against vanillajs: cdom 1.005 and 1.028 (two identical builds, so about 2% is noise), react-hooks 1.518.
First paint (11 rounds, loads 3.44-3.93): vanillajs 50.3 ms, cdom 90.5 ms (ratio 1.80). Compressed bytes 2.5 KB against 7.7 KB.

## Accepted changes

| # | change | operation | old ratio | new ratio | cause |
| --- | --- | --- | ---: | ---: | --- |
| 1 | inline the shim, start the wasm from <head> before the stylesheet, mount on DOMContentLoaded | first paint | 1.80 | 1.45 | the external script waited for the stylesheets (and their @import) before it could even start fetching the wasm; now compile and instantiate overlap the CSS |
| 2 | wasm at -Oz (20,452 -> 9,561 bytes) and the inlined shim minified with terser (index.html 6.8 KB -> 3.7 KB) | first paint | 1.45 | 0.98 | the harness server brotli-compresses every response synchronously at quality 11 (about 1.4 ms per KB raw), and the CSS waits behind it; compressed total 7.6 KB -> 5.5 KB. -Oz costs about 0.1 ms of script on 1k-row creation (fresh.mjs), under 0.5% of those totals; the paired CPU runs (results/pairs/oz, oz2) moved within noise apart from load spikes visible as 22-26 ms Layout outliers in single iterations |
| - | noise check of #2 (results/pairs/noise1, every start at load < 3, range 1.98-2.99) | all CPU | parent 1.039 | 1.014 | no regression: -O2+minified alone gave 1.027; every per-op difference is inside the parent's own spread |
| 3 | template slots: text set in the template, then one clone; no walk of the clone | create (run1k, replace, create10k, append) | create10k 1.023, run1k 1.070 | create10k 1.004, run1k 1.060 | vanilla does clone + firstChild x4 + nextSibling + 2 nodeValue + appendChild (9 DOM calls per row); cdom did the same plus a parentNode (10); cdom now does 2 nodeValue on the template + cloneNode + insertBefore (4). Script, harness medians: create10k 26.3 -> 21.0 ms (vanilla 20.8), run1k 3.2 -> 2.8, append 3.2 -> 2.8 (x2 throttle). results/pairs/slots, loads 1.15-2.50; geomean 1.037 -> 1.033 |
| - | final check of #3 (results/pairs/final1, final2; loads 1.76-2.98) | all CPU | first build 1.038 / 1.029 | 1.020 / 1.018 | react-hooks 1.477 / 1.464 in the same passes |
| rejected | freed ids coalesced into runs (one FREE op + Array.fill per run instead of one op per id) | clear | 1.048 | 1.040 | script 10.3 -> 10.1 ms at x8 throttle, inside noise; not worth the extra C state. The run code had a wraparound bug the new test found (a reset index of 0xffffffff matched nops == 0). The two-list adjacent-id test stays in listfuzz: it checks that freeing one list's ids never drops a live neighbour's reference |
| 4 | direct calls: every op is its own import, called at once; only strings are batched (queued at intern time); walks cross as one call | all CPU | 1.045 / 1.035 (op list, same passes) | 1.009 / 1.001 | the direct-call experiment below; results/pairs/dir3, dir4 (loads 1.83-2.96). Script vs vanilla, dir4: run1k 2.4 vs 2.3 (list 2.7), create10k 19.1 vs 20.8 (list 21.6), clear 9.6 vs 9.4 (list 10.4). First paint 1.005 (list 1.029), compressed 5.8 KB |

## Experiment: direct calls against the op list (branch direct-calls, worktree ~/Dev/cdom-direct)

Question: instead of C writing an op stream that the shim interprets, does C calling DOM functions directly through wasm imports win?

Arms, all with template slots, interned strings, -Oz and the inlined minified shim; only the emit layer differs:
- all-list: the op stream (shim 136 lines).
- (a) all-direct: one arrow import per op, generated from apply()'s cases (exp/gen_direct.py), sharing the shim's node and string tables; a direct op flushes pending list ops first.
- (b) direct-native: DOM methods imported as `Function.prototype.call.bind(method)`, with nodes, strings, templates and walk state in externref tables (`__externref_t`, `__builtin_wasm_table_*`, `-mreference-types`; clang 22 expressed it without trouble; shim 92 lines).
- hybrids: 12 balanced random-half configurations over the 16 ops the benchmark executes (each op direct in 6 of 12), the fit's best assignment, and "all direct except the update walk and CLEAR".

Micro (micro/direct, ns per call from a wasm loop, Chrome 156, quiet machine): nodeValue arrow 41, native-bound 48.5, op list 39.5; firstChild arrow 15-18, native 18, op list 9.5; cloneNode and insertBefore 570-1000 for all three.
V8 does not fast-path natively bound DOM methods called from wasm here: the bound setter is slower than an arrow. A crossing costs about 5 ns more than an op-list record.

Harness, weighted geomean vs vanillajs, three load-gated sessions (screen, val1, val2; loads 1.55-2.99):
| arm | screen | val1 | val2 |
| --- | ---: | ---: | ---: |
| all-list | 1.034 | 1.039 | 1.031 |
| (a) all-direct | 1.010 | 1.011 | 0.999 |
| (b) direct-native | 1.005 | 1.022 | 1.022 |
| best hybrid from the fit (CLOSE, LISTEN, TPL_TEXT, FREE) | - | 1.020 | 1.008 |
| hybrid: all direct but SEL/DOWN/NEXT/VALUE/CLEAR | - | 1.004 | 1.001 |

Fitted per-op deltas (direct minus list, ms of harness script, standard error; exp/fit.py over 210 iterations per benchmark): FREE -0.22 +-0.03 (replace) and -0.33 +-0.06 (clear); LISTEN -0.09 +-0.03 (run1k); CLOSE -0.46 +-0.12 and TPL_TEXT -0.65 +-0.18 (create10k); list better: CLEAR +0.15 +-0.05, DOWN +0.28 +-0.06, NEXT +0.12 +-0.05 (update), CLOSE +0.03 +-0.015 (append). The rest are inside 2 SE.
Flushes forced per click: random halves 1,000-2,000 per 1k rows (every switch between modes); all-direct 125-720, all from first-time strings; the update walk 100-300 crossings direct.
Why direct wins although each crossing costs more: in the harness's measured click the list arm takes incremental-GC marking steps and V8 interrupt handling (about 0.3 ms on run1k) that the direct arm does not, while synchronous script time is about equal; the one big apply() after five warm-up clicks is the likely difference. Not proven further.
The hybrids do not beat all-direct beyond noise. What the hybrid taught was where direct loses: the update walk (one crossing per step) and strings decoded one at a time. The shipped design fixes both inside the direct model: a walk crosses as one call, and strings are queued at intern time so a burst decodes in one call. That made update 1.03 / 1.01 instead of 1.05-1.06.
Every configuration passed the keyed check, smoke, DOM identity and listfuzz built with its own mask. One DOM-identity failure on the fit-best hybrid did not reproduce in 11 reruns; its log had been overwritten, so gate logs are now per entry.

## Final (results/pairs/last1, last2; every start at load < 3, range 1.82-2.99)

Weighted geomean vs vanillajs: first build (cdomb) 1.036 / 1.033, last build 1.007 / 0.998, react-hooks 1.466 / 1.457.
First paint 1.005 vs vanillajs (first build 1.80); compressed 5.8 KB (first 7.7, vanillajs 2.5).
Stop: every operation is within noise of vanillajs, and create10k is below it in all four passes since #4 (0.987-0.993; script 19.0-19.2 ms against 20.7-20.9).

DOM calls per operation (tools/domcount.mjs, one measured click after the harness's warm-ups):
| operation | vanillajs | cdom first | cdom last | floor |
| --- | ---: | ---: | ---: | --- |
| run1k, replace1k, append (per row) | 9 | 10 | 4 | 4: clone, 2 text writes, insert |
| create10k | 90,008 | 100,004 | 40,002 | 40,000 |
| update (100 rows) | 403 | 500 | 500 | 100 text writes, plus reaching the nodes (400 without cached references) |
| select | 12 | 7 | 7 | 2 className writes |
| swap | 8 | 4 | 4 | 2 insertBefore |
| remove | 17 | 9 | 9 | 1 remove |
| clear | 6 | 1 | 1 | 1 textContent |
cdom's select and remove counts include the event path the shim computes (which cell was hit), and swap's the parentNode of each moved row.

What is irreducible: rendering (style, layout, paint) is over 90% of every total and identical for identical DOM, so totals can move only by script; fragment appends and three clear strategies changed nothing measurable.
cdom's remaining script over the floor is the keyed library's own work: the model and keyed diff in wasm (about 0.15 ms per 1k rows), one reference and one expando per row so rows can be moved, removed and hit by events, and the event path.

## Port fixes (branch port-fixes, worktree ~/Dev/cdom-fixes)

What the foolish table port (foolish branch cdom-port, c/cdom_poc) found missing, folded back.
Every item has a test in tests/unit.c (the unit page, tools/unit.mjs), tests/listfuzz.c or tests/link, and tools/mutate.py breaks each one and checks the test fails for its own reason.

### API (all additive except where noted)

- A template cloned inside an open element is that element's child (the shim's clone never attached it).
- SVG: an `<svg>` and everything opened inside it is created in the SVG namespace; C keeps the depth (the interned id of "svg" is recorded when interned, so the check is an id compare) and calls `open_svg`.
- `cd_name_open(id)` names the innermost open element; `cd_name` keeps its meaning (the last node made) and its comment now says what that is after a close.
- `cd_set_style(id, prop, value)` and `cd_set_style_num(id, prop, v, unit)`: one `style.setProperty`, prop interned, value a string (transient or interned) or a double plus a unit enum.
- `cd_tmp(s, len)` / `cd_tmpz`: a one-off string, sent with the next op and never interned; using it after another op crossed traps.
- `CD_LIT("lit")`: a literal interned once per call site, through the helper `cd_lit_`; `"" lit` makes a variable a compile error.
- `cd_fmt(dst, v, decimals, unit)` and `cd_fmt_len(dst, double, unit)`: numbers as JS's `String(v / 10 ** d)` writes them, no printf.
- `cd_str_count()`, `cd_str_bytes()`, `cd_ids_live()`, `cd_ids_high()`, `cd_pending()`: counters for tests.
- Regions: `cd_region_show(&r, want)` / `cd_region_end` / `cd_region_hide`, a child or a set of children that comes and goes; every id made while it builds is freed when it goes (parent mode: one clear; root mode: one remove).
- `cd_list.end`: a list followed by fixed siblings inserts its tail before the first of them. `cd_list_forget(L, f)`: the list's DOM already went, free its ids.
- Freeing a free id traps (an `alive` byte per id).
- Time: `cd_timer_start(ms, token)` -> `cd_on_timer(token)`, `cd_raf_request(token)` -> `cd_on_raf(token, t)`, `cd_now()`. Hooks have empty weak defaults.
- Window: `cd_listen_window(CD_EV_RESIZE | CD_EV_VISIBILITY)` delivers through `cd_on_event` (x, y the viewport, key = hidden); `cd_viewport(&v)` reads it now.
- Images, in two steps (replacing the first pass's `cd_set_image` and `CD_EV_LOAD`): `cd_encode_image(rgba, w, h, mime, token)` encodes pixels in C memory (canvas, toBlob) and hands the file's bytes back to `cd_on_encoded(token, p, len)`; `cd_set_image_bytes(id, prop, p, len, mime)` puts an image file's bytes on a style property as a `url(blob:)`, now. Two steps so an app can keep the encoded file (the store, below) and set it again on a later load without encoding or decoding in C.
- Store: src/cdom_store.c and cdom_store.h (link it to use it): `cd_store_put(token, key, klen, p, len)` answered by `cd_on_store_put(token, ok)`, `cd_store_del(token, key, klen)` answered by `cd_on_store_del(token, ok)` (network pass), and `cd_store_get(token, key, klen, deadline_ms)` answered by `cd_on_store(token, p, len)`, len -1 for a miss, an error, or no answer by the deadline (`CD_STORE_DEADLINE_MS` 1500: iOS Safari's indexedDB.open can hang). IndexedDB database "cdom", object store "kv", fixed in the shim; the version lives in the key the app chooses. Bytes in, bytes out, nothing parsed.
- Bytes into C: one room for every incoming byte string, `cd_bytes_buf(n)` in cdom.c (the encoded image, the stored value, and the network's messages, whose own `cd_net_buf` is gone).
- Network: src/cdom_net.c and cdom_net.h (link it to use it): `cd_ws_open/send/close`, `cd_http`, answered by `cd_on_ws_open/msg/close`, `cd_on_http`; bytes go straight into a buffer cdom owns.
- Network, fit for live play (network pass; changed, not additive): `cd_http(token, method, mlen, url, ulen, headers, hlen, body, blen, deadline_ms)` sends request headers, one block of `Name: value\n` lines in C memory (one string across, built with a memcpy or two, rather than name/value pairs that would cross as two pointers each), and aborts at the deadline; the answer's status is the response's, `CD_HTTP_FAILED` (0) when it never arrived, `CD_HTTP_TIMEOUT` (1) when the deadline passed first. Response headers are not read: a c_base client reads the status and the body only, and the headers would need a format of their own across. `cd_ws_buffered(id)` reads bufferedAmount. A closed socket's id is free before C hears the close, and the next `cd_ws_open` takes it: the table holds the sockets open at once. `ws_send` sends a view of C memory (send copies; the `.slice()` is gone). `cd_epoch_ms()` is Date.now(), for a server's expiry times (netclient.h takes epoch ms). The store reads a put's or a get's key during the call (it was read after the database opened, so a key in a buffer C reused was read late).
- Auto-flush: every export (cd_start, cd_event, cd_timer, cd_raf, the four network answers) flushes after the app's hook. Apps no longer call `cd_flush`.
- Memory: `cd_alloc`, `cd_realloc`, `cd_free` go through `malloc`, `realloc`, `free`; cdom defines all four (and calloc) as weak symbols over its own heap. An app with its own allocator defines them, strong, and cdom allocates from it; cdom's free and realloc trap on a pointer their heap did not hand out. One heap whoever brings it.
- src/mini.c's memcmp and strlen are weak: an app's own libc links next to it.
- Not changed: template variants (the port's "10" face and thin card). A per-clone attribute slot would save nothing: the clone's root style is one setAttribute call either way, the "10" variant differs in an inner element's style (a slot would have to remove an attribute to match the plain face's DOM), and the thin card is another structure. Separate templates cost one build each at start.

### SVG: C tracks the namespace, measured (micro/svg)

40,000 opens (20,000 rows of div > span > text, built and cleared), median of 21 fresh loads: namespace in C (an id compare, `open_svg` import) 11.7 ms; the port's JS check per open (tag compare and the parent's `namespaceURI`) 12.0 ms. About 7 ns per open; C kept.

### SHIM_LINES: the shim's size, and what each import is for

The shim is written to be read (real names, one statement per line, a comment per function) and the page ships it minified (terser, `-c passes=2 -m`), so its size is measured minified, not in source lines.
| shim/cdom.js | minified | brotli (q11) | gzip (-9) |
| --- | ---: | ---: | ---: |
| golfed, before the rewrite (207 source lines) | 6,553 | 2,615 | 2,951 |
| readable (497 source lines) | 6,678 | 2,632 | 2,966 |
| `start`'s `openStore` option | 6,740 | 2,652 | 2,986 |
| shared helpers (`bytes`, `copyBytes`, `promised`, `place`), store_put answers | 6,623 | 2,658 | 2,987 |
| network pass: request headers, deadline, `ws_buffered`, socket ids reused, `epoch`, `store_del`, keys read during the call | 7,221 | 2,861 | 3,219 |
| `rect` (the foolish port's flights) | 7,351 | 2,919 | 3,271 |
| drag and keys (the foolish port's passes 4 and 5): seven mouse and touch types, the event's character, the field bit, `cd_prevent_default`, window listeners removed again, `hit` (same terser run: 7,352 / 2,921 / 3,274 before) | 8,112 | 3,211 | 3,592 |
| the event's own time in the record (`ms`, event.timeStamp) | 8,147 | 3,228 | 3,609 |
| `image_bytes` sets its url once the picture has decoded and answers `cd_image(token, ok)` (same terser run: 8,146 / 3,228 / 3,618 before) | 8,260 | 3,274 | 3,671 |
| `hit` reads every layer under the point (elementsFromPoint), as the React table's drop does | 8,258 | 3,276 | 3,678 |
The rewrite costs 125 bytes minified and 17 brotli: separate `const` and `let` declarations, the `if`/`else` blocks terser keeps as statements, and small named functions (`now`, `utf8`, `viewportRecord`) where arrows were inlined.

The imports the port-fixes branch added or changed, and what each is for:
- `open_svg`: an element in the SVG namespace (C keeps the depth, measured against a check per open in micro/svg).
- `clone` (changed): appends to the open element as `open` does (through `push`); the missing append was the bug.
- `name_open`: names the open element rather than the last node.
- `style`, `style_num`: one style property (`setProperty`), a string or a number with a unit.
- `encode`: pixels in C memory (RGBA, or RGB widened to RGBA so an opaque picture takes a quarter less room in C), placed at (x, y) in an image of a given size, encoded off the call (toBlob), the file's bytes delivered back into C. The placement is for a picture of which only a part is ever made but whose full size the page lays out by: the foolish port's wool is woven only where it shows and placed in the app's 3840 x 2160, so the browser sizes, snaps and resamples it as it does the app's (a crop given its own size landed 0.67 px off after snapping, and every pixel differed).
- `image_bytes`: an image file's bytes from C memory as a `url(blob:)` on a style property, set once the picture has decoded, then answered with `cd_image(token, ok)` (`cd_on_image` in C; 0 and nothing set for bytes that do not decode).
  It was set at once: the foolish port's card backs turned black (the colour under the fern) a frame before the fern itself painted, one frame of black on every back at every load, cold or warm (the browser loads a blob url's picture off the call).
  Set after the decode, the picture is ready when the property lands, and the answer lets the app set what goes with it (the black under the fern) in the same task, so both paint in one frame.
  Unit case "cd_set_image_bytes sets the url once the picture has decoded, answers 1 with what goes with it, and 0 and nothing for bytes that are no image" (a MutationObserver sees the url and the mark the answer sets always together); mutants image-set-early (the url set at the call, as before) and image-unanswered, both caught (114 bytes minified, 46 brotli).
- `win`, `viewport`: window events (resize, visibilitychange) through the event record, and the viewport read now: width, height, hidden and `devicePixelRatio` times 100 (so an app can size a picture for the screen's device pixels; the port's fern).
- `timer`, `raf`, `now`: setTimeout, requestAnimationFrame and performance.now for C's hooks.
- `ws_open`, `ws_send`, `ws_close`, `http`: the network (src/cdom_net.c), bytes in and out of C memory.
- Network pass (598 bytes minified, 203 brotli in all, with `store_del`): `http` (changed) takes a block of request header lines (`headerList` splits it) and a deadline (an AbortController and a timer; the answer tells an abort, 1, from a failure, 0: unit cases "cd_http sends request headers", "a request past its deadline", "a request that never arrives"; mutants http-no-headers, http-status, http-no-deadline, http-deadline-dropped, http-abort-is-failure, http-failure-is-abort); `ws_buffered`, bufferedAmount (case "cd_ws_buffered", mutant ws-buffered-zero); `ws_open` (changed) reuses the ids of closed sockets (`freeSockets`; case "sockets opened and closed 40 times reuse their id", mutant ws-ids-grow); `ws_send` (changed) sends a view, not a copy (the echo case now writes over the message as the send returns; mutant ws-send-late); `epoch`, Date.now() (case "cd_epoch_ms is Date.now", mutant epoch-page-clock).
- `store_del`: delete a key and answer `cd_on_store_del(token, ok)` (cases "the key misses after a delete" and "a delete that fails answers 0", mutants store-del-noop, store-del-unreported); `store_put` and `store_get` (changed) read the key during the call (case "the key is read during the call", mutant store-key-late).
- `rect`: a node's box on screen (getBoundingClientRect, transforms included) written into C memory as four doubles, left, top, width and height (130 bytes minified, 58 brotli; measured against the network pass's 7,221 and 2,861 in the same terser run, gzip 3,211 -> 3,271).
  The foolish port's card flights need it: a flight leaves from the centre of the card it replaces and lands on the centre of a battle cell the browser's flex row placed, which C cannot know without measuring; the row's glide (a FLIP) reads each cell before and after a change.
  The port measures once per step or row change, never per frame, as the React table does (flightGeometry.ts planFlights in a layout effect).
  Unit case "cd_rect: the box the styles give, then the transformed box, read in one entry" (a fixed box, then `scale(2)` set in the same entry with no flush between: 10 20 30 40, then -5 0 60 80); mutants rect-order (left and top swapped) and rect-untransformed (offsetLeft and friends, which ignore the transform), both caught.
- Drag and keys (the foolish port's passes 4 and 5: drag and drop by mouse and touch, keyboard play; 760 bytes minified, 290 brotli, 318 gzip in all):
  - `EVENTS` gains `mousedown`, `mousemove`, `mouseup`, `touchstart`, `touchmove`, `touchend` and `touchcancel` (CD_EV_MOUSEDOWN to CD_EV_TOUCHCANCEL, 14 to 20), so the per-node listen mask is a `Uint32Array` (a `Uint16Array` held 16 types). The React table drags with mouse and touch events, not pointer events: a touch pointer is captured by the element it pressed, and a pan the browser starts cancels it, where a document touchmove that is not passive keeps the drag and stops the scroll.
  - `post` (changed) reads a touch event's point from its first changed touch (`pointOf`), writes the key's character (`ch`, one UTF-16 unit, 0 for a named key; keyCode alone cannot tell "-" from "=" across layouts), a mods bit for a key typed into a field (`editing`: input, textarea, select, contenteditable), and, after `cd_event`, calls `preventDefault` when C asked (`cd_prevent_default`): no native drag after a mousedown on a draggable card, no scroll after a touchmove, no mouse events or click after a touchend, nothing typed after a handled key.
  - `win` (changed) listens once however often it is asked, delivers every type but resize and visibilitychange as the event itself (id 0), and is not passive; `unwin` removes it (a drag's moves are listened for only while it lasts, as the React table adds and removes its document listeners).
  - `post` writes the event's own time (`ms`: event.timeStamp rounded, on cd_now()'s clock): the React table times a tap by the press's and the release's stamps, because a handler held up behind a render read a 40 ms tap as a long press off the wall clock (DragContext.tsx endCardDrag). Unit case: the same mousedown case, the event made 30 ms before it is dispatched, its time within 1 ms of the event's stamp; mutant input-no-time (the time the handler ran), caught.
  - `hit(x, y)` (now: the first listening node among `elementsFromPoint`, topmost first, each element's ancestors after it, so a layer that takes pointer events but listens nowhere is seen through as React's drop reads through a seat or a flight; unit case "cd_hit: through a layer that takes pointer events but listens nowhere", mutants hit-topmost-only and hit-bottom-first, caught; it was) the nearest listening ancestor-or-self of `elementFromPoint`, 0 for none: a drop's target under the finger (the React table's `elementsFromPoint` hit test), seen through overlays that take no pointer events (the drag shadow, the flights). A freed id's node no longer answers (its mask is cleared).
  - Unit cases: "a mousedown and a touchstart are delegated with their point, and C can stop the default", "the document's moves while C listens on it, once however often asked, and none after", "a touchmove on the document is not passive: C stops the page scrolling", "a keydown on the document: its character, its modifiers, and whether it was typed into a field", "cd_hit: the listening node under a point, through an overlay that takes no pointer events, and not a freed one", and in tools/unit.mjs "a real ArrowLeft: keyCode 37, no character, the default stopped" (CDP's Input.dispatchKeyEvent).
  - Mutants, all caught: input-no-time, input-no-prevent, input-prevent-c, input-touch-point, input-mask-16, input-win-viewport, input-unwin-noop, input-win-twice, input-touchmove-passive, input-no-char, input-no-editing, hit-no-walk, hit-not-listening.
- `start(url, mount, env, options)`: `options.openStore` opens the store's database at once, while the wasm is fetched and compiled, for an app that reads its store as it starts (the foolish port's textures; unit case "options.openStore opens the store before the module starts", mutant store-not-early).
- `store_put`, `store_get`: the key/value store over IndexedDB (src/cdom_store.c): put a copy of the bytes and answer `cd_on_store_put(token, ok)` (1 kept, 0 failed, so a saved game's failed put is not silent; unit case "a put answers 1 when kept and 0 when it failed", mutant store-put-unreported); get answers once, with the bytes or a miss (-1), and the deadline's timer answers a miss.
- Not imports, shared by them: `post` (the event record writer, used by delegated, window and the old image events), `push` (open, open_svg and clone), `deliver` (bytes into C memory through `cd_bytes_buf`: the encoded image, a network message, a stored value), `utf8`.
- `EVENTS` gained "resize" and "visibilitychange" (a "load" for images came and went with the first image import).

### Gates and the paired runs

Every gate passes on the bench entry built from this branch (cdomf): the harness's keyed check, smoke, DOM identity against perf 0d0bdd7 (entry cdomp0), listfuzz (5 seeds, 40,000 rounds each: the owning list and the list with an end anchor), the unit page (18 cases) and the link tests.
tools/mutate.py: 37 mutants, every one caught by the test written for it (48 after the store with its answered put and early open, the two-step image with RGB and placement, and the device pixel ratio: all caught; the unit page 26 cases; 61 and 35 after the network pass; 63 and 36 with `rect`).

Paired, load-gated runs (tools/pair.sh, LOADMAX=3), vanillajs, cdomp0 (perf 0d0bdd7) and cdomf, CPU benchmarks 01-09; tools/ci.mjs bootstraps a 95% interval of the weighted geomean of cdomf over cdomp0.
| pass | cdomp0 vs vanillajs | cdomf vs vanillajs | cdomf / cdomp0 (95% interval) |
| --- | ---: | ---: | --- |
| fix1 | 1.000 | 1.023 | 1.023 (1.004-1.046) |
| fix1 without 04-06 | | | 1.004 (0.982-1.031) |
| fix2 | 0.999 | 1.002 | 1.003 (0.997-1.023) |
fix1's 04_select1k (6.2 ms against 4.4) ran while Spotlight was reindexing (load 2.50 before, 4.85 after); 05 and 06 in that pass also ended above 3.5. fix2 is clean of that and within noise; other processes on the machine still spiked some runs mid-run (load_after up to 6.3, in loads.tsv), equally across the three entries.
One thing to watch: select1k's script median is 0.4 ms for cdomf in both passes against 0.2-0.3 for cdomp0 (the harness reports to 0.1 ms; the click now goes through post() and cd_event's flush).

Bytes (bench entry, -Oz): app.wasm 9,031 -> 9,561 (brotli 4,312 -> 4,569); index.html with the minified shim inlined 4,003 -> 5,823 (brotli 1,662 -> 2,346). Shim source 138 -> 184 lines.
The wasm growth is the exports every app now carries (cd_timer, cd_raf), the weak allocator entry points, the alive byte per id, the region recording in cd_id_new, the svg depth and the transient check; the network is in cdom_net.c and costs an app nothing unless linked.
First paint (40_) was not rerun: the page is 684 bytes more brotli (the network and image code in the shim).

### What the platform branch would need to pick up (it is parked, untouched here)

platform rewrote the shim's event path (named events through `delegate(kind, h)`, a `post` with a key argument), indexes `strs` directly (no transient strings), and adds its own cdom.c work (cdom_html.c, httpd.c, ssr_page.c).
To take these fixes it would need: the clone append and `open_svg` (and the SVG namespace in cdom_html.c's server-side output); `name_open`; `style`, `style_num` and `image` (and style ops in the HTML renderer); the window, load, timer, frame and clock imports mapped onto its named-event delegate; transient strings back in its string path (`cd_tmp`); the network module; the auto-flush in every export it adds (cd_start_state among them); and, unchanged C it can take as is: regions, the list end anchor, `cd_list_forget`, the alive check, `CD_LIT`, `cd_fmt`, the weak allocator and the weak mini.c.
