# Strings: plain literals in C, a table baked into the page

Status: design, revised 2026-11-17 with the Chrome measurements in [strings-bench.md](strings-bench.md); approved by Alex, not implemented.
Branch: `strings` (from `tutorial`).
The first version of this file (e35056c) is what strings-bench.md measured; "What the measurements changed" below lists every difference.

## The problem

An app writes `#define L(s) CD_LIT(s)` and then `cd_open(L("div")); cd_text(L("Hello"));`.
Every string needs a wrapper, and the author has to choose between `CD_LIT`, `cd_intern`, `cd_internz`, `cd_tmp` and `cd_tmpz`.
At run time each literal is hashed and copied into a pool in C, copied again into a batch buffer, and decoded by the shim.

Alex's direction: the author writes a plain string in C, a build step turns each literal into a string id in the final wasm, and the table of literal strings is baked into `index.html`, so the shim never decodes a literal at run time.

## The design in one page

The author writes `cd_open("div"); cd_text("Hello");`.

1. **In the header.**
   Every cdom function that takes a string is also a macro of the same name that passes each string argument through `cd_s()`.
   `cd_s(x)` decides at compile time what `x` is.
   A string literal becomes a 4-byte slot in a data section named `cd_lits`, and the value passed on is the slot's address.
   The slot points at the literal's source spelling (`#x`, the preprocessor's stringification), which goes in a second section, `cd_chars`.
   Anything else of type `char *` becomes a runtime string: `cd_tmpz(x)`.
   A `cd_str` passes through unchanged.
2. **At link time.**
   wasm-ld gathers every slot into one contiguous range, `__start_cd_lits` to `__stop_cd_lits`, and drops the slots of functions that are never called.
3. **After the link, `tools/cdlits` (C, run by `cdom.mk`).**
   It reads the slots from the linked module, turns each spelling into its bytes with a small C string-literal lexer, removes both sections from the module, and writes the table as JavaScript text: one entry per slot, in slot order.
   The final wasm holds no byte of any literal that went to cdom: only `i32.const <slot address>` at each call site.
4. **In the page.**
   `tools/page.awk` writes the table in a script of its own, before the script that holds the shim and the module, so the text comes before the base64.
5. **In the shim.**
   `lits[(s - base) >> 2]` is the string.
   A literal is never decoded, never copied and never crosses the bridge.
   A runtime string is read from C memory by the operation that uses it: no copy in C, no extra crossing.
   A burst of fresh runtime strings that sit together in memory can be announced with `cd_tmp_ahead`, and is then decoded in one call and cut up, with nothing kept after the current call into C returns.

The intern table, the hash, the pool, the batch buffers and the `strings` import are deleted.

## What the measurements changed

strings-bench.md measured the first version in headless Chrome 157 and refuted three of its predictions and two of its timings.
The design's shape held (slots, the table, read on use); these parts changed:

| # | Was | Is now | The number behind it |
| --- | --- | --- | --- |
| 1 | runtime read: a char loop up to 12 bytes, `TextDecoder` above | an ASCII test, then `String.fromCharCode` with the bytes as arguments up to 32 bytes; `TextDecoder` above 32 bytes and for any non-ASCII string | 9 to 30 ASCII bytes read in 48 to 95 ns against 80 to 146; a `TextDecoder` call costs 100 to 140 ns in Chrome whatever the length, not Node's 84 to 90 |
| 2 | stale view found by `buffer !== memory.buffer` | by `u8.length === 0` | the getter is 5.7% of a short read's profile, 4 to 7 ns a string |
| 3 | the table in the `start` call, after the base64 | in its own script before the module | after the base64 the synthetic page is 5.9% larger than today; before it, 6.2% smaller (2,400 bytes of brotli apart) |
| 4 | create 10k script predicted 0.0 to 1.0 ms worse | predicted from the measured 0.8 ms (P5 below) | measured 2.8 ms worse with the old read, 0.8 ms with the new one |
| 5 | unique strings and a per-slot map | one entry per slot, repeats spelled again, no map | measured for this revision: 438 bytes of brotli less on amgi's 671 slots (table below) |
| 6 | every runtime string read on use | read on use, plus `cd_tmp_ahead` for a burst | today's batch decodes fresh short non-ASCII labels at 64 to 73 ns each against 153 to 189 for any read on use |

## Evidence

From the first prototype (clang 23.1.5, wasm-ld, 2026-11-17), still true:

| Question | Result |
| --- | --- |
| Does the macro compile and classify at `-O0`, `-Oz` and `-Oz -flto`? | Yes, and the three builds produce the same 10 slots and the same 34 operations with the same strings. |
| Is a literal classified in the front end, the same way at every optimization level? | Yes: `__builtin_choose_expr` needs a constant, so the choice cannot depend on inlining. A literal passed through an inlined helper is a runtime string at `-O0` and at `-Oz`. |
| Does wasm-ld give `__start_cd_lits` and `__stop_cd_lits`, and export them with `--export-if-defined`? | Yes, as immutable i32 globals. |
| Are slots of unreferenced functions dropped? | Yes: `--gc-sections` removed both literals of a function nothing called. |
| Does a mutable static buffer get cached by mistake? | No: `static char mut[8] = "mut"` is spelled `mut`, not `"..."`, so it is a runtime string, and the page showed `nut` after C changed it. |
| Does the stripped module validate and run? | Yes (`wasm-validate`, and run in Node): 1,028 bytes became 719, with no literal byte left in it. |
| Does the tool's lexer agree with clang? | Yes on the torture literal (simple escapes, `\x41`, `\102`, `−`, `\U0001F600`, raw UTF-8, `\e`, `\?`, `"\x1" "2"`, a macro that expands to a literal, the empty literal), checked against the compiler's own bytes in the check build described below. |
| Is the macro's argument evaluated once? | Yes: `cd_text(next())` and `cd_text(*p++)` each make one call and one increment. |
| Is a non-string a compile error? | Yes: `cd_text(id)` with a `uint32_t` fails with "incompatible integer to pointer conversion". |

From strings-bench.md (Chrome 157; A is today, B the first version of this design, Bx is B with the read of change 1 and the check of change 2):

