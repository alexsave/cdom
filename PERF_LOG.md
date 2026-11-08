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
- Sound: src/cdom_audio.c and cdom_audio.h (link it to use it), the network's and the store's pattern.
  `cd_audio_load(token, p, len)` decodes an encoded clip (whatever the browser plays) from bytes C holds, read during the call, and answers `cd_on_audio_loaded(token, clip)`, `CD_AUDIO_FAILED` (0) for bytes that are no sound.
  `cd_audio_play(clip, gain, rate, at_ms)` returns a voice id; `at_ms` is a time on `cd_now()`'s clock (a frame's t plus a delay), and a time already past plays now.
  `cd_audio_stop(voice)`, `cd_audio_set_gain(voice, gain)`, `cd_audio_master(gain)`, `cd_audio_mute(on)`, `cd_audio_free(clip)`, and `cd_on_audio_ended(voice)`, which always comes, played out or stopped.
  `cd_audio_state()` is `CD_AUDIO_LOCKED` (the context never ran: the browser waits for a gesture), `CD_AUDIO_RUNNING` or `CD_AUDIO_SUSPENDED` (it ran, and stopped: the page is hidden, or the system took it), and `cd_on_audio_state(state)` hears every change, so an app shows "tap for sound" until it runs without polling.
  C owns every decision and every id: clip and voice ids are handed out from 1 in C, a freed id first (the socket ids' rule, here in C with an alive byte per id, so freeing a free clip traps and a stop or a gain on an ended voice is ignored), and the mute is C's (the shim has one master gain and is told what it is).
- Random bytes: src/cdom_random.c and cdom_random.h (link it to use it).
  `cd_random_bytes(buf, len)` fills len bytes of C memory from the browser's cryptographic generator (crypto.getRandomValues), synchronously: the bytes are there when it returns, so no answer comes back.
  The browser fills at most 65536 bytes per call, so C asks chunk by chunk (`CD_RANDOM_CHUNK`) and any len works; the chunking is C's alone, and the shim fills the one view it is given.
  It returns `CD_RANDOM_OK` (1), or `CD_RANDOM_UNAVAILABLE` (0) with nothing written when the page has no `crypto.getRandomValues`, so an app that needs the bytes secret checks rather than falling back on Math.random.
- The address: src/cdom_url.c and cdom_url.h (link it to use it), for an app that routes in C.
  C owns routing: it reads the address, decides what a path shows and changes the address as it navigates; the shim only reports the address and applies what C asks, through the History API, so nothing loads.
  A path is what follows the origin, the pathname then the search and the hash, as UTF-8 bytes percent-encoded as the browser keeps them.
  `cd_url_read(part, dst, cap)` writes one part of the current address (`CD_URL_PATH`, `CD_URL_SEARCH` with its "?", `CD_URL_HASH` with its "#", or `CD_URL_ALL`, the three in a row) and returns its length, which may be more than cap: then only cap bytes were written, and C asks again with more room.
  `cd_url_push(path, len)` adds a history entry and `cd_url_replace(path, len)` changes the current one; a relative path resolves against the address, and no `cd_on_url` follows, as C shows what the path means itself.
  Both return `CD_URL_OK` (1), or `CD_URL_REFUSED` (0) with the address as it was when the browser threw (another origin, a page whose history cannot change, Safari past 100 changes in 30 seconds) or let the change go without throwing (Chrome's own throttle): the shim compares the address after the call with the path resolved before it.
  `cd_set_title(text, len)` sets the document's title.
  `cd_on_url(path, len)` hears back, forward and a link to a #fragment (popstate) with the new path; the shim listens from start for an app whose module carries the export (`cd_url_changed`), so an app that does not link cdom_url.c gets no listener.
- Languages and display preferences: src/cdom_locale.c and cdom_locale.h (link it to use it); C chooses what to do with them (a translation, a theme, an animation skipped), the shim only reads them.
  `cd_language(i, dst, cap)` writes the i-th preferred language's BCP 47 tag (navigator.languages in the user's order) and returns its length, which may be more than cap, or 0 past the last, so the first N are i from 0 to N - 1, stopping at the first 0.
  A browser without navigator.languages (or with an empty one) answers navigator.language as the only one; one without either answers 0 at once, and the app keeps its own default.
  `cd_prefs()` is a set of bits, `CD_PREFS_DARK` (prefers-color-scheme: dark) and `CD_PREFS_REDUCED_MOTION` (prefers-reduced-motion: reduce); 0, a light page with motion, is what the queries give a user who said nothing and what a browser without matchMedia reads as.
  `cd_on_prefs(prefs)` hears either query change with what `cd_prefs()` reads from then on; the shim listens from start for an app whose module carries the export (`cd_prefs_changed`), as for the address.
- The clipboard and the share sheet: src/cdom_share.c and cdom_share.h (link it to use it).
  `cd_clipboard_write(token, text, len)` puts UTF-8 text on the clipboard (navigator.clipboard.writeText) and answers `cd_on_clipboard(token, status)`; `cd_share(token, title, tlen, url, ulen)` opens the system's share sheet (navigator.share) and answers `cd_on_share(token, status)`, a title or a url of length 0 left out so either may go alone.
  The status is one of four small ints: `CD_SHARE_FAILED` (0: refused, or it went wrong), `CD_SHARE_DONE` (1), `CD_SHARE_UNAVAILABLE` (2: the browser has no such API, so the app shows its own way, a link in a field to copy) and `CD_SHARE_CANCELLED` (3: the user closed the sheet, an AbortError, which is not a failure to report).
  Every answer comes from the event loop, unavailable too, and the strings are read during the call.
  Both need a user gesture, and the header says so: call them while handling a click, a tap or a key (cd_on_event runs inside the browser's own handler), not from a timer, a frame or a network answer; outside one the browser refuses, unless the page holds the clipboard-write permission.
- The page's fonts: src/cdom_fonts.c and cdom_fonts.h (link it to use it), for an app that measures text or lays out by it.
  `cd_fonts_wait(token, deadline_ms)` answers `cd_on_fonts(token, status)` once, from the event loop: `CD_FONTS_READY` (1) once no font is loading (document.fonts.ready; a font that failed keeps its fallback, and the wait is over all the same), `CD_FONTS_TIMEOUT` (0) when deadline_ms passed first (0: no deadline), or `CD_FONTS_UNAVAILABLE` (2) without document.fonts.
  `cd_on_fonts_changed()` hears document.fonts' loadingdone, so text laid out in a fallback can be measured again; the shim listens from start for an app whose module carries the export (`cd_fonts_changed`), as for the address and the preferences.
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
| sound (`audio_load`, `audio_free`, `audio_play`, `audio_stop`, `audio_gain`, `audio_master`, `audio_state`; the unlock and the page's visibility), measured in one run against 8,258 / 3,275 / 3,666 before (node's zlib at brotli quality 11, gzip 1.12) | 9,602 | 3,750 | 4,191 |
| random bytes (`random`), measured in one run against 9,603 / 3,752 / 4,177 before | 9,716 | 3,789 | 4,223 |
| the address (`url_read`, `url_go`, `title`; popstate from start) | 10,306 | 3,997 | 4,450 |
| languages and display preferences (`language`, `prefs`; the two media queries from start), with `writeString` shared by `url_read` | 10,915 | 4,203 | 4,670 |
| the clipboard and the share sheet (`clipboard_write`, `share`) | 11,348 | 4,338 | 4,826 |
| the page's fonts (`fonts_wait`; loadingdone from start) | 11,637 | 4,432 | 4,926 |
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
- Sound (src/cdom_audio.c; 1,344 bytes minified, 475 brotli, 525 gzip in all): Web Audio and nothing else, every decision C's.
  - `audio_load` copies the bytes during the call (decodeAudioData takes the buffer it is given, and C may reuse its own) and decodes them; the buffer is kept under the clip id C chose, and the answer is `cd_audio_loaded(token, clip, ok)`, always from the event loop.
    `audio_free` lets the buffer go.
  - `audio_play` makes the only things a play allocates, two nodes (a buffer source at the rate, a gain at the voice's gain), connects source, voice gain, master gain, speakers, and starts it at `currentTime + (at_ms - performance.now()) / 1000`.
    Every voice shares one ended handler (the voice id is an expando on the source, as node ids are on elements), which disconnects the voice's gain, drops both nodes and calls `cd_audio_ended(voice)`, where C frees the id before the app's hook.
  - `audio_stop` stops the source (one not started yet never starts; its ended answer comes either way).
    `audio_gain` and `audio_master` ease the gain in (setTargetAtTime, 5 ms), so a change or a mute does not click.
  - `audio_state` reads the context: running, else suspended once it has run, else locked.
  - The context is made on the first audio call.
    The browser keeps it suspended until a gesture, so the shim listens on the window, capturing and passive, for pointerdown, pointerup, touchend, keydown and click, and resumes the context whenever it is not running: the first gesture unlocks it, a later one brings it back after the system took it (iOS suspends it for a call).
    These are listeners of its own, not the delegated ones: those exist only for the types C listens for and see only events under listening nodes, and C may stop an event's propagation.
  - The page's visibility (the `EVENTS[VISIBILITY]` event on the document, as `win` listens for it): hidden, the context suspends; shown, it resumes.
    Voices played while it is suspended wait, and play when it runs.
  - Unit cases (tests/unit.c section 14, tests/unit.html, a real click from tools/unit.mjs): "cd_audio_load: a WAV made in C decodes (its samples as written), bytes that are no sound answer FAILED, and its id is handed out again"; "cd_audio_free: the shim lets the buffer go, and the next load takes the id" (a WeakRef and gc(), Chrome run with --js-flags=--expose-gc); "playing a clip that is not loaded traps, and so does freeing a free clip"; "cd_audio_state is LOCKED until a gesture; a synthetic one asks the context to resume and leaves it locked"; "a real gesture (a click from tools/unit.mjs) unlocks it: RUNNING, and cd_on_audio_state hears it"; "cd_audio_play: clip, source, voice gain, master, speakers; cd_on_audio_ended once it played out, without a cd_flush, and the voice let go"; "a voice whose ended answer came: the shim holds nothing of it, and C ignores a stop or a gain on its id"; "cd_audio_play at a time on cd_now()'s clock"; "cd_audio_play at rate 4"; "cd_audio_stop: a playing voice and one not started yet both end at once"; "cd_audio_set_gain, cd_audio_master and cd_audio_mute: the voice's gain node and the master follow, and unmuting brings back the master set while muted"; "20 voices played one after another reuse one id"; "hidden, the context suspends (SUSPENDED, heard by C) and a voice waits; shown, it resumes and the voice plays"; and the click demo tapped.
    tools/unit.mjs runs Chrome with `--autoplay-policy=document-user-activation-required` (Chrome's own default, pinned) so the lock is real: a page cannot make sound before a real gesture, and the gesture is CDP input.
    `user-gesture-required` let the context run with no gesture in this Chrome, so it is not used.
    The hidden page is `document.hidden` overridden and a visibilitychange dispatched; the context's suspend and resume are real.
  - Mutants, all caught: audio-load-bytes, audio-load-late, audio-failed-is-clip, audio-failed-keeps-id, audio-loaded-no-flush, audio-free-keeps-id, audio-free-keeps-buffer, audio-play-unloaded, audio-double-free-silent, audio-locked-is-running, audio-no-unlock, audio-unlock-no-resume, audio-state-unheard, audio-no-start, audio-initial-gain, audio-no-voice-gain, audio-not-let-go, audio-ended-no-flush, audio-voice-kept, audio-stop-dead, audio-gain-dead, audio-at-ignored, audio-at-dropped, audio-rate-ignored, audio-stop-noop, audio-gain-noop, audio-master-noop, audio-mute-ignored, audio-master-lost-while-muted, audio-ids-grow, audio-ended-keeps-id, audio-ended-wrong-voice, audio-never-ran, audio-no-suspend, audio-no-resume-shown, demo-silent, demo-hint-stuck.
  - demo/click.c and demo/click.html: a tap plays a click (30 ms of noise made in C as WAV bytes, its pitch within 10% either way), "tap for sound" until the context runs; built by tests/build.sh at -Oz, and tapped by tools/unit.mjs.
- Random bytes (src/cdom_random.c; 113 bytes minified, 37 brotli, 46 gzip): `random(ptr, len)` fills a view of C memory with `crypto.getRandomValues` and answers 1, or answers 0 and writes nothing when the page has no `crypto.getRandomValues`.
  C splits a long fill into chunks of 65536 bytes (getRandomValues throws a QuotaExceededError past that).
  - Unit cases (tests/unit.c section 15, tests/unit.html, getRandomValues wrapped to record what it is asked to fill): "cd_random_bytes: 200,000 bytes in chunks of 65536, every chunk random, nothing written past either end" (asked 65536, 65536, 65536, 3392; each chunk's zero bytes near 1 in 256; a guard byte on either side kept); "cd_random_bytes: two fills differ, and len 0 asks for nothing and is OK"; "no crypto.getRandomValues: cd_random_bytes answers UNAVAILABLE and writes nothing" (the method removed for the call).
  - Mutants, all caught: random-one-call, random-chunk-small, random-no-advance, random-offset, random-noop, random-zero-asks, random-unavailable-ok, random-unavailable-ignored.
- The address (src/cdom_url.c; 590 bytes minified, 208 brotli, 227 gzip): `url_read(part, ptr, cap)` encodes `location.pathname`, `search`, `hash` or the three in a row and writes at most cap bytes, returning the full length.
  `url_go(ptr, len, replace)` resolves the path against `document.baseURI`, calls `pushState` or `replaceState` (state null: C keeps its own), and answers 1 only when `location.href` is the resolved path afterwards, 0 when it threw or the address did not change.
  `title(ptr, len)` sets `document.title`.
  `start` adds one popstate listener when the module exports `cd_url_changed`, and the listener delivers the new path into C through `cd_bytes_buf`.
  - Unit cases (tests/unit.c section 16, tests/unit.html): "cd_url_read: the path, the search and the hash, and all three as one path; a part longer than the room answers its length and writes only the room"; "cd_url_push adds a history entry and cd_url_replace changes the current one; a relative path resolves; neither calls cd_on_url" (history.length); "back, forward and a link to a #fragment reach cd_on_url with the new path, answered without a cd_flush"; "cd_url_push refused (another origin, a SecurityError as Safari throws past its limit, a change the browser let go, no History API): REFUSED, the address as it was" (pushState made to throw, made a no-op, and removed); "cd_set_title: the document's title, from UTF-8".
    The page pushes an entry of its own first, so a broken push cannot send the page back off itself.
  - Mutants, all caught: url-parts-swapped, url-all-path-only, url-read-overflow, url-read-written, url-push-is-replace, url-replace-is-push, url-refused-ok, url-let-go-ok, url-no-popstate, url-popped-path-only, url-changed-no-flush, url-title-noop.
- Languages and display preferences (src/cdom_locale.c; 609 bytes minified, 206 brotli, 220 gzip, with `writeString`): `language(i, ptr, cap)` writes the i-th tag of navigator.languages, else of navigator.language alone, and an empty tag past the last, so its length is 0.
  `prefs()` reads the two media queries (`matchMedia`, each call, so it is never stale) into C's bits, 0 for both without matchMedia.
  `start` adds a change listener to both media query lists when the module exports `cd_prefs_changed`, and keeps the lists (a list nobody holds could be collected with its listener); each change answers `cd_prefs_changed(prefs())`.
  The listener is `addEventListener("change")`; Safari before 14 had only `addListener`, and is not served.
  - Unit cases (tests/unit.c section 17, tests/unit.html, tools/unit.mjs): "cd_language: the user's languages in their order, then 0 past the last; a tag longer than the room answers its length and writes only the room" (the real list, then navigator.languages made fr-CA, fr, en-GB); "no navigator.languages (or an empty one): navigator.language alone; neither: 0 at once"; "cd_prefs: dark and reduced motion, each from its own media query; 0 without matchMedia" (matchMedia made to match one query, then removed); and in tools/unit.mjs, "cd_on_prefs: dark, then reduced motion too, then neither, each heard with what cd_prefs reads, and the export flushes" (CDP's Emulation.setEmulatedMedia, so the media queries really change).
  - Mutants, all caught: locale-languages-ignored, locale-past-last, locale-no-fallback, locale-empty-languages, locale-bits-swapped, locale-motion-query, locale-no-matchmedia-throws, locale-no-listen, locale-motion-unheard, locale-changed-stale, locale-prefs-no-flush.
- The clipboard and the share sheet (src/cdom_share.c; 433 bytes minified, 135 brotli, 156 gzip): `clipboard_write(token, ptr, len)` reads the text and calls `navigator.clipboard.writeText`; `share(token, title, tlen, url, ulen)` reads the title and the url, each only when C gave one, and calls `navigator.share` with them.
  One function, `shareAnswer`, owns the answer for both: no promise (the API is missing) is unavailable, a resolved one is done, a rejection with an AbortError is cancelled and any other rejection failed; it always answers through a promise, so even unavailable comes from the event loop.
  The answers are the exports `cd_clipboard_done` and `cd_share_done`, which call the app's hook and flush.
  - Unit cases (tests/unit.c section 18, tests/unit.html, tools/unit.mjs): "no navigator.clipboard (an insecure page): cd_clipboard_write answers UNAVAILABLE once, from the event loop, and the export flushes"; "no navigator.share (headless, most desktop browsers): cd_share answers UNAVAILABLE once, from the event loop"; "cd_share: the title and the url, read during the call, each left out when empty; DONE once, from the event loop, and the export flushes" (navigator.share stubbed to record what it is given, C's bytes scribbled over as each call returns); "cd_share: the sheet closed (AbortError) answers CANCELLED; refused (NotAllowedError) or invalid (TypeError) answers FAILED"; and in tools/unit.mjs, "cd_clipboard_write: denied, FAILED; granted, DONE and the text on the clipboard, read during the call, and the export flushes" (the real clipboard: CDP's Browser.setPermission denies clipboard-write, then Browser.grantPermissions grants it and the page reads the text back).
    Headless Chrome has no share sheet, so navigator.share is stubbed; a denied permission is refused even within a gesture's few seconds (the CDP key and the audio click give the page one), so the failure is real and not timing-dependent.
  - Mutants, all caught: share-unavailable-sync, share-unavailable-done, share-codes-swapped, share-no-clipboard-check, share-no-share-check, share-clipboard-token, share-wrong-export, share-title-late, share-empty-title-kept, share-empty-url-kept, share-url-is-title, share-lengths-swapped, share-done-no-flush, share-abort-is-failed, share-failed-is-cancelled, share-rejection-is-done, share-clipboard-noop, share-clipboard-late, share-clipboard-no-flush.
- The page's fonts (src/cdom_fonts.c; 289 bytes minified, 94 brotli, 100 gzip): `fonts_wait(token, deadline)` answers once (a flag, as `store_get` does): unavailable through a resolved promise when there is no document.fonts, timeout from a timer when C gave a deadline, ready when `document.fonts.ready` resolves, whichever comes first; the other is dropped.
  `ready` is read at the call, so the wait covers the fonts loading then, and is a promise even when nothing loads, so ready too comes from the event loop.
  `start` adds a loadingdone listener on document.fonts when the module exports `cd_fonts_changed`; Chrome fires loadingdone (then loadingerror) for a load that failed too, so a fallback that stays is heard as well.
  - Unit cases (tests/unit.c section 19, tests/unit.html, a font tools/unit.mjs's server answers after 800 ms with bytes that are no font): "cd_fonts_wait: nothing loading answers READY, from the event loop, and the export flushes"; "cd_fonts_wait: a font still loading past the deadline answers TIMEOUT; given time, or no deadline, READY once it is done; each answered once" (deadlines 100, 1200 and none, read past the last); "cd_on_fonts_changed: a load done (document.fonts loadingdone) is heard once, and the export flushes" (when C heard it, cd_now in the hook, at 700 ms or later); "no document.fonts: cd_fonts_wait answers UNAVAILABLE once, from the event loop" (document.fonts made undefined for the call).
  - Mutants, all caught: fonts-answered-twice, fonts-no-deadline, fonts-zero-deadline-now, fonts-deadline-dropped, fonts-timeout-is-ready, fonts-ready-not-waited, fonts-ready-sync, fonts-token, fonts-done-no-flush, fonts-no-listen, fonts-listen-loading, fonts-changed-no-flush, fonts-no-check, fonts-unavailable-sync, fonts-unavailable-is-ready, fonts-unavailable-falls-through.
- Not imports, shared by them: `post` (the event record writer, used by delegated, window and the old image events), `push` (open, open_svg and clone), `deliver` (bytes into C memory through `cd_bytes_buf`: the encoded image, a network message, a stored value, a path after back or forward), `utf8`, `writeString` (a string into room C gave, cut to it, answering its whole length: a part of the address, a language).
- `EVENTS` gained "resize" and "visibilitychange" (a "load" for images came and went with the first image import).

### Gates and the paired runs

Every gate passes on the bench entry built from this branch (cdomf): the harness's keyed check, smoke, DOM identity against perf 0d0bdd7 (entry cdomp0), listfuzz (5 seeds, 40,000 rounds each: the owning list and the list with an end anchor), the unit page (18 cases) and the link tests.
tools/mutate.py: 37 mutants, every one caught by the test written for it (48 after the store with its answered put and early open, the two-step image with RGB and placement, and the device pixel ratio: all caught; the unit page 26 cases; 61 and 35 after the network pass; 63 and 36 with `rect`); with sound, 116 mutants (visibility-target's text brought up to date: it had been skipped since `windowTarget`) and 58 unit cases, all caught and all green; with random bytes, 124 mutants and 61 unit cases; with the address, 136 mutants and 66 unit cases; with languages and preferences, 147 mutants and 70 unit cases, all caught and all green; with the clipboard and the share sheet, 166 mutants and 75 unit cases; with the page's fonts, 182 mutants and 79 unit cases, all caught and all green.

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

## The two lines merged (2026-11-08)

main had kept the batched op stream and added canvas-blit and host access on it; port-fixes had replaced the stream with direct calls and added the modules above.
The two had never been measured against each other: RESULTS.md's 1.04 is a factor against the fastest of seven entries in one pass, and this log's ratios are paired against vanillajs, so they do not compare.

### Head to head

Both lines' keyed entries were built side by side into jfb as their own defaults (cdom-main: main b8fbd1d at -O2 with the shim as a file; cdom-pf: port-fixes 4a5fc00 at -Oz with the shim minified and inlined) and run with tools/pair.sh, interleaved benchmark by benchmark with vanillajs, every start gated on the 1-minute load being under 4.
Pass h2hA ran vanillajs, cdom-main, cdom-pf; pass h2hB ran vanillajs, cdom-pf, cdom-main, so neither entry always went second.
Another agent ran short browser tests on the Mac meanwhile, so the gate waited up to 9 minutes before some starts; no start timed out.

| benchmark | vanillajs A / B | cdom-main A / B | cdom-pf A / B |
| --- | ---: | ---: | ---: |
| 01_run1k | 29.5 @3.60 / 28.8 @2.65 | 29.6 @3.90 / 29.4 @2.78 | 29.7 @3.72 / 28.8 @2.87 |
| 02_replace1k | 32.5 @3.48 / 31.1 @2.85 | 33.2 @3.20 / 32.2 @2.58 | 32.3 @3.52 / 31.2 @2.38 |
| 03_update10th1k_x16 | 21.2 @3.66 / 15.8 @2.54 | 17.8 @3.81 / 16.9 @2.17 | 17.1 @3.50 / 17.5 @2.63 |
| 04_select1k | 5.10 @3.54 / 4.70 @1.85 | 4.80 @2.84 / 5.80 @1.84 | 4.60 @3.72 / 5.20 @2.20 |
| 05_swap1k | 18.7 @3.18 / 22.0 @2.94 | 20.7 @3.83 / 20.2 @3.83 | 19.8 @3.81 / 21.4 @3.50 |
| 06_remove-one-1k | 15.7 @3.80 / 18.3 @3.55 | 15.4 @3.28 / 15.5 @2.85 | 15.4 @2.81 / 15.4 @3.51 |
| 07_create10k | 310.1 @2.39 / 310.3 @2.59 | 316.5 @2.31 / 319.4 @3.05 | 308.6 @2.38 / 314.4 @3.47 |
| 08_create1k-after1k_x2 | 34.3 @2.42 / 34.2 @3.69 | 35.9 @3.06 / 35.5 @2.82 | 35.2 @3.34 / 34.9 @2.99 |
| 09_clear1k_x8 | 13.8 @3.66 / 13.1 @3.60 | 14.0 @3.71 / 13.5 @2.71 | 13.7 @2.99 / 13.3 @2.98 |
| weighted geomean vs vanillajs | 1.000 / 1.000 | 0.988 / 1.015 | 0.968 / 1.002 |

Each cell is the median total in ms of that pass, with the load at the start of the run.
tools/ci.mjs, cdom-pf over cdom-main: h2hA 0.980 (95% interval 0.960-0.989), h2hB 0.987 (0.970-1.013).
Both passes pooled (each benchmark's iterations from the two passes resampled together): 0.983 (0.971-0.988); the same bootstrap of cdom-main against itself gives 1.000 (0.986-1.013), which is the width noise alone makes.
Script time tells the same story with less noise: cdom-pf against cdom-main, run1k 2.4 against 3.0 ms in both passes, replace1k 4.9 and 4.8 against 5.4, create10k 19.3 and 19.2 against 26.3 and 26.2, create1k-after1k 2.5 and 2.6 against 3.2 and 3.3.
tools/quick.mjs (script time per click, unthrottled, two complete interleaved runs at loads 3.66 and 3.85): run1k 1.79 and 1.74 ms against 2.30 and 2.20, replace1k 1.85 and 1.84 against 2.37 and 2.18, append 1.52 and 1.50 against 1.87 and 1.89, create10k 15.78 and 15.67 against 17.90 and 17.99; swap, update, select and clear sit at the timer floor for both (a third run lost vanillajs to a navigation race and is left out).
First paint (tools/fpruns.sh, 11 rounds, loads 3.28-3.90): vanillajs 68.1 ms, cdom-main 91.1 ms (1.34x), cdom-pf 75.5 ms (1.11x); the harness's compressed size 2.5, 9.4 and 8.9 KB.
So the direct calls are faster: about 1.7% on the weighted geomean, a fifth less script on the creating benchmarks, and 16 ms less to first paint, the last mostly from the inlined shim and -Oz.
-O2 against -Oz is part of the difference between the two entries, but this log measured -Oz no slower than -O2 on this app, and the script time gap is in the shim's DOM calls, not in C.

### What was ported, and what was dropped

The merge keeps port-fixes's direct calls and brings main's canvas-blit and host-access features onto them; RESULTS.md ("On direct calls") lists each one.
Where the two lines did the same thing twice, one was kept: `cd_rect` (not `cd_get_rect`), `cd_listen` on a host id (with `cd_listen_window` a mapping onto it), `get` behind `cd_input_value` (not `input`), `cd_call` as `cd_call_args` with no arguments, `cd_frames` over `cd_raf_request`, and the weak allocator (not `CDOM_NO_ALLOC`).
`cd_viewport` stays beside `cd_get_num(CD_WINDOW, "innerWidth")`: it reads the four values an app needs together in one call, where the paths are the general case.
API changes for an app on port-fixes: a whole-page event's record id is `CD_WINDOW` or `CD_DOCUMENT` (it was `CD_MOUNT`); the record's `prevent` is `flags`; node ids start at `CD_FIRST_ID` (6); `cd_rect` returns int; the event types past `CD_EV_TOUCHCANCEL` are new.
An app on main's op stream recompiles against the new event numbers (resize is 12, visibilitychange is `CD_EV_VISIBILITY`, 13) and uses `cd_rect` for `cd_get_rect`.

### Tests, mutants and bytes after the merge

The merged entry (cdom-merged, the default build) passes every gate: the harness's keyed check, smoke, DOM identity against both cdom-pf and cdom-main over 22 snapshots, listfuzz (5 seeds of 40,000 rounds), the unit page (86 cases), the link tests, tests/host (38 checks, real fullscreen and visibilitychange included) and tests/canvas.
tools/domcheck.mjs now pins `crypto.getRandomValues` through its pinned `Math.random`, because the shim seeds from crypto (cherry-picked from amgi 4a31c08) where the older entries seed from `Math.random`.
The unit page's prefs case names both media features at every step (amgi b0a3853): it had left one unset, so on a Mac in dark mode it failed on port-fixes itself.
tools/mutate.py has 216 mutants (main's host and canvas features, the unit page's host cases, the seed and cd_url_step added), and all 216 are caught.
It now runs them in parallel (`-j`, default half the cores; 6 workers took about 9 minutes, the serial run over an hour), each worker in a private copy of the tree, so the real tree is never mutated.
Getting there found two real gaps and three kinds of false miss.
The gaps: with direct calls, an event delivered inside a handler no longer re-applies a batch, so the old focus() case could not see the queue go (host-no-queue survived); a new host case prevents a click, calls focus() and then reads its own record, and fails three ways without the queue.
And the canvas test only stopped the loop from inside a frame, so dropping the pending frame's check survived (frames-no-stop); a stop from outside a frame is now tested.
The false misses: four mutants did not compile under clang 23's -Werror (unused variables; their texts now keep the variable read); tools/fuzz.mjs and tools/unit.mjs listened on fixed ports, so a second worktree running the suites at the same time made them fail to bind or load the other tree's page (they take a free port now, and tools/cdp.mjs a free debugging port in place of a random one from 8730-8739); and two mutants fail the page at start, before the case written for them, so they now expect that.
Bytes (bench entry, -Oz, brotli at q11): app.wasm 9,646 -> 9,803 (4,593 -> 4,662), index.html with the inlined minified shim 11,899 -> 14,269 (4,524 -> 5,404), so host access, canvas and the wider event set cost the page 949 bytes brotli; main's entry was 20,677 bytes of wasm (5,604) and a 10,853-byte shim (3,649) beside a 403-byte page.
Shim source 943 -> 1,149 lines.
The merged entry has not been timed in a paired pass of its own yet.

### A deferred event goes only to the node it was queued for

An event the browser dispatches while C runs (the focusout that removing a focused input fires) waits in the shim's queue, and by delivery the batch may have removed its node and handed the id to a new one (amgi: a rename input re-opened on Escape).
The queue now keeps the node each id named when the event was queued, and drain drops the event when the id names anything else; host ids are never reused, so their events always go out.
The unit page's deferred cases (a reused id, a freed id, a node still there) and five mutants (deferred-*) cover it.
The shim grows 14,219 -> 14,249 bytes minified (5,393 -> 5,416 brotli at q11).

### App helpers: cd_open_id, cd_on, cd_timer_stop and cd_timer_cb, cd_http_cb (branch app-helpers)

Four helpers an app moving from React (amgi) asked for, each built on what cdom already had, and each its own commit with a unit case and mutants.
- `cd_open_id(tag)` is cd_open, cd_id_new and cd_name_open in one call, so a building region records the id (mutants open-id-*).
- `cd_on(id, ev, fn, ctx)` calls fn(e, ctx) for the record's (id, type) in place of cd_on_event, which keeps every other event and now has an empty weak default, so an app that uses cd_on alone links (tests/link/on_only.c).
  The handlers live in one pair table in C (open addressing on the id, deletion by shifting the run back), with a byte per id, so freeing one of a list's 10,000 ids that has no handler costs one load.
  It found a real bug in the shim: forget left a freed node's id mark on the node, so a node left in place after its id was freed was delivered under that id once it was handed out again; forget now clears it (mutants on-*, on-forget-keeps-mark among them).
- `cd_timer_stop(token)` is a real clearTimeout.
  The shim's `timer` now returns its setTimeout handle, `timer_stop(handle)` is a new import, and `cd_timer(token, handle)` tells C which one fired, so C keeps every pending (token, handle) in the same pair table and the shim keeps nothing.
  Stopping a token that fired, was stopped or was never started does nothing.
- `cd_timer_cb(ms, fn, ctx)` and `cd_http_cb(fn, ctx, ...)` take their tokens from one slot table (src/cdom_cb.h, owned by cdom.c): a token is `CD_TOKEN_CDOM` (0x80000000) with a 15-bit generation and a 16-bit slot, so it never meets an app's token (those stay below `CD_TOKEN_CDOM`, and cd_timer_start and cd_http trap above it) or CD_FRAMES_TOKEN, and an old token never stops the timer that took its slot (mutants timer-* and httpcb-*).
The shim grows 14,249 -> 14,386 bytes minified (5,416 -> 5,444 brotli at q11, 6,012 -> 6,059 gzip at -9): the forget fix and the timer handle and `timer_stop`.

### A program's libc and a library's I/O (branch program-libc-and-io-cb)

Two collisions found by putting amgi's UI, cdom and the c_base C client in one wasm, under one rule: a libc belongs to the final program, never to a library, and a library linked inside an app never takes an app-wide hook.
- src/libc/string.h (on the include path with -isystem) declares memcpy, memmove, memset, memcmp and strlen and nothing else of libc; copy and fill go to the builtins, so even under -ffreestanding they are memory.copy and memory.fill in place.
  mini.c adds weak real memcpy, memmove and memset for an address taken or a file built without the header; the linker drops them when nothing reaches them.
  The c_base client core (client/src/client.c) compiles against it with c_base's flags and links with mini.c alone: 9,737 bytes, no imports, no c_base libc.
- `cd_ws_open_cb(fn, ctx, url, len)` sends a socket's open, messages and close to fn(ctx, id, ev, p, len) instead of cd_on_ws_*, routed by socket id in C; the slot (src/cdom_cb.h) is freed at the close.
- `cd_store_get_cb`, `cd_store_put_cb` and `cd_store_del_cb` answer to a callback with ctx instead of cd_on_store*, and the token forms now trap on a token in cdom's range, as cd_http and cd_timer_start do.
The shim is unchanged: 14,386 bytes minified (5,444 brotli at q11, 6,059 gzip at -9).
In C, at -Oz, cdom_net.c's code grows 322 -> 909 bytes and cdom_store.c's 120 -> 497.

### Zero edges, and the shim's offsets from C (branch zero-edges-offsets)

Two decisions of 2026-11-08: cdom and its sibling C library have zero edges between them, and cdom takes no struct generator.
- The name gate: tools/edges.mjs, run by tests/link/run.sh, fails naming file and line when a tracked file other than the docs (every *.md, and LICENSE) names the library or its old spelling, case-insensitively.
  The docs keep their history (this file, RESULTS.md, BRIDGE.md).
  The link test's second flag set was named for the library it was copied from; it is now named for what it is, "freestanding" (-ffreestanding -mno-nontrapping-fptoint -std=c11 -Wshadow -Wvla -Wstrict-prototypes), with both flag sets and the test unchanged.
  Mutants edges-name and edges-old-name (the name in a header comment, the old spelling in a script) are caught; mutate.py's worker copies are git repos now, so the gate sees the files a copy tracks.
- The offsets: cd_layout hands the shim one block of const data, its entry count and then CD_LAYOUT's entries (src/cdom.c): where the string batch and the event record live, and every field offset and struct size the shim reads or writes, from offsetof and sizeof.
  That is the batch's per-string record (now a struct, str_meta: id, bytes, chars), every field of cd_event_rec the shim fills (type to dy, ms, ch and the field bit's mods among them), cd_viewport_rec and cd_val (size, tag, num, u).
  rect's out-parameter is a double[4] in the order cdom.h documents and the byte buffers are (pointer, length), so neither has fields to map.
  The shim reads the block once at start, in the same order, and throws when the count is not the number it read (a shim from another cdom).
  The event record never moves, so each of its fields becomes a word index of its own at start, and post() writes `i32[evX]` where it wrote `i32[record + 2]`: no extra import and no extra add per event.
- The test: tools/offsets.mjs, run by tools/unit.mjs, finds every reach into C memory in shim/cdom.js (an index into u8, u32, i32, f32 or f64, their set and subarray, a typed array over memory.buffer, the bytes helpers) and fails on a number in one, on a base pointer indexed bare (a first field by position) and on a base stepped by a number; a shift is a unit, the one literal allowed.
  main's shim fails it in 30 places, this one in none of its 50 reaches.
  Unit case 31 fires a pointer, a wheel and a key at a grandchild of a cd_on box with a distinct value in every field, and C hands each field back by name; C also passes every cd_val kind to a page function and reads the viewport.
  With cd_event_rec's fields scrambled in cdom.h (dy first, then ms, flags, buttons, id, y, x, depth, path, mods, key, button, ch, type, dx, pointer), the unit page, host, link and the rest stay green, where main's shim would misread every field.
  Mutants offsets-literal (`i32[evType + 13]`, right at run time), offsets-bare-base (`u32[at]`), offsets-size (`index * 4`) and offsets-meta-step (`meta += 3`) are caught by the scan alone; offsets-event-order and offsets-val-order (two entries of the map swapped) by the field case; offsets-count (one entry the shim does not read) at start.
  The existing mutants on those lines (viewport-order, input-no-time, host-pointer-fields, host-wheel-dy, host-val-num) are rewritten onto the new text and still caught.
- Cost, measured against main's entry built beside it (bench/build.sh into a scratch jfb, served on a free port), interleaved, three rounds at loads 13-16: tools/evcost.mjs 7,625 / 7,640 / 7,830 ns per click on main against 7,570 / 7,580 / 7,725 here; tools/quick.mjs run1k 2.0 / 2.0 / 1.9 ms against 1.7 / 1.7 / 1.7, replace1k 2.2 / 2.0 / 2.0 against 2.1 / 1.9 / 2.0, create10k 18.0 / 15.9 / 16.5 against 18.0 / 16.1 / 16.5, the rest at the timer floor for both: the same within the noise.
- Bytes: the shim grows 14,387 -> 14,891 minified (5,444 -> 5,626 brotli at q11, 6,063 -> 6,290 gzip at -9; main measured in the same terser run), the 29 names the map fills and their reads at start; app.wasm (-Oz) 10,630 -> 10,725, the block being data the linker fills rather than code (as 31 stores at start it was 10,821).

## Bridge designs compared (2026-11-01)

One import per DOM operation stays: a generic dispatcher, a function table, natively bound imports, fused operations, the old op list and the Fast API were each measured against it on real pages and none was faster beyond the noise. Why, and what does help, is in BRIDGE.md; the benchmark is at the tag bridge-bench-2026-11-01.
