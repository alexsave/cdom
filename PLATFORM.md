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