| What | A, today | B | Bx |
| --- | ---: | ---: | ---: |
| A literal, per call, resolved only | 9.3 ns | 6.8 ns | 6.8 ns |
| A literal through a `Map` keyed by address | | 9.4 ns | |
| A literal with dense ids, `lits[id]` | | 7.1 ns | |
| Runtime string, 9 ASCII bytes | 125 ns (`cd_tmp`) | 80 ns | 48 ns |
| Runtime string, 30 ASCII bytes | 223 ns | 144 ns | 94 ns |
| Runtime string, 200 ASCII bytes | 339 ns | 156 to 172 ns | 153 to 162 ns |
| Runtime string, 9 to 13 bytes of UTF-8 | 220 to 241 ns | 172 to 255 ns | 163 to 189 ns |
| The same, 1,024 fresh ones interned and then shown (today's batch decode) | 63 to 110 ns | | |
| `cd_main` plus table install, 2,000 literal sites | 2.93 ms | 1.81 ms | |
| Table install, hello and the keyed entry | | 0.065 to 0.07 ms | |
| Initial wasm pages, hello / keyed | 6 / 8 | 2 / 3 | |
| Touched wasm memory after five create-and-clear cycles | 408 KB | 116 KB | |
| Run-and-clear memory (harness) | 0.85 MB | 0.72 MB | 0.78 MB (one iteration; the same module as B) |
| create 10k, script | 20.4 ms | 23.2 ms | 21.2 ms |
| create 1k, script | 2.8 ms | 2.9 ms | 2.6 ms |
| create 1k without templates, script | 4.5 ms | 5.2 ms | not run |
| Every benchmark's total, against A | 1.000 | 0.994 to 1.022 | 0.991 to 1.016 |
| tutorial/01-hello page, brotli | 8,848 | 8,597 | |
| Synthetic page, 2,000 sites, brotli, table before the module | 20,573 | 19,299 | |

Measured for this revision (2026-11-17): how to carry the table.
The literal call sites of three sources, in source order, as JavaScript text, brotli -q 11, counted as what the table adds to a page that already holds the shim:

| Source | Slots | Unique | Unique strings and a map (first version) | One entry per slot, repeats spelled again (chosen) | The same with non-ASCII entries in a second string |
| --- | ---: | ---: | ---: | ---: | ---: |
| amgi `ui/src/*.c` | 671 | 339 | 3,283 | 2,845 | 2,931 |
| tests/unit.c | 180 | 92 | 666 | 525 | 532 |
| tutorial/04-regions | 65 | 41 | 858 | 786 | 776 |
| tutorial/05-todo | 60 | 45 | 834 | 773 | 783 |

Spelling repeats again is 61 to 438 bytes smaller than the map in every case: 0.65 bytes per slot on amgi, half of what strings-bench.md measured for dense ids (1 to 1.5 bytes per slot, T4), with no rewrite of the module's code.

Code size (clang `-Oz`, object code section): seven literal arguments cost 193 bytes with `CD_LIT` today and 94 bytes as slots, about 14 bytes less per literal.
strings-bench.md measured the whole effect on the module: hello's 3,981 bytes lose 1,553.

## How a literal is told from a runtime string

The rule is the compiler's, not the documentation's.

```c
#define CD_LITQ(s) (__builtin_strncmp(CD_SPELL(s), "\"", 1) == 0 && \
                    __builtin_constant_p(__builtin_strlen(CD_CHARP(s))))
```

An argument is a literal when both hold:

- its spelling after macro expansion starts with `"` (so it is written as a literal, or is a macro that expands to one);
- the compiler can fold `strlen` of it (so every byte up to the terminator is a compile-time constant).

Everything else is a runtime string and is read from memory each time it is used.

| What the author passes | Classified as | What happens |
| --- | --- | --- |
| `"div"`, `"a" "b"`, `GREETING` where `#define GREETING "hi"` | literal | a slot; in the page's table; never decoded |
| a stack buffer, a `const char *` parameter, `names[i]` | runtime | read from C memory by the op that uses it |
| `static char buf[8]`, changed later | runtime | read each time, so it is never stale |
| `static const char NAME[] = "x"` | runtime | correct, read each time (its spelling is `NAME`) |
| `sel ? "danger" : ""` | runtime | correct, read each time; `sel ? cd_s("danger") : cd_s("")` makes both literals |
| a literal passed through a helper `label(const char *s)` | runtime | correct, read each time, at every optimization level |
| a `cd_str` (from `cd_s`, `cd_tmp`, `cd_tmpz`) | itself | passed through |
| an integer, a node id, a struct | | compile error |
| `"abc" + 1`, `L"wide"` and other spellings that start with `"` but are not only literals | literal by the macro | `cdlits` stops the build: "not a string literal: ..., write the literal itself or pass cd_tmpz(...)" |

There is no way to pass a runtime buffer "where a literal is expected": the same parameter takes both, and the wrong classification cannot happen silently.
The worst outcome of writing a string in an unusual way is a string that is read on each use (26 to 190 ns for up to 30 bytes, measured) where a literal would have cost 6.8 ns.

Why not `__builtin_constant_p` alone: the address of a mutable `static char buf[]` is a constant, and after optimization the compiler may or may not prove its bytes constant, so the answer could change with the optimization level.
The spelling test is purely textual and the `strlen` fold is evaluated in the front end (it sits in `__builtin_choose_expr`, which requires a constant), so the classification is the same at `-O0`, `-Oz` and under LTO.

Why the spelling and not the literal itself goes in the section: `static const char c[] = s;` only compiles when `s` is a literal, and both branches of `__builtin_choose_expr` and of `_Generic` are type-checked, so that line would make every runtime argument a compile error (tried: "array initializer must be an initializer list or string literal").
`#s` is a valid initializer whatever `s` is, and the unchosen branch emits nothing.
The price is that `cdlits` must turn a spelling into bytes exactly as clang does; the test plan has a differential check for that, against clang's own bytes.

## The API as it appears in cdom.h

```c
/* ---- strings ----
 * A string argument is written as plain C: cd_open("div"), cd_text(name), cd_text(cd_tmp(buf, n)).
 * Every function below that takes a cd_str is also a macro of the same name that passes each string
 * argument through cd_s(), so the functions are never called with a cd_str made by hand.
 *
 * A string literal costs nothing at run time: the build (tools/cdlits, run by cdom.mk) moves it out of
 * the module into the page's string table, and the argument is its slot. A literal is an argument spelled
 * as one ("...", adjacent literals, or a macro that expands to them).
 * Any other char pointer is a runtime string: its bytes (UTF-8, to the terminating 0) are read from C
 * memory by the call that uses it. Nothing is kept, so a string may change between calls.
 * cd_tmp is the same for bytes with a length and no terminator.
 *
 * A cd_str from a literal is good for the life of the page. A cd_str from a runtime string (cd_tmp,
 * cd_tmpz, or cd_s of a non-literal) is good for the next call that takes a string: that call reads the
 * bytes, and when it returns every such cd_str is dead (using one later throws out of the call that
 * uses it). The bytes must stay unchanged until that call. At most CD_TMP_CAP are alive at once. */
typedef struct { uint32_t v; } cd_str;  /* a slot address, or 0x80000000 | a serial; never made by hand */
#define CD_NONE ((cd_str){0})          /* no string: the path that reads the object itself */
#define CD_TMP_CAP 32
cd_str cd_tmp(const char *s, uint32_t len);
cd_str cd_tmpz(const char *s);          /* traps on a null pointer */

/* A burst of fresh runtime strings (a list's labels, built into one buffer and about to be shown):
 * cd_tmp_ahead(p, len) says the bytes [p, p + len) will not change until the range ends. The shim
 * decodes the range once, now, and until it ends a runtime string that lies wholly inside it is cut
 * from that decode instead of being read on its own; any other runtime string is read as always.
 * The range ends, and the shim drops the decoded text, when the outermost call into C returns
 * (cd_main, an event, a timer, a frame, a network answer), at the next cd_tmp_ahead, or at
 * cd_tmp_ahead(0, 0). It changes no result, only the time: use it for a few hundred strings or more.
 * Changing a byte inside a live range is an error the shim reports only under start's
 * options.checkAhead (the tests set it): otherwise the page shows the old text. */
void cd_tmp_ahead(const char *p, uint32_t len);

/* cd_s(x): the cd_str of x. Call it yourself only to keep a literal in a variable or to choose between
 * literals: cd_set_class(id, sel ? cd_s("danger") : cd_s("")). It is a statement expression, so it
 * works inside a function, not in a file-scope initializer. */
#define CD_SPELL_(s) #s
#define CD_SPELL(s) CD_SPELL_(s)
#define CD_CHARP(s) _Generic((s), cd_str: (const char *)0, default: (s))
#define CD_ASSTR(s) _Generic((s), cd_str: (s), default: (cd_str){0})
#define CD_LITQ(s) (__builtin_strncmp(CD_SPELL(s), "\"", 1) == 0 && \
                    __builtin_constant_p(__builtin_strlen(CD_CHARP(s))))
#ifdef CD_LITS_CHECK /* the check build: each slot also holds the compiler's own bytes (cdlits --check) */
typedef struct { const char *spell, *real; } cd_lit_slot;
#define CD_REAL(s) , (__builtin_constant_p(__builtin_strlen(CD_CHARP(s))) ? CD_CHARP(s) : 0)
#else
typedef struct { const char *spell; } cd_lit_slot;
#define CD_REAL(s)
#endif
#define CD_LIT_(s) ({ \
  static const char cd_c_[] __attribute__((section("cd_chars"), aligned(1))) = CD_SPELL(s); \
  static const cd_lit_slot cd_l_ __attribute__((section("cd_lits"))) = { cd_c_ CD_REAL(s) }; \
  (cd_str){(uint32_t)(uintptr_t)&cd_l_}; })
#define cd_s(s) _Generic((s), cd_str: CD_ASSTR(s), \
  default: __builtin_choose_expr(CD_LITQ(s), CD_LIT_(s), cd_tmpz(CD_CHARP(s))))

void cd_open(cd_str tag);
void cd_text(cd_str s);
void cd_attr(cd_str name, cd_str value);
/* ... every declaration as today, then, after all of them: */
#define cd_open(tag) (cd_open)(cd_s(tag))
#define cd_text(s) (cd_text)(cd_s(s))
#define cd_attr(name, value) (cd_attr)(cd_s(name), cd_s(value))
#define cd_class(value) (cd_class)(cd_s(value))
#define cd_open_id(tag) (cd_open_id)(cd_s(tag))
#define cd_value(s) (cd_value)(cd_s(s))
#define cd_tpl_text(t, k, s) (cd_tpl_text)(t, k, cd_s(s))
#define cd_set_text_node(id, s) (cd_set_text_node)(id, cd_s(s))
#define cd_set_attr(id, name, value) (cd_set_attr)(id, cd_s(name), cd_s(value))
#define cd_set_class(id, value) (cd_set_class)(id, cd_s(value))
#define cd_set_style(id, prop, value) (cd_set_style)(id, cd_s(prop), cd_s(value))
#define cd_set_style_num(id, prop, v, unit) (cd_set_style_num)(id, cd_s(prop), v, unit)
#define cd_encode_image(px, w, h, n, mime, token) (cd_encode_image)(px, w, h, n, cd_s(mime), token)
#define cd_encode_image_in(px, w, h, n, W, H, x, y, mime, token) \
  (cd_encode_image_in)(px, w, h, n, W, H, x, y, cd_s(mime), token)
#define cd_set_image_bytes(id, prop, p, len, mime, token) \
  (cd_set_image_bytes)(id, cd_s(prop), p, len, cd_s(mime), token)
#define cd_v_str(s) (cd_v_str)(cd_s(s))
#define cd_set(id, path, v) (cd_set)(id, cd_s(path), v)
#define cd_set_num(id, path, n) (cd_set_num)(id, cd_s(path), n)
#define cd_set_str(id, path, s) (cd_set_str)(id, cd_s(path), cd_s(s))
#define cd_set_bool(id, path, b) (cd_set_bool)(id, cd_s(path), b)
#define cd_call_args(id, method, n, a) (cd_call_args)(id, cd_s(method), n, a)
#define cd_call(id, method) (cd_call)(id, cd_s(method))
#define cd_get_num(id, path) (cd_get_num)(id, cd_s(path))
#define cd_get_str(id, path, dst, cap) (cd_get_str)(id, cd_s(path), dst, cap)
#define cd_get_id(id, path) (cd_get_id)(id, cd_s(path))
```

Notes on the API:

- The macro has the function's name, so `cd_text("x")` is what the author writes and reads, and `(cd_text)` (in parentheses) is the function, for a function pointer.
- `cd_str` becomes a struct, so a `cd_id` passed as a string, a `cd_str` stored in a `uint32_t`, and `a != b` on two strings are compile errors.
  Today all three compile and the last one silently depends on interning.
- Path 0 ("read the object itself") becomes `CD_NONE`: `cd_get_num(CD_RESULT, CD_NONE)`.
  A literal `0` there compiles (it is a null pointer) and traps in `cd_tmpz`, so a missed call site fails loudly on first use.
- A table of names is a `const char *` table read at run time, or a `cd_str` table filled inside a function with `cd_s("...")`.
- `cd_fmt` and `cd_fmt_len` are unchanged: they write bytes and return a length, which goes to `cd_tmp(buf, n)`.
- The optional modules (`cdom_net.h`, `cdom_store.h`, `cdom_url.h`, ...) take `(pointer, length)` already and do not change.
  The I/O contract (`tests/link/contract.c`) names no string function, so it is not touched.

### What is deleted

| Deleted | Replaced by |
| --- | --- |
| `CD_LIT(lit)`, `cd_lit_`, and every app's `#define L(s) CD_LIT(s)` | the literal itself |
| `cd_intern(s, len)` | `cd_tmp(s, len)` at each use; the app keeps its own bytes |
| `cd_internz(s)` | `s` |
| `cd_bytes(cd_str, &len)` | the app's own bytes |
| `cd_str_count()`, `cd_str_bytes()` | nothing: there is no table in C |
| `CDOM_INTERN_TEXT` and the `cdom-pertext` benchmark entry | nothing: a literal is always in the table, a runtime string is always read from memory |
| in `cdom.c`: `pool`, `strs`, `sent`, `htab`, `hash_bytes`, `htab_put`, `utf16_len`, `push_raw`, `push_str`, `send_strings`, `sbytes`, `smeta`, `str_meta`, `ref_name`, `ref_text`, `tmp_check`, `svg_tag`, `svg_depth`, `unit_ids`, the `strings` and `open_svg` imports | a 264-byte record block for runtime strings, and the `ahead` import |
| in the shim: `interned`, `transient`, `keep`, `decode`, `strings`, `stringOf`, `open_svg`, `SMALL` | `lits`, `str`, `readString`, `ascii`, `ahead` |
| `tests/link/lit_ok.c`, `tests/link/lit_var.c` | `tests/lits/` (below) |
| `micro/svg/` | a dated entry in PERF_LOG.md (the namespace check moves to the shim, below) |

`cd_tmp` and `cd_tmpz` keep their names and arguments: the name says how long the result lives, and the existing callers (10 in amgi) stay as they are.

## Runtime strings: read on use, and the batched read

### Read on use (every runtime string outside a range)

`cd_tmp(p, len)` writes one record `(p, len)` in C and copies nothing.
The operation that takes the string reads the record and then the bytes, in the shim:

```js
const F = String.fromCharCode;
// n ASCII bytes at p: fromCharCode with the bytes as arguments, 8 at a time.
function ascii(p, n) {
  const b = u8;
  let v = "";
  for (; n >= 8; p += 8, n -= 8) {
    v += F(b[p], b[p + 1], b[p + 2], b[p + 3], b[p + 4], b[p + 5], b[p + 6], b[p + 7]);
  }
  switch (n) {
    case 0: return v;
    case 1: return v + F(b[p]);
    // ... one case per count, to 7, each with its bytes as arguments
  }
}
function readString(p, n) {
  if (n <= 32) {
    let high = 0;
    for (let k = 0; k < n; k++) high |= u8[p + k];
    if (high < 128) return ascii(p, n);
  }
  return textDecoder.decode(u8.subarray(p, p + n));
}
```

This is strings-bench.md's Bx read with the ASCII test in JS; its reference is `bench/strings/proto/cdom-b.js` on branch `strings-bench`.
The ASCII test stays in JS: the test in C was 6 to 9 ns faster up to 30 bytes but 9 to 80 ns slower at 64 and 200, and bounding it would put a second length rule in C for a gain inside one DOM call's noise.

### The batched read: `cd_tmp_ahead`

Today's batch wins in one case: many fresh strings at once, most of all short non-ASCII ones, because one `TextDecoder` call (100 to 140 ns of fixed cost) is shared by all of them.
It gets that by interning, which keeps every string for ever.
The direct-call bridge has no op stream to look ahead in (BRIDGE.md), and the shim cannot look ahead in C memory on its own, because C may change any byte between two operations.
So the app says which bytes are about to be shown, and for how long they hold still.

**When it batches.**
After `cd_tmp_ahead(p, len)`, the shim has the range `[p, p + len)` decoded as one JS string, `aheadText`.
`str()` of a runtime string whose record `(q, n)` satisfies `q >= p && q + n <= p + len` returns `aheadText.substring(...)`.
Every other runtime string (outside the range, straddling its edge, or with no range alive) is read on use, exactly as above.
A string does not have to be declared: the range is found by address, so the calls that show the strings are the same `cd_tmp(q, n)` or plain `q` as without it.

**The import**, `ahead(p, len)`:

```js
function ahead(p, len) {
  if (!len) { aheadText = null; return; }
  if (u8.length === 0) views();
  aheadText = textDecoder.decode(u8.subarray(p, p + len));
  aheadAt = p;
  aheadEnd = p + len;
  if (aheadText.length === len) { aheadIndex = null; return; }   // all ASCII: a byte offset is a char offset
  aheadIndex = new Uint32Array(len + 1);                          // else: the UTF-16 offset of every byte
  let u = 0;
  for (let i = 0; i < len; i++) {
    aheadIndex[i] = u;
    const c = u8[p + i];
    u += (c & 0xc0) !== 0x80;                                     // a lead byte or an ASCII byte: one unit
    u += c >= 0xf0;                                               // an astral lead byte: a surrogate pair
  }
  aheadIndex[len] = u;
}
```

and in `str()`, before the read on use:

```js
if (aheadText !== null && q >= aheadAt && q + n <= aheadEnd) {
  const a = q - aheadAt;
  return aheadIndex === null ? aheadText.substring(a, a + n) : aheadText.substring(aheadIndex[a], aheadIndex[a + n]);
}
```

**Lifetimes.**
The range ends at the first of: the outermost call into C returning (the shim's export wrapper already counts `busy`; when it returns to 0 it sets `aheadText = null`), the next `cd_tmp_ahead`, and `cd_tmp_ahead(0, 0)`.
Nothing survives an entry from JS, so nothing accumulates: there is no table and no leak, and the worst a forgotten range can hold is one string until the handler returns.
The `cd_str` rules do not change: a `cd_tmp` handle is still dead after the call it was made for; the range only changes where the shim gets that call's text.
C keeps no state for it: `cd_tmp_ahead` is the import call and nothing else.

**The one rule, and its check.**
The bytes of a live range must not change.
If they do, an in-range read returns the old text (out-of-range reads are unaffected).
The shim cannot see a changed byte for free, so the check is an option: with `cdom.start(..., { checkAhead: true })` every in-range read also reads the bytes directly and throws "cdom: bytes inside a cd_tmp_ahead range changed" when the two differ.
`tests/unit.html` and the fuzz page set it.
Memory moving is safe: if the app's buffer is reallocated, its strings are at new addresses, which lie outside the old range and are read on use.

**What it costs when it does not batch.**
A page that never calls `cd_tmp_ahead` pays one `aheadText !== null` test per runtime string (predicted under 1 ns, P11) and about 25 lines of shim.
A range that is announced and then not used costs its one decode (100 to 140 ns plus about 0.3 ns a byte) and, with non-ASCII text, the index pass (predicted 1 to 2 ns a byte and 4 bytes of transient memory per byte).
For fewer than about five strings a range costs more than it saves; the header says "a few hundred or more".

**In the benchmark app.**
`build(count)` appends `count` labels to the app's arena in one run, then calls `cd_tmp_ahead(arena + first, added)`; `render()` runs in the same click, so every `cd_tpl_text(T_ROW, 1, cd_tmp(arena + off, len))` of the create is a `substring`.

## cdom.c

- **Runtime strings.**
  One static block: `struct { uint32_t serial0, n; struct { uint32_t p, len; } r[CD_TMP_CAP]; } tmp;`.
  `cd_tmp(p, len)` traps when `tmp.n == CD_TMP_CAP`, writes `r[tmp.n] = {p, len}` and returns `{0x80000000 | ((tmp.serial0 + tmp.n++) & 0x7fffffff)}`.
  `cd_tmpz` traps on null, measures, and calls `cd_tmp`.
  No byte is copied.
- **Every function that takes a `cd_str`** passes `s.v` to its import and then calls `tmp_done()`: `tmp.serial0 += tmp.n; tmp.n = 0;`.
  That is the whole lifetime rule: a serial that is not in the current window is stale, and the shim throws on it.
- **`cd_tmp_ahead(p, len)`** is `j_ahead(p, len)`.
- **The layout block** (`CD_LAYOUT`): the three string entries (`sbytes`, `smeta` and its fields) are replaced by the address of `tmp` and the offsets of `serial0`, `n`, `r`, and of `p` and `len` in a record, and its size.
  The shim and the module are always the same cdom, and `readLayout` already throws on a count mismatch.
- **The stamp.**
  `volatile uint32_t cd_lits_stamp = 0x7374696c;` and `EXPORT("cd_lits") uint32_t cd_lits(void) { return cd_lits_stamp; }`.
  `cdlits` overwrites the word in the data segment with the hash of the table it wrote.
  `volatile` and a non-zero initial value keep it a real word in `.data` under LTO.
- **SVG.**
  C can no longer compare a tag with "svg": a literal's bytes are not in the module.
  The namespace moves to the shim: `open(tag)` does `const name = str(tag); if (svgDepth || name === "svg") { svgDepth++; ...createElementNS } else ...createElement`, `clone` does `if (svgDepth) svgDepth++`, `close` does `if (svgDepth) svgDepth--`.
  The `open_svg` import and C's `svg_depth` go.
  PERF_LOG.md measured 7 ns per open in favour of C against a JS check of the tag and the parent's `namespaceURI`; the new check is one counter and one string compare, with no DOM read.
- **Units.**
  `cd_set_style_num` passes the unit's index, and the shim has `const UNITS = ["", "px", "%", "deg", "em", "rem", "vw", "vh", "ms", "s", "rad"]`.
  `UNIT_STR` stays in C for `cd_fmt`.
  A unit test sets one style per unit and reads it back, so the two lists cannot drift.
- `ready()` becomes `walk_done()`, and `cd_pending()` reports the held walk only.

## tools/cdlits.c

One C file, built for the host by `cdom.mk` (`HOST_CC ?= cc`), no dependencies, no JSON.

```
cdlits in.wasm out.wasm out.lits     the build step
cdlits --check in.wasm               a module built with -DCD_LITS_CHECK: compare, write nothing
cdlits --list in.wasm                print "slot<TAB>spelling", for tests and for looking
```

Steps of the build form:

1. Parse the sections.
   Globals (immutable `i32.const` initializers), exports, data (active segments at `i32.const` offsets in memory 0; anything else is an error), data count.
2. Find `__start_cd_lits`, `__stop_cd_lits`, `__start_cd_chars`, `__stop_cd_chars` and `cd_lits_stamp` among the exported globals.
   No `cd_lits_stamp`: "not a cdom module (link src/cdom.c with CDOM_LDFLAGS)".
   No `__start_cd_lits`: the module has no literal; the table is empty.
3. Read memory through an overlay of the data segments, where a byte no segment covers is 0 (wasm-ld trims trailing zeros and splits segments at runs of zeros: the prototype saw `cd_chars` in three segments).
4. For each 4-byte slot from start to stop: the pointer must lie in `cd_chars`; lex the spelling there.
   The lexer accepts a sequence of string literals separated by white space, each optionally prefixed `u8`, and nothing else.
   Escapes: `\a \b \f \n \r \t \v \\ \' \" \?`, `\e` (27, as clang), one to three octal digits, `\x` with one or more hex digits (value up to 255), `\uXXXX` and `\UXXXXXXXX` as UTF-8; any other byte is itself.
   Errors, each naming the spelling: not a string literal; a NUL inside a literal; bytes that are not UTF-8.
5. Stamp: FNV-1a 32 over every slot's bytes and a 0 after each, in slot order; 0 becomes 1.
   Find the 4 bytes of `cd_lits_stamp` in a data segment (they must read `0x7374696c`, else "already processed") and overwrite them.
6. Remove the two address ranges from the data section: a segment inside a range is dropped, a segment that overlaps one is cut (its head and tail kept as two segments), so a module that another tool repacked still works.
   Update the data count section.
   Memory layout does not change: the addresses stay reserved and read as 0.
7. Remove the five exports.
8. Write `out.wasm`, then `out.lits`, one line of JavaScript text:

   ```
   var cdomLits=[BASE,STAMP,"☃","every slot's string, in slot order, joined by \x00"];
   ```

   `BASE` is `__start_cd_lits` (0 with no literal).
   A string used at five call sites is written five times.
   Strings are written with `\\`, `\"`, `\x3c` for `<` (so `</script>` and `<!--` cannot end the script), `\xNN` for bytes below 32 and 127, ` ` and ` ` escaped, and every other byte as it is (UTF-8).
   The separator is always written `\x00`, never `\0`, so a digit after it cannot make an octal escape.
   The third element is U+2603 as raw UTF-8: the shim compares it with `"☃"`, and a page read in another encoding fails at start with a message instead of showing wrong text.
   (Any character that is non-ASCII, in the Basic Multilingual Plane and not combining would do; this one is the usual choice for the job.)
9. Both outputs are written to temporary names and renamed, `out.wasm` last.

With `--check`, slots are 8 bytes (`spell`, `real`); for each slot the lexed bytes must equal the bytes at `real` up to its 0, else exit 1 naming the spelling and both byte strings.

The reference for the lexer, the overlay and the strip is the Python prototype in the appendix, and `bench/strings/lits.py` on branch `strings-bench` (the stand-in the measurements used).

### Why one entry per slot

The first version wrote each unique string once and a per-slot map of indices.
Three ways to drop the map were weighed:

| Route | Bytes | Install | Heap | Needs |
| --- | --- | --- | --- | --- |
| One entry per slot, repeats spelled again (chosen) | 0.65 bytes of brotli per slot under the map on amgi (measured, table above) | one `split`: the measured form (d) for 1.25 N strings, 0.06 ms at 1,000 and 0.41 ms at 10,000 | a repeated string is held once per slot: at amgi's size about 10 KB more than unique strings | nothing: the id stays the linker's slot address |
| Dense ids, one per unique string | 1 to 1.5 bytes per slot under the map (T4) | the same `split`, of N strings | lowest of the joined forms | rewriting every `i32.const` in the linked module from the reloc sections, which this design rejects (see the alternatives): it is 0.3 ns slower per lookup (7.1 against 6.8 ns) and breaks if the optimizer derives one slot address from another |
| ASCII strings numbered first, carried as two joined strings (form g) | within 1% of dense ids | 1.72 against 2.83 ms at 10,000, 19.9 against 37.2 ms at 100,000 | 456 against 642 KB at 10,000 | dense ids, so the same rewrite; without renumbering it needs a third list of positions, which measured 86 bytes larger on amgi and about equal elsewhere |

One entry per slot is the only one of the three that needs no rewriting, and it is smaller than what it replaces on every real source measured.
Its weakness is a table with non-ASCII text: `split` of a string that holds any makes every piece two-byte, and pieces of 13 characters or more keep the whole string alive (T5).
At the sizes real pages have that is small: amgi, the largest app, has 671 slots and about 10 KB of table, so the cost is about 10 to 20 KB of heap and under 0.1 ms.
The two-string form is not taken now: it costs bytes at amgi's size and saves time only from about 10,000 slots, fifteen times the largest table there is.
The threshold to revisit it: a real app whose table passes 5,000 slots or whose install passes 0.3 ms.
It would be a change to `cdlits` and `start` only, in the same commit, because the page and the shim always come from the same cdom.

## The page: cdom.mk, page.awk, the shim, the boot

- **`CDOM_LDFLAGS`** gains `-Wl,--export-if-defined=__start_cd_lits`, `=__stop_cd_lits`, `=__start_cd_chars`, `=__stop_cd_chars` and `=cd_lits_stamp`.
- **`CDOM_LITS := $(CDOM)/tools/out/cdlits`**, built by a rule from `tools/cdlits.c` with `$(HOST_CC) -O2 -Wall -Wextra -Werror` to a temporary name and renamed (several apps may build it at once); `tools/out/` is ignored by git.
- **The `APP_WASM` rule** links to `$@.linked`, runs `$(CDOM_LITS) $@.linked $@ $@.lits`, and removes `$@.linked`.
  `$(call cdom_lits,linked.wasm,out.wasm)` is the same step for an app's own rules, and `tools/cdomflags.sh` hands it to cdom's own build scripts as `CDOM_LITS`.
- **The table's place in the page.**
  `page.awk` takes `-v lits=path/to/app.wasm.lits`.
  When it reaches the `/*CDOM_SHIM*/` line it writes the lits line, then `</script>`, then the opening tag of the script the marker is in (the last `<script` tag it saw, written again as it was), then the shim.
  So the table is a script of its own (whatever stood before the marker in that script, usually nothing, and the one `var`), it comes before the shim and before the module wherever `/*CDOM_WASM*/` is, and no page needs a new marker.
  It is ES3, so it also parses in a browser that cannot parse the shim, and the boot's fallback finds it.
  A shim marker with no `<script` tag before it is an error.
  `/*CDOM_WASM*/` is unchanged: the data URL or the URL, and a page may still name its module's URL itself when the module is a separate file.
  `$(call cdom_page,in.html,out.html,module.wasm)` reads `module.wasm.lits`; with no module named it takes a fourth argument, the lits file, and the `APP_PAGE` rule passes `$(APP_WASM).lits` in both modes.
- **`cdom.start(url, mount, env, options)`** keeps its arguments.
  Before `cd_start` it reads `globalThis.cdomLits`.
  Missing: "cdom: this page has no string table (build it with cdom.mk or tools/page.awk, which write it before the shim)".
  The canary must equal `"☃"`, else "cdom: this page was not read as UTF-8".
  `exports.cd_lits() >>> 0` must equal the stamp, else "cdom: the page's string table is not this module's (the page and the module are from different builds, or the module did not go through cdlits)".
  Then `lits = table[3].split("\0"); litBase = table[0];`.
- **`str(s)`**, the one way an import reads a string:

  ```js
  function str(s) {                       // s is an i32: a slot address, or negative for a runtime string
    if (s > 0) return lits[(s - litBase) >> 2];
    if (u8.length === 0) views();         // a grown memory detaches the old views: their length is 0
    const k = ((s & 0x7fffffff) - u32[tmpSerial0]) & 0x7fffffff;
    if (k >= u32[tmpCount]) throw new Error("cdom: a cd_tmp string used after the call it was made for");
    const at = tmpRecords + k * 2;
    const q = u32[at], n = u32[at + 1];
    if (aheadText !== null && q >= aheadAt && q + n <= aheadEnd) { /* the batched read, above */ }
    return readString(q, n);
  }
  ```

  Every other place the shim tests for a stale view (`post`, `set`, `call`, the old `strings`) takes the same length test, so there is one rule; `views()` still records nothing but the views.
- **Paths** (`segmentsOf`): `CD_NONE` is no segments; a literal's split is cached by slot index; a runtime path is split each time, as a one-off path is today.
- **`shim/boot.js`** is unchanged: the table is a global the shim reads, whichever shim runs.

### Old browsers

The no-WebAssembly path (the ES5 shim with polyfills, wasm2js, and the plain-array tier for IE 9 and 10) needs one thing checked and nothing new.
The table is an ES3 `var` of ES3 string literals in a script of its own; the shim's new code uses `split`, `substring`, `String.fromCharCode`, array indexing, the typed views and `TextDecoder`, all of which that path already has or polyfills.
The check: in the plain-array tier a view's length may not drop to 0 when memory grows, so the legacy build keeps whatever stale-view test that tier has today, and `tools/legacy.mjs` grows the memory once and reads a runtime string after it.
`tools/legacy/build.mjs app` compiles the stripped module, which has two data segments fewer.
The page must be read as UTF-8 for non-ASCII literals, as it must for any text in it, and the canary turns a wrong encoding into an error at start.

## The alternatives, and why each loses

**Where the rewrite happens.**

| Route | Verdict |
| --- | --- |
| The compiler places, the linker gathers, a post-link tool extracts and strips (chosen) | Uses only documented behaviour: section attributes, `__start_`/`__stop_` symbols, `--gc-sections`. No code is rewritten: the id in the code is the slot's address, exactly as the linker wrote it, so nothing the optimizer does with that constant (a lookup table of them, a select between two) can go wrong. |
| A clang plugin or an LLVM pass | It would see each literal as it reaches a `cd_*` parameter, which is the ideal view, but a plugin is C++ built against one exact LLVM, breaks on each Homebrew update, and cannot load into a clang built without plugin support. Rejected for robustness. |
| A source preprocessor (rewrite `cd_text("x")` to `cd_text(17)` before compiling) | It has to parse C well enough to know which arguments are string parameters through macros, wrappers and conditionals, and every editor and debugger then sees different source from what compiles. Rejected. |
| Rewriting constants in the linked wasm with `--emit-relocs` (dense ids) | Measured: the lookup is 7.1 ns against 6.8 for the slot address, so it is not faster. It saves 1 to 1.5 bytes of brotli per slot against the map, of which one entry per slot already gets 0.65 with no rewrite. It needs the linking section's symbol table and both reloc sections parsed, and it breaks if the optimizer ever derives one address from another. Rejected: a new failure mode for at most 0.85 bytes per slot. |
| Slots that point at the real literals in `.rodata`, stripped by reference analysis with `--emit-relocs` | It would avoid the lexer, since the bytes are the compiler's. But the linker merges identical literals, so a cdom literal shares its bytes with any other use of the same text, and proving a string unreferenced needs the full reloc and symbol analysis. A hole in that analysis silently blanks a string some other code reads. The lexer's failure mode is checkable (the check build compares every literal with the compiler's bytes); this one is not. Rejected. |

**Where the strings live at run time.**

| Route | Verdict |
| --- | --- |
| Baked into the page as JS text before the module, stripped from the wasm (chosen) | Measured: hello 8,848 to 8,597 bytes of brotli (-2.8%), the 2,000-site page 20,573 to 19,299 (-6.2%); `cd_main` plus install 38% faster at 2,000 sites; the wasm holds no literal byte. |
| Baked into the page after the module | 5.9% larger than today on the 2,000-site page: brotli codes text badly after 30 KB of base64. Rejected (this was the first version's placement). |
| Baked into the page, left in the wasm too | 2.1% to 6.7% more page bytes on the lessons. Rejected. |
| The shim decodes `cd_chars` from wasm memory once at load (no tool; C and C1 in strings-bench.md) | The simplest build: no host tool, no page change. Measured: within the noise on small pages and 0.4 ms behind at 2,000 sites, and it gives up the bytes (hello 8,707 against 8,597, the 2,000-site page 20,013 against 19,299). It is the fallback if the tool becomes a maintenance problem; the header and the shim's lookup would not change. |
| The address of the literal itself as the id, cached by the shim in a `Map` on first sight | Measured: 9.4 ns against 6.8, cheaper than the first version thought, but still slower, and the shim would have to trust that an address holds immutable bytes, which wasm-ld gives no symbol for. Rejected. |
| A wasm custom section holding the table | No skew is possible when the module is a separate file, and no page change is needed. But a bundled page would carry it inside the base64 again, the wasm2js path has no module object to read it from, and it would be a second carrier beside the page. One carrier with a stamp is simpler. Rejected. |

**The shape of the API.**

| Route | Verdict |
| --- | --- |
| Macros named like the functions, classifying each argument (chosen) | `cd_text("x")` and `cd_text(buf)` both work, and neither can be misclassified. |
| Literals only, runtime strings always wrapped by hand | The compiler would enforce it, but `cd_text(name)` is exactly the plain C Alex asked for, and the wrapped form adds nothing the classification does not already know. Rejected. |
| Keep `cd_intern` for strings used many times | Measured: a hit costs 13 to 51 ns at 1 to 30 bytes, against 26 to 95 ns for a read on use, so interning does win for a short string shown again and again unchanged. It loses at 200 bytes (333 against about 160 ns), it leaks (nothing ever leaves the table: 408 KB of touched wasm memory after five cycles against 116), and it is a second way to pass a runtime string. Rejected; an app that shows the same strings repeatedly can make them literals, or announce them with `cd_tmp_ahead`. |

**The batched read.**

| Route | Verdict |
| --- | --- |
| A range the app announces, decoded once and cut by address (chosen) | No handle type, no table, nothing that outlives the call into C; the calls that show the strings do not change. |
| Decode every runtime string of a flush, or of one cdom call, automatically | There is no flush: each operation is a direct call that needs its string at once, and cdom does not know the strings of a `cd_list_update` before its callbacks run. Deferring operations to look ahead is the op list BRIDGE.md measured and rejected. |
| The shim reads ahead on its own (decode past the string just asked for) | Unsound: C may change those bytes before they are asked for. |
| Handles the app declares in bulk (`cd_tmp` allowed to live across calls until a flush) | It needs the bytes copied together or a decode per string, and a second lifetime rule for `cd_str`. The range gets the same single decode from bytes that already sit together. Rejected. |

## Migration, file by file

In cdom:

| File | Change |
| --- | --- |
| `src/cdom.h` | the strings section above; `cd_str` a struct; `cd_tmp_ahead`; the wrapper macros; `CD_NONE`; comments that name `CD_LIT` or interning (the header comment on paths, `cd_text`'s, `cd_set_style`'s) |
| `src/cdom.c` | the "cdom.c" section above |
| `shim/cdom.js`, `shim/cdom.min.js` | `str`, `ascii`, `readString`, `ahead`, the table from `cdomLits`, the length test, SVG, units; regenerate the minified shim with `node tools/minshim.mjs` |
| `shim/boot.js`, `shim/page.html` | unchanged |
| `cdom.mk` | `HOST_CC`, `CDOM_LITS` and its rule, `cdom_lits`, the linked-then-cdlits recipe, `-v lits=`, the new flags, the header comment |
| `tools/page.awk` | the table script at the shim marker; it remembers the last `<script` tag |
| `tools/cdlits.c` | new |
| `tools/cdomflags.sh` | export `CDOM_LITS` and a `cdom_lits` shell function; add `tests/lits/run.sh` to `CDOM_FLAG_USERS` |
| `tools/new.sh` | the starter app without `CD_LIT` |
| `tools/mutate.py` | drop the mutants of deleted code (`tmp-interned`, `tmp-unchecked`, the `CD_LIT` ones); add the mutants listed in the test plan |
| `tools/offsets.mjs` | the layout's new entries |
| `tools/legacy/build.mjs` | nothing, unless its `page` step drops the table script (checked in step 6) |
| `tools/final.sh` | drop `cdom-pertext` from the default list |
| `tools/starter.mjs`, `tools/unit.mjs`, `tools/fuzz.mjs`, `tools/host.mjs`, `tools/canvas.mjs`, `tools/legacy.mjs`, `tools/smoke.mjs` | only where they name a deleted export or a page that changed |
| `bench/cdom-keyed/src/app.c` | labels in an arena the app owns, `cd_tmp` at each use, a revision number for the diff, `cd_tmp_ahead` after a build (steps 2 and 8); `bench/strings/apps/keyed_b.c` on branch `strings-bench` is the measured arena app to start from |
| `bench/cdom-keyed/index.html` | unchanged (it has the shim marker) |
| `bench/build.sh` | run cdlits; pass the lits to `page.awk`; drop the `cdom-pertext` build |
| `demo/click.c`, `demo/click.html` | no `L()`; the shim marker in place of `<script src="cdom.js">` |
| `tutorial/01-hello` to `07-particles`: `src/app.c`, `README.md` | no `L()`; 01 passes `FRUITS[i]` directly; 05 keeps each todo's text in its own struct; the READMEs' paragraphs on `CD_LIT`, interning and `cd_tmp` |
| `tutorial/README.md` | the "Strings are interned" bullet, lesson 1's row, the size column after measuring |
| `tests/unit.c`, `tests/unit.html` | no `L()`; the interning tests replaced by the string tests in the test plan; `checkAhead` on |
| `tests/listfuzz.c`, `tests/host.c`, `tests/canvas.c`, `tests/legacy.c` | no `cd_intern*`; the `z()` helpers go; `host.c`'s `P(len)` uses `cd_tmp` |
| `tests/*.html` | the shim at `/*CDOM_SHIM*/`, built by `page.awk` (the unminified shim, so `tools/mutate.py` still mutates what runs) |
| `tests/build.sh` | cdlits after each link; pages through `page.awk` |
| `tests/link/alloc.c`, `mk_app.c`, `run.sh`, `page/*.html`, `app.mk` | no `cd_intern*` (alloc.c grows the heap another way); the `lit_*` cases go; the page cases expect the table script |
| `tests/lits/` | new: `torture.c`, `kinds.c`, `run.sh` |
| `micro/svg/` | deleted |
| `README.md`, `RESULTS.md`, `PERF_LOG.md` | the strings paragraphs and the diagram label in README; a new dated section in RESULTS that points at `docs/strings-bench.md` and `docs/strings-results.md`; a dated PERF_LOG entry for SVG. Old sections stay as history. |

Outside cdom (each repo's manager owns it; through the super-coordinator, since it is a pin that moves with an API change):

| Repo | Change |
| --- | --- |
| amgi (`ui/src/*.c`, 15 files, 671 literal sites) | delete `CD_LIT(` wrappers; `cd_internz(x)` becomes `x` (13); `cd_intern(p, n)` becomes `cd_tmp(p, n)` with the bytes kept by the app (1, deck names in `state.c`); `cd_bytes` (4) reads the app's own copy; `dom.c`'s helpers that take a `cd_str name` keep working; its build runs through `cdom.mk`'s rule or `cdom_lits`, and its page through `page.awk` with the lits |
| cbase (`example/minimal/page/page.c`, and the adapter's page) | delete three `CD_LIT(` wrappers; the page built with the lits |

There is no backwards compatibility (Alex, 2026-11-17): the only users are Alex's own apps, and each pins cdom to a git commit, so they move when their pin moves.
Step 7 keeps `#define CD_LIT(s) cd_s(s)` only so that cdom's own call sites compile between steps 7 and 9; step 9 deletes it, and nothing waits for amgi or cbase.

## Predictions

Written 2026-11-17, before the real implementation is measured, from the numbers in strings-bench.md.
The baseline is that file's A (and its A2 for the noise); nothing is measured again before the change except the lesson sizes in step 1.
Each prediction is recorded as confirmed, refuted or not measurable, with the numbers, in `docs/strings-results.md`.
"Beyond the noise" means by more than the gap between the two baseline passes of that run.

| # | Prediction | Built on | Refuted by |
| --- | --- | --- | --- |
| P1 | Page, brotli -q 11: tutorial/01-hello between 8,450 and 8,700 (today 8,848 in the bench's build); every lesson smaller than its step 1 size; the 2,000-site synthetic page between 18,900 and 19,500 (today 20,573) | B measured 8,597 and 19,299 with a map; one entry per slot is 0.65 bytes per slot smaller on amgi; the shim grows by `ahead` and `ascii` | hello over 8,750, any lesson larger than today, or the synthetic page over 19,800 |
| P2 | Lesson 1's `app.wasm` between 2,300 and 2,600 bytes (today 3,981) | B stripped 1,553 bytes; `cd_tmp_ahead` adds one import and a few bytes | above 2,800 |
| P3 | Initial memory: hello 2 pages (today 6), the keyed entry 3 (today 8); touched wasm memory after five create-and-clear cycles 150 KB or less (today 408) | measured for B: 2, 3 and 116 KB | hello over 2, keyed over 4, or touched over 200 KB |
| P4 | Table install between 0.04 and 0.10 ms on hello and the keyed entry, and between 0.10 and 0.25 ms at 2,000 sites; `cd_main` plus install at 2,000 sites between 1.6 and 2.0 ms (today 2.93); first paint not measurable | B measured 0.065, 0.07, 0.22 and 1.81 ms with a `split` and a map pass; one `split` of 2,000 entries replaces both; first paint noise was 5.8 to 7.8 ms | install over 0.15 ms on hello or the keyed entry, over 0.35 ms at 2,000 sites, or `cd_main` plus install over 2.2 ms |
| P5 | Keyed benchmark, `cdom` entry: every total within 2.5% of A; create 10k script between 20.5 and 21.2 ms (A 20.4), that is 0.1 to 0.8 ms worse; create 1k script 2.8 ms or less (A 2.8) | Bx measured totals 0.991 to 1.016, create 10k 21.2 and create 1k 2.6; `cd_tmp_ahead` replaces 10,000 reads of about 60 to 90 ns with cuts of about 25 ns, 0.35 to 0.65 ms; the rest of the gap is the app's arena and the `cd_tmp` records | any total over 1.03 of A beyond the noise in two passes, create 10k script over 21.4 ms beyond the noise, or create 1k script over 3.0 ms |
| P5a | In the same build, create 10k script with the `cd_tmp_ahead` call is at least 0.2 ms faster than without it | the same arithmetic | a gain under 0.2 ms beyond the noise: then the call comes out of the benchmark app, and P5's band is Bx's 21.2 ms |
| P6 | Run-and-clear memory (harness) 0.78 MB or less (A 0.85) | B 0.72 and Bx 0.78 in single iterations of the same module | 0.82 MB or more in two iterations |
| P7 | The minified shim within 250 bytes of today's brotli size (5,930) | the batch decoder goes; `str`, `ascii` (about 400 bytes minified), `ahead`, the table install and the SVG counter come | outside that band |
| P8 | Without templates: create 1k script between 4.3 and 4.9 ms (A 4.5), create 10k script between 35 and 38 ms (A 35.9, A2 35.3) | B's 5.2 and 38.1 came from a `TextDecoder` call per label; the new read is 50 to 90 ns faster per label (0.07 ms per 1,000), a cut faster still, and each of the 12 literals per row is 2.5 ns cheaper than today | create 1k over 5.0 ms or create 10k over 38.5 ms, beyond the noise |
| P9 | The lexer and clang agree on every literal in cdom's tree and in the torture file | the grammar is small and the prototype agrees on the torture literal | one mismatch |
| P10 | No literal that went to cdom is in any final wasm | both sections are removed whole | `strings app.wasm` finds one |
| P11 | Read on use (micro, resolved only, the bench's method): 9 ASCII bytes between 44 and 52 ns, 30 ASCII bytes between 86 and 100 ns, 9 to 13 bytes of UTF-8 between 150 and 195 ns; a page that never calls `cd_tmp_ahead` pays under 2 ns for its test | Bx measured 48, 94 to 95 and 163 to 189; the test is one compare with null | 9 ASCII bytes over 55 ns, 30 over 105 ns, or short UTF-8 over 205 ns |
| P12 | Inside a range (micro, 1,024 fresh strings announced once and each shown once, the range's decode and index included): 9 to 13 bytes of UTF-8 between 25 and 55 ns each, 9 to 30 ASCII bytes between 20 and 45 ns each | a cut is a record read, three compares and a `substring`; the decode is 100 to 140 ns once plus about 0.3 ns a byte; the index pass 1 to 2 ns a byte, non-ASCII ranges only | short UTF-8 over 64 ns (no better than today's batch, 64 to 73), or ASCII over 48 ns (no better than read on use at 9 bytes) |
| P13 | A literal lookup, resolved only, between 6.5 and 7.2 ns (today 9.3) | B measured 6.8; the lookup did not change | over 7.5 ns |

What is not predicted to improve: a short runtime string shown again and again unchanged, which today's `cd_intern` hit serves in 13 to 51 ns and a read on use in 26 to 95.
No app in the tree does that outside the benchmark's label diff, which the arena app's revision number already skips.

## Test plan

End to end first: the tests drive a built page in headless Chrome, as a user would load it.
Every new test is mutation checked: the mutant is applied, the named test must fail on the named assertion, and the code is restored by re-editing (never `git checkout`).

| Test | What it proves | Mutant that must turn it red |
| --- | --- | --- |
| `tutorial/run.sh` (exists) | every lesson's page, built by `cdom.mk` from plain literals, shows the right text from disk (`file://`), with no fetch | in `cdlits.c`, skip the first slot when writing the table: lesson texts are wrong and `test.mjs` fails on the first text check |
| `tests/lits/run.sh`, differential | `torture.c` and every app in the tree (the seven lessons, the bench app, the demo, `tests/*.c`) built with `-DCD_LITS_CHECK` pass `cdlits --check` | in the lexer, `\t` gives 8; then octal reads four digits; then `\x` stops after two digits: each fails on the torture literal |
| `tests/lits/run.sh`, kinds | `kinds.c` has one call of each row of the classification table; `cdlits --list` must show exactly the expected slots, and a Node run with stub imports must see the expected strings, with `mut` reading `nut` after C changes it | `CD_LITQ` without the spelling test: `NAME` becomes a slot and the tool stops on it; `CD_LITQ` always 0: no slots |
| `tests/lits/run.sh`, three builds | `kinds.c` at `-O0`, `-Oz` and `-Oz -flto` gives the same `--list` output and the same strings | none: it is a comparison |
| `tests/lits/run.sh`, table | a literal used at three call sites is three equal entries; a literal with `</script>`, a quote, a backslash, a newline, U+2028 and a digit right after another entry's end comes back byte for byte when the line is run in Node | write the separator as `\0`: the entry that starts with a digit is wrong; drop the `<` escape: the page test below fails |
| `tests/lits/run.sh`, strip | the output validates (`wasm-validate` when present), no literal's bytes appear in `out.wasm`, the five exports are gone, and a module whose `cd_chars` segment was merged with its neighbour by hand still strips | skip the strip: the bytes are found; drop instead of cut on overlap: the neighbour's bytes are lost and the Node run fails |
| `tests/lits/run.sh`, errors | each build error fires with its message: `"abc" + 1`, `L"x"`, a literal with `\0` inside, invalid UTF-8, a module without `cd_lits_stamp`, a module processed twice | remove each check in turn |
| `tests/lits/run.sh`, compile errors | `cd_text(some_id)`, `cd_text(5)`, `cd_str a, b; a != b` do not compile, and a control file does | make `cd_str` a `uint32_t` again: all three compile |
| `tools/unit.mjs` + `tests/unit.c`, read paths | a runtime string of 0, 1, 7, 8, 9, 15, 16, 17, 31, 32, 33 and 300 ASCII bytes (every remainder of the 8-at-a-time loop, both sides of the 32-byte line), and the same lengths with one two-byte letter first, in the middle and last, a three-byte character and an astral one; each compared with the text C holds | `ascii`'s case 3 returning two characters; the loop stepping 7; `n <= 32` as `n <= 64` with the ASCII test removed (a non-ASCII string of 40 bytes comes back as mojibake); the ASCII test as `high < 256` |
| the same, every string position | a literal, a `cd_tmp`, a `cd_tmpz` and a ternary of `cd_s` in each position: tag, text, attribute name and value, class, style property and value, path, method, `cd_v_str` in arguments and in an object | `lits` index off by one; one function's `cd_s` dropped from its macro |
| the same, lifetime | a `cd_tmp` used after another string call throws with the shim's message; 32 alive at once work; the 33rd traps | `tmp_done()` removed from one function: the stale use is not caught; the `k >= count` check removed |
| the same, the stale view | a runtime string read right after C grew its memory (a large `cd_alloc`), in range and out of range | the length test removed from `str`: the read throws or returns nothing |
| the same, argument evaluated once | `cd_text(next())` with a counter | `cd_s` naming its argument twice in the chosen branch |
| the same, the batched read | 1,024 strings in one buffer, ASCII, mixed and astral, announced with `cd_tmp_ahead` and shown: each equals a read on use of the same bytes; a string that starts before the range, one that ends past it and one outside it are read on use and correct; the range is gone after the call returns (the test changes a byte in a later call and sees the new text); a second `cd_tmp_ahead` replaces the first; `cd_tmp_ahead(0, 0)` ends it; an empty string at the range's last byte | the index counting one unit for an astral lead byte; `q + n <= aheadEnd` as `q < aheadEnd` (the straddling string is cut short); the `busy` reset removed (the later call sees old text); the ASCII shortcut taken for a mixed range |
| the same, `checkAhead` | with the option on, changing a byte inside a live range and showing the string throws the message; with it off, the old text shows | the compare removed |
| the same, units and SVG | each `CD_UNIT_*` through `cd_set_style_num`, read back; an `<svg>` with children, a clone inside it, and an element after it, by `namespaceURI` | swap two entries of `UNITS`; drop the decrement in `close` |
| `tests/link/run.sh`, page cases | a bundled page and a URL page each have the table script before the shim's script and before the module; a marker with no `<script` before it fails the build; a page whose script tag has attributes gets them again | write the table after the shim: the order check fails |
| `tools/unit.mjs`, start | a page with no `cdomLits` throws the no-table message; a page whose table comes from another build (one literal changed, same count) throws the stamp message; the page served with `charset=iso-8859-1` throws the UTF-8 message | remove each compare in turn |
| `LEGACY=1 node tools/legacy.mjs` and `LEGACY=1 node tools/fuzz.mjs` (exist) | the table, both read paths and a range on the wasm2js build and the polyfilled `TextDecoder`, with Latin, Cyrillic, CJK and an emoji, and a read after the memory grew | none new: the existing mutants of the polyfills still apply |
| `node tools/fuzz.mjs` (exists) | the list fuzz with `checkAhead` on and a range announced for each round's labels: 40,000 transitions per seed with no mismatch | none new |
| a gate in `tests/link/run.sh` | `grep` finds no `CD_LIT`, `cd_intern`, `cd_internz`, `cd_bytes(`, `cd_str_count`, `CDOM_INTERN_TEXT` outside `docs/`, RESULTS.md, PERF_LOG.md and history | reintroduce one |

Per step, run only what the step can break (each step names it); the whole suite runs once, before the merge.

## Implementation steps

Each step is one executor, one commit or a few, and leaves the tree building and its tests green.
Sonnet unless marked Haiku.
Every executor: work in `/Users/alex/Dev/cdom-strings` on branch `strings`, commit and push after the step, no em dash, each Markdown sentence on its own line, no `Co-Authored-By` and no session trailer, do not edit CHANGELOG or generated files by hand (regenerate `shim/cdom.min.js` with `node tools/minshim.mjs`), restore a mutant by re-editing.
Steps that build or run browsers run one at a time on this Mac.
The measured prototypes are on branch `strings-bench` (worktree `/Users/alex/Dev/cdom-strings-bench`, `bench/strings/`): read them, copy from them, do not merge the branch.

1. **Record the lesson sizes** (Haiku).
   The benchmark, startup and memory baselines are in docs/strings-bench.md and are not run again.
   Run `tutorial/run.sh`; for each of the seven lessons record `wc -c build/app.wasm`, `brotli -q 11 -c build/index.html | wc -c` and the initial memory pages (`wasm-objdump -x`), and the size and brotli size of `shim/cdom.min.js`.
   Write them to `docs/strings-results.md` under "Before", with the P1 to P13 table copied in and an empty "Measured" column.
   Verify: seven rows, and lesson 1's page within 100 bytes of 8,942.

2. **`tools/cdlits.c` and `tests/lits/`, standing alone.**
   Write the tool to the specification above.
   `tests/lits/lits.h` is a private copy of the macro block from "The API" (with `cd_tmpz` declared), used only by this step's tests until step 7 replaces it with `cdom.h`.
   `tests/lits/torture.c` holds the torture literals; `tests/lits/kinds.c` the classification table; `tests/lits/run.sh` builds them with `tools/cdomflags.sh`'s flags plus the five `--export-if-defined` flags and a small stub runtime (`cd_tmp`, `cd_tmpz`, two imports, `cd_lits_stamp`), and runs the differential, kinds, three-builds, table, strip and error cases, with a Node stub for the run.
   Nothing else in the tree changes.
   Verify: `tests/lits/run.sh` green; each lexer, table and strip mutant from the test plan red, then restored.

3. **The benchmark app, the tests and two lessons stop interning**, under today's API (two commits: the benchmark app, then the rest).
   `bench/cdom-keyed/src/app.c`: take the arena app from `bench/strings/apps/keyed_b.c` (labels in a byte arena the app owns; `Row` holds offset, length and a revision; the arena is reset when the rows are replaced or cleared and compacted when dead bytes exceed live ones; every use is `cd_tmp(arena + off, len)`), with the `S_*` statics filled by `z()` turned into `CD_LIT` at their call sites.
   `tests/listfuzz.c`, `tests/host.c`, `tests/canvas.c`, `tests/legacy.c`, `tests/link/alloc.c`, `tests/link/mk_app.c`: `cd_internz("x")` and `z("x")` become `CD_LIT("x")` at the call site; interned runtime strings become `cd_tmp` at each use; `alloc.c` grows cdom's tables with ids instead of strings.
   `tutorial/01-hello` passes `cd_tmpz(FRUITS[i])`; `tutorial/05-todo` keeps each todo's text in its struct and uses `cd_tmp`.
   `tests/unit.c` keeps its interning tests (the feature still exists).
   No measurement: strings-bench.md already has this build (sb-at, create 10k script 2.0 ms worse on today's string path, totals 0.957 to 1.047), and the step is only a waypoint.
   Verify: `bench/build.sh && node tools/smoke.mjs && node tools/domcheck.mjs`; `tests/build.sh && node tools/unit.mjs && node tools/fuzz.mjs && node tools/canvas.mjs && node tools/host.mjs && tests/link/run.sh && node tools/legacy.mjs`; `tutorial/run.sh tutorial/01-hello tutorial/05-todo`.

4. **SVG and units move to the shim, and the stale-view test becomes a length test**, under today's API.
   The shim's `open` counts SVG depth as described; `open_svg`, `svg_tag` and `svg_depth` are deleted; `style_num` takes the unit's index and the shim has `UNITS`; every `buffer !== memory.buffer` becomes `u8.length === 0`, with the legacy tier checked as "Old browsers" says.
   Add the units, SVG and stale-view unit tests and their mutants; delete `micro/svg/` and remove it from `CDOM_FLAG_USERS`; add the PERF_LOG entry.
   Verify: `tests/build.sh && node tools/unit.mjs && node tools/host.mjs`, `node tools/offsets.mjs`, `node tools/minshim.mjs check`, `LEGACY=1 node tools/legacy.mjs`, and one lesson with `tutorial/run.sh`.

5. **Every page inlines the shim at the marker.**
   `tests/*.html`, `demo/click.html`, `tests/link/page/*.html` where they load `cdom.js` by `src`: the shim at `/*CDOM_SHIM*/`, built by `page.awk` (`tests/build.sh` calls it with the unminified `shim/cdom.js`).
   No shim or awk change.
   Verify: the test commands of step 3, and `node tools/mutate.py` on one existing shim mutant to prove the tests still run the mutated shim.

6. **The build carries a table** (every entry empty until step 7 makes slots).
   `cdom.c` gets `cd_lits_stamp` and `cd_lits`; `cdom.mk` gets `HOST_CC`, `CDOM_LITS`, the cdlits step in the `APP_WASM` rule, `cdom_lits`, the five flags and `-v lits=`; `tools/cdomflags.sh` hands `CDOM_LITS` to the scripts, and `tests/build.sh`, `tests/link/run.sh` and `bench/build.sh` run it after each link and pass the lits to `page.awk`; `page.awk` writes the table script; the shim's `start` reads `cdomLits`, checks the canary and the stamp and installs `lits`; `tools/legacy/build.mjs page` is checked to keep the table script.
   Add the page and start tests with their mutants.
   Verify: the test commands of step 3, `tutorial/run.sh`, `tests/lits/run.sh`, `LEGACY=1 node tools/legacy.mjs`, and `node tools/starter.mjs` (a new app builds and opens).

7. **The switch.**
   `cdom.h`: the strings section and wrapper macros without `cd_tmp_ahead`, `cd_str` a struct, `CD_NONE`, and for now `#define CD_LIT(s) cd_s(s)` marked "temporary, deleted in step 9".
   `cdom.c`: the intern machinery out, the `tmp` block and `tmp_done()` in, the layout entries.
   The shim: `str`, `ascii`, `readString`, the path cache by slot; `interned`, `transient`, `decode`, `strings` out; regenerate the minified shim.
   `tests/lits/lits.h` is deleted and its tests include `cdom.h`.
   `tests/unit.c`: the interning tests go; the read-path, string-position, lifetime and evaluated-once tests come, with their mutants; path 0 becomes `CD_NONE` everywhere.
   `tools/mutate.py`, `tools/offsets.mjs`: as in the migration table.
   `bench/build.sh` and `tools/final.sh`: `cdom-pertext` goes.
   `tests/link`: the `lit_*` cases go, the compile-error cases come.
   cdom's own call sites keep compiling through the temporary `CD_LIT` until step 9.
   Verify (the one step that touches everything): `tests/build.sh && node tools/unit.mjs && node tools/fuzz.mjs && node tools/canvas.mjs && node tools/host.mjs && tests/link/run.sh && node tools/legacy.mjs`, `LEGACY=1 node tools/fuzz.mjs && LEGACY=1 node tools/legacy.mjs`, `tests/lits/run.sh`, `tutorial/run.sh`, `bench/build.sh && node tools/smoke.mjs`.
   Then `strings` on a lesson's `app.wasm` must not find "Hello, cdom".

8. **The batched read.**
   `cdom.h` and `cdom.c`: `cd_tmp_ahead` and the `ahead` import.
   The shim: `ahead`, the range test in `str`, the reset when `busy` returns to 0, `options.checkAhead`; regenerate the minified shim.
   `tests/unit.c` and `tests/unit.html`: the batched-read and `checkAhead` tests with their mutants; `tests/listfuzz.c` announces each round's labels and its page sets `checkAhead`.
   `bench/cdom-keyed/src/app.c`: `cd_tmp_ahead` after `build()` and after the update's appends.
   Hypothesis for this step: P12 and P5a.
   Verify: `tests/build.sh && node tools/unit.mjs && node tools/fuzz.mjs && LEGACY=1 node tools/fuzz.mjs`, `bench/build.sh && node tools/smoke.mjs && node tools/domcheck.mjs`.

9. **Plain literals everywhere in cdom, and `CD_LIT` deleted** (Haiku, four commits: `tutorial/`, then `tests/` and `demo/` and `bench/`, then `tools/new.sh`, then `cdom.h`).
   Delete `#define L(s) CD_LIT(s)`, turn `L("x")` and `CD_LIT("x")` into `"x"`, drop `cd_tmpz(` around plain pointers where the argument goes straight into a call, and rewrite the comments and lesson READMEs that explain `L`, `CD_LIT` or interning (lesson 1's README and `tutorial/README.md`'s strings bullet say: write the string; a literal is in the page's table, anything else is read when used).
   Verify per commit: `tutorial/run.sh`; then `tests/build.sh && node tools/unit.mjs && node tools/host.mjs && tests/lits/run.sh` (the differential now covers every app); then `node tools/starter.mjs`.
   Look at each lesson's screenshot (`SHOTS=tutorial/out tutorial/run.sh`) against the one from before: they must be identical.
   The last commit deletes the temporary `CD_LIT` from `cdom.h` and adds the grep gate; verify it with `tests/link/run.sh` and the gate's mutant.

10. **Measure and write up.**
    Repeat step 1's sizes.
    Run the bench's scripts from branch `strings-bench` against this branch's build in place of its B (the micro for P11, P12 and P13; `startup.mjs` for P3 and P4; `jfb.sh` for P5, P5a, P6 and P8, with A and A2 built from `tutorial` for the noise), one Chrome at a time.
    Fill the "Measured" column and mark each of P1 to P13 confirmed, refuted or not measurable, with the numbers.
    Profile nothing unless a prediction is refuted; if one is, sample first (`sample`) and report to the repo manager.
    Update `tutorial/README.md`'s size column, README.md (the strings sentence, the diagram label, the shim's size line, "Building an app": the build needs a host C compiler, and a page gets its table from `page.awk`), `cdom.mk`'s header comment, and add a dated section to RESULTS.md that points at `docs/strings-bench.md` and `docs/strings-results.md`.
    Verify: `tutorial/run.sh`; the numbers in the READMEs match the results file.

## Risks

- **The lexer must agree with clang**, or a page shows wrong text.
  The differential check runs over every literal in the tree on every `tests/lits/run.sh`, and an app can run it too (build with `-DCD_LITS_CHECK`, then `cdlits --check`); the grammar is closed and small.
  A spelling the lexer does not know stops the build; it never guesses.
- **A live `cd_tmp_ahead` range trusts the app** not to change its bytes.
  It is the one rule in this design that the compiler does not enforce; `options.checkAhead` enforces it in tests, and the range cannot outlive the call into C.
- **create 10k script stays behind today's**, by 0.1 to 0.8 ms of 20.4 (P5), because the benchmark's labels are runtime strings the app now owns.
  Every total is predicted within 2.5%.
- **The API functions are macros.**
  A diagnostic for a wrong argument points into `cd_s`, and a function pointer needs `(cd_text)`.
  A string argument that contains a top-level comma outside parentheses (a compound literal) needs parentheses, as with any macro.
- **amgi and cbase break when they move their cdom pin past step 7.**
  That is accepted: no compatibility layer is kept, and each repo migrates (the table above) when it next bumps its pin.
- **The build gains a host compiler.**
  `cc` is present wherever clang is, but a cross-only environment must set `HOST_CC`.
- **A short runtime string shown again and again unchanged** costs 26 to 95 ns where an intern hit cost 13 to 51.
  No app in the tree does it; the answers are a literal, or a range.

## Appendix: the prototype tool

The Python below is the first prototype; `tools/cdlits.c` is its port, with the table written one entry per slot as the specification says.
It is here because the scratch directory it ran in does not survive.

```python
import sys, struct
def uleb(b, i):
    r = s = 0
    while True:
        c = b[i]; i += 1; r |= (c & 127) << s; s += 7
        if c < 128: return r, i
def sleb(b, i):
    r = s = 0
    while True:
        c = b[i]; i += 1; r |= (c & 127) << s; s += 7
        if c < 128:
            if c & 64: r -= 1 << s
            return r, i
def enc(n):
    o = bytearray()
    while True:
        c = n & 127; n >>= 7
        if n: o.append(c | 128)
        else: o.append(c); return bytes(o)
def die(m): sys.exit('cdlits: ' + m)
w = open(sys.argv[1], 'rb').read(); check = '--check' in sys.argv
secs = []; i = 8
while i < len(w):
    sid = w[i]; n, j = uleb(w, i + 1); secs.append((sid, w[j:j + n])); i = j + n
def sec(k): return next((b for s, b in secs if s == k), None)
gl = []; b = sec(6); n, i = uleb(b, 0)            # globals: i32.const or i64.const initializers
for _ in range(n):
    i += 2; v, i = sleb(b, i + 1); i += 1; gl.append(v & 0xffffffff)
ex = {}; exraw = []; b = sec(7); n, i = uleb(b, 0)  # exports
for _ in range(n):
    s = i; ln, i = uleb(b, i); name = b[i:i + ln].decode(); i += ln; kind = b[i]; idx, i = uleb(b, i + 1)
    exraw.append((name, b[s:i]))
    if kind == 3: ex[name] = gl[idx]
need = ['__start_cd_lits', '__stop_cd_lits', '__start_cd_chars', '__stop_cd_chars']
ls, le, cs, ce = (ex[k] for k in need)
segs = []; b = sec(11); n, i = uleb(b, 0)          # active data segments of memory 0
for _ in range(n):
    s = i; flag, i = uleb(b, i); off, i = sleb(b, i + 1); i += 1
    hdr = b[s:i]; ln, i = uleb(b, i); segs.append([off, b[i:i + ln], hdr]); i += ln
def mem(a, n):                                    # bytes no segment covers are zero
    out = bytearray(n)
    for off, d, _ in segs:
        lo, hi = max(a, off), min(a + n, off + len(d))
        if lo < hi: out[lo - a:hi - a] = d[lo - off:hi - off]
    return bytes(out)
def cstr(a, lim):
    m = mem(a, lim - a); z = m.find(b'\0')
    return m if z < 0 else m[:z]
SIMPLE = {'a': 7, 'b': 8, 'f': 12, 'n': 10, 'r': 13, 't': 9, 'v': 11, '\\': 92, "'": 39, '"': 34, '?': 63, 'e': 27}
def lex(sp):   # the bytes of adjacent C string literals as spelled; None for anything else
    o = bytearray(); i = 0; n = len(sp); any_ = False
    while i < n:
        if sp[i] in b' \t\n': i += 1; continue
        if sp[i:i + 2] == b'u8': i += 2
        if i >= n or sp[i] != 34: return None
        i += 1; any_ = True
        while True:
            if i >= n: return None
            c = sp[i]; i += 1
            if c == 34: break
            if c != 92: o.append(c); continue
            if i >= n: return None
            e = chr(sp[i]); i += 1
            if e in SIMPLE: o.append(SIMPLE[e])
            elif e in '01234567':
                v = int(e); k = 1
                while k < 3 and i < n and chr(sp[i]) in '01234567': v = v * 8 + sp[i] - 48; i += 1; k += 1
                if v > 255: return None
                o.append(v)
            elif e == 'x':
                v = 0; k = 0
                while i < n and chr(sp[i]) in '0123456789abcdefABCDEF': v = v * 16 + int(chr(sp[i]), 16); i += 1; k += 1
                if not k or v > 255: return None
                o.append(v)
            elif e in 'uU':
                k = 4 if e == 'u' else 8
                if len(sp[i:i + k]) != k: return None
                v = int(sp[i:i + k].decode(), 16); i += k; o += chr(v).encode()
            else: return None
    return bytes(o) if any_ else None
stride = 8 if check else 4
slots = []
for a in range(ls, le, stride):
    p = struct.unpack('<I', mem(a, 4))[0]
    sp = cstr(p, ce); v = lex(sp)
    if v is None: die('not a string literal: ' + sp.decode('utf8', 'replace'))
    if check:
        r = struct.unpack('<I', mem(a + 4, 4))[0]
        if cstr(r, r + len(v) + 2) != v: die('lexer mismatch: %r' % sp)
    slots.append(v)
# then: the stamp, the JS text (every slot's string, in order), and the data and export sections
# rewritten without the two ranges
```

The prototype did not patch the stamp, cut overlapping segments or write the canary; the specification above is the authority for those.
