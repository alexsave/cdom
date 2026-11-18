# Getting rid of CD_LIT in cdom apps (parked, 2026-11-10)

`CD_LIT("p")` written everywhere is a chore (amgi alone has 670 CD_LIT calls, 57 of them `CD_LIT("div")`).
Two ways to write `cd_open("p")` instead were debated.
Neither is scheduled; both are parked.

## What CD_LIT does today

`src/cdom.h:103`: `#define CD_LIT(lit) ({ static cd_str cd_lit_slot_; cd_lit_(&cd_lit_slot_, "" lit, sizeof(lit) - 1); })`.
It interns the literal once, on its first use, into a 4-byte id (`cd_str`) cached in a static slot.
The shim keeps the matching string table, so after the first use only the id crosses from wasm to JS.
`"" lit` makes anything but a string literal a compile error.

## Option A: same-name wrapper macros (no build step)

```c
#define cd_open(tag)  cd_open_s(CD_LIT(tag))
#define cd_text(s)    cd_text_s(CD_LIT(s))
#define cd_class(c)   cd_class_s(CD_LIT(c))
```

- The source becomes `cd_open("p")`, and the compiled code is byte-identical to today.
- The ~30 cdom entry points that take a `cd_str` each get an `_s` twin for runtime strings.
- An app's own helpers need their own macro too (amgi's `el`, `el_id`, `el_text`, `icon`, `icon_with` in `ui/src/dom.c`).
- A non-literal argument stays a compile error.
- A macro can't be taken as a function pointer.
- Migration: a mechanical rename across amgi's 670 calls.

## Option B: litgen, a build step that replaces each literal with its id

The prototype was built and works. It is in `tools/litgen/`: `litgen.c`, the counter app's `Makefile` (as `counter.Makefile`) and its `app.c` (as `counter_app.c`).

- `litgen.c` (250 lines of C) reads every source of the app at once.
  It finds every parameter declared `[const] cd_str` in the headers it is given (cdom.h plus the app's own, so amgi's `el()` is caught), de-duplicates the literals, and gives them stable ids 1..N, sorted by their bytes.
  It writes rewritten copies to `build/gen/`, so `cd_open("p")` becomes `cd_open(((cd_str)4))`, with `#line` keeping the line numbers.
- It emits `litgen_table.c`, a byte blob plus lengths, and `cd_lits_init()`.
  cdom gains `cd_lits_register()` (10 lines): it interns the N strings first, in order, so runtime `cd_intern` starts at N+1.
- The shim is unchanged: it still fills its table lazily, the first time an id is used.
- About 11 extra Makefile lines: the litgen rule with a stamp file, `APP_SRC` pointing at `build/gen`, and an `-iquote` per source directory.
- A call site litgen misses is a compile error (`-Wint-conversion`), never wrong at runtime.

Measured:

| | CD_LIT | litgen |
|---|---|---|
| counter wasm | 4,515 B | 4,538 B (+23) |
| amgi `decks.c` object, -Oz (80 sites, 58 unique strings) | 6,100 B | 3,608 B (-2,492, about 31 B a site) |
| runtime per use | a load and a branch, plus one intern the first time | none (N interns once at start) |

Across all of amgi this is an estimated 10-30 KB.

Costs, and how fixable each one is:

1. clangd flags every `cd_open("p")` as an int conversion error.
   Fixable: give the editor `const char *` declarations (under `#ifdef __clangd__`), so it sees valid code while the real build sees ids.
2. The debugger shows `cd_open(4)` instead of the string.
   Fixable: litgen writes `/* "p" */` next to each id, or a map file.
3. An extra build step and a generated directory; a source change regenerates every file, and two sources with the same name in different directories collide.
4. An error on a rewritten line shows the generated text, and its column shifts. The line number stays right.

With 1 and 2 fixed, B is about as pleasant as A and leaner at runtime.
To get startup to 0 interns as well, litgen would also emit the prebuilt intern tables (`str_ent`, the pool, the hash).
