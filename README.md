# cdom

cdom lets an app written in C own the DOM.
C holds the tree, the state and the keyed diff in WebAssembly, and a fixed JavaScript shim (`shim/cdom.js`) does one import per DOM operation and posts events back.
A browser without WebAssembly runs the same app through wasm2js on an ES5 shim (see RESULTS.md, "Without WebAssembly").

- The API is `src/cdom.h`, with the network in `src/cdom_net.h`, the store in `src/cdom_store.h`, and the other platform modules beside them.
- A program's libc comes from cdom: `src/libc/string.h` (add `-isystem cdom/src/libc`) and `src/mini.c`.
- Measurements, design notes and how to reproduce them are in RESULTS.md, PERF_LOG.md and BRIDGE.md.
- Tests: `tests/build.sh`, then `node tools/unit.mjs`, `node tools/fuzz.mjs`, `node tools/canvas.mjs`, `node tools/host.mjs`, `tests/link/run.sh` and `node tools/legacy.mjs`.
- The I/O calls external adapters build on (the `_cb` forms, the socket calls, `cd_timer_stop`, `cd_epoch_ms`, `cd_now` and the program's libc) are a stable contract, written at the top of `src/cdom.h`: they change only additively, and `tests/link/contract.c` fails to compile if one changes in place.
- cdom and its sibling C library have zero edges, neither depends on the other: `tools/edges.mjs` (run by `tests/link/run.sh`) fails if a tracked file other than the docs (`*.md`, LICENSE) names the C library it once sat beside.

## License

MIT, copyright Alex Saveliev. See LICENSE.
