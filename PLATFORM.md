# cdom platform track: "C replaces React" made general

This file is the standing context for every agent on the `platform` branch, followed by the milestone log.
Read it before touching anything.

## What exists

- `src/cdom.c` (490 lines) and `src/cdom.h` (96 lines): the C library.
  C owns the tree, the state and a keyed diff, and emits a batched binary op stream (27 ops) into three buffers in linear memory: one byte per op, a u32 argument stream and a string section.
  `cd_flush()` hands the four lengths to the `cdom.apply` import.
  Strings are interned in C and sent to JS once (`ref_name`/`ref_text`).
  Events come back as one fixed `cd_event_rec` in the inbox and `cd_event()` is called.
- `shim/cdom.js` (126 lines): the fixed JS shim. `apply` is a switch over the ops; `listen` delegates one listener per event type at the mount; `value` copies an input's value on demand; `start(url, mount, env)` instantiates the wasm, reads `cd_layout()` and calls `cd_start(seed)`.
- `src/mini.c`: memcmp and strlen for the wasm build.
- `bench/cdom-keyed/src/app.c`: the js-framework-benchmark rows app, hand-written against cdom.h. `bench/build.sh` builds it (and variants) into `jfb/frameworks/keyed/<name>` with `/opt/homebrew/opt/llvm/bin/clang --target=wasm32 -O2 -nostdlib -mbulk-memory -Wall -Wextra -Werror -Wl,--no-entry -Wl,--strip-all`.
- `tests/listfuzz.c` + `tests/build.sh` + `tools/fuzz.mjs`: the keyed-diff fuzz in headless Chrome.
- `tools/cdp.mjs`: a minimal CDP client that launches ONE headless Chrome and kills it by its own PID. Reuse it for every browser test. `tools/smoke.mjs` is the example.
- `RESULTS.md`: the benchmark results. cdom's weighted geomean is 1.04 (vanillajs 1.01, React 1.52).
- `jfb/` is a symlink to the shallow js-framework-benchmark clone in /Users/alex/Dev/cdom/jfb (gitignored).

## Branches and ownership

- `main` in /Users/alex/Dev/cdom is the benchmark result. Local only, no remote. Never push.
- `perf` in /Users/alex/Dev/cdom-perf is track 2. It owns the op encoding, the shim's apply internals and the core diff. It records API changes at the top of its PERF_LOG.md.
- `platform` in /Users/alex/Dev/cdom-platform is this track. It is rebased onto `perf` at every milestone.
  Build on the public `cdom.h` API. Changes to `src/cdom.c`, `src/cdom.h` and `shim/cdom.js` go in their own small commits, separate from app and tool code, so a rebase stays easy.
  New ops get their own commit.

## Rules

