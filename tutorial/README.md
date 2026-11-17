# The cdom tutorial

Seven small apps, each a lesson, each one C file that builds into one HTML page.
Every lesson puts the same app in React next to it, so you can read cdom in terms you already know.

| Lesson | What it teaches | The React it replaces |
| --- | --- | --- |
| [1. Hello](01-hello/) | building a page: `cd_open`, `cd_text`, `cd_attr`, interned strings | JSX |
| [2. Counter](02-counter/) | state in C variables, ids, `cd_on` handlers, updating one node | `useState`, `onClick` |
| [3. Input](03-input/) | reading a text field, derived state, one-off strings | a controlled `<input>` |
| [4. Show and hide](04-regions/) | regions: parts of the page that come and go | `{open && <Panel/>}` |
| [5. Todo list](05-todo/) | the keyed list: `cd_list_update` with create, patch and destroy | `items.map(i => <li key={i.id}>)` |
| [6. Stopwatch](06-stopwatch/) | time: frames, timers and the clock | `useEffect`, `setInterval`, cleanup |
| [7. Particles](07-particles/) | pixels from C to a canvas, with a seeded random generator | `useRef`, `useEffect`, a canvas loop |

## Running a lesson

You need a clang that targets wasm32 (on macOS, `brew install llvm lld`; on Debian or Ubuntu, `apt install clang lld` and `make WASM_CC=clang`).
No Node, npm or bundler is involved in building an app.

```sh
make -C tutorial/02-counter
open tutorial/02-counter/build/index.html     # one file; it opens from disk
```

Each lesson's directory is a complete app, laid out as `make new` lays one out: a `Makefile` that includes `cdom.mk`, and `src/app.c`.
Copy one to start your own, and set `CDOM` in its Makefile to where cdom is.

## How cdom differs from React, in one page

- **The app is C, and runs once.** `cd_main` builds the page and returns.
  After that, C runs only when the page calls in: an event, a timer, a frame, a network answer.
  There is no component function that runs again on every change.
- **There is no virtual DOM.** React finds what changed by building a new tree and diffing it against the old one.
  A cdom handler already knows what it changed, so it updates those nodes, by id, and nothing else.
  The one place that needs a diff, a list whose items come and go and move, has one: `cd_list_update` (lesson 5).
- **Nodes are named by ids,** small numbers C hands out (`cd_id_new`) and binds to a node.
  An id is an index into the shim's table, so `cd_set_text_node(id, ...)` costs one array lookup on the JavaScript side.
- **State is plain data.** A counter is an `int`, a todo list is an array of structs.
  No hooks, no rules about where state can be read, no stale closures.
- **Strings are interned.** A string crosses into JavaScript once and is named by a number after that.
  `CD_LIT("...")` interns a literal once per call site, and `cd_tmp` sends a one-off value (a typed name, a time) without keeping it.
- **One listener per event type.** The shim listens on the mount and passes each event to C, which routes it to the handler `cd_on` registered for that node.
- **The page is one file.** The shim and the module are inlined into `index.html`, so it opens from disk, and the page and its code always come from the same build.

The numbers (speed, startup, memory and bytes against React and vanilla JS) are in the [main README](../README.md#why-cdom) and [RESULTS.md](../RESULTS.md).

## Testing

`tutorial/run.sh` builds every lesson and runs [`test.mjs`](test.mjs), which opens each page from disk in headless Chrome, clicks and types into it with the browser's own input events, and checks what the page shows.
It fails on any page error, and on anything a page fetches.

```sh
tutorial/run.sh                         # every lesson
tutorial/run.sh tutorial/05-todo        # one
SHOTS=tutorial/out tutorial/run.sh      # also a screenshot of each lesson's final state
```

The test needs Node and Chrome; the lessons themselves need neither.
