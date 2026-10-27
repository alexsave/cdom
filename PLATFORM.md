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