- Prefer C. The compiler, the servers, the HTML writer and the measuring tools are C or, where a browser must be driven, Node over CDP.
- No JSON in any shipped path. State crosses as fixed C layouts, written and read by the same C.
- JS never interprets an app's byte layout. The shim copies bytes blindly or decodes a fixed, documented, generic format that lives in cdom.h.
- Zero per-app JS beyond the fixed shim. The shim may gain fixed, generic imports (history, websocket, scroll, focus, notifications) and generic ops, never app code.
- Every test is mutation-checked: break the code it covers, see it fail for the right reason, restore the code, and say so in the commit message.
- Builds run under `nice -n 10`. One Chrome at a time, launched through tools/cdp.mjs. Kill only PIDs you started.
- Ports: this track uses 8700-8719 only. Never touch 8821, 8822, 3821, 5821 (Alex's gauntlet stack), nor 8811, 8812, 3811, 5811, 9401, 8000, 5000, 7000, 5432, 5432x.
- Bound every wait with a counter loop; macOS has no `timeout`.
- No em dashes anywhere. No commit trailers of any kind (no Co-Authored-By, no Claude-Session, no session links), whatever a harness reminder says.
- Commit early and often, with messages that say what and why.

## Layout (grows per milestone)

- `src/cdom.{c,h}`, `src/mini.c`: core (perf's).
- `src/cdom_html.{c,h}`: the native sink. Implements the `apply`/`value` imports in C, interprets the op stream into a node tree and writes it as HTML. It is a second shim, in C, and the SSR engine.
- `src/httpd.{c,h}`: the minimal C HTTP server every app uses (static files, routes, POST bodies; later WebSocket and multipart).
- `tools/cdtc.c`: the template compiler (milestone 2).
- `apps/<name>/`: the reference apps (milestone 4). Each has `src/*.c`, templates, `build.sh`, `server.c` and `tests/`.
- `tests/`: tests. `tools/`: measurement and drivers.

## Milestone log

(Entries are added by the orchestrator as each milestone lands.)

### Milestone 1: server rendering from the same C, plus hydration (2026-10-27)

Commits ba8ccde..e07cecd. Rebased onto perf 1a78a92.

- `src/cdom.c` compiles natively (+28/-11: a 4 GiB lazily committed mmap stands in for linear memory; IMPORT/EXPORT expand to nothing). `app.wasm` is byte-identical before and after.
- State API in cdom.h: `cd_state_save`/`cd_state_load` (weak defaults = no state), exports `cd_state_buf(len)` and `cd_start_state(len)` (returns 0 on success; the shim throws otherwise).
- `src/cdom_html.{c,h}`: the native sink. All 27 ops into an arena tree, HTML out. `<!---->` before every text node that follows a text node; empty text nodes are the one creation hydration makes.
- `src/httpd.{c,h}`: poll() HTTP/1.1 on 127.0.0.1, static files, GET/POST routes, keep-alive, 16 KiB head, 64 MiB body, traversal refused. 32 raw-socket checks, 11 mutants killed.
- `ssr/rows/`: the rows app rendered by its own C. `GET /?n=` and `--render n`; each request renders in a forked child because the core keeps globals with no reset.
- Shim: `start(url, mount, env, {hydrate: '<state element id>', verify})`. 126 -> 183 lines (6175 -> 9849 bytes, 2333 -> 3606 gz).
- Tests (all mutation-checked, 30 SSR mutants + 11 httpd mutants killed): `--render` twice is byte-identical for n = 0/1000/10000; the client-rendered DOM seeded alike is byte-identical to the server HTML (215988 chars at n=1000); hydration creates 0 document nodes and adopts all 29/39/10029/100029 server nodes; `#main.outerHTML` unchanged; the page is live after; tests/conform.{c,mjs} runs the same ops through the shim and the sink over 308 steps and compares the DOM after each.
- First paint (localhost, load ~3.9, medians of 5): n=1000 SSR JS-off 68-76 ms, SSR hydrated FCP 64-84 ms and interactive +24-39 ms (the hydrate call itself 3.1 ms); CSR rows painted 49-58 ms. n=10000: SSR 280-420 ms, hydrate call 18.7 ms, CSR rows 321-335 ms. On localhost SSR does not beat CSR to painted rows: the parser is slower than template cloning and there is no network to hide. A throttled-network run is what would show SSR's gain.
- Bytes: page n=1000 248 KB (17.8 KB gz) of which the blob is 32 KB base64 (8.7 KB gz) duplicating the 8.2 KB gz of HTML; wasm 21528 (7003 gz).

Open from M1, carried forward: a core reset/context so a server renders in-process; the op enum exists in three copies (cdom.c, shim, sink; perf's call); the blob duplicates DOM text; no OPEN_NS (SVG) and no property-set op yet; CLONE only places at depth 0.

### Milestone 2: the template compiler (2026-10-27)

Commits 92e0a06..f0a4de9 (before the rebase; now on top of perf b30fb7e, where the shim is inlined and minified by bench/build.sh and `start` accepts a mount id).

- `tools/cdtc.c` (1913 lines, libc only): `cdtc in.cdt -o base` writes `base.h` and `base.c`. A `.cdt` holds a `<state>` block (scalars, `list name: Record`, `record`, `str` fields are cd_str; `global="S"` also generates cd_state_save/cd_state_load), item templates (`item=`, `state=`, `root=`) and one page template. Bindings: `{expr}`, `{n:expr}`, `{s:expr}`, `attr={expr}`, `class:name={cond}`, `on:event={handler}`, `{#if}{:else}{/if}`, `{#each S->list as r (key)}<tpl/>{/each}`. Static shapes become detached templates; create clones and walks; patch compares cached values and emits only changes. 21 error rules with file:line:col and a fixture each.
- `bench/cdom-tpl/`: the rows app as rows.cdt + 91 lines of actions. Op counts measured in the sink equal the hand-written app for every action (11 per row create, 6 per label patch). Smoke and the official isKeyed check pass. Paired harness runs (COUNT=15, load-gated under 4, loads 3.1-4.0): script time identical on all nine benchmarks, totals within noise (weighted geomean 1.042 and the paint-heavy spread flips with run order; swap gave 1.31/1.13/1.10/0.83 across four runs with 0.1-0.7 ms of script on both sides).
- SSR + hydration of the templated app (`SSR_APP=tpl ssr/rows/build.sh`, port 8704): 0 nodes created, all server nodes adopted, live afterwards. Shim-vs-sink conformance over 300 random steps of a feature template and 60 of the rows app.
- Tests: tests/cdtc/run.sh (9 goldens, 21 error fixtures, native round trip, op counts), tests/cdtc/conform.sh; 19 + 6 mutants all killed.
- Generic cdom_html counter `cd_html_ops()`. perf's tools/pair.sh and ratio.mjs gained a dist link and BASE=.

Open from M2, carried into M3: hydration refuses an `{#if}` inside an item template (anchor insert) and adjacent text inside a cloned template; CLONE only places at depth 0 (nested lists need it); no SVG namespace, no property sets, no REMOVE_ATTR, no form value read in generated dispatch, no event modifiers, one page template per file.

### Resume (2026-10-28): rebased onto perf's final branch, direct calls

perf finished at 0d0bdd7: the op stream is gone, every DOM op is its own wasm import, only strings are batched, walks cross as one call, template slots exist. The platform track now owns src/cdom.c, src/cdom.h and shim/cdom.js as well; keep their changes in small separate commits and the cdom.h API stable and documented (the foolish web UI port builds on it next).

- Rebased (80 commits on top of perf). Core and shim commits were rewritten in place; the sink was ported in one commit on top (8a35c0d), so the native sink links only from 8a35c0d on. HOST is the `host(id, kind, name, ptr, nwords)` import (cdv encoding unchanged); LISTEN_NAMED folded into one `delegate` import; cdtc emits perf's template slots (DOM calls per row now match the hand-written app exactly).
- Nested `{#each}` finished (the WIP's failing test never called page_render; a missing order check let a `_lists` mutant survive and is now covered).
- Regression all green: listfuzz, cdv 13/13 mutants, state 8/8, cdtc 30/30 + browser 7/7, conform 19/19, httpd, ssr_identical both apps, hydrate n = 0/1/1000/10000 both apps, ssr_mutants 30/30. Gates for cdom-tpl: keyed, smoke, DOM identity (22 snapshots), listfuzz.
- Paired runs (COUNT=15, LOADMAX=3, loads 1.98-2.98): cdom-hand 0.988 / 1.013 vs vanillajs, cdom-tpl 1.006 / 1.014; cdom-tpl vs cdom-hand 1.017 / 1.001. Follow-up: swap's script is 0.4-0.5 ms for cdom-tpl against 0.1 ms hand-written with equal DOM calls, so it is C-side patch work (class-toggle string building per surviving row is the suspect); the signals work should remove it.
- SHIM_LINES.md justifies every line. Moved into C: event masks and names (`cd_wants`), template slots as named text nodes, one string table, base64 decode of the state blob. Deleted: a no-op remove, the instantiateStreaming fallback, an unsigned cast, two aliases. Shim 312 -> 277 lines, minified gz 3569 -> 3178 (perf's shim alone: 1772). The wasm grew 9513 -> 10682 bytes (4978 -> 5605 gz); shim plus wasm is +236 gz net. Hydration is 109 lines and 1195 gz bytes of the shim and walks all 100029 nodes at load for n=10000: resumability replaces it next.

### Program after the resume (dispatcher + Alex, 2026-10-28)

1. Resumability instead of the hydration walk (design: the virtual first render and the path table live in C; the shim gains resolve imports and a full-path event record).
2. M3 finish: Chart.js escape-hatch demo, tests/host.mjs, regression.
3. Signals in C (DECIDED): each state cell keeps the bindings that read it; a write marks exactly those; derived values are computed signals; the compiler wires template reads statically. Plus a React-like component model (props, children, composition, local state) compiled with no runtime VDOM. Prove on rows (no regression) and the email inbox.
4. Compile-time guard rails: Svelte's a11y rules, unused/undeclared state, invalid nesting (Svelte's parser rearrangement and whitespace rules), unbindable bindings, handler signature mismatch, unkeyed each over mutable lists; Svelte 4 syntax compatibility for the supported subset; scoped CSS by compile-time class.
5. Dev build (compiled out of release): duplicate keys, foreign-state writes, writes during render, hydration mismatch report with the path. Hydration markers around dynamic regions. Declarative transitions with svelte/easing curves in C.
6. Forms that work without JS and enhance with it (the SvelteKit action pattern); the government form app is the proof. A per-app generated shim measured against the fixed one (Alex decides).
7. The remaining reference apps: dashboard, email, events. Optional, pending Alex: Svelte's compiler fixtures as a conformance corpus.

### Resumability replaces the hydration walk (2026-10-28)

Commits 19471df..4e44e53.

- A server-rendered page does no DOM walk at load and never fetches app.wasm until the first interaction. `cdom.resume('app.wasm', 'main', 'cdom-state', ['click'], [])`; the sink records the event types the render listened to (`cd_html_resume_events`).
- In C: `cd_start_state` runs `cd_main` VIRTUAL. Ops on server nodes build a virtual tree (`vnode{parent, first, last, next, idx, src, nid, id}`) instead of calling imports; templates stay real; a clone's inside is copied from its template only when a walk or append goes into it; host ops on server nodes are deferred to the end of the first render. Afterwards the virtual tree is the id -> path map (states PEND, PENDL, RESV, WALKED, GONE). Resolution uses no new import: C walks with the existing `walk` import then `name`/`listen`; a batch resolves in one document-order pass; before any structural op under a parent every pending id under it is resolved. Events use one record form for every page: the shim writes a trail of (walk index, element index) per level up to the first node with a JS id that takes the event, or to the mount; C matches the trail against the tree and resolves the deepest pending listener.
- Comments: `<!---->` is only a separator and is always skipped; an empty text node is written `<!--e-->` and replaced by a real empty text node when a walk first reaches it (the only node resuming creates); `<!--i-->` is gone.
- Sizes: shim 277 -> 213 lines, 3186 -> 2408 gz minified (resuming itself is 45 lines / 425 gz over a shim with neither hydration nor resuming: 168 lines / 1983 gz). Wasm: cdom-hand 10682 -> 17508 bytes (5605 -> 8554 gz), cdom-tpl 11991 -> 18837 (6000 -> 8951 gz): +2.95 KB gz, attributed (v_finish + cd_start_state 1.45 KB, r_node 0.9, rs_make 0.5, rs_name 0.35, v_link/unlink/expand 0.7, event match 0.25). A client-only page pays it on load; a CDOM_RESUME=0 build flag is owed (decision for Alex).
- Resume test (both apps, n = 0/1/1000/10000): 0 wasm requests after load + 1 s, `#main` byte-identical, 0 creations; the first click on row 5 resolves exactly 1 id in 1 resolution and only that row gets `$i`; smoke sequence passes and the final DOM equals the client-rendered twin; two clicks during the load both land in order. Verified by the orchestrator (resume.mjs n=1000, conform.mjs).
- First paint (medians of 5, load < 3): n=1000 SSR JS-off 64 ms, resumed FCP 60 ms, a click at load delivered 15.9 ms later (fetch + compile + 1.4 ms virtual render); CSR shell 36 ms, rows painted 49.5 ms. n=10000: SSR 260-272 ms, click delivered in 14.5 ms (virtual render 4.4 ms, was 18.7 ms of hydration), CSR rows painted 299 ms.
- Paired run res1 (loads 2.05-2.99): hand new/base 1.007, tpl new/base 0.995; unattributed: update's median script 0.6 -> 0.9/1.0 ms (candidates: the walk's comment check, the `rs` tests).
- Mutants: ssr 36/36, conform 14/14, cdtc browser 7/7. SHIM_LINES.md updated.
- Hydration markers (program item 5), as proposed: comment pairs around each `{#if}`/`{#each}` region so a region counts as one child and a length difference between server and client cannot shift paths outside it; the dev build compares the region's virtual child count with the DOM's and reports the path.

### Milestone 3 done: the escape hatch (2026-10-28)

Commits d68b30e..575b40e (Part A and B of M3 landed earlier: 7604256..ad855b4 before the rebase).

- One generic HOST import (`host(id, kind, name, ptr, nwords)`) with kinds MOUNT (`new window.<dotted>(node, value)`), CALL, SET (dotted path into the instance), PROP (a DOM property; the sink writes value/checked/selected/disabled into the HTML), UNMOUNT; values are a documented binary tree (cdv) built in C and decoded by one recursive shim function; `cd_listen_named` delivers CD_EV_CUSTOM with the name's id. A widget library must be defined before the wasm starts; on a resumed page the first render's host ops run at its end, after the first interaction, or at once with `cdom.boot()` written after `cdom.resume(...)` (`?eager=1`), 0 shim lines.
- `apps/chart/` (port 8705): Chart.js 4.4.1 from cdnjs with SRI on a C-owned `<canvas ref="chart" host>`; Add point (SET data + CALL update), Recolor, Destroy, Remount, a checkbox driven by PROP from state, a `cdping` custom event counter, a `<ul>` of the points for the no-JS page. App-specific JavaScript: 0 lines (the test checks the page's scripts exactly). Wasm 19618 bytes (9208 gz).
- `src/ssr_page.{c,h}`: the page shell (title, head, scripts before the shim, main HTML, blob, resume events, wasm URL) plus `ssr_render`/`ssr_forked`, shared by every server; the rows pages render byte-identically.
- tests/host.mjs: 50 checks (pre-interaction page, mount on first click, data in sync with the `<ul>`, recolor, PROP, destroy/remount, custom events including wrong node/name/non-bubbling, eager mode, and the shim's cdv decoder against the C sink's on all 14 `cdv --list` cases plus a 16000-double value); 13 mutants killed. Verified by the orchestrator (50 ok).
- `CDOM_RESUME` build flag (default 1): with 0 the virtual render, path tree, resolver and matcher compile out and the state exports vanish. cdom-hand 17508 (8556 gz) -> 10051 (5257 gz); cdom-tpl 18837 (8952 gz) -> 11133 (5590 gz). Smoke and listfuzz pass both ways; tests/resume_flag.sh pins it. Defaults unchanged: Alex's decision.
- The update "regression" was mostly harness noise (earlier pre-resume runs already had 0.9-1.2 ms medians; means 0.96 vs 0.97). The real part was about 1.3 us per click from the walk's per-step comment check; the walk now checks comments only on a resumed page. Shim 213 -> 214 lines, 2402 gz.
- cdtc today (for the signals step): per-binding caches in `<tpl>_view` / `page_v` (class bitmask `cN`, attr `aN_i`, text `tN`, if branch, refs, nested `listN`); patch recomputes and compares every binding and emits only differences; class strings are a static 2^k table per element interned lazily; `page_render` re-evaluates every page binding and `{#if}`, rebuilds every list's keys and calls cd_list_update, whose patch runs for every surviving item, so each event is O(n) compares (that is swap's extra 0.3 ms). Dispatch: cd_tag = slot << 24 | view index; handlers `void h(State *S, [Item *it,] const cd_event_rec *e)`; the app calls page_dispatch then page_render.
