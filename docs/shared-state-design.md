# Shared state: views that follow data owned somewhere else

Status: design, written 2026-11-18, not approved; nothing in `src/` or `shim/` is changed by it.
Branch: `shared-state` (worktree `/Users/alex/Dev/cdom-shared-state`), from main at cfee569.
The prototypes and the benchmark are under `bench/shared-state/`.

This file is written in two steps.
The predictions below were committed before anything was measured.
The candidates, the edge cases, the measurements and the recommendation follow them.

## Predictions

Written 2026-11-18, before any run.
Machine: this Mac, headless Chrome, no CPU throttle, script time of one entry into C (an exported function that ends in `cd_flush`), median of the samples, with the quartiles.
"Total" adds a forced layout (`document.body.offsetHeight`) after the entry.

### The entries

One benchmark app, built five ways.
The DOM they build is the same.

| Entry | What it is |
| --- | --- |
| `A` | re-run every live view: one list of live views, any setter asks for all of them, each view formats every time and writes through a `cd_bound` |
| `B` | the owner module keeps a watcher table per field and calls `cd_dirty` for each watcher (the `b-module` pattern, its table grown to fit) |
| `C` | versions: one list of live views as in `A`, each view compares the version it drew with the field's version and returns when they are equal |
| `E` | the `cd_state` prototype (`bench/shared-state/lib`): a version and an intrusive list of watchers per state, the changed states queued, the views found in the flush |
| `L` | `E`, but a list's rows are not views: the list is one view, and its render loops over the rows comparing each row's revision (the row's own counter plus the versions of the shared state it shows) |

In the row scenarios `A`, `B`, `C` and `E` make every row a view of its own (10,000 live views), and `L` makes the list one view.
`L`'s loop is a stand-in for the unimplemented `cd_list_row` pass of `docs/list-design.md`: one call and two compares a row.

### The hypothesis under test

The earlier recommendation was `C`.
The hypothesis written here, to be refuted or confirmed:

1. Up to about 1,000 live views, time does not choose between `A`, `C`, `E` and `L`: every one of them is under 1% of a 16.6 ms frame per change, so the choice is made on code, failure modes and memory.
2. At 10,000 rows, rows must not be views.
   `L` is the only shape whose bookkeeping is zero bytes a row, and it is as fast as `C` and `E`.
3. `B` as written is quadratic through `cd_dirty`'s dedup scan and through its slot search, and is unusable at 10,000 watchers.
4. `A` stops being free when a view's value is large (1 MB) or the views are many (10,000 and up).
5. So the design to recommend is `E` for views plus `L` for rows, with `C`'s version as the thing a row's revision and a memo are made of.

### The numbers

| # | Scenario | Predicted | Built on | Refuted by |
| --- | --- | --- | --- | --- |
| S1 | 10,000 rows each show a price in one shared currency; the currency changes (10,000 text writes) | `L` 3 to 6 ms of script; `A`, `C`, `E` within 1.15 of `L`; `B` 30 to 100 ms more than `L` | a formatted text write is about 0.3 to 0.6 us; `cd_dirty` scans the queue: 10,000 distinct asks are 50 million compares | `A`, `C` or `E` over 1.3 of `L`; `B` under 2 times `L` |
| S2 | 10,000 rows, the selected id changes (2 class writes) | `L` 0.02 to 0.08 ms; `C` 0.03 to 0.10 ms; `E` 0.05 to 0.20 ms; `A` 0.5 to 1.5 ms; `B` 30 to 100 ms | a call and a compare are 2 to 5 ns; a format and a `cd_bound_set_bytes` that matches is 50 to 150 ns | `L` over 0.15 ms; `A` under 3 times `C` (then the version check buys nothing here) |
| S3 | one row's own field changes, in 10,000 and in 100,000 rows (1 text write) | 10,000: `L` 0.02 to 0.08 ms, `C` 0.03 to 0.10 ms, `A` 0.5 to 1.5 ms; 100,000: `L` 0.2 to 0.8 ms, `C` 0.3 to 1.0 ms, `A` 5 to 15 ms. `B` and `E` with one state per row: under 0.01 ms at both sizes | as S2; `B` and `E` call one watcher | `L` at 100,000 over 1.5 ms; `A` at 100,000 under 3 ms |
| S4 | 1,000 views in nested regions, 100 fields, 10 views a field; one field is set per entry (10 text writes) | `A` 0.05 to 0.15 ms; `C` 0.003 to 0.012 ms; `B`, `E` 0.002 to 0.008 ms | as S2 | any entry over 0.5 ms (hypothesis 1 is then wrong) |
| S5 | the same 1,000 views, all 100 fields set in one entry (1,000 text writes) | `A`, `C`, `E` 0.3 to 0.7 ms and within 1.15 of each other; `B` 0.3 to 1.2 ms more | 1,000 distinct `cd_dirty` asks are 0.5 million compares | `B` within 1.1 of `E` |
| S6 | 10,000 rows live; one setter of the state every row shows is called 1,000 times in one entry (a bulk import), then the flush | `L`, `C`, `E`: S1's time plus under 0.1 ms; `A` the same; `B`: over 30 ms a setter call, so it is run with 10 calls and must take over 300 ms | `cd_changed`, and `A`'s and `C`'s single `cd_dirty`, are O(1) when already asked; `B` asks 10,000 watchers per call, each a scan | `E` over S1 plus 0.5 ms; `B` at 10 calls under 100 ms |
| S7 | build 10,000 rows, then take them away, with each row a view (`A`, `B`, `C`, `E`) or not (`L`) | build: `A`, `C`, `E` add 0.03 to 0.25 us a row to `L`; `B` adds 30 to 80 ms in all (slot search). Memory a row beyond the model: `L` 0; `E` 32 bytes (the intrusive watcher); `C` 4 to 12 (the seen version and the list slot); `A` 60 to 100 (a `cd_bound`, its copy of the bytes, the heap's rounding) | sizes of the structs; `cd_bound` is 36 bytes and keeps the bytes | `E` over 48 bytes a row; `A` under 50 |
| S8 | the URL case: 10,000 result rows on screen, one keystroke in the URL field (the draft changes, nothing shown does) | `E`, `L`, `B`: under 0.005 ms (nothing watches the draft); `C` 0.03 to 0.10 ms with rows as views, under 0.005 with the list as one view; `A` 0.5 to 1.5 ms with rows as views | as S2 | `A` under 0.2 ms |
| S9 | the URL case: a second load replaces 10,000 rows with 10,000 others. (a) reset: one clear, then build; (b) keyed diff with the keys of load 1 reused, every row patched; (c) reset, with each row a watcher (`E`), against (a) | (a) 20 to 40 ms of script; (b) 3 to 7 ms of script, total 0.3 to 0.7 of (a)'s; (c) within 1.10 of (a) | jfb 07 create 10,000 is 19.9 ms of script on this Mac; S1 | (b) slower than (a); (c) over 1.2 of (a) |
| S10 | a 1 MB textarea mirrored in a text node elsewhere; one keystroke | `E` 1 to 5 ms of script; `A` within 1.15 of `E`, and 1 MB more wasm memory (16 pages) for the `cd_bound`'s copy | the read and the write each cross 1 MB of UTF-8; a `memcmp` and a `memcpy` of 1 MB are about 0.05 ms each | `A` over 1.3 of `E`; `A`'s memory under 0.9 MB more |
| S11 | the same mirror live, and an unrelated field changes | `A` 0.05 to 0.3 ms (it copies and compares 1 MB); `C` and `E` under 0.005 ms | as S10 | `A` under 0.02 ms |
| S12 | `cd_dirty` itself: n distinct (fn, ctx) asked in one entry, each an empty function | 1,000: 0.3 to 1.0 ms; 10,000: 30 to 100 ms; the ratio between 60 and 140 | the scan is n squared over two | a ratio under 30 |
| S13 | `L`'s loop alone over rows that did not change | 10,000: 0.02 to 0.08 ms; 100,000: 0.2 to 0.8 ms | list-design's L12 | over 0.12 ms at 10,000 |
| S14 | a text input that watches its own field. (a) the view writes the value back unguarded, the same value; (b) the same while an IME composition is open; (c) the setter changes the value (upper case) | (a) the caret stays where it was; (b) the composition survives (I give this 70%); (c) the caret jumps to the end | the HTML standard moves the caret only when the value changes | (a) the caret moves; (b) the composition is cancelled: then the guard is required, not a nicety |

The hypothesis is refuted as a whole if `C` or `A` beats `E` and `L` by more than the spread anywhere that matters (S1 to S9), or if `L` is not within 1.2 of the best entry in S1 to S3.
